using System.Text.Json;

namespace OpenBfme.Stage1;

/// <summary>Runs the committed legal-safe economy scenario through the C# candidate.</summary>
public static class Stage2BundleScenario
{
    public static Stage2BundleResult Run(string bundleRoot)
    {
        string root = Path.GetFullPath(bundleRoot);
        JsonElement pack = ReadRoot(Path.Combine(root, "pack.json"));
        Require(pack.GetProperty("id").GetString() == "openbfme-test", "pack_id");
        JsonElement files = pack.GetProperty("files");
        JsonElement map = ReadRoot(SafePath(root, files.GetProperty("stage2Map").GetString()!));
        JsonElement objects = ReadRoot(SafePath(root, files.GetProperty("objects").GetString()!));
        JsonElement economyDocument = ReadRoot(SafePath(root, files.GetProperty("economy").GetString()!));
        EconomyDefinition economyDefinition = EconomyDefinition.Parse(economyDocument);

        Simulation simulation = BuildSimulation(map, objects);
        simulation.EnableEconomy(economyDefinition);

        JsonElement[] commands = map.GetProperty("scenario").GetProperty("commands").EnumerateArray()
            .Select(static command => command.Clone())
            .OrderBy(static command => command.GetProperty("executeTick").GetInt32())
            .ThenBy(static command => command.GetProperty("sequence").GetInt32())
            .ToArray();
        int commandIndex = 0;
        int ticks = map.GetProperty("scenario").GetProperty("simulationTicks").GetInt32();
        while (simulation.Tick < ticks)
        {
            while (commandIndex < commands.Length && commands[commandIndex].GetProperty("executeTick").GetInt32() == simulation.Tick)
            {
                ScheduleJustInTime(simulation, commands[commandIndex++]);
            }

            simulation.AdvanceOneTick();
        }

        Require(commandIndex == commands.Length, "stage2_commands_after_scenario");
        bool valid = simulation.ValidateState(out string failure);
        EconomySystem economy = simulation.Economy!;
        TeamEconomy blue = economy.FindEconomy(TeamId.Blue)!;
        TeamEconomy red = economy.FindEconomy(TeamId.Red)!;
        return new Stage2BundleResult(
            ticks,
            simulation.StateHash(),
            valid,
            failure,
            economy.Buildings.Count,
            economy.Buildings.Count(static building => building.IsCompleted && !building.IsDestroyed),
            simulation.Hordes.Count,
            simulation.Hordes.Sum(static horde => horde.AliveCount),
            blue.Resources,
            red.Resources,
            blue.TotalEarned,
            red.TotalEarned,
            blue.PopulationUsed,
            blue.PopulationReserved,
            simulation.Winner,
            simulation.Fortresses.Single(static fortress => fortress.Team == TeamId.Red).Health);
    }

    private static Simulation BuildSimulation(JsonElement map, JsonElement objects)
    {
        JsonElement gridElement = map.GetProperty("grid");
        Require(gridElement.GetProperty("cellSizeSubcells").GetInt32() == NavigationGrid.CellSize, "cell_size");
        NavigationGrid grid = new(gridElement.GetProperty("widthCells").GetInt32(), gridElement.GetProperty("heightCells").GetInt32());
        foreach (JsonElement blocker in map.GetProperty("staticBlockers").EnumerateArray())
        {
            int x = blocker.GetProperty("xCell").GetInt32();
            int y = blocker.GetProperty("yCell").GetInt32();
            int width = blocker.GetProperty("widthCells").GetInt32();
            int height = blocker.GetProperty("heightCells").GetInt32();
            for (int row = y; row < y + height; row++)
            {
                for (int column = x; column < x + width; column++)
                {
                    grid.SetBlocked(new GridCell(column, row));
                }
            }
        }

        Simulation simulation = new(grid);
        JsonElement[] sides = map.GetProperty("sides").EnumerateArray()
            .Select(static side => side.Clone())
            .OrderBy(static side => side.GetProperty("team").GetInt32())
            .ToArray();
        foreach (JsonElement side in sides)
        {
            JsonElement fortress = side.GetProperty("fortress");
            TeamId team = (TeamId)side.GetProperty("team").GetInt32();
            Fortress created = simulation.AddFortress(team, Position(fortress.GetProperty("position")), ObjectHealth(objects, fortress.GetProperty("objectId").GetString()!));
            Require(created.EntityId == fortress.GetProperty("entityId").GetInt32(), "fortress_id");
        }

        foreach (JsonElement side in sides)
        {
            JsonElement horde = side.GetProperty("horde");
            int memberCount = 0;
            int rangedCount = 0;
            foreach (JsonElement composition in horde.GetProperty("composition").EnumerateArray())
            {
                int count = composition.GetProperty("count").GetInt32();
                memberCount += count;
                if (composition.GetProperty("objectId").GetString() == "test.object.member.ranged")
                {
                    rangedCount += count;
                }
            }

            int rangedEvery = rangedCount == 0 ? 0 : memberCount / rangedCount;
            Horde created = simulation.AddHorde((TeamId)side.GetProperty("team").GetInt32(), Position(horde.GetProperty("anchor")), memberCount, rangedEvery);
            Require(created.EntityId == horde.GetProperty("entityId").GetInt32(), "horde_id");
            Require(created.Members[0].EntityId == horde.GetProperty("firstMemberEntityId").GetInt32(), "member_id");
        }

        return simulation;
    }

    private static void ScheduleJustInTime(Simulation simulation, JsonElement command)
    {
        int tick = command.GetProperty("executeTick").GetInt32();
        int sequence = command.GetProperty("sequence").GetInt32();
        string? order = command.GetProperty("order").GetString();
        switch (order)
        {
            case "place-building":
                simulation.ScheduleEconomyCommand(new EconomyCommand(
                    tick,
                    sequence,
                    EconomyCommandKind.Place,
                    (TeamId)command.GetProperty("team").GetInt32(),
                    0,
                    command.GetProperty("typeCode").GetInt32(),
                    Position(command.GetProperty("destination"))));
                break;
            case "train":
                simulation.ScheduleEconomyCommand(new EconomyCommand(
                    tick,
                    sequence,
                    EconomyCommandKind.Train,
                    TeamId.None,
                    command.GetProperty("buildingEntityId").GetInt32(),
                    command.GetProperty("typeCode").GetInt32(),
                    default));
                break;
            case "set-rally":
                simulation.ScheduleEconomyCommand(new EconomyCommand(
                    tick,
                    sequence,
                    EconomyCommandKind.Rally,
                    TeamId.None,
                    command.GetProperty("buildingEntityId").GetInt32(),
                    0,
                    Position(command.GetProperty("destination"))));
                break;
            case "move":
            case "attack-move":
            case "attack-target":
            case "stop":
                OrderKind kind = order switch
                {
                    "move" => OrderKind.Move,
                    "attack-move" => OrderKind.AttackMove,
                    "attack-target" => OrderKind.AttackTarget,
                    _ => OrderKind.Stop,
                };
                int hordeId = command.GetProperty("hordeEntityId").GetInt32();
                Require(simulation.Hordes.Any(horde => horde.EntityId == hordeId), "stage2_horde_not_produced");
                simulation.ScheduleCommand(new SimCommand(
                    tick,
                    sequence,
                    hordeId,
                    kind,
                    command.TryGetProperty("destination", out JsonElement destination) ? Position(destination) : default,
                    command.TryGetProperty("targetEntityId", out JsonElement target) ? target.GetInt32() : 0));
                break;
            default:
                throw new InvalidDataException("bundle_contract=stage2_command_kind");
        }
    }

    private static int ObjectHealth(JsonElement document, string id) =>
        document.GetProperty("objects").EnumerateArray()
            .First(item => item.GetProperty("id").GetString() == id)
            .GetProperty("maximumHealth").GetInt32();

    private static WorldPos Position(JsonElement element) =>
        new(element.GetProperty("xSubcells").GetInt32(), element.GetProperty("ySubcells").GetInt32());

    private static JsonElement ReadRoot(string path)
    {
        using JsonDocument document = JsonDocument.Parse(File.ReadAllBytes(path));
        return document.RootElement.Clone();
    }

    private static string SafePath(string root, string relative)
    {
        string candidate = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
        string prefix = root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        Require(candidate.StartsWith(prefix, StringComparison.OrdinalIgnoreCase), "path_escape");
        return candidate;
    }

    private static void Require(bool condition, string code)
    {
        if (!condition)
        {
            throw new InvalidDataException($"bundle_contract={code}");
        }
    }
}

public readonly record struct Stage2BundleResult(
    int Ticks,
    uint Hash,
    bool StateValid,
    string Failure,
    int Buildings,
    int CompletedBuildings,
    int Hordes,
    int LivingMembers,
    int BlueResources,
    int RedResources,
    int BlueTotalEarned,
    int RedTotalEarned,
    int BluePopulationUsed,
    int BluePopulationReserved,
    TeamId Winner,
    int RedFortressHealth);
