using OpenBfme.Sim.Pathing;

namespace OpenBfme.Sim;

public sealed record HordeMovementView(
    int HordeId,
    FixedVector2 LeaderPosition,
    Fixed64 LeaderHeading,
    Fixed64 CurrentSpeed,
    bool HasOrder,
    bool IsReforming,
    bool StoppedForReformLastTick,
    bool IsSettling);

/// <summary>
/// Deterministic object and horde locomotion over goal-rooted flow fields.
/// The cache is derived state and is invalidated whenever the immutable grid
/// instance changes. Authoritative per-object state lives in LocomotorModule;
/// virtual horde-leader state is serialized by SimWorld's extension block.
/// </summary>
public sealed class MovementSystem
{
    private static readonly Fixed64 ArrivalDistance = Fixed64.One;
    private static readonly Fixed64 ArrivalDistanceSquared = Fixed64.One;
    private static readonly Fixed64 MemberSlotTolerance = Fixed64.FromFraction(1, 4);
    private static readonly Fixed64 MemberSlotToleranceSquared =
        MemberSlotTolerance * MemberSlotTolerance;
    private static readonly Fixed64 HalfCell = Fixed64.Half;
    private static readonly Fixed64 InverseSquareRootTwo = Fixed64.FromRaw(3_037_000_499L);

    private readonly SortedDictionary<int, FlowField> _flowFields = new();
    private readonly SortedDictionary<int, HordeMotion> _hordeMotions = new();

    public MovementSystem(PassabilityGrid grid)
    {
        Grid = grid ?? throw new ArgumentNullException(nameof(grid));
    }

    public PassabilityGrid Grid { get; private set; }
    public int CachedFlowFieldCount => _flowFields.Count;
    internal IReadOnlyDictionary<int, HordeMotion> HordeMotions => _hordeMotions;

    public void ReplaceGrid(PassabilityGrid grid)
    {
        ArgumentNullException.ThrowIfNull(grid);
        if (ReferenceEquals(Grid, grid)) return;
        Grid = grid;
        _flowFields.Clear();
    }

    public bool CanReach(FixedVector2 destination)
    {
        var x = destination.X.ToIntFloor();
        var y = destination.Y.ToIntFloor();
        return Grid.IsPassable(x, y);
    }

    public void Tick(SimWorld world)
    {
        ArgumentNullException.ThrowIfNull(world);
        var memberTargets = BuildHordeMemberTargets(world);
        foreach (var gameObject in world.Objects.Values)
        {
            if (gameObject.IsDead || gameObject.IsDying) continue;
            if (memberTargets.TryGetValue(gameObject.Id, out var memberTarget))
            {
                StepMember(world, gameObject, memberTarget);
            }
            else
            {
                StepObject(world, gameObject);
            }
        }
        FinishHordeReforms(world);
    }

    public bool SetHordeOrder(
        SimWorld world,
        SnapshotHorde horde,
        FixedVector2 destination,
        MoveOrderKind kind)
    {
        ArgumentNullException.ThrowIfNull(world);
        ArgumentNullException.ThrowIfNull(horde);
        if (!CanReach(destination) || !TryHordeLocomotor(world, horde, out _)) return false;
        if (!_hordeMotions.TryGetValue(horde.Id, out var motion))
        {
            motion = CreateHordeMotion(world, horde);
            _hordeMotions.Add(horde.Id, motion);
        }
        motion.Destination = destination;
        motion.OrderKind = kind;
        motion.HasOrder = true;
        motion.IsSettling = false;
        return true;
    }

    public void StopHorde(int hordeId)
    {
        if (_hordeMotions.TryGetValue(hordeId, out var motion))
        {
            motion.HasOrder = false;
            motion.IsReforming = false;
            motion.IsSettling = false;
            motion.StoppedForReformLastTick = false;
        }
    }

    internal void RemoveHorde(int hordeId) => _hordeMotions.Remove(hordeId);

    public bool TryGetHordeState(int hordeId, out HordeMovementView? view)
    {
        if (_hordeMotions.TryGetValue(hordeId, out var motion))
        {
            view = new HordeMovementView(
                hordeId,
                motion.LeaderPosition,
                motion.LeaderHeading,
                motion.CurrentSpeed,
                motion.HasOrder,
                motion.IsReforming,
                motion.StoppedForReformLastTick,
                motion.IsSettling);
            return true;
        }
        view = null;
        return false;
    }

    public FixedVector2 GetHordeSlotPosition(SnapshotHorde horde, int memberIndex)
    {
        ArgumentNullException.ThrowIfNull(horde);
        if ((uint)memberIndex >= (uint)horde.Members.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(memberIndex));
        }
        if (!_hordeMotions.TryGetValue(horde.Id, out var motion))
        {
            throw new KeyNotFoundException($"Horde {horde.Id} has no movement state");
        }
        return SlotPosition(motion, horde.Members.Count, memberIndex);
    }

    internal void RestoreHordeMotion(HordeMotion motion) => _hordeMotions.Add(motion.HordeId, motion);

    private SortedDictionary<int, MemberTarget> BuildHordeMemberTargets(SimWorld world)
    {
        var targets = new SortedDictionary<int, MemberTarget>();
        foreach (var horde in world.Hordes)
        {
            if (!_hordeMotions.TryGetValue(horde.Id, out var motion)) continue;
            StepHordeLeader(world, horde, motion);
            for (var index = 0; index < horde.Members.Count; index++)
            {
                var memberId = horde.Members[index];
                if (world.Objects.ContainsKey(memberId))
                {
                    targets.Add(memberId, new MemberTarget(
                        horde.Id,
                        SlotPosition(motion, horde.Members.Count, index),
                        motion.LeaderHeading));
                }
            }
        }
        return targets;
    }

    private void StepHordeLeader(SimWorld world, SnapshotHorde horde, HordeMotion motion)
    {
        motion.StoppedForReformLastTick = false;
        if (!motion.HasOrder)
        {
            motion.CurrentSpeed = Decelerate(motion.CurrentSpeed, HordeData(world, horde).Braking);
            SynchronizeCarrier(world, horde, motion);
            return;
        }

        var locomotor = HordeData(world, horde);
        var distanceSquared = motion.LeaderPosition.DistanceSquaredTo(motion.Destination);
        if (distanceSquared <= ArrivalDistanceSquared)
        {
            motion.CurrentSpeed = Fixed64.Zero;
            motion.IsSettling = true;
            SynchronizeCarrier(world, horde, motion);
            return;
        }

        var field = FieldFor(motion.Destination);
        var direction = DirectionAt(field, motion.LeaderPosition);
        if (direction == FlowDirection.None)
        {
            motion.CurrentSpeed = Fixed64.Zero;
            SynchronizeCarrier(world, horde, motion);
            return;
        }
        var desiredHeading = FixedAngles.ForDirection(direction.X, direction.Y);
        var headingError = Fixed64.Abs(FixedAngles.ShortestDelta(motion.LeaderHeading, desiredHeading));
        var threshold = locomotor.MaxTurnWithoutReform;
        var mustReform = threshold >= Fixed64.Zero
            && headingError > FixedAngles.DegreesToRadians(threshold);
        motion.IsReforming |= mustReform;
        motion.LeaderHeading = FixedAngles.TurnTowards(
            motion.LeaderHeading,
            desiredHeading,
            locomotor.TurnRateRadians);

        if (motion.IsReforming)
        {
            motion.CurrentSpeed = Decelerate(motion.CurrentSpeed, locomotor.Braking);
            motion.StoppedForReformLastTick = true;
            SynchronizeCarrier(world, horde, motion);
            return;
        }

        motion.CurrentSpeed = SpeedForStep(
            locomotor,
            motion.CurrentSpeed,
            Fixed64.Sqrt(distanceSquared));
        motion.LeaderPosition = StepFlow(field, motion.LeaderPosition, motion.CurrentSpeed);
        if (IsInGoalCell(motion.LeaderPosition, motion.Destination)
            || motion.LeaderPosition.DistanceSquaredTo(motion.Destination) <= ArrivalDistanceSquared)
        {
            motion.LeaderPosition = motion.Destination;
            motion.CurrentSpeed = Fixed64.Zero;
            motion.IsSettling = true;
        }
        SynchronizeCarrier(world, horde, motion);
    }

    private void StepObject(SimWorld world, GameObject gameObject)
    {
        var module = gameObject.FindModule<LocomotorModule>();
        if (module == null) return;
        module.StoppedForReformLastTick = false;
        var locomotor = module.DataForTick(world.TickMilliseconds);
        if (!module.HasOrder)
        {
            module.CurrentSpeed = Decelerate(module.CurrentSpeed, locomotor.Braking);
            return;
        }
        var distanceSquared = gameObject.Position.DistanceSquaredTo(module.Destination);
        if (distanceSquared <= ArrivalDistanceSquared)
        {
            module.CurrentSpeed = Fixed64.Zero;
            module.ClearOrder();
            return;
        }
        var field = FieldFor(module.Destination);
        var direction = DirectionAt(field, gameObject.Position);
        if (direction == FlowDirection.None)
        {
            module.CurrentSpeed = Fixed64.Zero;
            return;
        }
        var desiredHeading = FixedAngles.ForDirection(direction.X, direction.Y);
        var heading = FixedAngles.TurnTowards(
            gameObject.HeadingRadians,
            desiredHeading,
            locomotor.TurnRateRadians);
        module.CurrentSpeed = SpeedForStep(
            locomotor,
            module.CurrentSpeed,
            Fixed64.Sqrt(distanceSquared));
        var position = StepFlow(field, gameObject.Position, module.CurrentSpeed);
        if (IsInGoalCell(position, module.Destination)) position = module.Destination;
        gameObject.SetTransform(position, gameObject.Elevation, heading);
        if (position.DistanceSquaredTo(module.Destination) <= ArrivalDistanceSquared)
        {
            module.CurrentSpeed = Fixed64.Zero;
            module.ClearOrder();
        }
    }

    private void StepMember(SimWorld world, GameObject member, MemberTarget target)
    {
        var module = member.FindModule<LocomotorModule>();
        if (module == null) return;
        var distanceSquared = member.Position.DistanceSquaredTo(target.Position);
        if (distanceSquared <= MemberSlotToleranceSquared)
        {
            member.SetTransform(target.Position, member.Elevation, target.Heading);
            module.CurrentSpeed = Fixed64.Zero;
            return;
        }
        var locomotor = module.DataForTick(world.TickMilliseconds);
        var distance = Fixed64.Sqrt(distanceSquared);
        module.CurrentSpeed = Fixed64.Min(
            locomotor.Speed,
            module.CurrentSpeed + locomotor.Acceleration);
        var step = Fixed64.Min(distance, module.CurrentSpeed);
        var offset = target.Position - member.Position;
        var position = member.Position + offset * (step / distance);
        if (!Grid.IsPassable(position.X.ToIntFloor(), position.Y.ToIntFloor()))
        {
            var goalX = target.Position.X.ToIntFloor();
            var goalY = target.Position.Y.ToIntFloor();
            if (Grid.IsPassable(goalX, goalY))
            {
                position = StepFlow(FieldFor(target.Position), member.Position, step);
            }
            else
            {
                position = member.Position;
                module.CurrentSpeed = Decelerate(module.CurrentSpeed, locomotor.Braking);
            }
        }
        member.SetTransform(position, member.Elevation, target.Heading);
    }

    private void FinishHordeReforms(SimWorld world)
    {
        foreach (var horde in world.Hordes)
        {
            if (!_hordeMotions.TryGetValue(horde.Id, out var motion)) continue;
            var allSlotted = true;
            for (var index = 0; index < horde.Members.Count; index++)
            {
                if (!world.Objects.TryGetValue(horde.Members[index], out var member)) continue;
                if (member.Position.DistanceSquaredTo(SlotPosition(motion, horde.Members.Count, index))
                    > MemberSlotToleranceSquared)
                {
                    allSlotted = false;
                    break;
                }
            }
            if (!allSlotted) continue;

            if (motion.IsReforming)
            {
                var field = FieldFor(motion.Destination);
                var direction = DirectionAt(field, motion.LeaderPosition);
                var desired = direction == FlowDirection.None
                    ? motion.LeaderHeading
                    : FixedAngles.ForDirection(direction.X, direction.Y);
                if (Fixed64.Abs(FixedAngles.ShortestDelta(motion.LeaderHeading, desired))
                    <= HordeData(world, horde).TurnRateRadians)
                {
                    motion.IsReforming = false;
                }
            }
            if (motion.IsSettling)
            {
                motion.HasOrder = false;
                motion.IsSettling = false;
            }
        }
    }

    private FlowField FieldFor(FixedVector2 destination)
    {
        var goalX = destination.X.ToIntFloor();
        var goalY = destination.Y.ToIntFloor();
        var key = Grid.IndexOf(goalX, goalY);
        if (!_flowFields.TryGetValue(key, out var field))
        {
            field = FlowField.Build(Grid, goalX, goalY);
            _flowFields.Add(key, field);
        }
        return field;
    }

    private static FlowDirection DirectionAt(FlowField field, FixedVector2 position)
    {
        var x = position.X.ToIntFloor();
        var y = position.Y.ToIntFloor();
        return field.Grid.IsPassable(x, y) ? field.DirectionAt(x, y) : FlowDirection.None;
    }

    private static FixedVector2 StepFlow(FlowField field, FixedVector2 position, Fixed64 distance)
    {
        var remaining = distance;
        while (remaining > Fixed64.Zero)
        {
            var direction = DirectionAt(field, position);
            if (direction == FlowDirection.None) break;
            var quantum = Fixed64.Min(remaining, HalfCell);
            var scale = direction.X != 0 && direction.Y != 0
                ? quantum * InverseSquareRootTwo
                : quantum;
            var dx = direction.X == 0 ? Fixed64.Zero : direction.X > 0 ? scale : -scale;
            var dy = direction.Y == 0 ? Fixed64.Zero : direction.Y > 0 ? scale : -scale;
            var candidate = new FixedVector2(position.X + dx, position.Y + dy);
            if (!field.Grid.IsPassable(candidate.X.ToIntFloor(), candidate.Y.ToIntFloor())) break;
            position = candidate;
            remaining -= quantum;
        }
        return position;
    }

    private static Fixed64 SpeedForStep(Locomotor data, Fixed64 current, Fixed64 distance)
    {
        _ = distance;
        return Fixed64.Min(data.Speed, current + data.Acceleration);
    }

    private static bool IsInGoalCell(FixedVector2 position, FixedVector2 destination) =>
        position.X.ToIntFloor() == destination.X.ToIntFloor()
        && position.Y.ToIntFloor() == destination.Y.ToIntFloor();

    private static Fixed64 Decelerate(Fixed64 speed, Fixed64 braking) =>
        Fixed64.Max(Fixed64.Zero, speed - braking);

    private static HordeMotion CreateHordeMotion(SimWorld world, SnapshotHorde horde)
    {
        if (world.Objects.TryGetValue(horde.Id, out var carrier))
        {
            return new HordeMotion(horde.Id, carrier.Position, carrier.HeadingRadians);
        }
        long x = 0;
        long y = 0;
        var count = 0;
        var heading = Fixed64.Zero;
        foreach (var memberId in horde.Members)
        {
            if (!world.Objects.TryGetValue(memberId, out var member)) continue;
            x = checked(x + member.Position.X.Raw);
            y = checked(y + member.Position.Y.Raw);
            if (count == 0) heading = member.HeadingRadians;
            count++;
        }
        if (count == 0) throw new InvalidOperationException($"Horde {horde.Id} has no live members");
        return new HordeMotion(
            horde.Id,
            new FixedVector2(Fixed64.FromRaw(x / count), Fixed64.FromRaw(y / count)),
            heading);
    }

    private static bool TryHordeLocomotor(
        SimWorld world,
        SnapshotHorde horde,
        out LocomotorModule? module)
    {
        if (world.Objects.TryGetValue(horde.Id, out var carrier)
            && carrier.FindModule<LocomotorModule>() is { } carrierLocomotor)
        {
            module = carrierLocomotor;
            return true;
        }
        foreach (var memberId in horde.Members)
        {
            if (world.Objects.TryGetValue(memberId, out var member)
                && member.FindModule<LocomotorModule>() is { } memberLocomotor)
            {
                module = memberLocomotor;
                return true;
            }
        }
        module = null;
        return false;
    }

    private static Locomotor HordeData(SimWorld world, SnapshotHorde horde)
    {
        if (!TryHordeLocomotor(world, horde, out var module))
        {
            throw new InvalidOperationException($"Horde {horde.Id} has no locomotor");
        }
        return module!.DataForTick(world.TickMilliseconds);
    }

    private static void SynchronizeCarrier(SimWorld world, SnapshotHorde horde, HordeMotion motion)
    {
        if (world.Objects.TryGetValue(horde.Id, out var carrier)
            && !horde.Members.Contains(carrier.Id))
        {
            carrier.SetTransform(motion.LeaderPosition, carrier.Elevation, motion.LeaderHeading);
        }
    }

    private static FixedVector2 SlotPosition(HordeMotion motion, int memberCount, int memberIndex)
    {
        var columns = 1;
        while (columns * columns < memberCount) columns++;
        var rows = (memberCount + columns - 1) / columns;
        var column = memberIndex % columns;
        var row = memberIndex / columns;
        var spacing = Fixed64.FromInt(2);
        var local = new FixedVector2(
            Fixed64.FromFraction(2L * column - (columns - 1), 2) * spacing,
            Fixed64.FromFraction(2L * row - (rows - 1), 2) * spacing);
        return motion.LeaderPosition + FixedAngles.Rotate(local, motion.LeaderHeading);
    }

    private readonly record struct MemberTarget(int HordeId, FixedVector2 Position, Fixed64 Heading);
}

internal sealed class HordeMotion
{
    public HordeMotion(int hordeId, FixedVector2 leaderPosition, Fixed64 leaderHeading)
    {
        HordeId = hordeId;
        LeaderPosition = leaderPosition;
        LeaderHeading = leaderHeading;
    }

    public int HordeId { get; }
    public FixedVector2 LeaderPosition { get; set; }
    public Fixed64 LeaderHeading { get; set; }
    public Fixed64 CurrentSpeed { get; set; }
    public FixedVector2 Destination { get; set; }
    public MoveOrderKind OrderKind { get; set; }
    public bool HasOrder { get; set; }
    public bool IsReforming { get; set; }
    public bool StoppedForReformLastTick { get; set; }
    public bool IsSettling { get; set; }
}
