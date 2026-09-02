using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class FlammableUpdateTests
{
    [Fact]
    public void FlameStartsPeriodicAflameDamageAndWaterExtinguishes()
    {
        var template = ModuleBatchBTestSupport.Template("tree", new[]
        {
            ModuleBatchBTestSupport.Spec(FlammableUpdateModule.TypeName,
                new Dictionary<string, long>
                {
                    ["FlameDamageLimit"] = 1, ["BurnedDelay"] = 0,
                    ["AflameDamageAmount"] = 5, ["AflameDamageDelay"] = 100,
                }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var tree = world.SpawnObject("tree", 0, ModuleBatchBTestSupport.At(0));

        world.DealDamage(tree, 1, "FLAME");
        world.Tick();
        Assert.True(tree.FindModule<FlammableUpdateModule>()!.IsBurning);
        Assert.Equal(Fixed64.FromInt(94), tree.Health);
        world.DealDamage(tree, 1, "WATER");
        Assert.False(tree.FindModule<FlammableUpdateModule>()!.IsBurning);
    }
}
