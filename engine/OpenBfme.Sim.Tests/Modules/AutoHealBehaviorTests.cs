using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class AutoHealBehaviorTests
{
    [Fact]
    public void AuthoredStartAndHealingDelayPulseExactAmount()
    {
        var template = ModuleBatchBTestSupport.Template("hero", new[]
        {
            ModuleBatchBTestSupport.Spec(AutoHealBehaviorModule.TypeName,
                new Dictionary<string, long>
                {
                    ["HealingAmount"] = 7, ["HealingDelay"] = 100,
                    ["StartHealingDelay"] = 0, ["StartsActive"] = 1,
                }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var hero = world.SpawnObject("hero", 0, ModuleBatchBTestSupport.At(0));
        world.DealDamage(hero, 20);

        world.Tick();

        Assert.Equal(Fixed64.FromInt(87), hero.Health);
    }
}
