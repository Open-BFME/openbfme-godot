using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class MonitorConditionUpdateTests
{
    [Fact]
    public void CorpusPairsSelectTheirAuthoredCommandSetsIndependently()
    {
        var modelTemplate = ModuleBatchBTestSupport.Template("catapult", new[]
        {
            ModuleBatchBTestSupport.Spec(MonitorConditionUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["ModelConditionFlags"] = "ATTACKING_POSITION",
                    ["ModelConditionCommandSet"] = "CatapultStopBombardCommandSet",
                }),
        }, commandSet: "CatapultCommandSet", techEnabled: true);
        var weaponTemplate = ModuleBatchBTestSupport.Template("corsairs", new[]
        {
            ModuleBatchBTestSupport.Spec(MonitorConditionUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["WeaponSetFlags"] = "WEAPONSET_TOGGLE_1",
                    ["WeaponToggleCommandSet"] = "CorsairFireBombCommandSet",
                }),
        }, commandSet: "CorsairCommandSet", techEnabled: true);
        var world = ModuleBatchBTestSupport.World(new[] { modelTemplate, weaponTemplate });
        var catapult = world.SpawnObject("catapult", 0, ModuleBatchBTestSupport.At(0));
        var corsairs = world.SpawnObject("corsairs", 0, ModuleBatchBTestSupport.At(0));
        catapult.SetConditionToken("MODEL:ATTACKING_POSITION");
        corsairs.SetConditionToken("WEAPONSET_TOGGLE_1");

        world.Tick();

        Assert.True(catapult.FindModule<MonitorConditionUpdateModule>()!.IsModelConditionActive);
        Assert.Equal("CatapultStopBombardCommandSet", catapult.CurrentCommandSet);
        Assert.True(corsairs.FindModule<MonitorConditionUpdateModule>()!.IsWeaponToggleActive);
        Assert.Equal("CorsairFireBombCommandSet", corsairs.CurrentCommandSet);
    }
}
