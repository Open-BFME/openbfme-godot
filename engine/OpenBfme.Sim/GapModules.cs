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

/// <summary>
/// Experience / rank state for battalions (retail experiencelevels.ini ladder).
/// Level starts at 1. GrantExperience applies awards; when RequiredExperience
/// thresholds for the next rank are met, Level increases up to LevelCap.
/// Design data: LevelCap (default 10), RequiredExperience:N keys for level N+1
/// thresholds (optional; GrantLevels can force ranks without XP tables).
/// </summary>
public sealed class ExperienceLevelModule : ModuleBase
{
    public const string TypeName = "ExperienceLevel";

    private readonly long _levelCap;
    private readonly SortedDictionary<int, long> _requiredForLevel = new();

    public int Level { get; private set; }
    public long Experience { get; private set; }

    public ExperienceLevelModule(ModuleSpec spec) : base(spec)
    {
        _levelCap = Math.Max(1, spec.GetLong("LevelCap", 10));
        Level = (int)Math.Clamp(spec.GetLong("InitialLevel", 1), 1, _levelCap);
        foreach (var pair in spec.Data)
        {
            if (pair.Key.StartsWith("RequiredExperience:", StringComparison.Ordinal)
                && int.TryParse(pair.Key.AsSpan("RequiredExperience:".Length), out var lvl)
                && lvl > 1)
            {
                _requiredForLevel[lvl] = Math.Max(0, pair.Value);
            }
        }
    }

    public void GrantExperience(long amount)
    {
        if (amount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(amount));
        }
        Experience += amount;
        while (Level < _levelCap)
        {
            var next = _requiredForLevel.FirstOrDefault(pair => pair.Key > Level);
            if (next.Key == 0 || Experience < next.Value)
            {
                break;
            }
            Level = next.Key;
        }
    }

    /// <summary>Force-apply LevelsToGain from LevelUpUpgrade (Basic Training).</summary>
    public void GrantLevels(int levelsToGain)
    {
        if (levelsToGain < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(levelsToGain));
        }
        Level = (int)Math.Min(_levelCap, Level + levelsToGain);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(Level);
        writer.WriteLong(Experience);
    }

    public override void ReadState(CanonicalReader reader)
    {
        Level = reader.ReadInt();
        Experience = reader.ReadLong();
    }
}

/// <summary>
/// Retail horde BannerCarriersAllowed + BannerCarrierMinLevel: when the horde's
/// ExperienceLevel reaches MinLevel (default 2), spawn the first allowed banner
/// object at the authored exact fixed-point Pos X/Y offset. If that object
/// dies, the horde is destroyed only when retail authored the corresponding
/// HordeContain flag. Replacement is likewise enabled only when the banner's
/// retail BannerCarrierUpdate authored a respawn timer.
/// Design: MinLevel (default 2), OffsetXRaw, OffsetYRaw,
/// DestroyHordeOnBannerDeath, optional RespawnTicks, string BannerTemplate.
/// </summary>
public sealed class BannerCarrierModule : ModuleBase
{
    public const string TypeName = "BannerCarrier";

    private readonly int _minLevel;
    private readonly Fixed64 _offsetX;
    private readonly Fixed64 _offsetY;
    private readonly string _bannerTemplate;
    private readonly bool _destroyHordeOnBannerDeath;
    private readonly bool _hasAuthoredRespawn;
    private readonly int _respawnTicks;
    private bool _spawned;
    private int _bannerObjectId;
    private int _respawnTicksRemaining = -1;

    public BannerCarrierModule(ModuleSpec spec) : base(spec)
    {
        _minLevel = (int)Math.Max(0, spec.GetLong("MinLevel", 2));
        // The loader converts authored decimal JSON directly to Q32.32. Do not
        // quantize through invented milli-units: source coordinates are not
        // contractually limited to three decimal places.
        _offsetX = Fixed64.FromRaw(spec.GetLong("OffsetXRaw", 0));
        _offsetY = Fixed64.FromRaw(spec.GetLong("OffsetYRaw", 0));
        _bannerTemplate = spec.GetString("BannerTemplate", "");
        _destroyHordeOnBannerDeath = spec.GetLong("DestroyHordeOnBannerDeath", 0) != 0;
        _hasAuthoredRespawn = spec.Data.ContainsKey("RespawnTicks");
        _respawnTicks = (int)Math.Clamp(spec.GetLong("RespawnTicks", 0), 0, int.MaxValue);
        if (string.IsNullOrEmpty(_bannerTemplate))
        {
            throw new ArgumentException("BannerCarrier requires BannerTemplate string data");
        }
    }

    public bool HasSpawned => _spawned;
    public int BannerObjectId => _bannerObjectId;
    public bool HasLivingBanner => _bannerObjectId != 0;
    public int RespawnTicksRemaining => _respawnTicksRemaining;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsDead || self.IsDying)
        {
            return;
        }
        if (_bannerObjectId != 0)
        {
            if (world.Objects.TryGetValue(_bannerObjectId, out var existing)
                && !existing.IsDead && !existing.IsDying)
            {
                return;
            }
            _bannerObjectId = 0;
            if (_destroyHordeOnBannerDeath)
            {
                world.HandleDeath(self);
                return;
            }
            if (!_hasAuthoredRespawn)
            {
                return;
            }
            _respawnTicksRemaining = _respawnTicks;
        }
        if (_spawned)
        {
            if (!_hasAuthoredRespawn || _respawnTicksRemaining < 0)
            {
                return;
            }
            if (_respawnTicksRemaining > 0)
            {
                _respawnTicksRemaining--;
                if (_respawnTicksRemaining > 0)
                {
                    return;
                }
            }
        }
        var level = self.FindModule<ExperienceLevelModule>()?.Level ?? 1;
        if (level < _minLevel)
        {
            return;
        }
        var position = new FixedVector2(
            self.Position.X + _offsetX,
            self.Position.Y + _offsetY);
        var banner = world.SpawnObject(_bannerTemplate, self.Team, position);
        _bannerObjectId = banner.Id;
        _spawned = true;
        _respawnTicksRemaining = -1;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_spawned);
        writer.WriteInt(_bannerObjectId);
        writer.WriteInt(_respawnTicksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _spawned = reader.ReadBool();
        _bannerObjectId = reader.ReadInt();
        _respawnTicksRemaining = reader.ReadInt();
    }
}

/// <summary>
/// CastleBehavior-shaped fortress unpack: on first update, spawn engine
/// every BSE piece at its authored offset and relative angle. Design string
/// keys PieceTemplate:N and exact Q32.32 OffsetXRaw:N / OffsetYRaw:N /
/// OffsetZRaw:N / AngleRadiansRaw:N. Legacy CitadelTemplate/PadTemplate keys
/// remain readable for old fixtures, but retail contracts use PieceTemplate.
/// </summary>
public sealed class CastleBehaviorModule : ModuleBase
{
    public const string TypeName = "CastleBehavior";

    private readonly List<(
        string Template,
        Fixed64 Ox,
        Fixed64 Oy,
        Fixed64 Oz,
        Fixed64 Angle)> _pieces = new();
    private bool _unpacked;

    public CastleBehaviorModule(ModuleSpec spec) : base(spec)
    {
        var authoredCount = spec.GetLong("PieceCount", -1);
        if (authoredCount > 64)
        {
            throw new ArgumentException("CastleBehavior PieceCount exceeds 64");
        }
        var limit = authoredCount >= 0 ? (int)authoredCount : 64;
        for (var i = 0; i < limit; i++)
        {
            var template = spec.GetString($"PieceTemplate:{i}", "");
            if (string.IsNullOrEmpty(template))
            {
                template = spec.GetString($"PadTemplate:{i}", "");
            }
            if (string.IsNullOrEmpty(template))
            {
                if (authoredCount >= 0)
                {
                    throw new ArgumentException(
                        $"CastleBehavior piece {i} is missing its template");
                }
                continue;
            }
            var ox = Fixed64.FromRaw(spec.GetLong($"OffsetXRaw:{i}", 0));
            var oy = Fixed64.FromRaw(spec.GetLong($"OffsetYRaw:{i}", 0));
            var oz = Fixed64.FromRaw(spec.GetLong($"OffsetZRaw:{i}", 0));
            var angle = Fixed64.FromRaw(spec.GetLong($"AngleRadiansRaw:{i}", 0));
            _pieces.Add((template, ox, oy, oz, angle));
        }
        var legacyCitadel = spec.GetString("CitadelTemplate", "");
        if (!string.IsNullOrEmpty(legacyCitadel))
        {
            _pieces.Add((legacyCitadel, Fixed64.Zero, Fixed64.Zero,
                Fixed64.Zero, Fixed64.Zero));
        }
        if (_pieces.Count == 0)
        {
            throw new ArgumentException("CastleBehavior requires at least one BSE piece");
        }
    }

    public bool HasUnpacked => _unpacked;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_unpacked || self.IsDead || self.IsDying)
        {
            return;
        }
        foreach (var (template, ox, oy, oz, angle) in _pieces)
        {
            var position = new FixedVector2(
                self.Position.X + ox,
                self.Position.Y + oy);
            world.SpawnObject(
                template,
                self.Team,
                position,
                self.Elevation + oz,
                self.HeadingRadians + angle);
        }
        _unpacked = true;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_unpacked);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _unpacked = reader.ReadBool();
    }
}
