using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

/// <summary>Regression tests for the adversarial-review findings plus P1 module semantics.</summary>
public class ReviewRegressionTests
{
    [Fact(Timeout = 20000)]
    public async Task SqrtTerminatesAndFloorsForAdversarialInputs()
    {
        // The naive `while (estimate != previous)` Newton loop oscillates between
        // k and k+1 for some targets; the monotone form must terminate everywhere
        // and return the floor square root. Timeout guards against regressions.
        await Task.Run(() =>
        {
            for (long raw = 0; raw <= 5000; raw++)
            {
                AssertFloorSqrt(raw);
            }
            var random = new DeterministicRandom(7);
            for (var i = 0; i < 5000; i++)
            {
                var raw = (long)random.NextUInt32() << 31 | random.NextUInt32();
                AssertFloorSqrt(raw & long.MaxValue);
            }
            // Perfect squares and their neighbours are the classic oscillation bait.
            for (long k = 1; k <= 3000; k++)
            {
                AssertFloorSqrt(k * k - 1);
                AssertFloorSqrt(k * k);
                AssertFloorSqrt(k * k + 1);
            }
        });
    }

    private static void AssertFloorSqrt(long raw)
    {
        var root = Fixed64.Sqrt(Fixed64.FromRaw(raw));
        var target = (System.Numerics.BigInteger)raw << Fixed64.FractionBits;
        var square = (System.Numerics.BigInteger)root.Raw * root.Raw;
        var nextSquare = ((System.Numerics.BigInteger)root.Raw + 1) * ((System.Numerics.BigInteger)root.Raw + 1);
        Assert.True(square <= target, $"sqrt overshoots at raw {raw}");
        Assert.True(nextSquare > target, $"sqrt not tight at raw {raw}");
    }

    [Fact]
    public void DeadObjectsDoNotUpdateOnTheirDeathTick()
    {
        var world = new SimWorld(TestWorlds.Config(), ModuleRegistry.CreateDefault());
        var farm = world.SpawnObject("farm", 0, FixedVector2.Zero);
        // Advance to one tick before a payout, then kill via command applied at
        // tick start: the farm must NOT collect its aligned payout that tick.
        world.Advance(14);
        Assert.Equal(0, world.TeamResources(0));
        world.SubmitCommand(TestWorlds.Command(15, 0, 0, "damage",
            ("id", CommandValue.OfLong(farm.Id)), ("amount", CommandValue.OfLong(500))));
        world.Advance(1);
        Assert.Empty(world.Objects);
        Assert.Equal(0, world.TeamResources(0));
    }

    [Fact]
    public void SpawningDuringModuleUpdateDoesNotThrowAndStaysDeterministic()
    {
        SimWorld Build()
        {
            var world = new SimWorld(BarracksConfig(), ModuleRegistry.CreateDefault());
            var barracks = world.SpawnObject("barracks", 0, FixedVector2.Zero);
            barracks.FindModule<ProductionModule>()!.TryQueue("soldier");
            barracks.FindModule<ProductionModule>()!.TryQueue("soldier");
            return world;
        }

        var a = Build();
        var b = Build();
        for (var tick = 1; tick <= 60; tick++)
        {
            a.Tick();
            b.Tick();
            Assert.Equal(a.StateHash(), b.StateHash());
        }
        Assert.Equal(3, a.Objects.Count);
    }

    public static SimConfig BarracksConfig() => new(
        new[]
        {
            new ObjectTemplate("soldier", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 120 }),
                new ModuleSpec(LinearMoverModule.TypeName, null),
            }),
            new ObjectTemplate("barracks", new[]
            {
                new ModuleSpec(GettingBuiltModule.TypeName, new Dictionary<string, long> { ["ConstructionTicks"] = 10 }),
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 800 }),
                new ModuleSpec(ProductionModule.TypeName, new Dictionary<string, long>
                {
                    ["Build:soldier"] = 20,
                }),
            }),
            new ObjectTemplate("dying_farm", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 100 }),
                new ModuleSpec(SlowDeathModule.TypeName, new Dictionary<string, long> { ["DeathTicks"] = 25 }),
                new ModuleSpec(ResourceGeneratorModule.TypeName, new Dictionary<string, long>
                {
                    ["IntervalTicks"] = 10,
                    ["Amount"] = 7,
                }),
            }),
        },
        randomSeed: 99,
        teamCount: 2);
}

public class P1ModuleTests
{
    [Fact]
    public void ProductionWaitsForConstructionThenSpawnsOnSchedule()
    {
        var world = new SimWorld(ReviewRegressionTests.BarracksConfig(), ModuleRegistry.CreateDefault());
        var barracks = world.SpawnObject("barracks", 0, new FixedVector2(Fixed64.FromInt(5), Fixed64.FromInt(5)));
        var production = barracks.FindModule<ProductionModule>()!;
        Assert.True(production.TryQueue("soldier"));

        // Construction runs ticks 1-10 and clears at the END of tick 10; the
        // production module (later in template order) sees the cleared flag the
        // same tick, so build ticks are 10..29 and the soldier appears at 29.
        world.Advance(9);
        Assert.True(barracks.IsUnderConstruction);
        Assert.Single(world.Objects);
        world.Advance(1);
        Assert.False(barracks.IsUnderConstruction);

        world.Advance(18);
        Assert.Single(world.Objects);
        world.Advance(1);
        Assert.Equal(2, world.Objects.Count);
        var soldier = world.Objects.Values.First(o => o.TemplateName == "soldier");
        Assert.Equal(new FixedVector2(Fixed64.FromInt(7), Fixed64.FromInt(5)), soldier.Position);
    }

    [Fact]
    public void ProductionQueueRejectsUnknownTemplatesAndOverflow()
    {
        var world = new SimWorld(ReviewRegressionTests.BarracksConfig(), ModuleRegistry.CreateDefault());
        var barracks = world.SpawnObject("barracks", 0, FixedVector2.Zero);
        var production = barracks.FindModule<ProductionModule>()!;
        Assert.False(production.TryQueue("catapult"));
        for (var i = 0; i < ProductionModule.MaxQueueLength; i++)
        {
            Assert.True(production.TryQueue("soldier"));
        }
        Assert.False(production.TryQueue("soldier"));
    }

    [Fact]
    public void SlowDeathDelaysRemovalAndStopsIncome()
    {
        var world = new SimWorld(ReviewRegressionTests.BarracksConfig(), ModuleRegistry.CreateDefault());
        var farm = world.SpawnObject("dying_farm", 0, FixedVector2.Zero);
        world.Advance(10);
        Assert.Equal(7, world.TeamResources(0));

        world.DealDamage(farm, 100);
        Assert.Single(world.Objects);

        // Dying object persists for DeathTicks=25 but produces nothing.
        world.Advance(24);
        Assert.Single(world.Objects);
        Assert.Equal(7, world.TeamResources(0));
        world.Advance(1);
        Assert.Empty(world.Objects);
    }

    [Fact]
    public void SlowDeathWorldSnapshotRoundTripsMidDeath()
    {
        var config = ReviewRegressionTests.BarracksConfig();
        var world = new SimWorld(config, ModuleRegistry.CreateDefault());
        var farm = world.SpawnObject("dying_farm", 0, FixedVector2.Zero);
        world.DealDamage(farm, 100);
        world.Advance(10);

        var restored = SimWorld.Restore(world.Snapshot(), config, ModuleRegistry.CreateDefault());
        for (var tick = 0; tick < 30; tick++)
        {
            world.Tick();
            restored.Tick();
            Assert.Equal(world.StateHash(), restored.StateHash());
        }
        Assert.Empty(world.Objects);
        Assert.Empty(restored.Objects);
    }

    [Fact]
    public void QueueProductionCommandRoutesThroughTeamValidation()
    {
        var world = new SimWorld(ReviewRegressionTests.BarracksConfig(), ModuleRegistry.CreateDefault());
        var barracks = world.SpawnObject("barracks", 0, FixedVector2.Zero);
        world.Advance(11); // finish construction

        // Wrong team may not queue on this producer.
        world.SubmitCommand(TestWorlds.Command(12, 1, 0, "queue_production",
            ("id", CommandValue.OfLong(barracks.Id)), ("template", CommandValue.OfString("soldier"))));
        // Right team queues successfully.
        world.SubmitCommand(TestWorlds.Command(12, 0, 0, "queue_production",
            ("id", CommandValue.OfLong(barracks.Id)), ("template", CommandValue.OfString("soldier"))));
        world.Advance(1);
        Assert.Equal(1, barracks.FindModule<ProductionModule>()!.QueueLength);
    }

    [Fact]
    public void DyingObjectsIgnoreFurtherDamage()
    {
        var world = new SimWorld(ReviewRegressionTests.BarracksConfig(), ModuleRegistry.CreateDefault());
        var farm = world.SpawnObject("dying_farm", 0, FixedVector2.Zero);
        world.DealDamage(farm, 100);
        var hashAfterDeath = world.StateHash();
        world.DealDamage(farm, 100);
        Assert.Equal(hashAfterDeath, world.StateHash());
    }
}
