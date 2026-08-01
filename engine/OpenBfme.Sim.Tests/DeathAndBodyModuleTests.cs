using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class DeathAndBodyModuleTests
{
    private static SimConfig Config() => new(
        new[]
        {
            new ObjectTemplate("immortal_shrine", new[]
            {
                new ModuleSpec(ImmortalBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 50 }),
            }),
            new ObjectTemplate("summon", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 60 }),
                new ModuleSpec(LifetimeModule.TypeName, new Dictionary<string, long> { ["LifetimeTicks"] = 40 }),
                new ModuleSpec(DestroyDieModule.TypeName, null),
            }),
            new ObjectTemplate("tower", new[]
            {
                new ModuleSpec(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 300 }),
                new ModuleSpec(StructureCollapseModule.TypeName, new Dictionary<string, long> { ["CollapseTicks"] = 12 }),
            }),
            new ObjectTemplate("monument", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 10 }),
                new ModuleSpec(KeepObjectDieModule.TypeName, null),
            }),
        },
        randomSeed: 7,
        teamCount: 2);

    private static SimWorld NewWorld() => new(Config(), ModuleRegistry.CreateDefault());

    [Fact]
    public void ImmortalBodySurvivesArbitraryDamageAtOneHealth()
    {
        var world = NewWorld();
        var shrine = world.SpawnObject("immortal_shrine", 0, FixedVector2.Zero);
        world.DealDamage(shrine, 10_000);
        world.Advance(5);
        Assert.Single(world.Objects);
        Assert.Equal(1, shrine.FindModule<ImmortalBodyModule>()!.Health);
        world.DealDamage(shrine, 10_000);
        Assert.Equal(1, shrine.FindModule<ImmortalBodyModule>()!.Health);
    }

    [Fact]
    public void LifetimeExpiresThroughDeathPipelineOnExactTick()
    {
        var world = NewWorld();
        world.SpawnObject("summon", 0, FixedVector2.Zero);
        world.Advance(39);
        Assert.Single(world.Objects);
        world.Advance(1);
        Assert.Empty(world.Objects);
    }

    [Fact]
    public void StructureCollapseHoldsRubbleThenRemoves()
    {
        var world = NewWorld();
        var tower = world.SpawnObject("tower", 1, FixedVector2.Zero);
        world.DealDamage(tower, 300);
        Assert.True(tower.IsDying);
        world.Advance(11);
        Assert.Single(world.Objects);
        world.Advance(1);
        Assert.Empty(world.Objects);
    }

    [Fact]
    public void KeepObjectDieCorpsePersistsIndefinitely()
    {
        var world = NewWorld();
        var monument = world.SpawnObject("monument", 0, FixedVector2.Zero);
        world.DealDamage(monument, 10);
        world.Advance(500);
        Assert.Single(world.Objects);
        Assert.True(monument.IsDying);
    }

    [Fact]
    public void MixedDeathScenarioIsDeterministicAndSnapshotSafe()
    {
        SimWorld Build()
        {
            var world = NewWorld();
            world.SpawnObject("immortal_shrine", 0, FixedVector2.Zero);
            world.SpawnObject("summon", 0, new FixedVector2(Fixed64.FromInt(1), Fixed64.Zero));
            world.SpawnObject("tower", 1, new FixedVector2(Fixed64.FromInt(2), Fixed64.Zero));
            world.SpawnObject("monument", 1, new FixedVector2(Fixed64.FromInt(3), Fixed64.Zero));
            world.SubmitCommand(TestWorlds.Command(5, 1, 0, "damage",
                ("id", CommandValue.OfLong(3)), ("amount", CommandValue.OfLong(300))));
            world.SubmitCommand(TestWorlds.Command(6, 0, 1, "damage",
                ("id", CommandValue.OfLong(4)), ("amount", CommandValue.OfLong(10))));
            return world;
        }

        var a = Build();
        var b = Build();
        a.Advance(10);
        b.Advance(10);
        var restored = SimWorld.Restore(a.Snapshot(), Config(), ModuleRegistry.CreateDefault());
        for (var tick = 11; tick <= 80; tick++)
        {
            a.Tick();
            b.Tick();
            restored.Tick();
            Assert.Equal(a.StateHash(), b.StateHash());
            Assert.Equal(a.StateHash(), restored.StateHash());
        }
        // Survivors: shrine (immortal) + monument corpse (kept). Summon expired, tower collapsed.
        Assert.Equal(2, a.Objects.Count);
    }

    [Fact]
    public void RegistryCoversElevenModuleTypes()
    {
        var registry = ModuleRegistry.CreateDefault();
        var typeNames = new[]
        {
            ActiveBodyModule.TypeName, ResourceGeneratorModule.TypeName, LinearMoverModule.TypeName,
            SlowDeathModule.TypeName, ProductionModule.TypeName, GettingBuiltModule.TypeName,
            StructureBodyModule.TypeName, ImmortalBodyModule.TypeName, LifetimeModule.TypeName,
            DestroyDieModule.TypeName, KeepObjectDieModule.TypeName, StructureCollapseModule.TypeName,
        };
        foreach (var typeName in typeNames)
        {
            Assert.True(registry.TryCreate(new ModuleSpec(typeName, null), out _), $"{typeName} not registered");
        }
    }
}
