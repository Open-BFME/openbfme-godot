namespace OpenBfme.Sim;

/// <summary>
/// Single-weapon attack cycle: fires at the current target every ReloadTicks
/// when within RangeRaw, dealing Damage of DamageType. Target selection is the
/// AI module's job; this module owns only the firing mechanics.
/// </summary>
[SageModule("Weapon", ModuleTier.Structural)]
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
