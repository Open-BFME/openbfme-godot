using System.Diagnostics;
using OpenBfme.Sim;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

[Collection(TickBudgetCollection.Name)]
public sealed class AiRetailProofTests
{
    private const int RetailLimitTicks = 54_000;
    private readonly ITestOutputHelper _output;

    public AiRetailProofTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void AnnihilationIgnoresDestroyedPersistentBuildingFoundations()
    {
        var building = new ModuleSpec(BuildingBehaviorModule.TypeName);
        var template = new ObjectTemplate(
            "fortress",
            new[] { building },
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(100)),
            kindOf: new[] { "STRUCTURE" });
        var world = new SimWorld(
            new SimConfig(new[] { template }, randomSeed: 19, teamCount: 2),
            ModuleRegistry.CreateDefault());
        var fortress = world.SpawnObject("fortress", 1, At(0, 0));

        world.DealDamage(fortress, 100);

        Assert.True(fortress.IsDying);
        Assert.False(HasStructure(world, 1));
    }

    [Fact]
    public void MenHardVsMordorMediumRetailProofReportsHonestOutcomeAndTwinHash()
    {
        var path = CorpusPath();
        if (!File.Exists(path))
        {
            _output.WriteLine($"SKIP: corpus bundle absent at {path}; retail AI proof unavailable");
            return;
        }

        var document = BundleDocument.Load(path);
        var setup = SelectSetup(document, "Men", "Mordor");
        var first = Build(document, setup, "hard", "medium");
        var second = Build(document, setup, "hard", "medium");
        var leftFortress = first.Objects.Values.Single(value => value.Team == 0 && value.Position == At(100, 100));
        var rightFortress = first.Objects.Values.Single(value => value.Team == 1 && value.Position == At(400, 100));
        _output.WriteLine($"retail_ai_templates left_fortress={leftFortress.TemplateName} " +
            $"left_builder={setup.Left.Builder} left_resource={setup.Left.Resource} left_producer={setup.Left.Producer} " +
            $"right_fortress={rightFortress.TemplateName} right_resource={setup.Right.Resource} " +
            $"right_builder={setup.Right.Builder} right_producer={setup.Right.Producer}");

        var limitTicks = int.TryParse(Environment.GetEnvironmentVariable("OPENBFME_RETAIL_AI_TICK_LIMIT"), out var configured)
            && configured is > 0 and <= RetailLimitTicks
                ? configured
                : RetailLimitTicks;
        var peaks = new long[2];
        var finishedTick = 0;
        var damageEvents = 0;
        var deathEvents = 0;
        var stopwatch = Stopwatch.StartNew();
        for (var tick = 1; tick <= limitTicks; tick++)
        {
            first.Tick();
            second.Tick();
            damageEvents += first.EventsThisTick.Count(value => value.Kind == "damage");
            deathEvents += first.EventsThisTick.Count(value => value.Kind == "death");
            if (tick == 1 || tick % 300 == 0)
                Assert.Equal(first.StateHash(), second.StateHash());
            peaks[0] = Math.Max(peaks[0], ArmyValue(first, 0));
            peaks[1] = Math.Max(peaks[1], ArmyValue(first, 1));
            if (!HasStructure(first, 0) || !HasStructure(first, 1))
            {
                finishedTick = tick;
                break;
            }
        }
        stopwatch.Stop();
        Assert.Equal(first.StateHash(), second.StateHash());

        var ticks = finishedTick == 0 ? limitTicks : finishedTick;
        var winner = Winner(first);
        var leftCommands = FormatCounts(first.AiCommandCounts(0));
        var rightCommands = FormatCounts(first.AiCommandCounts(1));
        var averageMicroseconds = stopwatch.ElapsedTicks * 1_000_000L
            / Stopwatch.Frequency / Math.Max(1, ticks * 2L);
        _output.WriteLine($"retail_ai_result finished={finishedTick != 0} winner={winner} ticks={ticks} " +
            $"army_peaks={peaks[0]}:{peaks[1]} average_ms_per_tick={FormatMilliseconds(averageMicroseconds)} " +
            $"left_commands={leftCommands} right_commands={rightCommands} damage_events={damageEvents} " +
            $"death_events={deathEvents} twin_hash_equal=true");
        for (var team = 0; team < 2; team++)
        {
            var structures = first.Objects.Values.Where(value => value.Team == team
                    && (AiTemplateRoles.Classify(value.Template) & AiUnitRole.Structure) != 0)
                .Select(value => $"{value.Id}:{value.TemplateName}@{value.Position.X.ToIntFloor()}:{value.Position.Y.ToIntFloor()}={value.Health.ToIntFloor()}");
            _output.WriteLine($"retail_structures team={team} {string.Join(',', structures)}");
            _output.WriteLine($"retail_hordes team={team} count={first.Hordes.Count(value => value.Owner == team)} " +
                $"members={first.Hordes.Where(value => value.Owner == team).Sum(value => value.Members.Count)}");
            foreach (var horde in first.Hordes.Where(value => value.Owner == team).Take(4))
            {
                var carrier = first.Objects[horde.Id];
                var member = first.Objects[horde.Members[0]];
                first.Movement.TryGetHordeState(horde.Id, out var movement);
                _output.WriteLine($"retail_horde_state team={team} id={horde.Id} " +
                    $"carrier={carrier.Position.X.ToIntFloor()}:{carrier.Position.Y.ToIntFloor()} " +
                    $"member={member.Position.X.ToIntFloor()}:{member.Position.Y.ToIntFloor()} " +
                    $"member_template={member.TemplateName} weapons={member.Template.WeaponSets.Count} " +
                    $"horde_order={movement?.HasOrder} speed={movement?.CurrentSpeed} " +
                    $"reforming={movement?.IsReforming} stopped_reform={movement?.StoppedForReformLastTick} " +
                    $"combat_order={member.Combat?.OrderKind} " +
                    $"combat_goal={member.Combat?.AttackMoveGoal.X.ToIntFloor()}:{member.Combat?.AttackMoveGoal.Y.ToIntFloor()} " +
                    $"engaged={member.Combat?.EngagedTargetId}");
            }
        }
        _output.WriteLine("retail_command_diagnostics=" + string.Join(',', first.Diagnostics
            .GroupBy(value => value.Code)
            .OrderBy(value => value.Key, StringComparer.Ordinal)
            .Select(value => $"{value.Key}:{value.Count()}")));
        foreach (var diagnostic in first.AiDiagnostics.TakeLast(20))
            _output.WriteLine($"retail_ai_diag player={diagnostic.Player} tick={diagnostic.Tick} " +
                $"action={diagnostic.Action} score={diagnostic.Score} {diagnostic.Detail}");

        Assert.True(first.AiCommandCounts(0).TryGetValue("build", out var leftBuild) && leftBuild > 0);
        Assert.True(first.AiCommandCounts(1).TryGetValue("build", out var rightBuild) && rightBuild > 0);
        Assert.True(first.AiCommandCounts(0).TryGetValue("train", out var leftTrain) && leftTrain > 0);
        Assert.True(first.AiCommandCounts(1).TryGetValue("train", out var rightTrain) && rightTrain > 0);
        Assert.True(finishedTick > 0,
            $"retail match did not annihilate a side within {RetailLimitTicks} ticks");
        Assert.DoesNotContain(winner, new[] { "none", "draw" });
    }

    internal static RetailSetup SelectSetup(BundleDocument document, string left, string right) =>
        new(SelectSide(document, left), SelectSide(document, right));

    internal static SimWorld Build(
        BundleDocument document,
        RetailSetup setup,
        string leftDifficulty,
        string rightDifficulty)
    {
        var launch = new MatchLaunch(
            MatchLaunch.SchemaName,
            0xB0FEEUL,
            new MatchLaunchPack("retail-ai-proof", document.Source.EffectiveTreeSha256),
            new MatchLaunchMap("maps/ai-proof/ai-proof.map", null),
            new MatchLaunchRules(
                33,
                100_000,
                Fixed64.One,
                false,
                Fixed64.One,
                "annihilation",
                false,
                new SortedDictionary<string, bool>(StringComparer.Ordinal)),
            new[]
            {
                new MatchLaunchPlayer(0, 0, "Faction" + setup.Left.Side, "ai", leftDifficulty,
                    null, 0, null, null, setup.Left.Side),
                new MatchLaunchPlayer(1, 1, "Faction" + setup.Right.Side, "ai", rightDifficulty,
                    null, 1, null, null, setup.Right.Side),
            },
            "skirmish",
            null);
        var world = SimWorld.FromBundle(launch, document);
        var leftBase = SpawnFortress(world, document, setup.Left, 0, At(100, 100));
        var rightBase = SpawnFortress(world, document, setup.Right, 1, At(400, 100));
        if (!world.IsAttackable(leftBase) || !world.IsAttackable(rightBase))
            throw new InvalidOperationException("Retail AI proof requires attackable starting structures");
        var leftBuilder = world.SpawnObject(setup.Left.Builder, 0, At(110, 100));
        var rightBuilder = world.SpawnObject(setup.Right.Builder, 1, At(390, 100));
        world.SetBuildPlots(PlotRing(leftBase, leftBuilder, setup.Left, 1)
            .Concat(PlotRing(rightBase, rightBuilder, setup.Right, -1))
            .ToArray());
        return world;
    }

    private static SideSetup SelectSide(BundleDocument document, string side)
    {
        var rows = document.Templates.Where(value =>
                string.Equals(value.Side, side, StringComparison.OrdinalIgnoreCase))
            .OrderBy(value => value.Name, StringComparer.Ordinal)
            .ToArray();
        var fortress = rows.FirstOrDefault(value => value.Name == side + "Fortress")
            ?? rows.First(value => value.KindOf.Contains("BASE_SITE", StringComparer.OrdinalIgnoreCase)
                || value.Name.Contains("Fortress", StringComparison.OrdinalIgnoreCase));
        var resourceCandidates = rows.Where(value => value.KindOf.Any(token => token is
                "ECONOMY_STRUCTURE" or "+ECONOMY_STRUCTURE" or "FS_CASH_PRODUCER"))
            .OrderBy(value => value.BuildCost?.Raw ?? long.MaxValue)
            .ThenBy(value => value.Name, StringComparer.Ordinal)
            .ToArray();
        var preferredResource = side switch
        {
            "Men" => "GondorFarm",
            "Mordor" => "MordorSlaughterHouse",
            "Elves" => "ElvenMallornTree",
            "Dwarves" => "DwarvenMineShaft",
            "Isengard" => "IsengardFurnace",
            "Wild" => "WildMineShaft",
            "Angmar" => "AngmarMill",
            _ => "",
        };
        var resource = document.Templates.FirstOrDefault(value => value.Name == preferredResource)
            ?? resourceCandidates.FirstOrDefault(value => value.Name == preferredResource)
            ?? resourceCandidates.First();
        var preferredBuilder = side + "Porter";
        var builders = rows.Where(value => value.KindOf.Contains("DOZER", StringComparer.OrdinalIgnoreCase)
                && HasConstructButton(document, value))
            .OrderBy(value => value.Name, StringComparer.Ordinal)
            .ToArray();
        var builder = builders.FirstOrDefault(value => value.Name == preferredBuilder) ?? builders.First();
        var producerCandidates = rows.Where(value => value.BuildCost is { Raw: > 0 }
                && value.KindOf.Contains("STRUCTURE", StringComparer.OrdinalIgnoreCase)
                && value.Modules.Any(module => module.Type == ProductionModule.TypeName)
                && value.Modules.Any(module => module.Type == GettingBuiltModule.TypeName)
                && HasUnitBuildButton(document, value)
                && value.Name != resource.Name)
            .OrderBy(value => value.BuildCost?.Raw ?? long.MaxValue)
            .ThenBy(value => value.Name, StringComparer.Ordinal)
            .ToArray();
        var preferredProducer = side switch
        {
            "Men" => "GondorBarracks",
            "Mordor" => "MordorOrcPit",
            "Elves" => "ElvenBarracks",
            "Dwarves" => "DwarvenHallOfWarriors",
            "Isengard" => "IsengardUrukPit",
            "Wild" => "GoblinCave",
            "Angmar" => "AngmarBarracks",
            _ => "",
        };
        var producer = producerCandidates.FirstOrDefault(value => value.Name == preferredProducer)
            ?? producerCandidates.First();
        return new SideSetup(side, fortress.Name, builder.Name, resource.Name, producer.Name);
    }

    private static bool HasUnitBuildButton(BundleDocument document, BundleTemplateRow row)
    {
        if (!row.Fields.TryGetValue("CommandSet", out var value)
            || value.Kind != BundleValueKind.String) return false;
        var set = document.CommandSets?.FirstOrDefault(item => item.Name == value.String);
        if (set == null) return false;
        var buttons = document.CommandButtons?.ToDictionary(item => item.Name, StringComparer.Ordinal)
            ?? new Dictionary<string, CommandButtonTemplate>(StringComparer.Ordinal);
        return set.Entries.Any(entry => entry.Button != null
            && buttons.TryGetValue(entry.Button, out var button)
            && button.Command == "UNIT_BUILD");
    }

    private static bool HasConstructButton(BundleDocument document, BundleTemplateRow row)
    {
        if (!row.Fields.TryGetValue("CommandSet", out var value)
            || value.Kind != BundleValueKind.String) return false;
        var set = document.CommandSets?.FirstOrDefault(item => item.Name == value.String);
        if (set == null) return false;
        var buttons = document.CommandButtons?.ToDictionary(item => item.Name, StringComparer.Ordinal)
            ?? new Dictionary<string, CommandButtonTemplate>(StringComparer.Ordinal);
        return set.Entries.Any(entry => entry.Button != null
            && buttons.TryGetValue(entry.Button, out var button)
            && button.Command == "DOZER_CONSTRUCT");
    }

    private static GameObject SpawnFortress(
        SimWorld world,
        BundleDocument document,
        SideSetup side,
        int team,
        FixedVector2 position)
    {
        var candidates = new[] { side.Fortress }.Concat(document.Templates
                .Where(value => value.Side == side.Side
                    && value.KindOf.Contains("STRUCTURE", StringComparer.OrdinalIgnoreCase)
                    && !value.KindOf.Contains("UNATTACKABLE", StringComparer.OrdinalIgnoreCase)
                    && value.Modules.Any(module => module.Carrier == "Body"
                        && module.Type != ImmortalBodyModule.TypeName))
                .Select(value => value.Name))
            .Concat(new[] { side.Producer, side.Resource })
            .Distinct(StringComparer.Ordinal)
            .OrderBy(value => value == side.Fortress ? 0 : 1)
            .ThenBy(value => value == side.Producer ? 0 : 1)
            .ThenBy(value => value, StringComparer.Ordinal);
        var failures = new List<string>();
        foreach (var name in candidates)
        {
            try
            {
                var candidate = world.SpawnObject(name, team, position);
                if (world.IsAttackable(candidate)) return candidate;
                candidate.MarkDead();
                world.Tick();
                failures.Add($"{name}: spawned object is not attackable");
            }
            catch (Exception exception)
            {
                failures.Add($"{name}: {exception.Message}");
            }
        }
        throw new InvalidOperationException(
            $"No spawnable fortress for {side.Side}: {string.Join(" | ", failures)}");
    }

    private static IEnumerable<BuildPlot> PlotRing(
        GameObject fortress,
        GameObject builder,
        SideSetup setup,
        int direction)
    {
        for (var index = 0; index < 10; index++)
        {
            var distance = 80 + index * 12;
            yield return new BuildPlot(
                builder.Id,
                index,
                At(fortress.Position.X.ToIntFloor() + direction * distance,
                    fortress.Position.Y.ToIntFloor() + (index % 2 == 0 ? 40 : -40)),
                new[] { setup.Resource, setup.Producer });
        }
    }

    private static long ArmyValue(SimWorld world, int team) => world.Objects.Values
        .Where(value => value.Team == team
            && !AiTemplateRoles.IsNonCombat(value.Template)
            && (AiTemplateRoles.Classify(value.Template) & AiUnitRole.Structure) == 0)
        .Sum(value => Math.Max(1, value.Template.Economy.BuildCost)
            + Math.Max(0, value.Health.ToIntFloor()));

    private static bool HasStructure(SimWorld world, int team) => world.Objects.Values.Any(value =>
        value.Team == team && !value.IsDead && !value.IsDying
            && (AiTemplateRoles.Classify(value.Template) & AiUnitRole.Structure) != 0);

    private static string Winner(SimWorld world)
    {
        var left = HasStructure(world, 0);
        var right = HasStructure(world, 1);
        return left == right ? (left ? "none" : "draw") : left ? "Men" : "Mordor";
    }

    private static string FormatCounts(IReadOnlyDictionary<string, int> counts) =>
        string.Join(',', counts.Select(value => $"{value.Key}:{value.Value}"));

    private static string FormatMilliseconds(long microseconds) =>
        $"{microseconds / 1_000}.{microseconds % 1_000:D3}";

    private static FixedVector2 At(int x, int y) =>
        new(Fixed64.FromInt(x), Fixed64.FromInt(y));

    private static string CorpusPath() => MatchLaunchTests.RepoPath(
        "workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");

    internal sealed record SideSetup(
        string Side,
        string Fortress,
        string Builder,
        string Resource,
        string Producer);
    internal sealed record RetailSetup(SideSetup Left, SideSetup Right);
}
