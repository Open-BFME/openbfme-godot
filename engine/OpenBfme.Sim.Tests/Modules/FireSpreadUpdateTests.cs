using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class FireSpreadUpdateTests
{
    [Fact]
    public void SpreadTryRangeIgnitesNeighbourOnAuthoredCadence()
    {
        var source = ModuleBatchBTestSupport.Template("source", new[]
        {
            ModuleBatchBTestSupport.Spec(FlammableUpdateModule.TypeName, new Dictionary<string, long> { ["FlameDamageLimit"] = 1 }),
            ModuleBatchBTestSupport.Spec(FireSpreadUpdateModule.TypeName,
                new Dictionary<string, long> { ["SpreadTryRange"] = 5, ["MinSpreadDelay"] = 100, ["MaxSpreadDelay"] = 100 }),
        });
        var target = ModuleBatchBTestSupport.Template("target", new[]
        {
            ModuleBatchBTestSupport.Spec(FlammableUpdateModule.TypeName, new Dictionary<string, long> { ["FlameDamageLimit"] = 1 }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { source, target });
        var burning = world.SpawnObject("source", 0, ModuleBatchBTestSupport.At(0));
        var neighbour = world.SpawnObject("target", 0, ModuleBatchBTestSupport.At(3));
        burning.FindModule<FlammableUpdateModule>()!.Ignite(world, burning);

        world.Advance(2);

        Assert.True(neighbour.FindModule<FlammableUpdateModule>()!.IsBurning);
    }
}
