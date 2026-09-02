using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class FXListDieTests
{
    [Fact]
    public void DeathRaisesAuthoredFxNameForPresentation()
    {
        var template = ModuleBatchBTestSupport.Template("rock", new[]
        {
            ModuleBatchBTestSupport.Spec(FXListDieModule.TypeName,
                strings: new Dictionary<string, string> { ["DeathFX"] = "FX_RockImpact" }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var rock = world.SpawnObject("rock", 0, ModuleBatchBTestSupport.At(0));

        world.DealDamage(rock, 100);

        Assert.Contains(world.EventsThisTick,
            value => value.Kind == "death" && value.Object == rock.Id && value.Name == "FX_RockImpact");
    }
}
