namespace OpenBfme.Sim;

public sealed record SimDiagnostic(
    int Tick,
    int Seat,
    int Team,
    int Object,
    string Code,
    string Message);

public sealed partial class SimWorld
{
    private const int MovementExtensionMagic = 0x4D4F5645;
    private readonly List<SimDiagnostic> _diagnostics = new();
    private readonly SortedDictionary<int, int> _seatTeams = new();

    public IReadOnlyList<SimDiagnostic> Diagnostics => _diagnostics;

    public bool SubmitCommandBundle(SimCommandBundle bundle)
    {
        ArgumentNullException.ThrowIfNull(bundle);
        if (bundle.Tick <= TickIndex || !TryTeamForSeat(bundle.Seat, out var team)) return false;
        foreach (var command in bundle.Commands)
        {
            if (!SubmitCommand(command.WithTeam(team))) return false;
        }
        return true;
    }

    private bool TryTeamForSeat(int seat, out int team)
    {
        if (_seatTeams.TryGetValue(seat, out team)) return true;
        if (_seatTeams.Count == 0 && seat >= 0 && seat < _config.TeamCount)
        {
            team = seat;
            return true;
        }
        team = -1;
        return false;
    }

    private void ApplyMovementCommand(SimCommand command)
    {
        var ids = CommandObjectIds(command);
        var isStop = command.Type == "stop";
        var destination = isStop
            ? FixedVector2.Zero
            : new FixedVector2(command.GetFixed("x"), command.GetFixed("y"));
        var orderKind = command.Type == "attack_move"
            ? MoveOrderKind.AttackMove
            : MoveOrderKind.Move;

        foreach (var longId in ids)
        {
            if (longId is < 1 or > int.MaxValue)
            {
                RecordDiagnostic(command, 0, "invalid_object", $"object id {longId} is outside the simulation range");
                continue;
            }
            var id = (int)longId;
            var horde = FindHorde(id);
            if (horde != null)
            {
                if (horde.Owner != command.Team)
                {
                    RecordOwnershipDiagnostic(command, id, horde.Owner);
                    continue;
                }
                if (isStop)
                {
                    Movement.StopHorde(id);
                }
                else if (!Movement.SetHordeOrder(this, horde, destination, orderKind))
                {
                    RecordDiagnostic(command, id, "movement_unavailable",
                        $"horde {id} has no locomotor or its goal cell is impassable");
                }
                continue;
            }

            if (!_objects.TryGetValue(id, out var gameObject))
            {
                RecordDiagnostic(command, id, "unknown_object", $"object {id} does not exist");
                continue;
            }
            if (gameObject.Team != command.Team)
            {
                RecordOwnershipDiagnostic(command, id, gameObject.Team);
                continue;
            }
            var locomotor = gameObject.FindModule<LocomotorModule>();
            if (locomotor != null)
            {
                if (isStop)
                {
                    locomotor.ClearOrder();
                }
                else if (Movement.CanReach(destination))
                {
                    locomotor.SetOrder(destination, orderKind);
                }
                else
                {
                    RecordDiagnostic(command, id, "impassable_goal",
                        $"object {id} goal cell is outside the grid or impassable");
                }
                continue;
            }

            var linear = gameObject.FindModule<LinearMoverModule>();
            if (linear != null)
            {
                if (isStop) linear.ClearDestination();
                else linear.SetDestination(destination);
                continue;
            }
            RecordDiagnostic(command, id, "missing_locomotor", $"object {id} has no movement module");
        }
    }

    private static IReadOnlyList<long> CommandObjectIds(SimCommand command)
    {
        if (command.Args.TryGetValue("objects", out var objects)
            && objects.Kind == CommandValueKind.LongList)
        {
            return objects.LongListValue!;
        }
        if (command.Args.TryGetValue("object", out var single)
            && single.Kind == CommandValueKind.Long)
        {
            return new[] { single.LongValue };
        }
        if (command.Args.TryGetValue("id", out var legacy)
            && legacy.Kind == CommandValueKind.Long)
        {
            return new[] { legacy.LongValue };
        }
        throw new KeyNotFoundException($"Command '{command.Type}' has no object target");
    }

    private SnapshotHorde? FindHorde(int id)
    {
        foreach (var horde in _hordes)
        {
            if (horde.Id == id) return horde;
            if (horde.Id > id) break;
        }
        return null;
    }

    internal SnapshotHorde? FindHordeForCombat(int id) => FindHorde(id);

    internal void RecordCombatOwnershipDiagnostic(SimCommand command, int id, int ownerTeam) =>
        RecordOwnershipDiagnostic(command, id, ownerTeam);

    internal void RecordCombatDiagnostic(SimCommand command, int id, string code, string message) =>
        RecordDiagnostic(command, id, code, message);

    private void RecordOwnershipDiagnostic(SimCommand command, int id, int ownerTeam) =>
        RecordDiagnostic(
            command,
            id,
            "wrong_team",
            $"seat {command.SourceSeat} team {command.Team} cannot command object {id} owned by team {ownerTeam}");

    private void RecordDiagnostic(SimCommand command, int id, string code, string message) =>
        _diagnostics.Add(new SimDiagnostic(
            TickIndex,
            command.SourceSeat,
            command.Team,
            id,
            code,
            message));

    private void WriteMovementExtension(CanonicalWriter writer)
    {
        if (_hordes.Count == 0 && Movement.HordeMotions.Count == 0 && _diagnostics.Count == 0) return;
        writer.WriteInt(MovementExtensionMagic);
        writer.WriteInt(_hordes.Count);
        foreach (var horde in _hordes)
        {
            writer.WriteInt(horde.Id);
            writer.WriteInt(horde.Owner);
            writer.WriteInt(horde.TemplateIndex);
            writer.WriteInt(horde.Formation);
            writer.WriteInt(horde.Members.Count);
            foreach (var member in horde.Members) writer.WriteInt(member);
        }
        writer.WriteInt(Movement.HordeMotions.Count);
        foreach (var motion in Movement.HordeMotions.Values)
        {
            writer.WriteInt(motion.HordeId);
            writer.WriteVector(motion.LeaderPosition);
            writer.WriteFixed(motion.LeaderHeading);
            writer.WriteFixed(motion.CurrentSpeed);
            writer.WriteVector(motion.Destination);
            writer.WriteByte((byte)motion.OrderKind);
            writer.WriteBool(motion.HasOrder);
            writer.WriteBool(motion.IsReforming);
            writer.WriteBool(motion.StoppedForReformLastTick);
            writer.WriteBool(motion.IsSettling);
        }
        writer.WriteInt(_diagnostics.Count);
        foreach (var diagnostic in _diagnostics)
        {
            writer.WriteInt(diagnostic.Tick);
            writer.WriteInt(diagnostic.Seat);
            writer.WriteInt(diagnostic.Team);
            writer.WriteInt(diagnostic.Object);
            writer.WriteString(diagnostic.Code);
            writer.WriteString(diagnostic.Message);
        }
    }

    private void ReadMovementExtension(CanonicalReader reader)
    {
        if (reader.ReadInt() != MovementExtensionMagic)
        {
            throw new InvalidDataException("Unknown canonical state extension");
        }
        var hordeCount = ReadCount(reader, "horde");
        for (var index = 0; index < hordeCount; index++)
        {
            var id = reader.ReadInt();
            var owner = reader.ReadInt();
            var template = reader.ReadInt();
            var formation = reader.ReadInt();
            var memberCount = ReadCount(reader, "horde member");
            var members = new int[memberCount];
            for (var member = 0; member < memberCount; member++) members[member] = reader.ReadInt();
            AddHorde(new SnapshotHorde(id, owner, template, members, formation));
        }
        var motionCount = ReadCount(reader, "horde motion");
        for (var index = 0; index < motionCount; index++)
        {
            var motion = new HordeMotion(reader.ReadInt(), reader.ReadVector(), reader.ReadFixed())
            {
                CurrentSpeed = reader.ReadFixed(),
                Destination = reader.ReadVector(),
                OrderKind = (MoveOrderKind)reader.ReadByte(),
                HasOrder = reader.ReadBool(),
                IsReforming = reader.ReadBool(),
                StoppedForReformLastTick = reader.ReadBool(),
                IsSettling = reader.ReadBool(),
            };
            Movement.RestoreHordeMotion(motion);
        }
        var diagnosticCount = ReadCount(reader, "diagnostic");
        for (var index = 0; index < diagnosticCount; index++)
        {
            _diagnostics.Add(new SimDiagnostic(
                reader.ReadInt(),
                reader.ReadInt(),
                reader.ReadInt(),
                reader.ReadInt(),
                reader.ReadString(),
                reader.ReadString()));
        }
    }

    private static int ReadCount(CanonicalReader reader, string name)
    {
        var count = reader.ReadInt();
        return count >= 0
            ? count
            : throw new InvalidDataException($"Negative {name} count in canonical state");
    }
}
