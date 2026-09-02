using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class FlammableUpdateTests
{
    [Fact]
    public void ShipFieldsUseAuthoredDamageTypeWithoutBurnedStatus()
    {
        var template = ModuleBatchBTestSupport.Template("ship", new[]
        {
            ModuleBatchBTestSupport.Spec(FlammableUpdateModule.TypeName,
                new Dictionary<string, long>
                {
                    ["FlameDamageLimit"] = 1, ["BurnedDelay"] = 0,
                    ["AflameDamageAmount"] = 5, ["AflameDamageDelay"] = 100,
                    ["SetBurnedStatus"] = 0,
                },
                new Dictionary<string, string> { ["DamageType"] = "FORCE" }),
            ModuleBatchBTestSupport.Spec(BuildingBehaviorModule.TypeName,
                strings: new Dictionary<string, string> { ["FireWindowName"] = "WINDOW_F01" }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var ship = world.SpawnObject("ship", 0, ModuleBatchBTestSupport.At(0));

        ship.FindModule<FlammableUpdateModule>()!.Ignite(world, ship);
        world.Tick();
        Assert.True(ship.FindModule<FlammableUpdateModule>()!.IsBurning);
        Assert.DoesNotContain("BURNING", ship.ConditionTokens);
        Assert.DoesNotContain("MODEL:FIRE:WINDOW_F01", ship.ConditionTokens);
        Assert.Equal(Fixed64.FromInt(95), ship.Health);
        world.DealDamage(ship, 1, "WATER");
        Assert.False(ship.FindModule<FlammableUpdateModule>()!.IsBurning);
    }
}
