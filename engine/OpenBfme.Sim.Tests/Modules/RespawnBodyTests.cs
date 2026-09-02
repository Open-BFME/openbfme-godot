using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class RespawnBodyTests
{
    [Fact]
    public void DyingSnapshotPersistsThenFreshIdRespawnsFullAtAnchor()
    {
        var template = new ObjectTemplate("hero", new[]
        {
            new ModuleSpec(RespawnBodyModule.TypeName,
                new Dictionary<string, long> { ["MaxHealth"] = 50, ["RespawnTime"] = 66 }),
        });
        var config = new SimConfig(new[] { template }, 2, 2);
        var registry = ModuleRegistry.CreateDefault();
        var world = new SimWorld(config, registry, 33);
        var hero = world.SpawnObject("hero", 0,
            new FixedVector2(Fixed64.FromInt(3), Fixed64.FromInt(4)));

        world.DealDamage(hero, 50);
        Assert.True(hero.IsDying);
        var restored = SimWorld.Restore(world.Snapshot(), config, registry);
        Assert.True(restored.Objects[hero.Id].IsDying);

        world.Advance(2);

        var replacement = Assert.Single(world.Objects.Values);
        Assert.NotEqual(hero.Id, replacement.Id);
        Assert.Equal(hero.Position, replacement.Position);
        Assert.Equal(50, replacement.FindModule<RespawnBodyModule>()!.Health);
    }
}
