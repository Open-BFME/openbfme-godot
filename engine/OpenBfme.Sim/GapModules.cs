namespace OpenBfme.Sim;

/// <summary>
/// HordeContain-shaped container (122 objects in the union corpus): the horde
/// object holds MemberCount member slots as DATA — this tier spawns no child
/// sim objects (member positions/formations are presentation; child-object
/// containment arrives with the horde AI lane). Aggregate health delegates to
/// the members: incoming damage fills member slots in ascending slot order
/// (lowest living slot first, overflow kills through to the next slot), which
/// is deterministic by construction. When every member is at zero the horde
/// routes through the normal death pipeline.
/// Design data: MemberCount (default 1), MemberHealth per member (default 100).
/// </summary>
public sealed class HordeContainModule : ModuleBase
{
    public const string TypeName = "HordeContain";

    private readonly long _memberMaxHealth;
    private readonly long[] _memberHealth;

    public HordeContainModule(ModuleSpec spec) : base(spec)
    {
        var memberCount = (int)Math.Clamp(spec.GetLong("MemberCount", 1), 1, 1024);
        _memberMaxHealth = Math.Max(1, spec.GetLong("MemberHealth", 100));
        _memberHealth = new long[memberCount];
        for (var i = 0; i < _memberHealth.Length; i++)
        {
            _memberHealth[i] = _memberMaxHealth;
        }
    }

    public int MemberCount => _memberHealth.Length;
    public long MemberMaxHealth => _memberMaxHealth;
    public long MemberHealthAt(int slot) => _memberHealth[slot];

    public int AliveMemberCount
    {
        get
        {
            var alive = 0;
            foreach (var health in _memberHealth)
            {
                if (health > 0)
                {
                    alive++;
                }
            }
            return alive;
        }
    }

    public long TotalHealth
    {
        get
        {
            long total = 0;
            foreach (var health in _memberHealth)
            {
                total += health;
            }
            return total;
        }
    }

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (amount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(amount), "Damage must be non-negative");
        }
        if (TotalHealth == 0)
        {
            return true;
        }
        for (var slot = 0; slot < _memberHealth.Length && amount > 0; slot++)
        {
            if (_memberHealth[slot] == 0)
            {
                continue;
            }
            var applied = Math.Min(_memberHealth[slot], amount);
            _memberHealth[slot] -= applied;
            amount -= applied;
        }
        if (TotalHealth == 0)
        {
            world.HandleDeath(self);
        }
        return true;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        // Member count is config; only the healths are mutable state.
        foreach (var health in _memberHealth)
        {
            writer.WriteLong(health);
        }
    }

    public override void ReadState(CanonicalReader reader)
    {
        for (var i = 0; i < _memberHealth.Length; i++)
        {
            _memberHealth[i] = reader.ReadLong();
        }
    }
}

/// <summary>
/// BezierProjectileBehavior-lite (237 objects in the union corpus): the module
/// TYPE maps now; trajectory fidelity is presentation. This tier flies a
/// straight line — Launch aims at a target object, the projectile closes the
/// remaining offset in equal fractions over FlightTicks updates (re-aiming at
/// the target's current position each tick while it lives), and on the arrival
/// update deals Damage of DamageType via world.DealDamage, then expires through
/// the death pipeline. The full bezier arc is a later fidelity pass.
/// Design data: FlightTicks (default 10), Damage (default 10),
/// string DamageType (default "default").
/// </summary>
public sealed class BezierProjectileModule : ModuleBase
{
    public const string TypeName = "BezierProjectile";

    private readonly int _flightTicks;
    private readonly long _damage;
    private readonly string _damageType;

    private bool _inFlight;
    private int _targetId;
    private FixedVector2 _aimPoint;
    private int _ticksRemaining;

    public BezierProjectileModule(ModuleSpec spec) : base(spec)
    {
        _flightTicks = (int)Math.Max(1, spec.GetLong("FlightTicks", 10));
        _damage = Math.Max(0, spec.GetLong("Damage", 10));
        _damageType = spec.GetString("DamageType", DamageTypes.Default);
    }

    public bool InFlight => _inFlight;
    public int TargetId => _targetId;

    /// <summary>
    /// Arms the projectile at the target: it arrives (and deals damage) on its
    /// FlightTicks-th update after launch.
    /// </summary>
    public void Launch(SimWorld world, GameObject self, int targetId)
    {
        _inFlight = true;
        _targetId = targetId;
        _aimPoint = world.Objects.TryGetValue(targetId, out var target) ? target.Position : self.Position;
        _ticksRemaining = _flightTicks;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_inFlight || self.IsDying)
        {
            return;
        }
        var target = world.Objects.TryGetValue(_targetId, out var found) && !found.IsDead ? found : null;
        if (target != null)
        {
            _aimPoint = target.Position;
        }
        // Equal-fraction closing: step = remaining offset / remaining ticks.
        // The final tick's step is the whole remaining offset — exact arrival.
        var offset = _aimPoint - self.Position;
        var ticks = Fixed64.FromInt(_ticksRemaining);
        var step = new FixedVector2(offset.X / ticks, offset.Y / ticks);
        self.SetPosition(_ticksRemaining == 1 ? _aimPoint : self.Position + step);
        _ticksRemaining--;
        if (_ticksRemaining > 0)
        {
            return;
        }
        _inFlight = false;
        if (target != null && _damage > 0)
        {
            world.DealDamage(target, _damage, _damageType);
        }
        world.HandleDeath(self);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_inFlight);
        writer.WriteInt(_targetId);
        writer.WriteVector(_aimPoint);
        writer.WriteInt(_ticksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _inFlight = reader.ReadBool();
        _targetId = reader.ReadInt();
        _aimPoint = reader.ReadVector();
        _ticksRemaining = reader.ReadInt();
    }
}

/// <summary>
/// AttributeModifierAuraUpdate-lite (227 objects in the union corpus): a radius
/// aura granting a flat ARMOR basis-point modifier to allied objects in range
/// (the single chosen integration for this tier — the damage-bonus side arrives
/// with the weapon-modifier lane). Every RecomputeTicks the aura rescans
/// world.Objects in ascending id order (deterministic) and caches the allied
/// ids within RadiusRaw; at the END of each world tick, SimWorld rebuilds its
/// aura armor table from every living, non-dying, constructed carrier's cache,
/// so the table is stable for the whole following tick (commands and the module
/// sweep alike). world.DealDamage consults the table after the per-module
/// ModifyIncomingDamage chain.
///
/// STACKING RULE (documented contract): contributions from multiple aura
/// carriers ADD; the summed basis points are clamped to [0, 10000] at damage
/// application, so stacked auras can reach full immunity but never heal.
/// The table is derived state — rebuilt deterministically from module caches —
/// so it is NOT serialized; SimWorld.Restore rebuilds it after loading.
/// Design data: RadiusRaw (Fixed64 raw, default 10), RecomputeTicks (default 5),
/// ArmorBonusBp (default 0; 1000 = 10% less incoming damage).
/// Aura carriers do not buff themselves.
/// </summary>
public sealed class AttributeModifierAuraModule : ModuleBase
{
    public const string TypeName = "AttributeModifierAura";

    private readonly Fixed64 _radius;
    private readonly int _recomputeTicks;
    private readonly long _armorBonusBp;

    private int _ticksUntilScan;
    private readonly List<int> _affectedIds = new();

    public AttributeModifierAuraModule(ModuleSpec spec) : base(spec)
    {
        _radius = spec.GetFixed("RadiusRaw", Fixed64.FromInt(10));
        _recomputeTicks = (int)Math.Max(1, spec.GetLong("RecomputeTicks", 5));
        _armorBonusBp = Math.Clamp(spec.GetLong("ArmorBonusBp", 0), 0, 10_000);
        _ticksUntilScan = 1; // first update scans immediately
    }

    public long ArmorBonusBp => _armorBonusBp;
    public IReadOnlyList<int> AffectedIds => _affectedIds;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying)
        {
            return;
        }
        _ticksUntilScan--;
        if (_ticksUntilScan > 0)
        {
            return;
        }
        _ticksUntilScan = _recomputeTicks;
        _affectedIds.Clear();
        var radiusSquared = _radius * _radius;
        foreach (var candidate in world.Objects.Values) // ascending id — deterministic
        {
            if (candidate.Id == self.Id || candidate.Team != self.Team
                || candidate.IsDead || candidate.IsDying)
            {
                continue;
            }
            if (self.Position.DistanceSquaredTo(candidate.Position) <= radiusSquared)
            {
                _affectedIds.Add(candidate.Id);
            }
        }
    }

    /// <summary>Adds this aura's cached contributions into the world table (additive stacking).</summary>
    internal void ContributeTo(SortedDictionary<int, long> armorBonusBpByObjectId, SimWorld world)
    {
        if (_armorBonusBp == 0)
        {
            return;
        }
        foreach (var id in _affectedIds)
        {
            if (!world.Objects.ContainsKey(id))
            {
                continue; // member died since the last rescan
            }
            armorBonusBpByObjectId[id] =
                armorBonusBpByObjectId.TryGetValue(id, out var existing) ? existing + _armorBonusBp : _armorBonusBp;
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksUntilScan);
        writer.WriteInt(_affectedIds.Count);
        foreach (var id in _affectedIds)
        {
            writer.WriteInt(id);
        }
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksUntilScan = reader.ReadInt();
        _affectedIds.Clear();
        var count = reader.ReadInt();
        for (var i = 0; i < count; i++)
        {
            _affectedIds.Add(reader.ReadInt());
        }
    }
}
