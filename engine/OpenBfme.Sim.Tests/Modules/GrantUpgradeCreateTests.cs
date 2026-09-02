using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class GrantUpgradeCreateTests
{
    [Fact]
    public void UpgradeToGrantIsOwnedImmediatelyOnCreation()
    {
        var template = ModuleBatchBTestSupport.Template("porter", new[]
        {
            ModuleBatchBTestSupport.Spec(GrantUpgradeCreateModule.TypeName,
                strings: new Dictionary<string, string> { ["UpgradeToGrant"] = "HorseShield" }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });

        var porter = world.SpawnObject("porter", 0, ModuleBatchBTestSupport.At(0));

        Assert.Contains("HorseShield", porter.OwnedUpgrades);
    }
}
