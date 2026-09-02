namespace OpenBfme.Sim.Pathing;

/// <summary>Immutable row-major passability and positive traversal cost grid.</summary>
public sealed class PassabilityGrid
{
    private readonly bool[] _passable;
    private readonly int[] _cost;

    public PassabilityGrid(int width, int height, bool[] passable, int[] cost)
    {
        if (width < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(width));
        }
        if (height < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(height));
        }
        var cellCount = checked(width * height);
        if (passable.Length != cellCount)
        {
            throw new ArgumentException("Passability length must equal width * height", nameof(passable));
        }
        if (cost.Length != cellCount)
        {
            throw new ArgumentException("Cost length must equal width * height", nameof(cost));
        }
        for (var index = 0; index < cellCount; index++)
        {
            if (passable[index] && cost[index] < 1)
            {
                throw new ArgumentOutOfRangeException(nameof(cost), $"Passable cell {index} must have positive cost");
            }
        }
        Width = width;
        Height = height;
        _passable = (bool[])passable.Clone();
        _cost = (int[])cost.Clone();
    }

    public int Width { get; }
    public int Height { get; }
    public int CellCount => _passable.Length;

    public bool IsInside(int x, int y) => x >= 0 && y >= 0 && x < Width && y < Height;

    public bool IsPassable(int x, int y) => IsInside(x, y) && _passable[IndexOf(x, y)];

    public int Cost(int x, int y)
    {
        if (!IsInside(x, y))
        {
            throw new ArgumentOutOfRangeException(nameof(x), $"Cell ({x}, {y}) is outside the grid");
        }
        return _cost[IndexOf(x, y)];
    }

    public int IndexOf(int x, int y)
    {
        if (!IsInside(x, y))
        {
            throw new ArgumentOutOfRangeException(nameof(x), $"Cell ({x}, {y}) is outside the grid");
        }
        return y * Width + x;
    }

    public (int X, int Y) CoordinatesOf(int index)
    {
        if ((uint)index >= (uint)CellCount)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        return (index % Width, index / Width);
    }

    public static PassabilityGrid Uniform(int width, int height, int cost = 1)
    {
        var cellCount = checked(width * height);
        return new PassabilityGrid(
            width,
            height,
            Enumerable.Repeat(true, cellCount).ToArray(),
            Enumerable.Repeat(cost, cellCount).ToArray());
    }
}
