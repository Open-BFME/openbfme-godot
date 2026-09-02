namespace OpenBfme.Sim;

/// <summary>Shared deterministic four-phase timer for SAGE hero ability updates.</summary>
public abstract class TimedSpecialAbilityModuleBase : SpecialPowerEffectModuleBase
{
    private readonly long _unpackMilliseconds;
    private readonly long _preparationMilliseconds;
    private readonly long _persistentPreparationMilliseconds;
    private readonly long _packMilliseconds;
    private readonly int _persistentCount;
    private readonly Fixed64 _startRange;
    private int _ticksRemaining;
    private int _persistentRemaining;
    private int _phase;
    private int _targetId;
    private FixedVector2 _targetPosition;
    private string _powerName = "";

    protected TimedSpecialAbilityModuleBase(ModuleSpec spec) : base(spec)
    {
        _unpackMilliseconds = Math.Max(0, spec.GetLong("UnpackTime", 0));
        _preparationMilliseconds = Math.Max(0, spec.GetLong("PreparationTime", 0));
        _persistentPreparationMilliseconds = Math.Max(0, spec.GetLong("PersistentPrepTime", 0));
        _packMilliseconds = Math.Max(0, spec.GetLong("PackTime", 0));
        _persistentCount = checked((int)Math.Max(1, spec.GetLong("PersistentCount", 1)));
        _startRange = ModuleRuntime.ReadFixed(spec, "StartAbilityRange", Fixed64.Zero);
    }

    public bool IsActive => _phase != 0;
    public int Phase => _phase;
    internal bool CastAccepted { get; private set; }

    internal sealed override void Cast(
        SimWorld world,
        GameObject caster,
        int targetId,
        FixedVector2 targetPosition)
    {
        CastAccepted = false;
        if (_phase != 0) return;
        if (_startRange > Fixed64.Zero
            && caster.Position.DistanceSquaredTo(targetPosition) > _startRange * _startRange) return;
        CastAccepted = true;
        _targetId = targetId;
        _targetPosition = targetPosition;
        _powerName = Spec.GetString("SpecialPowerTemplate", Spec.GetString("SpecialPower", ""));
        _persistentRemaining = _persistentCount;
        var beforeEffect = checked(_unpackMilliseconds + _preparationMilliseconds
            + _persistentPreparationMilliseconds);
        if (beforeEffect == 0)
        {
            FireAndAdvance(world, caster);
            return;
        }
        _phase = 1;
        _ticksRemaining = ModuleRuntime.MillisecondsToTicks(beforeEffect, world.TickMilliseconds);
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_phase == 0) return;
        if (_ticksRemaining > 1)
        {
            _ticksRemaining--;
            return;
        }
        if (_phase == 1) FireAndAdvance(world, self);
        else
        {
            _phase = 0;
            _ticksRemaining = 0;
        }
    }

    protected abstract void FireEffect(
        SimWorld world,
        GameObject caster,
        int targetId,
        FixedVector2 targetPosition,
        string powerName);

    private void FireAndAdvance(SimWorld world, GameObject caster)
    {
        FireEffect(world, caster, _targetId, _targetPosition, _powerName);
        _persistentRemaining--;
        if (_persistentRemaining > 0 && _persistentPreparationMilliseconds > 0)
        {
            _phase = 1;
            _ticksRemaining = ModuleRuntime.MillisecondsToTicks(
                _persistentPreparationMilliseconds, world.TickMilliseconds);
            return;
        }
        if (_packMilliseconds > 0)
        {
            _phase = 2;
            _ticksRemaining = ModuleRuntime.MillisecondsToTicks(_packMilliseconds, world.TickMilliseconds);
            return;
        }
        _phase = 0;
        _ticksRemaining = 0;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksRemaining);
        writer.WriteInt(_persistentRemaining);
        writer.WriteInt(_phase);
        writer.WriteInt(_targetId);
        writer.WriteVector(_targetPosition);
        writer.WriteString(_powerName);
        writer.WriteBool(CastAccepted);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksRemaining = reader.ReadInt();
        _persistentRemaining = reader.ReadInt();
        _phase = reader.ReadInt();
        _targetId = reader.ReadInt();
        _targetPosition = reader.ReadVector();
        _powerName = reader.ReadString();
        CastAccepted = reader.ReadBool();
    }
}

public sealed partial class SimWorld
{
    internal void ExecuteNestedPowerEffects(
        GameObject caster,
        ModuleBase driver,
        string powerName,
        int targetId,
        FixedVector2 targetPosition)
    {
        if (!_config.Tech.SpecialPowers.TryGetValue(powerName, out var power)) return;
        foreach (var module in caster.Modules)
            if (!ReferenceEquals(module, driver)
                && module is SpecialPowerEffectModuleBase effect
                && module is not TimedSpecialAbilityModuleBase
                && effect.Matches(power))
                effect.Cast(this, caster, targetId, targetPosition);
    }
}
