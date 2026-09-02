namespace OpenBfme.Sim.Pathing;

public readonly record struct FlowDirection(sbyte X, sbyte Y)
{
    public static readonly FlowDirection None = new(0, 0);
}

/// <summary>
/// Goal-rooted integration and 8-way direction fields. Equal-cost work is
/// ordered by row-major cell index and neighbors use one fixed compass order.
/// </summary>
public sealed class FlowField
{
    private static readonly FlowDirection[] NeighborOrder =
    {
        new(0, -1),
        new(1, -1),
        new(1, 0),
        new(1, 1),
        new(0, 1),
        new(-1, 1),
        new(-1, 0),
        new(-1, -1),
    };

    private FlowField(PassabilityGrid grid, int goalX, int goalY, long[] integration, FlowDirection[] direction)
    {
        Grid = grid;
        GoalX = goalX;
        GoalY = goalY;
        Integration = integration;
        Direction = direction;
    }

    public PassabilityGrid Grid { get; }
    public int GoalX { get; }
    public int GoalY { get; }
    public long[] Integration { get; }
    public FlowDirection[] Direction { get; }

    public FlowDirection DirectionAt(int x, int y) => Direction[Grid.IndexOf(x, y)];

    public static FlowField Build(PassabilityGrid grid, int goalX, int goalY)
    {
        ArgumentNullException.ThrowIfNull(grid);
        if (!grid.IsPassable(goalX, goalY))
        {
            throw new ArgumentException($"Goal cell ({goalX}, {goalY}) is not passable");
        }

        var integration = Enumerable.Repeat(long.MaxValue, grid.CellCount).ToArray();
        var direction = new FlowDirection[grid.CellCount];
        var frontier = new SortedSet<FrontierEntry>();
        var goalIndex = grid.IndexOf(goalX, goalY);
        integration[goalIndex] = 0;
        frontier.Add(new FrontierEntry(0, goalIndex));

        while (frontier.Count > 0)
        {
            var current = frontier.Min;
            frontier.Remove(current);
            if (integration[current.Index] != current.Cost)
            {
                continue;
            }
            var (x, y) = grid.CoordinatesOf(current.Index);
            foreach (var step in NeighborOrder)
            {
                var neighborX = x + step.X;
                var neighborY = y + step.Y;
                if (!CanTraverse(grid, x, y, neighborX, neighborY))
                {
                    continue;
                }
                var neighborIndex = grid.IndexOf(neighborX, neighborY);
                var candidate = checked(current.Cost + grid.Cost(neighborX, neighborY));
                if (candidate >= integration[neighborIndex])
                {
                    continue;
                }
                integration[neighborIndex] = candidate;
                frontier.Add(new FrontierEntry(candidate, neighborIndex));
            }
        }

        for (var index = 0; index < grid.CellCount; index++)
        {
            if (index == goalIndex || integration[index] == long.MaxValue)
            {
                continue;
            }
            var (x, y) = grid.CoordinatesOf(index);
            var bestCost = integration[index];
            var best = FlowDirection.None;
            foreach (var step in NeighborOrder)
            {
                var neighborX = x + step.X;
                var neighborY = y + step.Y;
                if (!CanTraverse(grid, x, y, neighborX, neighborY))
                {
                    continue;
                }
                var neighborCost = integration[grid.IndexOf(neighborX, neighborY)];
                if (neighborCost < bestCost)
                {
                    bestCost = neighborCost;
                    best = step;
                }
            }
            direction[index] = best;
        }

        return new FlowField(grid, goalX, goalY, integration, direction);
    }

    private static bool CanTraverse(PassabilityGrid grid, int fromX, int fromY, int toX, int toY)
    {
        if (!grid.IsPassable(toX, toY))
        {
            return false;
        }
        var dx = toX - fromX;
        var dy = toY - fromY;
        return dx == 0 || dy == 0
            || (grid.IsPassable(fromX + dx, fromY) && grid.IsPassable(fromX, fromY + dy));
    }

    private readonly record struct FrontierEntry(long Cost, int Index) : IComparable<FrontierEntry>
    {
        public int CompareTo(FrontierEntry other)
        {
            var byCost = Cost.CompareTo(other.Cost);
            return byCost != 0 ? byCost : Index.CompareTo(other.Index);
        }
    }
}
