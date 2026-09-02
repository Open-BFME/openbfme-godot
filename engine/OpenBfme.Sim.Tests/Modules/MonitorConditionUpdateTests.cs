using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class MonitorConditionUpdateTests
{
    [Fact]
    public void WeaponSetFlagRaisesAuthoredModelCondition()
    {
        var template = ModuleBatchBTestSupport.Template("corsairs", new[]
        {
            ModuleBatchBTestSupport.Spec(MonitorConditionUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["WeaponSetFlags"] = "WEAPONSET_TOGGLE_1", ["ModelConditionFlags"] = "USER_1",
                }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var corsairs = world.SpawnObject("corsairs", 0, ModuleBatchBTestSupport.At(0));
        corsairs.SetConditionToken("WEAPONSET_TOGGLE_1");

        world.Tick();

        Assert.True(corsairs.FindModule<MonitorConditionUpdateModule>()!.IsActive);
        Assert.Contains("MODEL:USER_1", corsairs.ConditionTokens);
    }
}
