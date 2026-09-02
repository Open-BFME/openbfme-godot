using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class SpecialAbilityUpdateTests
{
    [Fact]
    public void AuthoredTimelineRaisesAbilityAndInvokesNestedEffect()
    {
        var power = new SpecialPowerTemplate("Summon", "POWER_SUMMON", 500, Array.Empty<string>(), false);
        var button = new CommandButtonTemplate("SummonButton", "SPECIAL_POWER", "", "", "", power.Name);
        var set = new CommandSetTemplate("HeroSet", new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(specialPowers: new[] { power }, commandButtons: new[] { button }, commandSets: new[] { set });
        var hero = ModuleBatchBTestSupport.Template("hero", new[]
        {
            ModuleBatchBTestSupport.Spec(SpecialAbilityUpdateModule.TypeName,
                new Dictionary<string, long> { ["UnpackTime"] = 100, ["PreparationTime"] = 100, ["PackTime"] = 100 },
                new Dictionary<string, string> { ["SpecialPowerTemplate"] = power.Name }),
            ModuleBatchBTestSupport.Spec(OCLSpecialPowerModule.TypeName, strings: new Dictionary<string, string>
            {
                ["SpecialPowerTemplate"] = power.Name, ["ObjectNames"] = "wolf",
            }),
        }, commandSet: set.Name);
        var wolf = ModuleBatchBTestSupport.Template("wolf", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { hero, wolf }, tech: tech);
        var caster = world.SpawnObject("hero", 0, ModuleBatchBTestSupport.At(0));
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "power",
            ("objects", CommandValue.OfLongList(new long[] { caster.Id })),
            ("name", CommandValue.OfString(power.Name)))));

        world.Advance(2);

        Assert.Contains(world.EventsThisTick, value => value.Kind == "ability" && value.Name == power.Name);
        Assert.Contains(world.Objects.Values, value => value.TemplateName == "wolf");
    }
}
