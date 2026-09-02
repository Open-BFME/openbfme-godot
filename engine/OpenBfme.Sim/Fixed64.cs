namespace OpenBfme.Sim;

/// <summary>
/// Q32.32 signed fixed-point number. The only scalar type permitted in simulation
/// math — floats are banned from authoritative state so that every platform
/// produces bit-identical results under lockstep.
/// </summary>
public readonly struct Fixed64 : IEquatable<Fixed64>, IComparable<Fixed64>
{
    public const int FractionBits = 32;
    public const long OneRaw = 1L << FractionBits;

    public static readonly Fixed64 Zero = new(0);
    public static readonly Fixed64 One = new(OneRaw);
    public static readonly Fixed64 Half = new(OneRaw >> 1);
    public static readonly Fixed64 MaxValue = new(long.MaxValue);
    public static readonly Fixed64 MinValue = new(long.MinValue);

    public long Raw { get; }

    private Fixed64(long raw) => Raw = raw;

    public static Fixed64 FromRaw(long raw) => new(raw);
    public static Fixed64 FromInt(int value) => new((long)value << FractionBits);
    public static Fixed64 FromInt64(long value)
    {
        var raw = (System.Numerics.BigInteger)value << FractionBits;
        if (raw > long.MaxValue || raw < long.MinValue)
        {
            throw new OverflowException("Fixed64 integer conversion overflow");
        }
        return new Fixed64((long)raw);
    }

    /// <summary>Exact rational construction; the only sanctioned bridge from design data.</summary>
    public static Fixed64 FromFraction(long numerator, long denominator)
    {
        if (denominator == 0)
        {
            throw new DivideByZeroException("Fixed64.FromFraction denominator is zero");
        }
        return new Fixed64((long)(((System.Numerics.BigInteger)numerator << FractionBits) / denominator));
    }

    public int ToIntFloor() => (int)(Raw >> FractionBits);

    public static Fixed64 operator +(Fixed64 a, Fixed64 b) => new(checked(a.Raw + b.Raw));
    public static Fixed64 operator -(Fixed64 a, Fixed64 b) => new(checked(a.Raw - b.Raw));
    public static Fixed64 operator -(Fixed64 a) => new(checked(-a.Raw));

    public static Fixed64 operator *(Fixed64 a, Fixed64 b)
    {
        var product = (Int128)a.Raw * b.Raw >> FractionBits;
        if (product > long.MaxValue || product < long.MinValue)
        {
            throw new OverflowException("Fixed64 multiplication overflow");
        }
        return new Fixed64((long)product);
    }

    public static Fixed64 operator /(Fixed64 a, Fixed64 b)
    {
        if (b.Raw == 0)
        {
            throw new DivideByZeroException("Fixed64 division by zero");
        }
        var quotient = ((Int128)a.Raw << FractionBits) / b.Raw;
        if (quotient > long.MaxValue || quotient < long.MinValue)
        {
            throw new OverflowException("Fixed64 division overflow");
        }
        return new Fixed64((long)quotient);
    }

    public static bool operator <(Fixed64 a, Fixed64 b) => a.Raw < b.Raw;
    public static bool operator >(Fixed64 a, Fixed64 b) => a.Raw > b.Raw;
    public static bool operator <=(Fixed64 a, Fixed64 b) => a.Raw <= b.Raw;
    public static bool operator >=(Fixed64 a, Fixed64 b) => a.Raw >= b.Raw;
    public static bool operator ==(Fixed64 a, Fixed64 b) => a.Raw == b.Raw;
    public static bool operator !=(Fixed64 a, Fixed64 b) => a.Raw != b.Raw;

    public static Fixed64 Min(Fixed64 a, Fixed64 b) => a.Raw <= b.Raw ? a : b;
    public static Fixed64 Max(Fixed64 a, Fixed64 b) => a.Raw >= b.Raw ? a : b;
    public static Fixed64 Abs(Fixed64 a) => a.Raw < 0 ? new Fixed64(checked(-a.Raw)) : a;

    public static Fixed64 Clamp(Fixed64 value, Fixed64 min, Fixed64 max)
    {
        if (min > max)
        {
            throw new ArgumentException("Fixed64.Clamp min exceeds max");
        }
        return value < min ? min : value > max ? max : value;
    }

    /// <summary>Integer Newton-Raphson square root; deterministic on every platform.</summary>
    public static Fixed64 Sqrt(Fixed64 value)
    {
        if (value.Raw < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(value), "Fixed64.Sqrt of negative value");
        }
        if (value.Raw == 0)
        {
            return Zero;
        }
        // Monotone integer Newton (x, y=(x+t/x)/2 while y<x) — terminates for every
        // input and returns floor(sqrt(t)). The naive `while (estimate != previous)`
        // form oscillates forever between k and k+1 for some inputs (e.g. t=8).
        var target = (UInt128)value.Raw << FractionBits;
        var x = target;
        var y = (x + 1) >> 1;
        while (y < x)
        {
            x = y;
            y = (x + target / x) >> 1;
        }
        return new Fixed64((long)x);
    }

    public bool Equals(Fixed64 other) => Raw == other.Raw;
    public override bool Equals(object? obj) => obj is Fixed64 other && Equals(other);
    public override int GetHashCode() => Raw.GetHashCode();
    public int CompareTo(Fixed64 other) => Raw.CompareTo(other.Raw);

    public override string ToString()
    {
        // Diagnostic only — never feeds simulation math.
        return ((double)Raw / OneRaw).ToString("0.######", System.Globalization.CultureInfo.InvariantCulture);
    }
}
