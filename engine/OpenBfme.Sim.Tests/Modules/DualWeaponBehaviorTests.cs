using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class DualWeaponBehaviorTests
{
    [Fact]
    public void CloseRangeUsesSecondaryWeaponFromAuthoredWeaponSet()
    {
        var primary = ModuleBatchBTestSupport.Weapon("Bow", 5);
        var secondary = ModuleBatchBTestSupport.Weapon("Sword", 20);
        var set = new WeaponSet(null, new Dictionary<WeaponSlot, string>
        {
            [WeaponSlot.PRIMARY] = primary.Name,
            [WeaponSlot.SECONDARY] = secondary.Name,
        });
        var attackerTemplate = ModuleBatchBTestSupport.Template("ranger", new[]
        {
            ModuleBatchBTestSupport.Spec(DualWeaponBehaviorModule.TypeName,
                new Dictionary<string, long> { ["SwitchWeaponOnCloseRangeDistance"] = 5 }),
        }, weaponSets: new[] { set });
        var targetTemplate = ModuleBatchBTestSupport.Template("target", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { attackerTemplate, targetTemplate }, new[] { primary, secondary });
        var attacker = world.SpawnObject("ranger", 0, ModuleBatchBTestSupport.At(0));
        var target = world.SpawnObject("target", 1, ModuleBatchBTestSupport.At(2));
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "attack",
            ("objects", CommandValue.OfLongList(new long[] { attacker.Id })),
            ("target", CommandValue.OfLong(target.Id)))));

        world.Tick();

        Assert.Equal(Fixed64.FromInt(80), target.Health);
        Assert.Contains(world.EventsThisTick, value => value.Kind == "fire" && value.Name == secondary.Name);
    }
}
