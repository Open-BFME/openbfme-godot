using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public static class TestWorlds
{
    public static SimConfig Config(ulong seed = 42) => new(
        new[]
        {
            new ObjectTemplate("soldier", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 120 }),
                new ModuleSpec(LinearMoverModule.TypeName, new Dictionary<string, long>
                {
                    ["SpeedPerTickRaw"] = Fixed64.FromFraction(1, 4).Raw,
                }),
            }),
            new ObjectTemplate("farm", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 500 }),
                new ModuleSpec(ResourceGeneratorModule.TypeName, new Dictionary<string, long>
                {
                    ["IntervalTicks"] = 15,
                    ["Amount"] = 5,
                }),
            }),
            new ObjectTemplate("mystery", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 10 }),
                new ModuleSpec("NotYetImplementedBehavior", null),
            }),
        },
        seed,
        teamCount: 2);

    public static List<SimCommand> ScriptedCommands()
    {
        Fixed64 F(long n, long d = 1) => Fixed64.FromFraction(n, d);
        var commands = new List<SimCommand>
        {
            Command(1, 0, 0, "spawn", ("template", CommandValue.OfString("farm")), ("x", CommandValue.OfFixed(F(-20))), ("y", CommandValue.OfFixed(F(-10)))),
            Command(1, 1, 0, "spawn", ("template", CommandValue.OfString("farm")), ("x", CommandValue.OfFixed(F(20))), ("y", CommandValue.OfFixed(F(10)))),
            Command(2, 0, 1, "spawn", ("template", CommandValue.OfString("soldier")), ("x", CommandValue.OfFixed(F(-18))), ("y", CommandValue.OfFixed(F(-9)))),
            Command(2, 1, 1, "spawn", ("template", CommandValue.OfString("soldier")), ("x", CommandValue.OfFixed(F(18))), ("y", CommandValue.OfFixed(F(9)))),
            Command(5, 0, 2, "move", ("id", CommandValue.OfLong(3)), ("x", CommandValue.OfFixed(F(15))), ("y", CommandValue.OfFixed(F(7)))),
            Command(5, 1, 2, "move", ("id", CommandValue.OfLong(4)), ("x", CommandValue.OfFixed(F(-15, 1))), ("y", CommandValue.OfFixed(F(-71, 10)))),
            Command(40, 0, 3, "roll"),
            Command(40, 1, 3, "roll"),
            Command(200, 0, 4, "damage", ("id", CommandValue.OfLong(4)), ("amount", CommandValue.OfLong(80))),
            Command(900, 1, 5, "damage", ("id", CommandValue.OfLong(3)), ("amount", CommandValue.OfLong(200))),
            Command(1600, 0, 6, "spawn", ("template", CommandValue.OfString("soldier")), ("x", CommandValue.OfFixed(F(0))), ("y", CommandValue.OfFixed(F(0)))),
            Command(1610, 0, 7, "move", ("id", CommandValue.OfLong(5)), ("x", CommandValue.OfFixed(F(30))), ("y", CommandValue.OfFixed(F(-30)))),
            Command(2500, 1, 8, "roll"),
        };
        return commands;
    }

    public static SimCommand Command(int tick, int team, int seq, string type,
        params (string Key, CommandValue Value)[] args) =>
        new(tick, team, seq, type, args.Select(a => new KeyValuePair<string, CommandValue>(a.Key, a.Value)));

    public static SimWorld BuildAndSubmit(IEnumerable<SimCommand>? commands = null, ulong seed = 42)
    {
        var world = new SimWorld(Config(seed), ModuleRegistry.CreateDefault());
        foreach (var command in commands ?? ScriptedCommands())
        {
            Assert.True(world.SubmitCommand(command), $"command {command.Type}@{command.Tick} rejected");
        }
        return world;
    }
}

public class DeterminismTests
{
    private const int ProofTicks = 3000;

    [Fact]
    public void TwinRunsStayHashIdenticalForEveryTick()
    {
        var a = TestWorlds.BuildAndSubmit();
        var b = TestWorlds.BuildAndSubmit();
        for (var tick = 1; tick <= ProofTicks; tick++)
        {
            a.Tick();
            b.Tick();
            Assert.Equal(a.StateHash(), b.StateHash());
        }
        Assert.True(a.Objects.Count > 0, "scenario should leave survivors");
    }

    [Fact]
    public void HashIsSensitiveToASingleAlteredCommand()
    {
        var commands = TestWorlds.ScriptedCommands();
        var altered = TestWorlds.ScriptedCommands();
        altered[4] = TestWorlds.Command(5, 0, 2, "move",
            ("id", CommandValue.OfLong(3)),
            ("x", CommandValue.OfFixed(Fixed64.FromFraction(151, 10))),
            ("y", CommandValue.OfFixed(Fixed64.FromInt(7))));

        var a = TestWorlds.BuildAndSubmit(commands);
        var b = TestWorlds.BuildAndSubmit(altered);
        // The altered unit is destroyed later in the script, so the worlds may
        // legitimately reconverge; sensitivity means the hash differs while the
        // divergent state exists.
        var diverged = false;
        for (var tick = 1; tick <= ProofTicks; tick++)
        {
            a.Tick();
            b.Tick();
            if (a.StateHash() != b.StateHash())
            {
                diverged = true;
            }
        }
        Assert.True(diverged, "altered command never changed the state hash");
    }

    [Fact]
    public void CommandSubmissionOrderDoesNotAffectOutcome()
    {
        var forward = TestWorlds.ScriptedCommands();
        var reversed = TestWorlds.ScriptedCommands();
        reversed.Reverse();

        var a = TestWorlds.BuildAndSubmit(forward);
        var b = TestWorlds.BuildAndSubmit(reversed);
        a.Advance(ProofTicks);
        b.Advance(ProofTicks);
        Assert.Equal(a.StateHash(), b.StateHash());
    }

    [Fact]
    public void SnapshotRoundTripsMidRunAndStaysIdentical()
    {
        var original = TestWorlds.BuildAndSubmit();
        original.Advance(1500);

        var restored = SimWorld.Restore(original.Snapshot(), TestWorlds.Config(), ModuleRegistry.CreateDefault());
        Assert.Equal(original.StateHash(), restored.StateHash());

        for (var tick = 1501; tick <= ProofTicks; tick++)
        {
            original.Tick();
            restored.Tick();
            Assert.Equal(original.StateHash(), restored.StateHash());
        }
    }

    [Fact]
    public void SnapshotOfRestoredWorldIsByteIdentical()
    {
        var original = TestWorlds.BuildAndSubmit();
        original.Advance(777);
        var snapshot = original.Snapshot();
        var restored = SimWorld.Restore(snapshot, TestWorlds.Config(), ModuleRegistry.CreateDefault());
        Assert.Equal(snapshot, restored.Snapshot());
    }

    [Fact]
    public void RandomStreamIsAuthoritativeState()
    {
        var withRoll = TestWorlds.BuildAndSubmit();
        var withoutRoll = TestWorlds.BuildAndSubmit(
            TestWorlds.ScriptedCommands().Where(c => c.Type != "roll").ToList());
        withRoll.Advance(50);
        withoutRoll.Advance(50);
        Assert.NotEqual(withRoll.StateHash(), withoutRoll.StateHash());
    }

    [Fact]
    public void UnimplementedModuleTypesAreRecordedAsGaps()
    {
        var world = new SimWorld(TestWorlds.Config(), ModuleRegistry.CreateDefault());
        world.SpawnObject("mystery", 0, FixedVector2.Zero);
        world.SpawnObject("mystery", 1, FixedVector2.Zero);
        Assert.Equal(2, world.ModuleGaps["NotYetImplementedBehavior"]);
    }

    [Fact]
    public void CommandsForPastTicksAreRejected()
    {
        var world = new SimWorld(TestWorlds.Config(), ModuleRegistry.CreateDefault());
        world.Advance(10);
        Assert.False(world.SubmitCommand(TestWorlds.Command(10, 0, 0, "roll")));
        Assert.False(world.SubmitCommand(TestWorlds.Command(3, 0, 0, "roll")));
        Assert.True(world.SubmitCommand(TestWorlds.Command(11, 0, 0, "roll")));
    }

    [Fact]
    public void DeadObjectsAreRemovedAndIncomeStops()
    {
        var world = new SimWorld(TestWorlds.Config(), ModuleRegistry.CreateDefault());
        var farm = world.SpawnObject("farm", 0, FixedVector2.Zero);
        world.Advance(15);
        var incomeAfter15 = world.TeamResources(0);
        Assert.Equal(5, incomeAfter15);
        world.DealDamage(farm, 500);
        world.Advance(1);
        Assert.Empty(world.Objects);
        world.Advance(60);
        Assert.Equal(incomeAfter15, world.TeamResources(0));
    }

    [Fact]
    public void MoverArrivesExactlyAtDestination()
    {
        var world = new SimWorld(TestWorlds.Config(), ModuleRegistry.CreateDefault());
        var soldier = world.SpawnObject("soldier", 0, FixedVector2.Zero);
        var destination = new FixedVector2(Fixed64.FromInt(3), Fixed64.FromInt(4));
        soldier.FindModule<LinearMoverModule>()!.SetDestination(destination);
        world.Advance(200);
        Assert.Equal(destination, soldier.Position);
    }
}

public class Fixed64Tests
{
    [Fact]
    public void FractionRoundTripsThroughArithmetic()
    {
        var quarter = Fixed64.FromFraction(1, 4);
        Assert.Equal(Fixed64.One, quarter + quarter + quarter + quarter);
        Assert.Equal(Fixed64.FromInt(1), Fixed64.FromFraction(4, 4));
        Assert.Equal(Fixed64.FromFraction(3, 8), quarter * Fixed64.FromFraction(3, 2));
    }

    [Fact]
    public void SqrtIsExactForPerfectSquaresAndMonotonic()
    {
        Assert.Equal(Fixed64.FromInt(12), Fixed64.Sqrt(Fixed64.FromInt(144)));
        Assert.Equal(Fixed64.Zero, Fixed64.Sqrt(Fixed64.Zero));
        var previous = Fixed64.Zero;
        for (var i = 1; i <= 500; i++)
        {
            var root = Fixed64.Sqrt(Fixed64.FromInt(i));
            Assert.True(root >= previous, $"sqrt not monotonic at {i}");
            Assert.True(root * root <= Fixed64.FromInt(i), $"sqrt overshoots at {i}");
            previous = root;
        }
    }

    [Fact]
    public void DivisionByZeroAndOverflowThrow()
    {
        Assert.Throws<DivideByZeroException>(() => Fixed64.One / Fixed64.Zero);
        Assert.Throws<OverflowException>(() => Fixed64.MaxValue + Fixed64.One);
        Assert.Throws<OverflowException>(() => Fixed64.MaxValue * Fixed64.FromInt(2));
    }
}

public class DeterministicRandomTests
{
    [Fact]
    public void SameSeedSameSequence()
    {
        var a = new DeterministicRandom(1234, 7);
        var b = new DeterministicRandom(1234, 7);
        for (var i = 0; i < 1000; i++)
        {
            Assert.Equal(a.NextUInt32(), b.NextUInt32());
        }
    }

    [Fact]
    public void SerializedStreamContinuesIdentically()
    {
        var original = new DeterministicRandom(99);
        for (var i = 0; i < 137; i++)
        {
            original.NextUInt32();
        }
        var (state, increment) = original.Serialize();
        var restored = DeterministicRandom.Deserialize(state, increment);
        for (var i = 0; i < 1000; i++)
        {
            Assert.Equal(original.NextUInt32(), restored.NextUInt32());
        }
    }

    [Fact]
    public void NextBelowStaysInRange()
    {
        var random = new DeterministicRandom(5);
        for (var i = 0; i < 10000; i++)
        {
            Assert.InRange(random.NextBelow(7), 0u, 6u);
        }
    }
}
