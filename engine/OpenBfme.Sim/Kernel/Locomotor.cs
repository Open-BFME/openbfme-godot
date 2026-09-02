namespace OpenBfme.Sim;

/// <summary>
/// Immutable SAGE locomotor values converted to the world's fixed tick. Speed,
/// damaged speed, and minimum turn speed are world units/tick; acceleration and
/// braking are world units/tick squared; TurnRate is degrees/tick. The reform
/// threshold is an angle in degrees and therefore is not time-scaled.
/// </summary>
public sealed record Locomotor
{
    public Fixed64 Speed { get; }
    public Fixed64 SpeedDamaged { get; }
    public Fixed64 TurnRate { get; }
    public Fixed64 Acceleration { get; }
    public Fixed64 Braking { get; }
    public Fixed64 MinTurnSpeed { get; }
    public Fixed64 MaxTurnWithoutReform { get; }

    public Fixed64 TurnRateRadians => FixedAngles.DegreesToRadians(TurnRate);

    public Locomotor(ModuleSpec spec, int tickMilliseconds)
        : this(ReadSpec(spec), tickMilliseconds)
    {
    }

    public Locomotor(IReadOnlyDictionary<string, long> sageValues, int tickMilliseconds)
        : this(ReadIntegers(sageValues), tickMilliseconds, valuesAreFixed: true)
    {
    }

    public Locomotor(IReadOnlyDictionary<string, Fixed64> sageValues, int tickMilliseconds)
        : this(sageValues, tickMilliseconds, valuesAreFixed: true)
    {
    }

    private Locomotor(
        IReadOnlyDictionary<string, Fixed64> values,
        int tickMilliseconds,
        bool valuesAreFixed)
    {
        _ = valuesAreFixed;
        if (tickMilliseconds < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(tickMilliseconds));
        }
        var speed = Read(values, "Speed", Fixed64.Zero);
        var damaged = Read(values, "SpeedDamaged", speed);
        var acceleration = Read(values, "Acceleration", Fixed64.Zero);
        var braking = Read(values, "Braking", acceleration);
        Speed = Scale(speed, tickMilliseconds, 1000);
        SpeedDamaged = Scale(damaged, tickMilliseconds, 1000);
        TurnRate = Scale(Read(values, "TurnRate", Fixed64.Zero), tickMilliseconds, 1000);
        var tickSquared = checked((long)tickMilliseconds * tickMilliseconds);
        Acceleration = Scale(acceleration, tickSquared, 1_000_000);
        Braking = Scale(braking, tickSquared, 1_000_000);
        MinTurnSpeed = Scale(Read(values, "MinTurnSpeed", Fixed64.Zero), tickMilliseconds, 1000);
        MaxTurnWithoutReform = Read(
            values,
            "MaxTurnWithoutReform",
            Fixed64.FromInt(-1));
    }

    private static IReadOnlyDictionary<string, Fixed64> ReadSpec(ModuleSpec spec)
    {
        ArgumentNullException.ThrowIfNull(spec);
        var result = new SortedDictionary<string, Fixed64>(StringComparer.Ordinal);
        foreach (var key in FieldNames)
        {
            if (spec.Data.TryGetValue(key + "Raw", out var raw))
            {
                result[key] = Fixed64.FromRaw(raw);
            }
            else if (spec.Data.TryGetValue(key, out var integer))
            {
                result[key] = Fixed64.FromInt64(integer);
            }
        }
        return result;
    }

    private static IReadOnlyDictionary<string, Fixed64> ReadIntegers(
        IReadOnlyDictionary<string, long> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        var result = new SortedDictionary<string, Fixed64>(StringComparer.Ordinal);
        foreach (var (key, value) in values)
        {
            result.Add(key, Fixed64.FromInt64(value));
        }
        return result;
    }

    private static Fixed64 Read(
        IReadOnlyDictionary<string, Fixed64> values,
        string key,
        Fixed64 fallback) => values.TryGetValue(key, out var value) ? value : fallback;

    private static Fixed64 Scale(Fixed64 value, long numerator, long denominator)
    {
        var raw = (System.Numerics.BigInteger)value.Raw * numerator / denominator;
        if (raw > long.MaxValue || raw < long.MinValue)
        {
            throw new OverflowException("Locomotor tick conversion overflow");
        }
        return Fixed64.FromRaw((long)raw);
    }

    private static readonly string[] FieldNames =
    {
        "Speed", "SpeedDamaged", "TurnRate", "Acceleration", "Braking",
        "MinTurnSpeed", "MaxTurnWithoutReform",
    };
}

public enum MoveOrderKind : byte
{
    Move = 1,
    AttackMove = 2,
}

/// <summary>
/// Per-object movement state. The immutable SAGE data is config; only order,
/// speed, and reform state enter the canonical module state.
/// </summary>
[SageModule("Locomotor", ModuleTier.Structural, kernel: true)]
public sealed class LocomotorModule : ModuleBase
{
    public const string TypeName = "Locomotor";

    private Locomotor? _cachedData;
    private int _cachedTickMilliseconds;

    public LocomotorModule(ModuleSpec spec) : base(spec)
    {
    }

    public bool HasOrder { get; private set; }
    public MoveOrderKind OrderKind { get; private set; }
    public FixedVector2 Destination { get; private set; }
    public Fixed64 CurrentSpeed { get; internal set; }
    public bool IsReforming { get; internal set; }
    public bool StoppedForReformLastTick { get; internal set; }

    public Locomotor DataForTick(int tickMilliseconds)
    {
        if (_cachedData == null || _cachedTickMilliseconds != tickMilliseconds)
        {
            _cachedData = new Locomotor(Spec, tickMilliseconds);
            _cachedTickMilliseconds = tickMilliseconds;
        }
        return _cachedData;
    }

    public void SetOrder(FixedVector2 destination, MoveOrderKind kind)
    {
        Destination = destination;
        OrderKind = kind;
        HasOrder = true;
    }

    public void ClearOrder()
    {
        HasOrder = false;
        IsReforming = false;
        StoppedForReformLastTick = false;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(HasOrder);
        writer.WriteByte((byte)OrderKind);
        writer.WriteVector(Destination);
        writer.WriteFixed(CurrentSpeed);
        writer.WriteBool(IsReforming);
        writer.WriteBool(StoppedForReformLastTick);
    }

    public override void ReadState(CanonicalReader reader)
    {
        HasOrder = reader.ReadBool();
        OrderKind = (MoveOrderKind)reader.ReadByte();
        Destination = reader.ReadVector();
        CurrentSpeed = reader.ReadFixed();
        IsReforming = reader.ReadBool();
        StoppedForReformLastTick = reader.ReadBool();
    }
}
