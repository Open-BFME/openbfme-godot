using System.Text.Json;
using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class EconomyProductionTests
{
    private const long FarmCost = 200;
    private const long HordeCost = 300;

    [Fact]
    public void TrainHordeChargesAtEnqueueSpawnsFiveMembersAndRallies()
    {
        var world = World(tickMilliseconds: 33, startingResources: 1_000, hordeBuildMilliseconds: 20_000);
        var fortress = world.SpawnObject("fortress", 0, At(10, 10));
        Submit(world, Rally(1, 0, fortress.Id, At(40, 30)));
        Submit(world, Train(1, 1, fortress.Id, "horde", 1));

        world.Tick();

        Assert.Equal(1_000 - HordeCost, world.TeamResources(0));
        Assert.Equal(1, fortress.FindModule<ProductionModule>()!.QueueLength);
        var buildTicks = EconomyTemplate.MillisecondsToTicks(20_000, 33);
        world.Advance(buildTicks - 1);
        Assert.Empty(world.Hordes);

        world.Tick();

        var horde = Assert.Single(world.Hordes);
        Assert.Equal(5, horde.Members.Count);
        Assert.Equal("horde", world.Objects[horde.Id].TemplateName);
        foreach (var memberId in horde.Members)
        {
            var member = world.Objects[memberId];
            Assert.Equal(At(15, 10), member.Position);
            var locomotor = member.FindModule<LocomotorModule>();
            Assert.NotNull(locomotor);
            Assert.True(locomotor!.HasOrder);
            Assert.Equal(At(40, 30), locomotor.Destination);
        }
        Assert.Equal(60, world.CommandPointsUsed(0));
    }

    [Fact]
    public void CancelRefundsTheExactQueuedEntryCost()
    {
        var world = World(startingResources: 1_000);
        var fortress = world.SpawnObject("fortress", 0, FixedVector2.Zero);
        Submit(world, Train(1, 0, fortress.Id, "horde", 2));
        Submit(world, Cancel(2, 1, fortress.Id, 1));

        world.Tick();
        Assert.Equal(400, world.TeamResources(0));
        Assert.Equal(2, fortress.FindModule<ProductionModule>()!.QueueLength);
        world.Tick();

        Assert.Equal(700, world.TeamResources(0));
        Assert.Equal(1, fortress.FindModule<ProductionModule>()!.QueueLength);
    }

    [Fact]
    public void MoneyAndCommandPointRefusalsAreDiagnosticAndAtomic()
    {
        var poor = World(startingResources: HordeCost - 1);
        var poorFortress = poor.SpawnObject("fortress", 0, FixedVector2.Zero);
        Submit(poor, Train(1, 0, poorFortress.Id, "horde", 1));
        poor.Tick();

        Assert.Equal(HordeCost - 1, poor.TeamResources(0));
        Assert.Equal(0, poorFortress.FindModule<ProductionModule>()!.QueueLength);
        Assert.Contains(poor.Diagnostics, value => value.Code == "insufficient_money");

        var capped = World(startingResources: 1_000, commandPointMultiplier: Fixed64.FromFraction(1, 20));
        var cappedFortress = capped.SpawnObject("fortress", 0, FixedVector2.Zero);
        Assert.Equal(50, capped.CommandPointsMaximum(0));
        Submit(capped, Train(1, 0, cappedFortress.Id, "horde", 1));
        capped.Tick();

        Assert.Equal(1_000, capped.TeamResources(0));
        Assert.Equal(0, cappedFortress.FindModule<ProductionModule>()!.QueueLength);
        Assert.Contains(capped.Diagnostics, value => value.Code == "command_points_exceeded");
    }

    [Fact]
    public void BuildUsesAuthoredPlotRampsHealthAndBlocksSecondStructure()
    {
        var world = World(startingResources: 1_000, farmBuildMilliseconds: 3_300);
        var fortress = world.SpawnObject("fortress", 0, FixedVector2.Zero);
        world.SetBuildPlots(new[]
        {
            new BuildPlot(fortress.Id, 0, At(25, 5), new[] { "resource" }),
        });
        Submit(world, Build(1, 0, fortress.Id, "farm", 0));
        Submit(world, Build(2, 1, fortress.Id, "farm", 0));

        world.Tick();

        var farm = Assert.Single(world.Objects.Values, value => value.TemplateName == "farm");
        Assert.True(farm.IsUnderConstruction);
        Assert.Equal(At(25, 5), farm.Position);
        Assert.Equal(Fixed64.FromInt(100), farm.Health);
        Assert.Equal(800, world.TeamResources(0));

        world.Tick();
        Assert.Equal(800, world.TeamResources(0));
        Assert.Contains(world.Diagnostics, value => value.Code == "plot_occupied");

        var buildTicks = EconomyTemplate.MillisecondsToTicks(3_300, 33);
        world.Advance(buildTicks - 1);
        Assert.False(farm.IsUnderConstruction);
        Assert.Equal(Fixed64.FromInt(1_000), farm.Health);
    }

    [Theory]
    [InlineData(33)]
    [InlineData(100)]
    public void AutoDepositPaysTheTickRoundedTotalOverSixtySeconds(int tickMilliseconds)
    {
        var world = World(tickMilliseconds, startingResources: 0);
        world.SpawnObject("complete-farm", 0, FixedVector2.Zero);
        var elapsedTicks = EconomyTemplate.MillisecondsToTicks(60_000, tickMilliseconds);
        var periodTicks = EconomyTemplate.MillisecondsToTicks(3_300, tickMilliseconds);

        world.Advance(elapsedTicks);

        Assert.Equal((elapsedTicks / periodTicks) * 25, world.TeamResources(0));
    }

    [Fact]
    public void SellRefundsConfiguredPercentageAndFreesPlot()
    {
        var world = World(startingResources: 1_000, farmBuildMilliseconds: 33);
        var fortress = world.SpawnObject("fortress", 0, FixedVector2.Zero);
        world.SetBuildPlots(new[]
        {
            new BuildPlot(fortress.Id, 0, At(20, 0), new[] { "resource" }),
        });
        Submit(world, Build(1, 0, fortress.Id, "farm", 0));
        world.Advance(2);
        var farm = Assert.Single(world.Objects.Values, value => value.TemplateName == "farm");
        Assert.False(farm.IsUnderConstruction);
        Submit(world, Sell(3, 1, farm.Id));

        world.Tick();

        Assert.DoesNotContain(farm.Id, world.Objects.Keys);
        Assert.Equal(900, world.TeamResources(0));
        Assert.Equal(0, Assert.Single(world.BuildPlots).OccupantObjectId);
    }

    [Fact]
    public void EconomyCommandLogTwinRunsHashIdenticallyForTwelveHundredTicks()
    {
        SimWorld BuildWorld()
        {
            var world = World(startingResources: 5_000, hordeBuildMilliseconds: 330, farmBuildMilliseconds: 330);
            var fortress = world.SpawnObject("fortress", 0, FixedVector2.Zero);
            world.SetBuildPlots(new[]
            {
                new BuildPlot(fortress.Id, 0, At(20, -10), new[] { "resource" }),
                new BuildPlot(fortress.Id, 1, At(20, 10), new[] { "resource" }),
            });
            Submit(world, Build(1, 0, fortress.Id, "farm", 0));
            Submit(world, Build(1, 1, fortress.Id, "farm", 1));
            Submit(world, Train(1, 2, fortress.Id, "horde", 3));
            Submit(world, TestWorlds.Command(50, 0, 3, "attack_move",
                ("objects", CommandValue.OfLongList(new long[] { 4, 10, 16 })),
                ("x", CommandValue.OfFixed(Fixed64.FromInt(100))),
                ("y", CommandValue.OfFixed(Fixed64.Zero))));
            return world;
        }

        var first = BuildWorld();
        var second = BuildWorld();
        for (var tick = 1; tick <= 1_200; tick++)
        {
            first.Tick();
            second.Tick();
            Assert.Equal(first.StateHash(), second.StateHash());
        }

        Assert.Equal(2, first.Objects.Values.Count(value => value.TemplateName == "farm"));
        Assert.Equal(3, first.Hordes.Count);
    }

    [Fact]
    public void SnapshotEventsExposeBuildStartBuildDoneThenProducedSpawnsDeterministically()
    {
        static IReadOnlyList<(int Tick, string Kind, string? Name)> Trace()
        {
            var world = World(startingResources: 1_000, hordeBuildMilliseconds: 33, farmBuildMilliseconds: 33);
            var fortress = world.SpawnObject("fortress", 0, FixedVector2.Zero);
            world.SetBuildPlots(new[]
            {
                new BuildPlot(fortress.Id, 0, At(20, 0), new[] { "resource" }),
            });
            Submit(world, Build(1, 0, fortress.Id, "farm", 0));
            Submit(world, Train(3, 1, fortress.Id, "horde", 1));
            var result = new List<(int, string, string?)>();
            for (var tick = 1; tick <= 4; tick++)
            {
                world.Tick();
                using var snapshot = JsonDocument.Parse(SnapshotWriter.Write(world));
                result.AddRange(snapshot.RootElement.GetProperty("events").EnumerateArray()
                    .Where(value => value.GetProperty("kind").GetString() is "build_start" or "build_done" or "spawn")
                    .Select(value => (
                        tick,
                        value.GetProperty("kind").GetString()!,
                        value.TryGetProperty("name", out var name) ? name.GetString() : null)));
            }
            return result;
        }

        var first = Trace();
        var second = Trace();
        Assert.Equal(first, second);
        Assert.Contains(first, value => value.Kind == "build_start" && value.Name == "farm");
        Assert.Contains(first, value => value.Kind == "build_done" && value.Name == "farm");
        Assert.Contains(first, value => value.Kind == "spawn" && value.Name == "horde");
        Assert.True(
            first.ToList().FindIndex(value => value.Kind == "build_start")
            < first.ToList().FindIndex(value => value.Kind == "build_done"));
        Assert.True(
            first.ToList().FindIndex(value => value.Kind == "build_done")
            < first.ToList().FindIndex(value => value.Kind == "spawn" && value.Name == "horde"));
    }

    [Fact]
    public void EconomyFieldsParseFromModuleAndIniShapedDataInAuthoredOrder()
    {
        var spec = new ModuleSpec(
            "ObjectEconomy",
            new Dictionary<string, long>
            {
                ["BuildCost"] = 450,
                ["BuildTime"] = 20_000,
                ["CommandPoints"] = 75,
                ["ProductionExitXRaw"] = Fixed64.FromInt(7).Raw,
                ["ProductionExitYRaw"] = Fixed64.FromInt(-2).Raw,
                ["DepositAmount"] = 25,
                ["DepositTiming"] = 6_000,
                ["SellRefundPercent"] = 40,
            },
            new Dictionary<string, string>
            {
                ["CommandSet"] = "farm horde hero",
                ["BuildKind"] = "resource",
                ["MemberTemplate"] = "fighter",
            });
        var fromModule = EconomyTemplate.Parse(spec);
        var fromObjectTemplate = new ObjectTemplate("parsed", new[] { spec }).Economy;

        Assert.Equal(new[] { "farm", "horde", "hero" }, fromModule.CommandSet);
        Assert.Equal(fromModule.CommandSet, fromObjectTemplate.CommandSet);
        Assert.Equal(450, fromModule.BuildCost);
        Assert.Equal(20_000, fromModule.BuildTimeMilliseconds);
        Assert.Equal(75, fromModule.CommandPoints);
        Assert.Equal(At(7, -2), fromModule.ProductionExitOffset);
        Assert.Equal(Fixed64.FromFraction(2, 5), fromModule.SellRefundMultiplier);
        Assert.Equal("fighter", fromModule.HordeMemberTemplate);

        var fromIni = EconomyTemplate.Parse(new Dictionary<string, object?>
        {
            ["fields"] = new Dictionary<string, object?>
            {
                ["BuildCost"] = 450L,
                ["BuildTime"] = 20_000L,
                ["CommandPoints"] = 75L,
                ["CommandSet"] = new[] { "farm", "horde", "hero" },
                ["ProductionExitX"] = "7.0",
                ["ProductionExitY"] = "-2.0",
                ["DepositAmount"] = 25L,
                ["DepositTiming"] = 6_000L,
                ["SellRefundPercent"] = "40%",
            },
        });
        Assert.Equal(fromModule.BuildCost, fromIni.BuildCost);
        Assert.Equal(fromModule.BuildTimeMilliseconds, fromIni.BuildTimeMilliseconds);
        Assert.Equal(fromModule.CommandPoints, fromIni.CommandPoints);
        Assert.Equal(fromModule.CommandSet, fromIni.CommandSet);
        Assert.Equal(fromModule.ProductionExitOffset, fromIni.ProductionExitOffset);
        Assert.Equal(fromModule.SellRefundMultiplier, fromIni.SellRefundMultiplier);
    }

    [Fact]
    public void QueuedHordesReserveCapAndHordeDeathFreesIt()
    {
        var world = World(startingResources: 2_000, hordeBuildMilliseconds: 33, baseCommandPoints: 100);
        var fortress = world.SpawnObject("fortress", 0, FixedVector2.Zero);
        Submit(world, Train(1, 0, fortress.Id, "horde", 1));
        Submit(world, Train(1, 1, fortress.Id, "horde", 1));
        world.Advance(2);

        Assert.Contains(world.Diagnostics, value => value.Code == "command_points_exceeded");
        var horde = Assert.Single(world.Hordes);
        Assert.Equal(60, world.CommandPointsUsed(0));

        world.DealDamage(world.Objects[horde.Id], 500);
        world.Tick();

        Assert.Empty(world.Hordes);
        Assert.Equal(0, world.CommandPointsUsed(0));
        Assert.DoesNotContain(world.Objects.Values, value => value.TemplateName is "horde" or "fighter");
    }

    [Fact]
    public void AutoDepositAppliesPerStructureCrowdingMultiplier()
    {
        var world = World(startingResources: 0);
        var farm = world.SpawnObject("complete-farm", 0, FixedVector2.Zero);
        farm.FindModule<AutoDepositUpdateModule>()!
            .SetCrowdingMultiplier(Fixed64.FromFraction(4, 5));
        var periodTicks = EconomyTemplate.MillisecondsToTicks(3_300, 33);

        world.Advance(periodTicks * 2);

        Assert.Equal(40, world.TeamResources(0));
    }

    [Fact]
    public void LaunchCapUsesRulesValueTimesMatchMultiplierAndSnapshotsRealEconomy()
    {
        var world = World(
            startingResources: 1_234,
            commandPointMultiplier: Fixed64.FromFraction(1, 2),
            baseCommandPoints: 600);
        world.SpawnObject("horde", 0, FixedVector2.Zero);

        using var snapshot = JsonDocument.Parse(SnapshotWriter.Write(world));
        var player = Assert.Single(snapshot.RootElement.GetProperty("players").EnumerateArray());
        Assert.Equal(1_234, player.GetProperty("resources").GetInt64());
        Assert.Equal(60, player.GetProperty("command_points").GetInt64());
        Assert.Equal(300, player.GetProperty("command_points_max").GetInt64());
        Assert.Equal(0, player.GetProperty("power_points").GetInt64());
    }

    [Fact]
    public void CanonicalSnapshotRoundTripsPlotsConstructionQueueAndEconomy()
    {
        var templates = Templates(hordeBuildMilliseconds: 660, farmBuildMilliseconds: 660);
        var config = new SimConfig(templates, 919, 1, maxCommandPoints: 1_000);
        var registry = ModuleRegistry.CreateDefault();
        var original = new SimWorld(config, registry, 33);
        original.AddTeamResources(0, 2_000);
        var fortress = original.SpawnObject("fortress", 0, FixedVector2.Zero);
        original.SetBuildPlots(new[]
        {
            new BuildPlot(fortress.Id, 0, At(20, 0), new[] { "resource" }),
        });
        Submit(original, Build(1, 0, fortress.Id, "farm", 0));
        Submit(original, Train(1, 1, fortress.Id, "horde", 1));
        original.Tick();

        var restored = SimWorld.Restore(original.Snapshot(), config, registry);
        Assert.Equal(original.StateHash(), restored.StateHash());
        for (var tick = 0; tick < 100; tick++)
        {
            original.Tick();
            restored.Tick();
            Assert.Equal(original.StateHash(), restored.StateHash());
        }
        Assert.Equal(original.TeamResources(0), restored.TeamResources(0));
        Assert.Equal(original.CommandPointsUsed(0), restored.CommandPointsUsed(0));
        var originalPlot = Assert.Single(original.BuildPlots);
        var restoredPlot = Assert.Single(restored.BuildPlots);
        Assert.Equal(originalPlot.BaseObjectId, restoredPlot.BaseObjectId);
        Assert.Equal(originalPlot.Index, restoredPlot.Index);
        Assert.Equal(originalPlot.Position, restoredPlot.Position);
        Assert.Equal(originalPlot.OccupantObjectId, restoredPlot.OccupantObjectId);
        Assert.Equal(originalPlot.AllowedKinds, restoredPlot.AllowedKinds);
    }

    private static SimWorld World(
        int tickMilliseconds = 33,
        long startingResources = 2_000,
        Fixed64? commandPointMultiplier = null,
        long hordeBuildMilliseconds = 660,
        long farmBuildMilliseconds = 660,
        long baseCommandPoints = SimConfig.DefaultMaxCommandPoints)
    {
        var launch = new MatchLaunch(
            MatchLaunch.SchemaName,
            919UL,
            new MatchLaunchPack("test", new string('0', 64)),
            new MatchLaunchMap("maps/test/test.map", null),
            new MatchLaunchRules(
                tickMilliseconds,
                startingResources,
                commandPointMultiplier ?? Fixed64.One,
                false,
                Fixed64.One,
                "annihilation",
                false,
                new SortedDictionary<string, bool>(StringComparer.Ordinal)),
            new[] { new MatchLaunchPlayer(0, 0, "FactionMen", "human", null, null, 0, null, null, "Tester") },
            "skirmish",
            null);
        return new SimWorld(
            launch,
            Templates(hordeBuildMilliseconds, farmBuildMilliseconds),
            baseCommandPoints: baseCommandPoints);
    }

    private static IReadOnlyList<ObjectTemplate> Templates(long hordeBuildMilliseconds, long farmBuildMilliseconds)
    {
        var commandSet = new[] { "farm", "horde" };
        return new[]
        {
            new ObjectTemplate(
                "fortress",
                new ModuleSpec[]
                {
                    new(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 5_000 }),
                    new(ProductionModule.TypeName, new Dictionary<string, long> { ["MaxQueueEntries"] = 9 }),
                },
                bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(5_000)),
                economy: new EconomyTemplate(
                    commandSet: commandSet,
                    productionExitOffset: At(5, 0))),
            new ObjectTemplate(
                "horde",
                new ModuleSpec[]
                {
                    new(HordeContainModule.TypeName,
                        new Dictionary<string, long> { ["MemberCount"] = 5, ["MemberHealth"] = 100 },
                        new Dictionary<string, string> { ["MemberTemplate"] = "fighter" }),
                },
                economy: new EconomyTemplate(
                    buildCost: HordeCost,
                    buildTimeMilliseconds: hordeBuildMilliseconds,
                    commandPoints: 60,
                    hordeMemberTemplate: "fighter")),
            new ObjectTemplate(
                "fighter",
                new ModuleSpec[]
                {
                    new(LocomotorModule.TypeName, new Dictionary<string, long>
                    {
                        ["Speed"] = 30,
                        ["Acceleration"] = 900,
                        ["Braking"] = 900,
                        ["TurnRate"] = 360,
                    }),
                    new(DestroyDieModule.TypeName),
                },
                bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(100))),
            new ObjectTemplate(
                "farm",
                new ModuleSpec[]
                {
                    new(StructureBodyModule.TypeName, new Dictionary<string, long>
                    {
                        ["MaxHealth"] = 1_000,
                        ["InitialHealth"] = 100,
                    }),
                    new(GettingBuiltModule.TypeName),
                    new(AutoDepositUpdateModule.TypeName),
                },
                bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(1_000), Fixed64.FromInt(100)),
                economy: new EconomyTemplate(
                    buildCost: FarmCost,
                    buildTimeMilliseconds: farmBuildMilliseconds,
                    buildKind: "resource",
                    depositAmount: 25,
                    depositTimingMilliseconds: 3_300,
                    sellRefundMultiplier: Fixed64.FromFraction(1, 2))),
            new ObjectTemplate(
                "complete-farm",
                new ModuleSpec[]
                {
                    new(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 1_000 }),
                    new(AutoDepositUpdateModule.TypeName),
                },
                bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(1_000)),
                economy: new EconomyTemplate(
                    depositAmount: 25,
                    depositTimingMilliseconds: 3_300)),
        };
    }

    private static SimCommand Build(int tick, int seq, int baseId, string template, int plot) =>
        TestWorlds.Command(tick, 0, seq, "build",
            ("objects", CommandValue.OfLongList(new long[] { baseId })),
            ("template", CommandValue.OfString(template)),
            ("index", CommandValue.OfLong(plot)));

    private static SimCommand Train(int tick, int seq, int producerId, string template, int count) =>
        TestWorlds.Command(tick, 0, seq, "train",
            ("objects", CommandValue.OfLongList(new long[] { producerId })),
            ("template", CommandValue.OfString(template)),
            ("count", CommandValue.OfLong(count)));

    private static SimCommand Cancel(int tick, int seq, int producerId, int index) =>
        TestWorlds.Command(tick, 0, seq, "cancel",
            ("objects", CommandValue.OfLongList(new long[] { producerId })),
            ("index", CommandValue.OfLong(index)));

    private static SimCommand Rally(int tick, int seq, int producerId, FixedVector2 point) =>
        TestWorlds.Command(tick, 0, seq, "rally",
            ("objects", CommandValue.OfLongList(new long[] { producerId })),
            ("x", CommandValue.OfFixed(point.X)),
            ("y", CommandValue.OfFixed(point.Y)));

    private static SimCommand Sell(int tick, int seq, int objectId) =>
        TestWorlds.Command(tick, 0, seq, "sell",
            ("objects", CommandValue.OfLongList(new long[] { objectId })));

    private static void Submit(SimWorld world, SimCommand command) => Assert.True(world.SubmitCommand(command));

    private static FixedVector2 At(int x, int y) =>
        new(Fixed64.FromInt(x), Fixed64.FromInt(y));
}
