namespace OpenBfme.Sim;

public sealed partial class SimWorld
{
    private const int TechExtensionMagic = 0x54454348;
    private SortedSet<string>[] _teamUpgrades = null!;
    private SortedSet<string>[] _teamSciences = null!;
    private SortedDictionary<string, int>[] _powerReadyTicks = null!;

    private void InitializeTechState()
    {
        _teamUpgrades = Enumerable.Range(0, _config.TeamCount)
            .Select(_ => new SortedSet<string>(StringComparer.Ordinal)).ToArray();
        _teamSciences = Enumerable.Range(0, _config.TeamCount)
            .Select(_ => new SortedSet<string>(StringComparer.Ordinal)).ToArray();
        _powerReadyTicks = Enumerable.Range(0, _config.TeamCount)
            .Select(_ => new SortedDictionary<string, int>(StringComparer.Ordinal)).ToArray();
    }

    public bool TeamHasUpgrade(int team, string name) =>
        _teamUpgrades[ValidateTeam(team)].Contains(name);

    public bool TeamHasScience(int team, string name) =>
        _teamSciences[ValidateTeam(team)].Contains(name);

    /// <summary>Match-setup grant for authored starting sciences and scenario rewards.</summary>
    public void GrantScience(int team, string name)
    {
        ValidateTeam(team);
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        _teamSciences[team].Add(name);
    }

    public long TeamPowerPoints(int team) => _powerPoints[PlayerIndexForTeam(team)];

    public int PowerReadyTick(int team, string name) =>
        _powerReadyTicks[ValidateTeam(team)].TryGetValue(name, out var tick) ? tick : 0;

    internal bool ObjectHasUpgrade(GameObject gameObject, string name) =>
        gameObject.HasObjectUpgrade(name)
        || (gameObject.Team >= 0 && TeamHasUpgrade(gameObject.Team, name));

    internal bool HasUpgradeTemplate(string name) => _config.Tech.Upgrades.ContainsKey(name);
    internal UpgradeTemplate UpgradeTemplate(string name) => _config.Tech.Upgrades[name];

    internal void RecordTechGap(string typeName) =>
        _moduleGaps[typeName] = _moduleGaps.TryGetValue(typeName, out var count) ? count + 1 : 1;

    internal bool IsUpgradeInProgress(GameObject buyer, UpgradeTemplate upgrade)
    {
        foreach (var gameObject in _objects.Values)
        {
            if ((upgrade.Type == UpgradeType.Player ? gameObject.Team == buyer.Team : gameObject.Id == buyer.Id)
                && gameObject.FindModule<ProductionModule>() is { } production
                && production.HasQueuedUpgrade(upgrade.Name)) return true;
        }
        return false;
    }

    private void ApplyUpgradeCommand(SimCommand command)
    {
        var name = CommandName(command, "upgrade");
        if (!_config.Tech.Upgrades.TryGetValue(name, out var upgrade))
        {
            RecordDiagnostic(command, 0, "unknown_upgrade", $"unknown upgrade '{name}'");
            return;
        }
        foreach (var idValue in CommandObjectIds(command))
        {
            if (!TryOwnedObject(command, idValue, out var producer)) continue;
            if (!CommandAllows(producer, "upgrade", name))
            {
                RecordDiagnostic(command, producer.Id, "not_in_command_set",
                    $"object {producer.Id} command set '{producer.CurrentCommandSet}' does not offer upgrade '{name}'");
                continue;
            }
            var production = producer.FindModule<ProductionModule>();
            if (production == null)
            {
                RecordDiagnostic(command, producer.Id, "missing_production",
                    $"object {producer.Id} has no ProductionUpdate");
                continue;
            }
            if (!production.TryQueueUpgrade(this, producer, upgrade, out var refusal))
                RecordDiagnostic(command, producer.Id, refusal,
                    $"object {producer.Id} refused upgrade '{name}'");
        }
    }

    internal void CompleteUpgrade(GameObject producer, UpgradeTemplate upgrade)
    {
        if (upgrade.Type == UpgradeType.Player)
        {
            if (!_teamUpgrades[producer.Team].Add(upgrade.Name)) return;
            foreach (var gameObject in _objects.Values)
                if (gameObject.Team == producer.Team) EvaluateUpgradeModules(gameObject);
        }
        else
        {
            if (!producer.AddObjectUpgrade(upgrade.Name)) return;
            EvaluateUpgradeModules(producer);
        }
        RaiseEvent(new SimEvent("upgrade", producer.Id, Name: upgrade.Name));
    }

    private void ApplyOwnedPlayerUpgrades(GameObject gameObject)
    {
        if (gameObject.Team < 0) return;
        if (_teamUpgrades[gameObject.Team].Count > 0) EvaluateUpgradeModules(gameObject);
    }

    private void EvaluateUpgradeModules(GameObject gameObject)
    {
        foreach (var module in gameObject.Modules)
            if (module is UpgradeTriggeredModuleBase upgradeModule)
                upgradeModule.Evaluate(this, gameObject);
    }

    private void ApplyPowerCommand(SimCommand command)
    {
        var name = CommandName(command, "power");
        if (_config.Tech.Sciences.TryGetValue(name, out var science))
        {
            PurchaseScience(command, science);
            return;
        }
        if (!_config.Tech.SpecialPowers.TryGetValue(name, out var power))
        {
            RecordDiagnostic(command, 0, "unknown_power", $"unknown science or special power '{name}'");
            return;
        }
        CastPower(command, power);
    }

    private void PurchaseScience(SimCommand command, ScienceTemplate science)
    {
        var issuer = OptionalOwnedIssuer(command);
        if (issuer == null && HasObjectArgument(command)) return;
        if (issuer != null && !CommandAllows(issuer, "power", science.Name))
        {
            RecordDiagnostic(command, issuer.Id, "not_in_command_set",
                $"object {issuer.Id} command set '{issuer.CurrentCommandSet}' does not offer science '{science.Name}'");
            return;
        }
        if (!science.IsGrantable)
        {
            RecordDiagnostic(command, issuer?.Id ?? 0, "science_not_grantable", $"science '{science.Name}' is not grantable");
            return;
        }
        if (_teamSciences[command.Team].Contains(science.Name))
        {
            RecordDiagnostic(command, issuer?.Id ?? 0, "science_already_owned", $"team {command.Team} already owns '{science.Name}'");
            return;
        }
        if (science.PrerequisiteSciences.Any(value => !_teamSciences[command.Team].Contains(value)))
        {
            RecordDiagnostic(command, issuer?.Id ?? 0, "science_prerequisite_missing",
                $"team {command.Team} lacks a prerequisite for '{science.Name}'");
            return;
        }
        var player = PlayerIndexForTeam(command.Team);
        if (_powerPoints[player] < science.PurchasePointCost)
        {
            RecordDiagnostic(command, issuer?.Id ?? 0, "insufficient_power_points",
                $"team {command.Team} cannot afford science '{science.Name}'");
            return;
        }
        _powerPoints[player] -= science.PurchasePointCost;
        _teamSciences[command.Team].Add(science.Name);
    }

    private void CastPower(SimCommand command, SpecialPowerTemplate power)
    {
        var caster = OptionalOwnedIssuer(command);
        if (caster == null)
        {
            if (!HasObjectArgument(command))
                RecordDiagnostic(command, 0, "missing_caster", $"special power '{power.Name}' requires an issuing object");
            return;
        }
        if (!CommandAllows(caster, "power", power.Name))
        {
            RecordDiagnostic(command, caster.Id, "not_in_command_set",
                $"object {caster.Id} command set '{caster.CurrentCommandSet}' does not offer power '{power.Name}'");
            return;
        }
        if (power.RequiredSciences.Any(value => !_teamSciences[command.Team].Contains(value)))
        {
            RecordDiagnostic(command, caster.Id, "required_science_missing",
                $"team {command.Team} lacks a required science for '{power.Name}'");
            return;
        }
        if (PowerReadyTick(command.Team, power.Name) > TickIndex)
        {
            RecordDiagnostic(command, caster.Id, "power_reloading", $"special power '{power.Name}' is reloading");
            return;
        }
        if (!TryPowerTarget(command, caster, out var targetId, out var position)) return;

        foreach (var module in caster.Modules)
            if (module is SpecialPowerEffectModuleBase effect && effect.Matches(power))
                effect.Cast(this, caster, targetId, position);
        RaiseEvent(new SimEvent("ability", caster.Id, targetId == 0 ? null : targetId, Name: power.Name));
        _powerReadyTicks[command.Team][power.Name] = checked(TickIndex + power.ReloadTicks(TickMilliseconds));
    }

    private bool TryPowerTarget(
        SimCommand command,
        GameObject caster,
        out int targetId,
        out FixedVector2 position)
    {
        targetId = 0;
        position = caster.Position;
        if (command.Args.TryGetValue("target", out var target))
        {
            if (target.Kind != CommandValueKind.Long || target.LongValue is < 1 or > int.MaxValue
                || !_objects.TryGetValue((int)target.LongValue, out var targetObject)
                || targetObject.IsDead || targetObject.IsDying)
            {
                RecordDiagnostic(command, caster.Id, "invalid_power_target", "special power target is invalid");
                return false;
            }
            targetId = targetObject.Id;
            position = targetObject.Position;
        }
        var hasX = command.Args.TryGetValue("x", out var x);
        var hasY = command.Args.TryGetValue("y", out var y);
        if (hasX != hasY || (hasX && (x.Kind != CommandValueKind.Fixed || y.Kind != CommandValueKind.Fixed)))
        {
            RecordDiagnostic(command, caster.Id, "invalid_power_target", "special power position requires exact x and y");
            return false;
        }
        if (hasX) position = new FixedVector2(Fixed64.FromRaw(x.LongValue), Fixed64.FromRaw(y.LongValue));
        return true;
    }

    internal bool CommandAllows(GameObject issuer, string commandType, string targetName)
    {
        if (issuer.CurrentCommandSet.Length == 0) return true;
        if (!_config.Tech.CommandSets.TryGetValue(issuer.CurrentCommandSet, out var set)) return false;
        foreach (var entry in set.Entries)
        {
            var button = entry.Button;
            if (commandType == "train" && button.Command == "UNIT_BUILD" && button.Object == targetName) return true;
            if (commandType == "build" && button.Command == "DOZER_CONSTRUCT" && button.Object == targetName) return true;
            if (commandType == "upgrade" && button.Command is "PLAYER_UPGRADE" or "OBJECT_UPGRADE"
                && button.Upgrade == targetName) return true;
            if (commandType == "power" && button.Command == "PURCHASE_SCIENCE" && button.Science == targetName) return true;
            if (commandType == "power" && button.Command == "SPECIAL_POWER" && button.SpecialPower == targetName) return true;
        }
        return false;
    }

    private GameObject? OptionalOwnedIssuer(SimCommand command)
    {
        if (!HasObjectArgument(command)) return null;
        var ids = CommandObjectIds(command);
        if (ids.Count != 1)
        {
            RecordDiagnostic(command, 0, "invalid_caster", "power commands require exactly one issuing object");
            return null;
        }
        return TryOwnedObject(command, ids[0], out var issuer) ? issuer : null;
    }

    private static bool HasObjectArgument(SimCommand command) =>
        command.Args.ContainsKey("objects") || command.Args.ContainsKey("object") || command.Args.ContainsKey("id");

    private static string CommandName(SimCommand command, string legacy)
    {
        if (command.Args.TryGetValue("name", out var name) && name.Kind == CommandValueKind.String)
            return name.StringValue!;
        if (command.Args.TryGetValue(legacy, out var value) && value.Kind == CommandValueKind.String)
            return value.StringValue!;
        throw new KeyNotFoundException($"Command '{command.Type}' has no technology name");
    }

    private int PlayerIndexForTeam(int team)
    {
        ValidateTeam(team);
        for (var index = 0; index < _playerTeams.Length; index++)
            if (_playerTeams[index] == team) return index;
        throw new InvalidOperationException($"Team {team} has no player economy");
    }

    private void WriteTechExtension(CanonicalWriter writer)
    {
        if (_config.Tech.IsEmpty
            && _teamUpgrades.All(value => value.Count == 0)
            && _teamSciences.All(value => value.Count == 0)
            && _powerReadyTicks.All(value => value.Count == 0)) return;
        writer.WriteInt(TechExtensionMagic);
        writer.WriteInt(_config.TeamCount);
        for (var team = 0; team < _config.TeamCount; team++)
        {
            writer.WriteInt(_teamUpgrades[team].Count);
            foreach (var name in _teamUpgrades[team]) writer.WriteString(name);
            writer.WriteInt(_teamSciences[team].Count);
            foreach (var name in _teamSciences[team]) writer.WriteString(name);
            writer.WriteInt(_powerReadyTicks[team].Count);
            foreach (var (name, tick) in _powerReadyTicks[team])
            {
                writer.WriteString(name);
                writer.WriteInt(tick);
            }
        }
    }

    private void ReadTechExtension(CanonicalReader reader)
    {
        var teams = ReadCount(reader, "tech team");
        if (teams != _config.TeamCount) throw new InvalidDataException("Tech team count does not match config");
        for (var team = 0; team < teams; team++)
        {
            ReadNames(reader, _teamUpgrades[team], "player upgrade");
            ReadNames(reader, _teamSciences[team], "science");
            var cooldowns = ReadCount(reader, "power cooldown");
            for (var index = 0; index < cooldowns; index++)
                _powerReadyTicks[team].Add(reader.ReadString(), reader.ReadInt());
        }
    }

    private static void ReadNames(CanonicalReader reader, ISet<string> destination, string kind)
    {
        var count = ReadCount(reader, kind);
        for (var index = 0; index < count; index++) destination.Add(reader.ReadString());
    }
}
