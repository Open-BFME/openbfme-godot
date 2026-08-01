namespace OpenBfme.Sim;

/// <summary>Canonical damage-type identifiers. Basis for armor scaling and crush gating.</summary>
public static class DamageTypes
{
    public const string Default = "default";
    public const string Slash = "slash";
    public const string Pierce = "pierce";
    public const string Siege = "siege";
    public const string Crush = "crush";
}

/// <summary>
/// ArmorSet-shaped scaling (SAGE armor.ini semantics): per-damage-type basis
/// points, 10000 = 100% damage taken. Design data keys: "Armor:&lt;type&gt;";
/// unlisted types take "ArmorDefault" (default 10000).
/// </summary>
public sealed class ArmorModule : ModuleBase
{
    public const string TypeName = "Armor";

    public ArmorModule(ModuleSpec spec) : base(spec)
    {
    }

    public override long ModifyIncomingDamage(GameObject self, string damageType, long amount)
    {
        var basisPoints = Spec.GetLong("Armor:" + damageType, Spec.GetLong("ArmorDefault", 10_000));
        basisPoints = Math.Clamp(basisPoints, 0, 100_000);
        return amount * basisPoints / 10_000;
    }
}

/// <summary>
/// SquishCollide marker (374 objects): declares the object crushable. Crush
/// damage is dropped by the world for objects without this module.
/// </summary>
public sealed class SquishCollideModule : ModuleBase
{
    public const string TypeName = "SquishCollide";

    public SquishCollideModule(ModuleSpec spec) : base(spec)
    {
    }
}

/// <summary>
/// Single-weapon attack cycle: fires at the current target every ReloadTicks
/// when within RangeRaw, dealing Damage of DamageType. Target selection is the
/// AI module's job; this module owns only the firing mechanics.
/// </summary>
public sealed class WeaponModule : ModuleBase
{
    public const string TypeName = "Weapon";

    private readonly Fixed64 _range;
    private readonly long _damage;
    private readonly int _reloadTicks;
    private readonly string _damageType;
    private int _cooldown;
    private int _targetId;

    public WeaponModule(ModuleSpec spec) : base(spec)
    {
        _range = spec.GetFixed("RangeRaw", Fixed64.FromInt(2));
        _damage = spec.GetLong("Damage", 10);
        _reloadTicks = (int)Math.Max(1, spec.GetLong("ReloadTicks", 10));
        _damageType = spec.GetString("DamageType", DamageTypes.Default);
    }

    public int TargetId => _targetId;
    public Fixed64 Range => _range;

    public void SetTarget(int targetId) => _targetId = targetId;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_cooldown > 0)
        {
            _cooldown--;
        }
        if (self.IsUnderConstruction || self.IsDying || _targetId == 0)
        {
            return;
        }
        if (!world.Objects.TryGetValue(_targetId, out var target) || target.IsDead || target.IsDying)
        {
            _targetId = 0;
            return;
        }
        var rangeSquared = _range * _range;
        if (self.Position.DistanceSquaredTo(target.Position) > rangeSquared)
        {
            return;
        }
        if (_cooldown > 0)
        {
            return;
        }
        world.DealDamage(target, _damage, _damageType);
        _cooldown = _reloadTicks;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_cooldown);
        writer.WriteInt(_targetId);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _cooldown = reader.ReadInt();
        _targetId = reader.ReadInt();
    }
}

/// <summary>
/// AIUpdateInterface-lite (539 objects): acquires the nearest living enemy
/// within VisionRangeRaw (ties broken by lowest id — deterministic), hands it
/// to the weapon, and walks into weapon range via the LinearMover when needed.
/// </summary>
public sealed class AiCombatModule : ModuleBase
{
    public const string TypeName = "AiCombat";

    private readonly Fixed64 _visionRange;
    private readonly int _scanIntervalTicks;
    private int _ticksUntilScan;

    public AiCombatModule(ModuleSpec spec) : base(spec)
    {
        _visionRange = spec.GetFixed("VisionRangeRaw", Fixed64.FromInt(12));
        _scanIntervalTicks = (int)Math.Max(1, spec.GetLong("ScanIntervalTicks", 5));
        _ticksUntilScan = 1;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying)
        {
            return;
        }
        var weapon = self.FindModule<WeaponModule>();
        if (weapon == null)
        {
            return;
        }
        _ticksUntilScan--;
        if (_ticksUntilScan <= 0)
        {
            _ticksUntilScan = _scanIntervalTicks;
            if (weapon.TargetId == 0)
            {
                weapon.SetTarget(FindNearestEnemyId(world, self));
            }
        }
        if (weapon.TargetId == 0 || !world.Objects.TryGetValue(weapon.TargetId, out var target))
        {
            return;
        }
        var mover = self.FindModule<LinearMoverModule>();
        if (mover == null)
        {
            return;
        }
        var rangeSquared = weapon.Range * weapon.Range;
        if (self.Position.DistanceSquaredTo(target.Position) > rangeSquared)
        {
            mover.SetDestination(target.Position);
        }
    }

    private int FindNearestEnemyId(SimWorld world, GameObject self)
    {
        var bestId = 0;
        var bestDistanceSquared = _visionRange * _visionRange;
        foreach (var candidate in world.Objects.Values)
        {
            if (candidate.Team == self.Team || candidate.IsDead || candidate.IsDying)
            {
                continue;
            }
            var distanceSquared = self.Position.DistanceSquaredTo(candidate.Position);
            // Strict < with ascending-id iteration = lowest id wins ties. Deterministic.
            if (distanceSquared < bestDistanceSquared || (bestId == 0 && distanceSquared == bestDistanceSquared))
            {
                bestId = candidate.Id;
                bestDistanceSquared = distanceSquared;
            }
        }
        return bestId;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_ticksUntilScan);
    public override void ReadState(CanonicalReader reader) => _ticksUntilScan = reader.ReadInt();
}
