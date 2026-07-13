namespace OpenBfme.Stage1;

/// <summary>Integer-only authoritative world position. One grid cell is 1,000 subcells.</summary>
public readonly record struct WorldPos(int X, int Y)
{
    public static long DistanceSquared(WorldPos a, WorldPos b)
    {
        long dx = (long)a.X - b.X;
        long dy = (long)a.Y - b.Y;
        return (dx * dx) + (dy * dy);
    }
}

public readonly record struct GridCell(int X, int Y);

public enum TeamId
{
    None = -1,
    Blue = 0,
    Red = 1,
}

public enum OrderKind
{
    Stop = 0,
    Move = 1,
    AttackMove = 2,
    AttackTarget = 3,
}

public readonly record struct SimCommand(
    int ExecuteTick,
    int Sequence,
    int HordeId,
    OrderKind Kind,
    WorldPos Destination,
    int TargetEntityId = 0);

public static class IntegerMath
{
    public static int SquareRoot(long value)
    {
        if (value <= 0)
        {
            return 0;
        }

        ulong n = (ulong)value;
        ulong result = 0;
        ulong bit = 1UL << 62;
        while (bit > n)
        {
            bit >>= 2;
        }

        while (bit != 0)
        {
            if (n >= result + bit)
            {
                n -= result + bit;
                result = (result >> 1) + bit;
            }
            else
            {
                result >>= 1;
            }

            bit >>= 2;
        }

        return result > int.MaxValue ? int.MaxValue : (int)result;
    }

    public static WorldPos MoveTowards(WorldPos from, WorldPos to, int maximumStep)
    {
        int dx = to.X - from.X;
        int dy = to.Y - from.Y;
        long distanceSquared = ((long)dx * dx) + ((long)dy * dy);
        if (distanceSquared == 0 || distanceSquared <= (long)maximumStep * maximumStep)
        {
            return to;
        }

        int distance = SquareRoot(distanceSquared);
        if (distance == 0)
        {
            return from;
        }

        int stepX = (int)((long)dx * maximumStep / distance);
        int stepY = (int)((long)dy * maximumStep / distance);
        if (stepX == 0 && dx != 0)
        {
            stepX = Math.Sign(dx);
        }

        if (stepY == 0 && dy != 0)
        {
            stepY = Math.Sign(dy);
        }

        return new WorldPos(from.X + stepX, from.Y + stepY);
    }
}
