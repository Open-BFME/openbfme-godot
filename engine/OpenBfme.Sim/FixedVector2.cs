namespace OpenBfme.Sim;

/// <summary>World-plane vector (X/Z ground coordinates), fixed-point throughout.</summary>
public readonly struct FixedVector2 : IEquatable<FixedVector2>
{
    public static readonly FixedVector2 Zero = new(Fixed64.Zero, Fixed64.Zero);

    public Fixed64 X { get; }
    public Fixed64 Y { get; }

    public FixedVector2(Fixed64 x, Fixed64 y)
    {
        X = x;
        Y = y;
    }

    public static FixedVector2 operator +(FixedVector2 a, FixedVector2 b) => new(a.X + b.X, a.Y + b.Y);
    public static FixedVector2 operator -(FixedVector2 a, FixedVector2 b) => new(a.X - b.X, a.Y - b.Y);
    public static FixedVector2 operator *(FixedVector2 a, Fixed64 scalar) => new(a.X * scalar, a.Y * scalar);

    public static bool operator ==(FixedVector2 a, FixedVector2 b) => a.X == b.X && a.Y == b.Y;
    public static bool operator !=(FixedVector2 a, FixedVector2 b) => !(a == b);

    public Fixed64 LengthSquared() => X * X + Y * Y;
    public Fixed64 Length() => Fixed64.Sqrt(LengthSquared());

    public Fixed64 DistanceSquaredTo(FixedVector2 other) => (this - other).LengthSquared();

    /// <summary>Zero vector normalizes to zero rather than throwing — callers branch on it.</summary>
    public FixedVector2 Normalized()
    {
        var length = Length();
        return length == Fixed64.Zero ? Zero : new FixedVector2(X / length, Y / length);
    }

    public bool Equals(FixedVector2 other) => this == other;
    public override bool Equals(object? obj) => obj is FixedVector2 other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(X.Raw, Y.Raw);
    public override string ToString() => $"({X}, {Y})";
}
