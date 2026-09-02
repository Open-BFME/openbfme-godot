using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class PickupStuffUpdateTests
{
    [Fact]
    public void StuffFilterPicksSalvageCrateAndGrantsAuthoredResources()
    {
        var collector = ModuleBatchBTestSupport.Template("collector", new[]
        {
            ModuleBatchBTestSupport.Spec(PickupStuffUpdateModule.TypeName,
                new Dictionary<string, long> { ["ScanRange"] = 10, ["ScanIntervalSecondsRaw"] = Fixed64.One.Raw },
                new Dictionary<string, string> { ["StuffToPickUp"] = "NONE +CRATE" }),
        });
        var crate = ModuleBatchBTestSupport.Template("crate", new[]
        {
            ModuleBatchBTestSupport.Spec("SalvageCrateCollide",
                new Dictionary<string, long> { ["MinResource"] = 20, ["MaxResource"] = 40 }),
        }, kindOf: new[] { "CRATE" });
        var world = ModuleBatchBTestSupport.World(new[] { collector, crate });
        world.SpawnObject("collector", 0, ModuleBatchBTestSupport.At(0));
        var pickup = world.SpawnObject("crate", 1, ModuleBatchBTestSupport.At(2));

        world.Tick();

        Assert.Equal(30, world.TeamResources(0));
        Assert.DoesNotContain(pickup.Id, world.Objects.Keys);
    }
}
