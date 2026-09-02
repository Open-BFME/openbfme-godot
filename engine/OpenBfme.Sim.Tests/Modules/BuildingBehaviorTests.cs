using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class BuildingBehaviorTests
{
    [Fact]
    public void RebuildTimeKeepsFoundationAndRestoresStructure()
    {
        var template = ModuleBatchBTestSupport.Template("tower", new[]
        {
            ModuleBatchBTestSupport.Spec(BuildingBehaviorModule.TypeName,
                new Dictionary<string, long> { ["RebuildTime"] = 200 }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var tower = world.SpawnObject("tower", 0, ModuleBatchBTestSupport.At(0));

        world.DealDamage(tower, 100);
        Assert.True(tower.FindModule<BuildingBehaviorModule>()!.IsRebuildHole);
        world.Advance(2);

        Assert.False(tower.IsDying);
        Assert.Equal(tower.MaxHealth, tower.Health);
    }
}
