using Xunit;

namespace OpenBfme.Sim.Tests;

public sealed class ModuleBatchCTechTests
{
    [Fact]
    public void SpecialPowerModuleHonorsStartsPausedAndTimedModelCondition()
    {
        var power = new SpecialPowerTemplate("Roar", "SPECIAL_POWER_ROAR", 330, Array.Empty<string>(), false);
        var controller = new ModuleSpec(GenericSpecialPowerModule.TypeName,
            new Dictionary<string, long> { ["StartsPaused"] = 0, ["UpdateModuleStartsAttack"] = 1,
                ["SetModelConditionTime"] = 66 },
            new Dictionary<string, string> { ["SpecialPowerTemplate"] = power.Name,
                ["SetModelCondition"] = "USER_1" });
        var world = PowerWorld(controller, power);
        var caster = world.SpawnObject("caster", 0, FixedVector2.Zero);

        SubmitPower(world, caster.Id, power.Name);
        world.Tick();

        Assert.True(caster.HasConditionToken("USER_1"));
        Assert.Equal(1, caster.FindModule<GenericSpecialPowerModule>()!.ConditionTicksRemaining);
        world.Advance(1);
        Assert.False(caster.HasConditionToken("USER_1"));
    }

    [Fact]
    public void UnpauseSpecialPowerUpgradeRoutesThroughUpgradeEvaluator()
    {
        var power = new SpecialPowerTemplate("Roar", "SPECIAL_POWER_ROAR", 0, Array.Empty<string>(), false);
        var controller = new ModuleSpec(GenericSpecialPowerModule.TypeName,
            new Dictionary<string, long> { ["StartsPaused"] = 1, ["UpdateModuleStartsAttack"] = 1 },
            new Dictionary<string, string> { ["SpecialPowerTemplate"] = power.Name });
        var unpause = new ModuleSpec(UnpauseSpecialPowerUpgradeModule.TypeName, stringData:
            new Dictionary<string, string> { ["SpecialPowerTemplate"] = power.Name,
                ["TriggeredBy"] = "Upgrade_Roar" });
        var world = PowerWorld(new[] { controller, unpause }, power);
        var caster = world.SpawnObject("caster", 0, FixedVector2.Zero);

        SubmitPower(world, caster.Id, power.Name, tick: 1);
        world.Tick();
        Assert.Contains(world.Diagnostics, diagnostic => diagnostic.Code == "power_paused");

        world.CompleteUpgrade(caster, new UpgradeTemplate("Upgrade_Roar", UpgradeType.Object, 0, 0,
            Array.Empty<string>()));
        SubmitPower(world, caster.Id, power.Name, tick: 2);
        world.Tick();
        Assert.Contains(world.EventsThisTick, value => value.Kind == "ability" && value.Name == power.Name);
    }

    [Fact]
    public void AttributeModifierUpgradeAppliesAuthoredModifierToken()
    {
        var module = new ModuleSpec(AttributeModifierUpgradeModule.TypeName, stringData:
            new Dictionary<string, string> { ["AttributeModifier"] = "FearlessForever",
                ["TriggeredBy"] = "Upgrade_Fearless" });
        var (world, gameObject) = UpgradeWorld(module);

        world.CompleteUpgrade(gameObject, ObjectUpgrade("Upgrade_Fearless"));

        Assert.True(gameObject.HasConditionToken("FearlessForever"));
    }

    [Fact]
    public void StatusBitsUpgradeHonorsOptionalStatusToSetShape()
    {
        var module = new ModuleSpec(StatusBitsUpgradeModule.TypeName, stringData:
            new Dictionary<string, string> { ["StatusToSet"] = "UNSELECTABLE",
                ["TriggeredBy"] = "Upgrade_Status" });
        var (world, gameObject) = UpgradeWorld(module);

        world.CompleteUpgrade(gameObject, ObjectUpgrade("Upgrade_Status"));

        Assert.True(gameObject.HasConditionToken("UNSELECTABLE"));
        Assert.True(gameObject.FindModule<StatusBitsUpgradeModule>()!.Consumed);
    }

    [Fact]
    public void ModelConditionUpgradeAddsRemovesAndExpiresTemporaryFlags()
    {
        var module = new ModuleSpec(ModelConditionUpgradeModule.TypeName,
            new Dictionary<string, long> { ["Permanent"] = 1, ["TempConditionTime"] = 66 },
            new Dictionary<string, string> { ["AddConditionFlags"] = "ONE_RING",
                ["RemoveConditionFlags"] = "OLD", ["AddTempConditionFlag"] = "USER_2",
                ["TriggeredBy"] = "Upgrade_Ring" });
        var (world, gameObject) = UpgradeWorld(module);
        gameObject.SetConditionToken("OLD");

        world.CompleteUpgrade(gameObject, ObjectUpgrade("Upgrade_Ring"));
        Assert.True(gameObject.HasConditionToken("ONE_RING"));
        Assert.False(gameObject.HasConditionToken("OLD"));
        Assert.True(gameObject.HasConditionToken("USER_2"));
        world.Advance(2);
        Assert.False(gameObject.HasConditionToken("USER_2"));
    }

    private static (SimWorld World, GameObject Object) UpgradeWorld(ModuleSpec module)
    {
        var template = new ObjectTemplate("unit", new[] { module }, bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(100)));
        var world = new SimWorld(new SimConfig(new[] { template }, 19, 2), ModuleRegistry.CreateDefault(), 33);
        return (world, world.SpawnObject("unit", 0, FixedVector2.Zero));
    }

    private static UpgradeTemplate ObjectUpgrade(string name) =>
        new(name, UpgradeType.Object, 0, 0, Array.Empty<string>());

    private static SimWorld PowerWorld(ModuleSpec module, SpecialPowerTemplate power) =>
        PowerWorld(new[] { module }, power);

    private static SimWorld PowerWorld(IReadOnlyList<ModuleSpec> modules, SpecialPowerTemplate power)
    {
        var button = new CommandButtonTemplate("PowerButton", "SPECIAL_POWER", "", "", "", power.Name);
        var set = new CommandSetTemplate("PowerSet", new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var template = new ObjectTemplate("caster", modules, bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(100)),
            commandSetName: set.Name);
        return new SimWorld(new SimConfig(new[] { template }, 17, 2,
            tech: new TechCatalog(specialPowers: new[] { power }, commandButtons: new[] { button },
                commandSets: new[] { set })), ModuleRegistry.CreateDefault(), 33);
    }

    private static void SubmitPower(SimWorld world, int casterId, string name, int tick = 1) =>
        Assert.True(world.SubmitCommand(TestWorlds.Command(tick, 0, 0, "power",
            ("objects", CommandValue.OfLongList(new long[] { casterId })),
            ("name", CommandValue.OfString(name)))));
}
