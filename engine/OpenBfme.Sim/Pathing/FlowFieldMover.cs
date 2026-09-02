namespace OpenBfme.Sim.Pathing;

/// <summary>Stateless deterministic agent stepper over a flow field.</summary>
public static class FlowFieldMover
{
    private static readonly Fixed64 InverseSquareRootTwo = Fixed64.FromRaw(3_037_000_499L);

    public static FixedVector2 Step(FlowField field, FixedVector2 position, Fixed64 speed)
    {
        ArgumentNullException.ThrowIfNull(field);
        if (speed < Fixed64.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(speed));
        }
        var cellX = position.X.ToIntFloor();
        var cellY = position.Y.ToIntFloor();
        if (!field.Grid.IsInside(cellX, cellY)
            || !field.Grid.IsPassable(cellX, cellY)
            || (cellX == field.GoalX && cellY == field.GoalY)
            || speed == Fixed64.Zero)
        {
            return position;
        }
        var direction = field.DirectionAt(cellX, cellY);
        if (direction == FlowDirection.None)
        {
            return position;
        }
        var scale = direction.X != 0 && direction.Y != 0
            ? speed * InverseSquareRootTwo
            : speed;
        var deltaX = direction.X == 0
            ? Fixed64.Zero
            : direction.X > 0 ? scale : -scale;
        var deltaY = direction.Y == 0
            ? Fixed64.Zero
            : direction.Y > 0 ? scale : -scale;
        return new FixedVector2(position.X + deltaX, position.Y + deltaY);
    }
}
