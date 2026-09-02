using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class TerrainResourceBehaviorTests
{
    [Fact]
    public void NearbySiblingAppliesAuthoredCrowdingPenaltyToDeposits()
    {
        var template = new ObjectTemplate("farm", new ModuleSpec[]
        {
            new(TerrainResourceBehaviorModule.TypeName,
                new Dictionary<string, long> { ["Radius"] = 5, ["CrowdingPenaltyPercent"] = 25 }),
            new(AutoDepositUpdateModule.TypeName,
                new Dictionary<string, long> { ["DepositAmount"] = 100, ["DepositTiming"] = 100 }),
        });
        var world = new SimWorld(new SimConfig(new[] { template }, 14, 2), ModuleRegistry.CreateDefault(), 100);
        var first = world.SpawnObject("farm", 0, FixedVector2.Zero);
        world.SpawnObject("farm", 0, new FixedVector2(Fixed64.FromInt(3), Fixed64.Zero));

        world.Tick();

        Assert.Equal(150, world.TeamResources(0));
        Assert.Equal(1, first.FindModule<TerrainResourceBehaviorModule>()!.SiblingCount);
        Assert.Equal(Fixed64.FromFraction(3, 4), first.FindModule<AutoDepositUpdateModule>()!.CrowdingMultiplier);
    }
}
