namespace OpenBfme.Stage1;

/// <summary>A legal-safe integer grid and deterministic four-neighbor A*.</summary>
public sealed class NavigationGrid
{
    public const int CellSize = 1_000;

    private static readonly GridCell[] NeighborOffsets =
    [
        new(0, -1), // north
        new(1, 0),  // east
        new(0, 1),  // south
        new(-1, 0), // west
    ];

    private readonly bool[] _staticBlocked;
    private readonly int[] _dynamicBlockOwners;

    public NavigationGrid(int width, int height)
    {
        if (width < 3 || height < 3)
        {
            throw new ArgumentOutOfRangeException(nameof(width), "The grid must be at least 3x3.");
        }

        Width = width;
        Height = height;
        _staticBlocked = new bool[width * height];
        _dynamicBlockOwners = new int[width * height];
    }

    public int Width { get; }
    public int Height { get; }

    public void SetBlocked(GridCell cell, bool blocked = true)
    {
        if (!Contains(cell))
        {
            throw new ArgumentOutOfRangeException(nameof(cell));
        }

        _staticBlocked[Index(cell)] = blocked;
    }

    public bool IsBlocked(GridCell cell) => !Contains(cell) ||
        _staticBlocked[Index(cell)] || _dynamicBlockOwners[Index(cell)] != 0;

    public bool IsStaticallyBlocked(GridCell cell) => !Contains(cell) || _staticBlocked[Index(cell)];

    public int DynamicBlockOwner(GridCell cell) => Contains(cell) ? _dynamicBlockOwners[Index(cell)] : -1;

    public bool TrySetDynamicBlocker(int ownerId, IReadOnlyList<GridCell> cells)
    {
        if (ownerId <= 0 || cells.Count == 0)
        {
            return false;
        }

        foreach (GridCell cell in cells)
        {
            if (!Contains(cell) || IsStaticallyBlocked(cell) || _dynamicBlockOwners[Index(cell)] != 0)
            {
                return false;
            }
        }

        foreach (GridCell cell in cells)
        {
            _dynamicBlockOwners[Index(cell)] = ownerId;
        }

        return true;
    }

    public void ClearDynamicBlocker(int ownerId)
    {
        if (ownerId <= 0)
        {
            return;
        }

        for (int index = 0; index < _dynamicBlockOwners.Length; index++)
        {
            if (_dynamicBlockOwners[index] == ownerId)
            {
                _dynamicBlockOwners[index] = 0;
            }
        }
    }

    public bool IsWalkable(WorldPos position) =>
        position.X >= 0 && position.X < Width * CellSize &&
        position.Y >= 0 && position.Y < Height * CellSize &&
        !IsBlocked(ToCell(position));

    public GridCell ToCell(WorldPos position) =>
        new(Math.Clamp(position.X / CellSize, 0, Width - 1), Math.Clamp(position.Y / CellSize, 0, Height - 1));

    public static WorldPos CellCenter(GridCell cell) =>
        new((cell.X * CellSize) + (CellSize / 2), (cell.Y * CellSize) + (CellSize / 2));

    public IReadOnlyList<GridCell> FindPath(WorldPos startPosition, WorldPos destinationPosition)
    {
        GridCell start = ToCell(startPosition);
        GridCell destination = ToCell(destinationPosition);
        if (IsBlocked(start) || IsBlocked(destination))
        {
            return Array.Empty<GridCell>();
        }

        int count = Width * Height;
        int[] cost = new int[count];
        int[] parent = new int[count];
        bool[] closed = new bool[count];
        Array.Fill(cost, int.MaxValue);
        Array.Fill(parent, -1);

        int startIndex = Index(start);
        int destinationIndex = Index(destination);
        cost[startIndex] = 0;

        MinHeap frontier = new();
        int sequence = 0;
        int startHeuristic = Manhattan(start, destination);
        frontier.Push(new OpenNode(startIndex, startHeuristic, startHeuristic, start.Y, start.X, sequence++));

        while (frontier.Count > 0)
        {
            OpenNode currentNode = frontier.Pop();
            int currentIndex = currentNode.CellIndex;
            if (closed[currentIndex])
            {
                continue;
            }

            closed[currentIndex] = true;
            if (currentIndex == destinationIndex)
            {
                return Reconstruct(parent, startIndex, destinationIndex);
            }

            GridCell current = FromIndex(currentIndex);
            foreach (GridCell offset in NeighborOffsets)
            {
                GridCell neighbor = new(current.X + offset.X, current.Y + offset.Y);
                if (IsBlocked(neighbor))
                {
                    continue;
                }

                int neighborIndex = Index(neighbor);
                if (closed[neighborIndex])
                {
                    continue;
                }

                int tentativeCost = cost[currentIndex] + 10;
                if (tentativeCost >= cost[neighborIndex])
                {
                    continue;
                }

                cost[neighborIndex] = tentativeCost;
                parent[neighborIndex] = currentIndex;
                int heuristic = Manhattan(neighbor, destination) * 10;
                frontier.Push(new OpenNode(
                    neighborIndex,
                    tentativeCost + heuristic,
                    heuristic,
                    neighbor.Y,
                    neighbor.X,
                    sequence++));
            }
        }

        return Array.Empty<GridCell>();
    }

    public IEnumerable<GridCell> BlockedCells()
    {
        for (int y = 0; y < Height; y++)
        {
            for (int x = 0; x < Width; x++)
            {
                GridCell cell = new(x, y);
                if (IsBlocked(cell))
                {
                    yield return cell;
                }
            }
        }
    }

    public bool Contains(GridCell cell) =>
        cell.X >= 0 && cell.X < Width && cell.Y >= 0 && cell.Y < Height;

    private int Index(GridCell cell) => (cell.Y * Width) + cell.X;

    private GridCell FromIndex(int index) => new(index % Width, index / Width);

    private static int Manhattan(GridCell a, GridCell b) => Math.Abs(a.X - b.X) + Math.Abs(a.Y - b.Y);

    private IReadOnlyList<GridCell> Reconstruct(int[] parent, int startIndex, int destinationIndex)
    {
        List<GridCell> result = [];
        int cursor = destinationIndex;
        while (cursor != -1)
        {
            result.Add(FromIndex(cursor));
            if (cursor == startIndex)
            {
                result.Reverse();
                return result;
            }

            cursor = parent[cursor];
        }

        return Array.Empty<GridCell>();
    }

    private readonly record struct OpenNode(
        int CellIndex,
        int TotalCost,
        int Heuristic,
        int Y,
        int X,
        int Sequence) : IComparable<OpenNode>
    {
        public int CompareTo(OpenNode other)
        {
            int value = TotalCost.CompareTo(other.TotalCost);
            if (value != 0) return value;
            value = Heuristic.CompareTo(other.Heuristic);
            if (value != 0) return value;
            value = Y.CompareTo(other.Y);
            if (value != 0) return value;
            value = X.CompareTo(other.X);
            if (value != 0) return value;
            return Sequence.CompareTo(other.Sequence);
        }
    }

    private sealed class MinHeap
    {
        private readonly List<OpenNode> _items = [];

        public int Count => _items.Count;

        public void Push(OpenNode value)
        {
            _items.Add(value);
            int child = _items.Count - 1;
            while (child > 0)
            {
                int parent = (child - 1) / 2;
                if (_items[parent].CompareTo(value) <= 0)
                {
                    break;
                }

                _items[child] = _items[parent];
                child = parent;
            }

            _items[child] = value;
        }

        public OpenNode Pop()
        {
            OpenNode root = _items[0];
            int lastIndex = _items.Count - 1;
            OpenNode last = _items[lastIndex];
            _items.RemoveAt(lastIndex);
            if (_items.Count == 0)
            {
                return root;
            }

            int parent = 0;
            while (true)
            {
                int left = (parent * 2) + 1;
                if (left >= _items.Count)
                {
                    break;
                }

                int right = left + 1;
                int smallest = right < _items.Count && _items[right].CompareTo(_items[left]) < 0 ? right : left;
                if (_items[smallest].CompareTo(last) >= 0)
                {
                    break;
                }

                _items[parent] = _items[smallest];
                parent = smallest;
            }

            _items[parent] = last;
            return root;
        }
    }
}
