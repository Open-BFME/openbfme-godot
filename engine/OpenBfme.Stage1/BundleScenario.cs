using System.Text.Json;

namespace OpenBfme.Stage1;

/// <summary>Loads the committed neutral test bundle into the C# candidate.</summary>
public static class BundleScenario
{
    public static BundleResult Run(string bundleRoot)
    {
        string root = Path.GetFullPath(bundleRoot);
        JsonElement pack = ReadRoot(Path.Combine(root, "pack.json"));
        Require(pack.GetProperty("id").GetString() == "openbfme-test", "pack_id");
        JsonElement files = pack.GetProperty("files");
        JsonElement map = ReadRoot(SafePath(root, files.GetProperty("entryMap").GetString()!));
        JsonElement objects = ReadRoot(SafePath(root, files.GetProperty("objects").GetString()!));
        JsonElement weapons = ReadRoot(SafePath(root, files.GetProperty("weapons").GetString()!));
        JsonElement locomotion = ReadRoot(SafePath(root, files.GetProperty("locomotion").GetString()!));

        VerifyRules(weapons, locomotion);
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

        foreach (JsonElement command in map.GetProperty("scenario").GetProperty("commands").EnumerateArray().Select(static item => item.Clone()).OrderBy(static item => item.GetProperty("sequence").GetInt32()))
        {
            OrderKind kind = command.GetProperty("order").GetString() switch
            {
                "move" => OrderKind.Move,
                "attack-move" => OrderKind.AttackMove,
                "attack-target" => OrderKind.AttackTarget,
                "stop" => OrderKind.Stop,
                _ => throw new InvalidDataException("bundle_contract=command_kind"),
            };
            simulation.ScheduleCommand(new SimCommand(
                command.GetProperty("executeTick").GetInt32(),
                command.GetProperty("sequence").GetInt32(),
                command.GetProperty("hordeEntityId").GetInt32(),
                kind,
                Position(command.GetProperty("destination")),
                command.TryGetProperty("targetEntityId", out JsonElement target) ? target.GetInt32() : 0));
        }

        int ticks = map.GetProperty("scenario").GetProperty("simulationTicks").GetInt32();
        simulation.Advance(ticks);
        bool valid = simulation.ValidateState(out string failure);
        return new BundleResult(
            ticks,
            simulation.StateHash(),
            valid,
            failure,
            simulation.Hordes.Count,
            simulation.Hordes.Sum(static horde => horde.Members.Count),
            simulation.Hordes.Sum(static horde => horde.AliveCount),
            simulation.Winner,
            simulation.Fortresses.Single(static fortress => fortress.Team == TeamId.Blue).Health,
            simulation.Fortresses.Single(static fortress => fortress.Team == TeamId.Red).Health);
    }

    private static void VerifyRules(JsonElement weapons, JsonElement locomotion)
    {
        JsonElement melee = ById(weapons.GetProperty("weapons"), "test.weapon.practice-baton");
        JsonElement ranged = ById(weapons.GetProperty("weapons"), "test.weapon.foam-sphere-launcher");
        JsonElement projectile = ranged.GetProperty("projectile");
        JsonElement foot = ById(locomotion.GetProperty("locomotion"), "test.locomotion.foot");
        Require(melee.GetProperty("damage").GetInt32() == Simulation.MeleeDamage, "melee_damage");
        Require(melee.GetProperty("maximumRangeSubcells").GetInt32() == Simulation.MeleeRange, "melee_range");
        Require(melee.GetProperty("cooldownTicks").GetInt32() == Simulation.MeleeCooldown, "melee_cooldown");
        Require(ranged.GetProperty("damage").GetInt32() == Simulation.ProjectileDamage, "projectile_damage");
        Require(ranged.GetProperty("maximumRangeSubcells").GetInt32() == Simulation.RangedRange, "ranged_range");
        Require(ranged.GetProperty("cooldownTicks").GetInt32() == Simulation.RangedCooldown, "ranged_cooldown");
        Require(projectile.GetProperty("speedSubcellsPerTick").GetInt32() == Simulation.ProjectileMovePerTick, "projectile_speed");
        Require(foot.GetProperty("maximumSpeedSubcellsPerTick").GetInt32() == Simulation.MemberMovePerTick, "member_speed");
        Require(foot.GetProperty("hordeAnchorSpeedSubcellsPerTick").GetInt32() == Simulation.HordeMovePerTick, "horde_speed");
    }

    private static int ObjectHealth(JsonElement document, string id) =>
        ById(document.GetProperty("objects"), id).GetProperty("maximumHealth").GetInt32();

    private static JsonElement ById(JsonElement rows, string id) =>
        rows.EnumerateArray().First(item => item.GetProperty("id").GetString() == id);

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

public readonly record struct BundleResult(
    int Ticks,
    uint Hash,
    bool StateValid,
    string Failure,
    int Hordes,
    int Members,
    int LivingMembers,
    TeamId Winner,
    int BlueFortressHealth,
    int RedFortressHealth);
