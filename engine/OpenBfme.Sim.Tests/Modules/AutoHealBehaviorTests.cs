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

    [Fact]
    public void ButtonTriggeredSingleBurstHealsAlliedHeroesWhenPowerIsCast()
    {
        var power = new SpecialPowerTemplate("SpecialAbilityElrondElvenGrace", "ELVEN_GRACE",
            1_000, Array.Empty<string>(), false);
        var button = new CommandButtonTemplate("Command_SpecialAbilityElrondElvenGrace", "SPECIAL_POWER",
            "", "", "", power.Name);
        var set = new CommandSetTemplate("ElrondCommandSet",
            new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(specialPowers: new[] { power }, commandButtons: new[] { button },
            commandSets: new[] { set });
        var elrondTemplate = ModuleBatchBTestSupport.Template("ElvenElrond", new[]
        {
            ModuleBatchBTestSupport.Spec(AutoHealBehaviorModule.TypeName,
                new Dictionary<string, long>
                {
                    ["ButtonTriggered"] = 1,
                    ["HealingAmount"] = 600,
                    ["HealingDelay"] = 200,
                    ["Radius"] = 200,
                    ["SingleBurst"] = 1,
                    ["StartsActive"] = 0,
                },
                new Dictionary<string, string> { ["KindOf"] = "HERO" }),
            ModuleBatchBTestSupport.Spec(GenericSpecialPowerModule.TypeName,
                strings: new Dictionary<string, string> { ["SpecialPowerTemplate"] = power.Name }),
        }, kindOf: new[] { "HERO" }, commandSet: set.Name);
        var allyTemplate = ModuleBatchBTestSupport.Template("ally-hero", Array.Empty<ModuleSpec>(),
            kindOf: new[] { "HERO" });
        var world = ModuleBatchBTestSupport.World(new[] { elrondTemplate, allyTemplate }, tech: tech);
        var elrond = world.SpawnObject("ElvenElrond", 0, ModuleBatchBTestSupport.At(0));
        var ally = world.SpawnObject("ally-hero", 0, ModuleBatchBTestSupport.At(2));
        world.DealDamage(ally, 50);
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "power",
            ("objects", CommandValue.OfLongList(new long[] { elrond.Id })),
            ("name", CommandValue.OfString(power.Name)))));

        world.Tick();

        Assert.Equal(ally.MaxHealth, ally.Health);
    }
}
