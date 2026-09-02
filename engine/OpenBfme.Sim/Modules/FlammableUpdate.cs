namespace OpenBfme.Sim;

/// <summary>FLAME exposure, authored burn delay/duration, periodic AflameDamageAmount, and WATER extinguishing.</summary>
[SageModule("FlammableUpdate", ModuleTier.Structural)]
public sealed class FlammableUpdateModule : ModuleBase
{
    public const string TypeName = "FlammableUpdate";

    private readonly Fixed64 _flameLimit;
    private readonly long _burnedDelayMilliseconds;
    private readonly long _damageDelayMilliseconds;
    private readonly long _durationMilliseconds;
    private readonly long _fireDamage;
    private readonly string _damageType;
    private readonly bool _setBurnedStatus;
    private Fixed64 _flameExposure;
    private int _igniteTicks;
    private int _damageTicks;
    private int _durationTicks;
    private bool _burning;

    public FlammableUpdateModule(ModuleSpec spec) : base(spec)
    {
        _flameLimit = ModuleRuntime.ReadFixed(spec, "FlameDamageLimit", Fixed64.One);
        _burnedDelayMilliseconds = Math.Max(0, spec.GetLong("BurnedDelay", spec.GetLong("BurningDelay", 0)));
        _damageDelayMilliseconds = Math.Max(1, spec.GetLong("AflameDamageDelay", spec.GetLong("FireDamageDelay", 1_000)));
        _durationMilliseconds = Math.Max(0, spec.GetLong("AflameDuration", spec.GetLong("FlameDamageExpiration", 0)));
        _fireDamage = Math.Max(0, spec.GetLong("AflameDamageAmount", spec.GetLong("FireDamage", 0)));
        _damageType = spec.GetString("DamageType", "FLAME");
        _setBurnedStatus = ModuleRuntime.ReadBool(spec, "SetBurnedStatus", true);
    }

    public bool IsBurning => _burning;

    public void Ignite(SimWorld world, GameObject self)
    {
        _flameExposure = Fixed64.Max(_flameExposure, _flameLimit);
        if (_burning || _igniteTicks > 0) return;
        _igniteTicks = _burnedDelayMilliseconds == 0
            ? 1
            : ModuleRuntime.MillisecondsToTicks(_burnedDelayMilliseconds, world.TickMilliseconds);
    }

    public void Extinguish(GameObject self)
    {
        _burning = false;
        _igniteTicks = 0;
        _damageTicks = 0;
        _durationTicks = 0;
        _flameExposure = Fixed64.Zero;
        self.TrySetConditionToken("BURNING", false);
    }

    public override void OnDamageReceived(SimWorld world, GameObject self, Fixed64 amount, string damageType)
    {
        var normalized = damageType.ToUpperInvariant();
        if (normalized is "WATER")
        {
            Extinguish(self);
            return;
        }
        if (normalized is not ("FLAME" or "LOGICAL_FIRE")) return;
        _flameExposure += amount;
        if (_flameExposure >= _flameLimit) Ignite(world, self);
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_burning && _igniteTicks > 0 && --_igniteTicks <= 0)
        {
            _burning = true;
            _damageTicks = ModuleRuntime.MillisecondsToTicks(_damageDelayMilliseconds, world.TickMilliseconds);
            _durationTicks = _durationMilliseconds == 0
                ? 0
                : ModuleRuntime.MillisecondsToTicks(_durationMilliseconds, world.TickMilliseconds);
            if (_setBurnedStatus) self.TrySetConditionToken("BURNING");
        }
        if (!_burning) return;
        if (_durationTicks > 0 && --_durationTicks <= 0)
        {
            Extinguish(self);
            return;
        }
        if (--_damageTicks <= 0)
        {
            _damageTicks = ModuleRuntime.MillisecondsToTicks(_damageDelayMilliseconds, world.TickMilliseconds);
            if (_fireDamage > 0) world.DealDamage(self, _fireDamage, _damageType);
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteFixed(_flameExposure);
        writer.WriteInt(_igniteTicks);
        writer.WriteInt(_damageTicks);
        writer.WriteInt(_durationTicks);
        writer.WriteBool(_burning);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _flameExposure = reader.ReadFixed();
        _igniteTicks = reader.ReadInt();
        _damageTicks = reader.ReadInt();
        _durationTicks = reader.ReadInt();
        _burning = reader.ReadBool();
    }
}
