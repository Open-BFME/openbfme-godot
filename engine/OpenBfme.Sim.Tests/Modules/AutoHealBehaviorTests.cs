using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class AutoHealBehaviorTests
{
    [Fact]
    public void BareCorpusKindOfListRestrictsRadiusHeal()
    {
        var healerTemplate = ModuleBatchBTestSupport.Template("well", new[]
        {
            ModuleBatchBTestSupport.Spec(AutoHealBehaviorModule.TypeName,
                new Dictionary<string, long>
                {
                    ["HealingAmount"] = 7, ["HealingDelay"] = 100,
                    ["StartHealingDelay"] = 0, ["StartsActive"] = 1, ["Radius"] = 10,
                },
                new Dictionary<string, string> { ["KindOf"] = "INFANTRY" }),
        });
        var infantryTemplate = ModuleBatchBTestSupport.Template("soldier", Array.Empty<ModuleSpec>(),
            kindOf: new[] { "INFANTRY" });
        var cavalryTemplate = ModuleBatchBTestSupport.Template("rider", Array.Empty<ModuleSpec>(),
            kindOf: new[] { "CAVALRY" });
        var world = ModuleBatchBTestSupport.World(new[] { healerTemplate, infantryTemplate, cavalryTemplate });
        world.SpawnObject("well", 0, ModuleBatchBTestSupport.At(0));
        var soldier = world.SpawnObject("soldier", 0, ModuleBatchBTestSupport.At(1));
        var rider = world.SpawnObject("rider", 0, ModuleBatchBTestSupport.At(2));
        world.DealDamage(soldier, 20);
        world.DealDamage(rider, 20);

        world.Tick();

        Assert.Equal(Fixed64.FromInt(87), soldier.Health);
        Assert.Equal(Fixed64.FromInt(80), rider.Health);
    }
}
