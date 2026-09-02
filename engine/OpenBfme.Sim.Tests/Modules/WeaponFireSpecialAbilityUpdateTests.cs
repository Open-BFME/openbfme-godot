using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class WeaponFireSpecialAbilityUpdateTests
{
    [Fact]
    public void CastFiresSpecialWeaponOnceThroughCombatDamage()
    {
        var power = new SpecialPowerTemplate("Spear", "POWER_SPEAR", 500, Array.Empty<string>(), false);
        var button = new CommandButtonTemplate("SpearButton", "SPECIAL_POWER", "", "", "", power.Name);
        var set = new CommandSetTemplate("HeroSet", new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(specialPowers: new[] { power }, commandButtons: new[] { button }, commandSets: new[] { set });
        var weapon = ModuleBatchBTestSupport.Weapon("HeroSpear", 25);
        var hero = ModuleBatchBTestSupport.Template("hero", new[]
        {
            ModuleBatchBTestSupport.Spec(WeaponFireSpecialAbilityUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["SpecialPowerTemplate"] = power.Name, ["SpecialWeapon"] = weapon.Name,
                }),
        }, commandSet: set.Name);
        var targetTemplate = ModuleBatchBTestSupport.Template("target", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { hero, targetTemplate }, new[] { weapon }, tech);
        var caster = world.SpawnObject("hero", 0, ModuleBatchBTestSupport.At(0));
        var target = world.SpawnObject("target", 1, ModuleBatchBTestSupport.At(2));
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "power",
            ("objects", CommandValue.OfLongList(new long[] { caster.Id })),
            ("name", CommandValue.OfString(power.Name)),
            ("target", CommandValue.OfLong(target.Id)))));

        world.Tick();

        Assert.Equal(Fixed64.FromInt(75), target.Health);
        Assert.Contains(world.EventsThisTick, value => value.Kind == "fire" && value.Name == weapon.Name);
    }
}
