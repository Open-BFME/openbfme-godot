using OpenBfme.Sim;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

public sealed class AiPlannerTests
{
    private readonly ITestOutputHelper _output;

    public AiPlannerTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void EconomyGrowsBeforeProductionAndArmyFillsNearCap()
    {
        var world = FixtureWorld("brutal", startingResources: 10_000, commandPointCap: 100, plots: true);

        world.Advance(180);

        Assert.True(world.Objects.Values.Count(IsEconomy) >= 5);
        Assert.True(world.CommandPointsUsed(0) >= 80);
        Assert.True(world.AiCommandCounts(0)["build"] >= 5);
        Assert.True(world.AiCommandCounts(0)["train"] >= 4);
        var firstBuild = world.AiDiagnostics.ToList().FindIndex(value => value.Action == "build_resource");
        var firstTrain = world.AiDiagnostics.ToList().FindIndex(value => value.Action == "train");
        Assert.True(firstBuild >= 0 && firstBuild < firstTrain);
    }

    [Fact]
    public void IdleProducerQueuesAgainAtEveryEligiblePlanStep()
    {
        var world = FixtureWorld("brutal", startingResources: 10_000, commandPointCap: 1_000);
        world.Advance(80);

        var trainTicks = world.AiDiagnostics
            .Where(value => value.Action == "train")
            .Select(value => value.Tick)
            .ToArray();
        Assert.Equal(new[] { 1, 16, 31, 46, 61, 76 }, trainTicks);
    }

    [Fact]
    public void CavalryObservationPrefersPikeCounter()
    {
        var world = FixtureWorld("hard", startingResources: 1_000, commandPointCap: 100);
        world.SpawnObject("cavalry", 1, At(90, 0));

        world.Advance(3);

        Assert.Contains(world.Objects.Values, value => value.Team == 0 && value.TemplateName == "pike");
        Assert.Contains(world.AiDiagnostics,
            value => value.Action == "train" && value.Detail.Contains("template=pike", StringComparison.Ordinal));
    }

    [Fact]
    public void NearbyEnemyTriggersDefendAttackMove()
    {
        var world = FixtureWorld("hard", startingResources: 0, commandPointCap: 100);
        var defender = world.SpawnObject("infantry", 0, At(5, 0));
        var threat = world.SpawnObject("cavalry", 1, At(40, 0));

        world.Tick();

        var locomotor = defender.FindModule<LocomotorModule>()!;
        Assert.True(locomotor.HasOrder);
        // Combat retains the attack-move goal while its pursuit layer emits a
        // concrete move order to the currently engaged threat.
        Assert.Equal(MoveOrderKind.Move, locomotor.OrderKind);
        Assert.Equal(threat.Position, locomotor.Destination);
        Assert.Contains(world.AiDiagnostics, value => value.Action == "defend" && value.Score > 0);
    }

    [Fact]
    public void AttackRequiresMarginAndArmyArrivesAtEnemyStructure()
    {
        var weak = FixtureWorld("hard", startingResources: 0, commandPointCap: 100);
        weak.SpawnObject("infantry", 0, At(5, 0));
        weak.Tick();
        Assert.DoesNotContain(weak.AiDiagnostics, value => value.Action == "attack" && value.Score > 0);

        var strong = FixtureWorld("hard", startingResources: 0, commandPointCap: 100);
        for (var index = 0; index < 4; index++) strong.SpawnObject("infantry", 0, At(5, index));
        strong.Tick();
        Assert.Contains(strong.AiDiagnostics, value => value.Action == "attack" && value.Score > 0);

        var enemyFortressId = strong.Objects.Values.Single(value => value.Team == 1 && IsStructure(value)).Id;
        for (var tick = 0; tick < 300 && strong.Objects.ContainsKey(enemyFortressId); tick++) strong.Tick();
        Assert.DoesNotContain(enemyFortressId, strong.Objects.Keys);
    }

    [Fact]
    public void LosingLowHealthArmyRetreatsToRallyPoint()
    {
        var world = FixtureWorld("hard", startingResources: 0, commandPointCap: 100);
        var army = Enumerable.Range(0, 4)
            .Select(index => world.SpawnObject("infantry", 0, At(5, index)))
            .ToArray();
        world.Tick();
        foreach (var unit in army) world.DealDamage(unit, 70);

        world.Advance(world.AiPlanIntervalTicks(0));

        Assert.Contains(world.AiDiagnostics, value => value.Action == "retreat" && value.Score > 0);
        Assert.All(army, unit =>
        {
            var locomotor = unit.FindModule<LocomotorModule>()!;
            Assert.True(locomotor.HasOrder);
            Assert.Equal(At(0, 0), locomotor.Destination);
        });
    }

    [Fact]
    public void DifficultyCadenceOrdersArmyGrowth()
    {
        var brutal = ReachFiveUnits("brutal");
        var hard = ReachFiveUnits("hard");
        var medium = ReachFiveUnits("medium");
        var easy = ReachFiveUnits("easy");

        _output.WriteLine($"army_value_threshold_ticks brutal={brutal} hard={hard} medium={medium} easy={easy}");
        Assert.True(brutal < hard, $"brutal={brutal} hard={hard}");
        Assert.True(hard < medium, $"hard={hard} medium={medium}");
        Assert.True(medium < easy, $"medium={medium} easy={easy}");
    }

    [Fact]
    public void TwoAiTwinRunsHashIdenticallyForThreeThousandTicks()
    {
        var first = FixtureWorld("hard", "medium", 20_000, 200, plots: true);
        var second = FixtureWorld("hard", "medium", 20_000, 200, plots: true);

        for (var tick = 1; tick <= 3_000; tick++)
        {
            first.Tick();
            second.Tick();
            Assert.Equal(first.StateHash(), second.StateHash());
        }
    }

    [Fact]
    public void AiVsAiFixtureFinishesWithinBoundedTicks()
    {
        var world = FixtureWorld("hard", "medium", 20_000, 100);
        var finishedTick = 0;
        for (var tick = 1; tick <= 3_000; tick++)
        {
            world.Tick();
            if (!HasLivingStructure(world, 0) || !HasLivingStructure(world, 1))
            {
                finishedTick = tick;
                break;
            }
        }

        _output.WriteLine($"fixture_ai_vs_ai_finished_tick={finishedTick}");
        Assert.InRange(finishedTick, 1, 3_000);
    }

    [Fact]
    public void BundleLoaderPreservesSideAndKindOfForPlanner()
    {
        var document = BundleDocument.Load(MatchLaunchTests.RepoPath(
            "contracts", "fixtures", "bundle-v1.json"));
        var loaded = BundleTemplateLoader.Load(document, ModuleRegistry.CreateDefault(), 33);
        var cookBase = Assert.Single(loaded.Templates, value => value.Name == "CookBase");

        Assert.Equal("CookFaction", cookBase.Side);
        Assert.Equal(new[] { "PRELOAD", "SELECTABLE", "CAN_ATTACK" }, cookBase.KindOf);
    }

    [Fact]
    public void ExplicitLaunchHandicapRestrictsBudgetWithoutGrantingResources()
    {
        var handicapped = FixtureWorld(
            "hard",
            startingResources: 100,
            commandPointCap: 100,
            plots: true,
            handicap: Fixed64.Half);
        handicapped.Tick();
        Assert.Equal(100, handicapped.TeamResources(0));
        Assert.Empty(handicapped.AiCommandCounts(0));

        var normal = FixtureWorld("hard", startingResources: 100, commandPointCap: 100, plots: true);
        normal.Tick();
        Assert.Equal(0, normal.TeamResources(0));
        Assert.Equal(1, normal.AiCommandCounts(0)["build"]);
    }

    private static int ReachFiveUnits(string difficulty)
    {
        var world = FixtureWorld(difficulty, startingResources: 10_000, commandPointCap: 1_000);
        for (var tick = 1; tick <= 1_000; tick++)
        {
            world.Tick();
            var armyValue = world.Objects.Values
                .Where(value => value.Team == 0 && !IsStructure(value))
                .Sum(value => value.Template.Economy.BuildCost);
            if (armyValue >= 500) return tick;
        }
        return int.MaxValue;
    }

    internal static SimWorld FixtureWorld(
        string difficulty,
        long startingResources,
        long commandPointCap,
        bool plots = false,
        Fixed64? handicap = null) =>
        FixtureWorld(difficulty, null, startingResources, commandPointCap, plots, handicap);

    internal static SimWorld FixtureWorld(
        string difficulty,
        string? opponentDifficulty,
        long startingResources,
        long commandPointCap,
        bool plots = false,
        Fixed64? handicap = null)
    {
        var players = new List<MatchLaunchPlayer>
        {
            new(0, 0, "FactionMen", "ai", difficulty, null, 0, handicap, null, "Left AI"),
            new(1, 1, "FactionMen", opponentDifficulty == null ? "human" : "ai",
                opponentDifficulty, null, 1, null, null, "Right AI"),
        };
        var launch = new MatchLaunch(
            MatchLaunch.SchemaName,
            0xA11CEUL,
            new MatchLaunchPack("ai-fixture", new string('0', 64)),
            new MatchLaunchMap("maps/ai-fixture.map", null),
            new MatchLaunchRules(
                33,
                startingResources,
                Fixed64.One,
                false,
                Fixed64.One,
                "annihilation",
                false,
                new SortedDictionary<string, bool>(StringComparer.Ordinal)),
            players,
            "skirmish",
            null);
        var world = new SimWorld(
            launch,
            new SimConfig(
                Templates(),
                launch.Seed,
                2,
                weaponTemplates: new[] { Sword() },
                maxCommandPoints: commandPointCap,
                tech: Tech()),
            ModuleRegistry.CreateDefault());
        var left = world.SpawnObject("fortress", 0, At(0, 0));
        var right = world.SpawnObject("fortress", 1, At(100, 0));
        if (plots)
        {
            world.SetBuildPlots(Plots(left, 1).Concat(Plots(right, -1)).ToArray());
        }
        return world;
    }

    private static IReadOnlyList<BuildPlot> Plots(GameObject fortress, int direction)
    {
        var result = new List<BuildPlot>();
        for (var index = 0; index < 8; index++)
        {
            result.Add(new BuildPlot(
                fortress.Id,
                index,
                At(fortress.Position.X.ToIntFloor() + direction * (10 + index * 3), 10 + index),
                new[] { "resource", "farm", "barracks" }));
        }
        return result;
    }

    private static IReadOnlyList<ObjectTemplate> Templates()
    {
        var mover = new ModuleSpec(LocomotorModule.TypeName, new Dictionary<string, long>
        {
            ["Speed"] = 30,
            ["Acceleration"] = 900,
            ["Braking"] = 900,
            ["TurnRate"] = 10_000,
        });
        return new[]
        {
            new ObjectTemplate(
                "fortress",
                new ModuleSpec[]
                {
                    new(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 600 }),
                    new(ProductionModule.TypeName, new Dictionary<string, long> { ["MaxQueueEntries"] = 9 }),
                    new(DestroyDieModule.TypeName),
                },
                bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(600)),
                economy: new EconomyTemplate(productionExitOffset: At(3, 0)),
                commandSetName: "FortressCommands",
                techEnabled: true,
                side: "Men",
                kindOf: new[] { "STRUCTURE", "BASE_SITE" }),
            new ObjectTemplate(
                "farm",
                new ModuleSpec[]
                {
                    new(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 200 }),
                    new(GettingBuiltModule.TypeName),
                    new(AutoDepositUpdateModule.TypeName),
                    new(DestroyDieModule.TypeName),
                },
                bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(200)),
                economy: new EconomyTemplate(100, 33, buildKind: "resource", depositAmount: 25,
                    depositTimingMilliseconds: 330),
                side: "Men",
                kindOf: new[] { "STRUCTURE", "ECONOMY_STRUCTURE", "FS_CASH_PRODUCER" }),
            new ObjectTemplate(
                "barracks",
                new ModuleSpec[]
                {
                    new(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 300 }),
                    new(GettingBuiltModule.TypeName),
                    new(ProductionModule.TypeName, new Dictionary<string, long> { ["MaxQueueEntries"] = 9 }),
                    new(DestroyDieModule.TypeName),
                },
                bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(300)),
                economy: new EconomyTemplate(150, 33, productionExitOffset: At(3, 0)),
                commandSetName: "BarracksCommands",
                techEnabled: true,
                side: "Men",
                kindOf: new[] { "STRUCTURE", "FS_FACTORY" }),
            Unit("infantry", new[] { "INFANTRY" }, mover),
            Unit("pike", new[] { "INFANTRY", "PIKE" }, mover),
            Unit("archer", new[] { "INFANTRY", "ARCHER" }, mover),
            Unit("cavalry", new[] { "CAVALRY" }, mover),
        };
    }

    private static ObjectTemplate Unit(string name, IReadOnlyList<string> kindOf, ModuleSpec mover) => new(
        name,
        new[]
        {
            mover,
            new ModuleSpec(ActiveBodyModule.TypeName,
                new Dictionary<string, long> { ["MaxHealth"] = 100 }),
            new ModuleSpec(DestroyDieModule.TypeName),
        },
        new[] { new WeaponSet(null, new Dictionary<WeaponSlot, string> { [WeaponSlot.PRIMARY] = "Sword" }) },
        economy: new EconomyTemplate(100, 66, 20),
        side: "Men",
        kindOf: kindOf.Concat(new[] { "CAN_ATTACK" }).ToArray());

    private static WeaponTemplate Sword() => new(
        "Sword",
        Fixed64.FromInt(3),
        Fixed64.Zero,
        1,
        0,
        PreAttackType.PER_SHOT,
        0,
        0,
        0,
        new[] { new DamageNugget(Fixed64.FromInt(20), Fixed64.Zero, 0, DamageType.SLASH, "", "NORMAL") });

    private static TechCatalog Tech()
    {
        var buttons = new[]
        {
            Button("BuildFarm", "DOZER_CONSTRUCT", objectName: "farm"),
            Button("BuildBarracks", "DOZER_CONSTRUCT", objectName: "barracks"),
            Button("TrainInfantry", "UNIT_BUILD", objectName: "infantry"),
            Button("TrainPike", "UNIT_BUILD", objectName: "pike"),
            Button("TrainArcher", "UNIT_BUILD", objectName: "archer"),
            Button("TrainCavalry", "UNIT_BUILD", objectName: "cavalry"),
        };
        CommandSetTemplate Set(string name, params CommandButtonTemplate[] entries) => new(
            name,
            entries.Select((button, index) => new CommandSetEntryTemplate(index + 1, button.Name, button)).ToArray());
        return new TechCatalog(
            commandButtons: buttons,
            commandSets: new[]
            {
                Set("FortressCommands", buttons),
                Set("BarracksCommands", buttons.Skip(2).ToArray()),
            });
    }

    private static CommandButtonTemplate Button(string name, string command, string objectName = "") =>
        new(name, command, objectName, "", "", "");

    private static bool HasLivingStructure(SimWorld world, int team) =>
        world.Objects.Values.Any(value => value.Team == team && IsStructure(value));

    private static bool IsEconomy(GameObject value) =>
        (AiTemplateRoles.Classify(value.Template) & AiUnitRole.Economy) != 0;

    private static bool IsStructure(GameObject value) =>
        (AiTemplateRoles.Classify(value.Template) & AiUnitRole.Structure) != 0;

    private static FixedVector2 At(int x, int y) =>
        new(Fixed64.FromInt(x), Fixed64.FromInt(y));
}
