namespace OpenBfme.Sim;

public sealed record BuildPlot(
    int BaseObjectId,
    int Index,
    FixedVector2 Position,
    IReadOnlyList<string> AllowedKinds,
    int OccupantObjectId = 0);

public sealed partial class SimWorld
{
    private const int EconomyExtensionMagic = 0x45434F4E;
    private readonly SortedDictionary<(int BaseObjectId, int Index), BuildPlot> _buildPlots = new();

    public IReadOnlyList<BuildPlot> BuildPlots => _buildPlots.Values.ToArray();

    public void SetBuildPlots(IEnumerable<BuildPlot> plots)
    {
        ArgumentNullException.ThrowIfNull(plots);
        _buildPlots.Clear();
        foreach (var plot in plots)
        {
            if (plot.BaseObjectId < 1 || plot.Index < 0 || plot.OccupantObjectId < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(plots));
            }
            var copy = plot with { AllowedKinds = plot.AllowedKinds.ToArray() };
            _buildPlots.Add((copy.BaseObjectId, copy.Index), copy);
        }
    }

    public long CommandPointsUsed(int team)
    {
        ValidateTeam(team);
        var used = BaseCommandPointsForTeam(team);
        var members = new SortedSet<int>();
        var carriers = new SortedSet<int>();
        foreach (var horde in _hordes)
        {
            foreach (var member in horde.Members) members.Add(member);
            carriers.Add(horde.Id);
            if (horde.Owner != team) continue;
            if (_objects.TryGetValue(horde.Id, out var carrier))
            {
                if (!carrier.IsDead && !carrier.IsDying && !carrier.IsUnderConstruction)
                {
                    used = checked(used + carrier.Template.Economy.CommandPoints);
                }
            }
            else if (horde.Members.Any(_objects.ContainsKey))
            {
                used = checked(used + _config.TemplateAtIndex(horde.TemplateIndex).Economy.CommandPoints);
            }
        }
        foreach (var gameObject in _objects.Values)
        {
            if (gameObject.Team != team || gameObject.IsDead || gameObject.IsDying
                || gameObject.IsUnderConstruction || members.Contains(gameObject.Id)
                || carriers.Contains(gameObject.Id))
            {
                continue;
            }
            used = checked(used + gameObject.Template.Economy.CommandPoints);
        }
        return used;
    }

    public long CommandPointsMaximum(int team)
    {
        ValidateTeam(team);
        for (var index = 0; index < _playerTeams.Length; index++)
        {
            if (_playerTeams[index] == team) return _commandPointsMax[index];
        }
        return 0;
    }

    internal bool CanReserveCommandPoints(int team, long additional)
    {
        if (additional < 0) return false;
        var reserved = 0L;
        foreach (var gameObject in _objects.Values)
        {
            if (gameObject.Team == team && !gameObject.IsDead && !gameObject.IsDying
                && gameObject.FindModule<ProductionModule>() is { } production)
            {
                reserved = checked(reserved + production.ReservedCommandPoints);
            }
        }
        return checked(CommandPointsUsed(team) + reserved + additional) <= CommandPointsMaximum(team);
    }

    internal bool TryGetTemplate(string name, out ObjectTemplate template) =>
        _config.Templates.TryGetValue(name, out template!);

    internal bool CanSpawnProductionTemplate(ObjectTemplate template)
    {
        var hordeSpec = template.Modules.FirstOrDefault(module => module.TypeName == HordeContainModule.TypeName);
        if (hordeSpec == null) return true;
        var memberTemplate = !string.IsNullOrWhiteSpace(template.Economy.HordeMemberTemplate)
            ? template.Economy.HordeMemberTemplate
            : hordeSpec.GetString("MemberTemplate", "");
        return !string.IsNullOrWhiteSpace(memberTemplate) && _config.Templates.ContainsKey(memberTemplate);
    }

    internal void SpawnProducedObject(
        GameObject producer,
        string templateName,
        FixedVector2 exitPosition,
        FixedVector2? rallyPoint)
    {
        var template = _config.Templates[templateName];
        var hordeSpec = template.Modules.FirstOrDefault(module => module.TypeName == HordeContainModule.TypeName);
        if (hordeSpec == null)
        {
            var unit = SpawnObject(templateName, producer.Team, exitPosition);
            ApplyRallyOrder(unit, rallyPoint);
            return;
        }

        var carrier = SpawnObject(templateName, producer.Team, exitPosition);
        var contain = carrier.FindModule<HordeContainModule>()!;
        var memberTemplate = !string.IsNullOrWhiteSpace(template.Economy.HordeMemberTemplate)
            ? template.Economy.HordeMemberTemplate
            : contain.MemberTemplateName;
        if (string.IsNullOrWhiteSpace(memberTemplate) || !_config.Templates.ContainsKey(memberTemplate))
        {
            throw new InvalidOperationException($"Horde template '{templateName}' has no valid MemberTemplate");
        }
        var members = new int[contain.MemberCount];
        for (var index = 0; index < members.Length; index++)
        {
            var member = SpawnObject(memberTemplate, producer.Team, exitPosition);
            members[index] = member.Id;
            ApplyRallyOrder(member, rallyPoint);
        }
        AddHorde(new SnapshotHorde(
            carrier.Id,
            producer.Team,
            _config.TemplateIndexOf(templateName),
            members,
            0));
    }

    private static void ApplyRallyOrder(GameObject gameObject, FixedVector2? rallyPoint)
    {
        if (!rallyPoint.HasValue) return;
        if (gameObject.FindModule<LocomotorModule>() is { } locomotor)
        {
            locomotor.SetOrder(rallyPoint.Value, MoveOrderKind.Move);
        }
        else
        {
            gameObject.FindModule<LinearMoverModule>()?.SetDestination(rallyPoint.Value);
        }
    }

    private void ApplyTrainCommand(SimCommand command)
    {
        var countLong = command.Args.TryGetValue("count", out var countValue)
            ? countValue.LongValue
            : 1;
        if (countLong is < 1 or > int.MaxValue)
        {
            RecordDiagnostic(command, 0, "invalid_count", $"train count {countLong} is outside the simulation range");
            return;
        }
        var count = (int)countLong;
        var template = command.GetString("template");
        foreach (var idValue in CommandObjectIds(command))
        {
            if (!TryOwnedObject(command, idValue, out var producer)) continue;
            var production = producer.FindModule<ProductionModule>();
            if (production == null)
            {
                RecordDiagnostic(command, producer.Id, "missing_production", $"object {producer.Id} has no ProductionUpdate");
                continue;
            }
            if (!CommandAllows(producer, "train", template))
            {
                RecordDiagnostic(command, producer.Id, "not_in_command_set",
                    $"object {producer.Id} command set '{producer.CurrentCommandSet}' does not offer '{template}'");
                continue;
            }
            if (!production.TryQueue(this, producer, template, count, out var refusal))
            {
                RecordDiagnostic(command, producer.Id, refusal, $"object {producer.Id} refused training '{template}' x{count}");
            }
        }
    }

    private void ApplyCancelCommand(SimCommand command)
    {
        var indexLong = command.GetLong("index");
        if (indexLong is < 0 or > int.MaxValue)
        {
            RecordDiagnostic(command, 0, "invalid_queue_index", $"production index {indexLong} is outside the simulation range");
            return;
        }
        var index = (int)indexLong;
        foreach (var idValue in CommandObjectIds(command))
        {
            if (!TryOwnedObject(command, idValue, out var producer)) continue;
            var production = producer.FindModule<ProductionModule>();
            if (production == null || !production.TryCancel(this, producer, index))
            {
                RecordDiagnostic(command, producer.Id, "invalid_queue_index", $"object {producer.Id} has no production entry {index}");
            }
        }
    }

    private void ApplyRallyCommand(SimCommand command)
    {
        var point = new FixedVector2(command.GetFixed("x"), command.GetFixed("y"));
        foreach (var idValue in CommandObjectIds(command))
        {
            if (!TryOwnedObject(command, idValue, out var producer)) continue;
            var production = producer.FindModule<ProductionModule>();
            if (production == null)
            {
                RecordDiagnostic(command, producer.Id, "missing_production", $"object {producer.Id} has no ProductionUpdate");
                continue;
            }
            production.SetRallyPoint(point);
        }
    }

    private void ApplyBuildCommand(SimCommand command)
    {
        var templateName = command.GetString("template");
        var plotIndexLong = command.GetLong("index");
        if (plotIndexLong is < 0 or > int.MaxValue)
        {
            RecordDiagnostic(command, 0, "invalid_plot", $"plot index {plotIndexLong} is outside the simulation range");
            return;
        }
        var plotIndex = (int)plotIndexLong;
        foreach (var idValue in CommandObjectIds(command))
        {
            if (!TryOwnedObject(command, idValue, out var baseObject)) continue;
            if (!_buildPlots.TryGetValue((baseObject.Id, plotIndex), out var plot))
            {
                RecordDiagnostic(command, baseObject.Id, "invalid_plot", $"base {baseObject.Id} has no plot {plotIndex}");
                continue;
            }
            if (plot.OccupantObjectId != 0)
            {
                RecordDiagnostic(command, baseObject.Id, "plot_occupied", $"base {baseObject.Id} plot {plotIndex} is occupied");
                continue;
            }
            if (!_config.Templates.TryGetValue(templateName, out var template))
            {
                RecordDiagnostic(command, baseObject.Id, "unknown_template", $"unknown build template '{templateName}'");
                continue;
            }
            if (!CommandAllows(baseObject, "build", templateName))
            {
                RecordDiagnostic(command, baseObject.Id, "not_in_command_set",
                    $"base {baseObject.Id} command set '{baseObject.CurrentCommandSet}' does not offer '{templateName}'");
                continue;
            }
            if (baseObject.Template.Economy.CommandSet.Count > 0
                && !baseObject.Template.Economy.CommandSet.Contains(templateName, StringComparer.Ordinal))
            {
                RecordDiagnostic(command, baseObject.Id, "not_in_command_set", $"base {baseObject.Id} cannot build '{templateName}'");
                continue;
            }
            var kind = string.IsNullOrWhiteSpace(template.Economy.BuildKind) ? templateName : template.Economy.BuildKind;
            if (plot.AllowedKinds.Count > 0 && !plot.AllowedKinds.Contains(kind, StringComparer.Ordinal))
            {
                RecordDiagnostic(command, baseObject.Id, "plot_kind_not_allowed", $"plot {plotIndex} does not allow '{kind}'");
                continue;
            }
            if (!template.Modules.Any(module => module.TypeName == GettingBuiltModule.TypeName))
            {
                RecordDiagnostic(command, baseObject.Id, "not_constructible", $"template '{templateName}' has no GettingBuiltBehavior");
                continue;
            }
            if (_teamResources[command.Team] < template.Economy.BuildCost)
            {
                RecordDiagnostic(command, baseObject.Id, "insufficient_money", $"team {command.Team} cannot afford '{templateName}'");
                continue;
            }
            _teamResources[command.Team] -= template.Economy.BuildCost;
            var structure = SpawnObject(templateName, command.Team, plot.Position);
            structure.FindModule<GettingBuiltModule>()!.StartConstruction(this, structure);
            _buildPlots[(baseObject.Id, plotIndex)] = plot with { OccupantObjectId = structure.Id };
            RaiseEvent(new SimEvent("build_start", structure.Id, Target: baseObject.Id, Name: templateName));
        }
    }

    private void ApplySellCommand(SimCommand command)
    {
        foreach (var idValue in CommandObjectIds(command))
        {
            if (!TryOwnedObject(command, idValue, out var structure)) continue;
            if (!structure.Modules.Any(module => module is StructureBodyModule)
                && !_buildPlots.Values.Any(plot => plot.OccupantObjectId == structure.Id))
            {
                RecordDiagnostic(command, structure.Id, "not_sellable", $"object {structure.Id} is not a structure");
                continue;
            }
            var refund = EconomyTemplate.ScaleInteger(
                structure.Template.Economy.BuildCost,
                structure.Template.Economy.SellRefundMultiplier);
            AddTeamResources(structure.Team, refund);
            structure.FindModule<ProductionModule>()?.RefundAll(this, structure);
            structure.MarkDead();
        }
    }

    private bool TryOwnedObject(SimCommand command, long idValue, out GameObject gameObject)
    {
        gameObject = null!;
        if (idValue is < 1 or > int.MaxValue)
        {
            RecordDiagnostic(command, 0, "invalid_object", $"object id {idValue} is outside the simulation range");
            return false;
        }
        var id = (int)idValue;
        if (!_objects.TryGetValue(id, out gameObject!))
        {
            RecordDiagnostic(command, id, "unknown_object", $"object {id} does not exist");
            return false;
        }
        if (gameObject.Team != command.Team)
        {
            RecordOwnershipDiagnostic(command, id, gameObject.Team);
            return false;
        }
        return true;
    }

    private long BaseCommandPointsForTeam(int team)
    {
        for (var index = 0; index < _playerTeams.Length; index++)
        {
            if (_playerTeams[index] == team) return _commandPoints[index];
        }
        return 0;
    }

    private void ReleasePlotForObject(int objectId)
    {
        foreach (var key in _buildPlots.Keys.ToArray())
        {
            var plot = _buildPlots[key];
            if (plot.OccupantObjectId == objectId)
            {
                _buildPlots[key] = plot with { OccupantObjectId = 0 };
            }
        }
    }

    private void WriteEconomyExtension(CanonicalWriter writer)
    {
        var identityTeams = _playerTeams.Length == _config.TeamCount
            && _playerTeams.Select((team, index) => team == index).All(value => value);
        if (_buildPlots.Count == 0 && identityTeams
            && _commandPoints.All(value => value == 0)
            && _commandPointsMax.All(value => value == 0)
            && _powerPoints.All(value => value == 0))
        {
            return;
        }
        writer.WriteInt(EconomyExtensionMagic);
        writer.WriteInt(_playerTeams.Length);
        for (var index = 0; index < _playerTeams.Length; index++)
        {
            writer.WriteInt(_playerTeams[index]);
            writer.WriteLong(_commandPoints[index]);
            writer.WriteLong(_commandPointsMax[index]);
            writer.WriteLong(_powerPoints[index]);
        }
        writer.WriteInt(_buildPlots.Count);
        foreach (var plot in _buildPlots.Values)
        {
            writer.WriteInt(plot.BaseObjectId);
            writer.WriteInt(plot.Index);
            writer.WriteVector(plot.Position);
            writer.WriteInt(plot.OccupantObjectId);
            writer.WriteInt(plot.AllowedKinds.Count);
            foreach (var kind in plot.AllowedKinds) writer.WriteString(kind);
        }
    }

    private void ReadStateExtension(CanonicalReader reader)
    {
        var magic = reader.ReadInt();
        switch (magic)
        {
            case MovementExtensionMagic:
                ReadMovementExtension(reader);
                break;
            case EconomyExtensionMagic:
                ReadEconomyExtension(reader);
                break;
            case TechExtensionMagic:
                ReadTechExtension(reader);
                break;
            default:
                throw new InvalidDataException($"Unknown canonical state extension 0x{magic:X8}");
        }
    }

    private void ReadEconomyExtension(CanonicalReader reader)
    {
        var playerCount = ReadCount(reader, "player economy");
        _playerTeams = new int[playerCount];
        _commandPoints = new long[playerCount];
        _commandPointsMax = new long[playerCount];
        _powerPoints = new long[playerCount];
        for (var index = 0; index < playerCount; index++)
        {
            _playerTeams[index] = reader.ReadInt();
            _commandPoints[index] = reader.ReadLong();
            _commandPointsMax[index] = reader.ReadLong();
            _powerPoints[index] = reader.ReadLong();
        }
        var plotCount = ReadCount(reader, "build plot");
        _buildPlots.Clear();
        for (var plotIndex = 0; plotIndex < plotCount; plotIndex++)
        {
            var baseId = reader.ReadInt();
            var index = reader.ReadInt();
            var position = reader.ReadVector();
            var occupant = reader.ReadInt();
            var kindCount = ReadCount(reader, "build plot kind");
            var kinds = new string[kindCount];
            for (var kind = 0; kind < kindCount; kind++) kinds[kind] = reader.ReadString();
            var plot = new BuildPlot(baseId, index, position, kinds, occupant);
            _buildPlots.Add((baseId, index), plot);
        }
    }
}
