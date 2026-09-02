using Xunit;

namespace OpenBfme.Sim.Tests;

public sealed class ModuleBatchCPowerRespawnTests
{
    [Fact]
    public void AutoAbilityBehaviorSubmitsOrdinaryPowerCommandAgainstNearestEnemy()
    {
        var power = new SpecialPowerTemplate("AutoPower", "SPECIAL_POWER_AUTO", 0, Array.Empty<string>(), false);
        var button = Button("AutoButton", power.Name);
        var set = Set("AutoSet", button);
        var auto = new ModuleSpec(AutoAbilityBehaviorModule.TypeName,
            data: new Dictionary<string, long> { ["MaxScanRangeRaw"] = Fixed64.FromInt(50).Raw },
            stringData: new Dictionary<string, string> { ["Query"] = "ANY +INFANTRY ENEMIES",
                ["SpecialAbility"] = button.Name });
        var controller = new ModuleSpec(GenericSpecialPowerModule.TypeName, stringData:
            new Dictionary<string, string> { ["SpecialPowerTemplate"] = power.Name });
        var casterTemplate = new ObjectTemplate("caster", new[] { auto, controller },
            bodyHealth: Body(100), commandSetName: set.Name);
        var targetTemplate = new ObjectTemplate("target", Array.Empty<ModuleSpec>(), bodyHealth: Body(100));
        var world = World(new[] { casterTemplate, targetTemplate }, power, button, set);
        world.SpawnObject("caster", 0, At(0));
        world.SpawnObject("target", 1, At(10));

        world.Advance(2);

        Assert.Contains(world.EventsThisTick, value => value.Kind == "ability" && value.Name == power.Name);
    }

    [Fact]
    public void AutoAbilityBehaviorHonorsRichRangeStartAndActivationShape()
    {
        var power = new SpecialPowerTemplate("AutoPower", "SPECIAL_POWER_AUTO", 0, Array.Empty<string>(), false);
        var button = Button("AutoButton", power.Name);
        var set = Set("AutoSet", button);
        // Exact one-row corpus shape.
        var auto = new ModuleSpec(AutoAbilityBehaviorModule.TypeName,
            data: new Dictionary<string, long>
            {
                ["AdjustAttackMeleePosition"] = 0,
                ["BaseMaxRangeFromStartPosRaw"] = Fixed64.FromInt(20).Raw,
                ["IdleTimeSecondsRaw"] = Fixed64.FromFraction(1, 33).Raw,
                ["MaxScanRangeRaw"] = Fixed64.FromInt(50).Raw,
                ["MinScanRangeRaw"] = Fixed64.FromInt(5).Raw,
                ["StartsActive"] = 0,
            },
            stringData: new Dictionary<string, string>
            {
                ["Query"] = "ANY +INFANTRY ENEMIES",
                ["SpecialAbility"] = button.Name,
            });
        var controller = new ModuleSpec(GenericSpecialPowerModule.TypeName, stringData:
            new Dictionary<string, string> { ["SpecialPowerTemplate"] = power.Name });
        var casterTemplate = new ObjectTemplate("caster", new[] { auto, controller },
            bodyHealth: Body(100), commandSetName: set.Name);
        var targetTemplate = new ObjectTemplate("target", Array.Empty<ModuleSpec>(), bodyHealth: Body(100));
        var world = World(new[] { casterTemplate, targetTemplate }, power, button, set);
        var caster = world.SpawnObject("caster", 0, At(0));
        world.SpawnObject("target", 1, At(2));
        world.SpawnObject("target", 1, At(10));

        world.Advance(2);
        Assert.DoesNotContain(world.EventsThisTick, value => value.Kind == "ability");
        caster.FindModule<AutoAbilityBehaviorModule>()!.SetActive(true);
        world.Advance(2);

        Assert.Contains(world.EventsThisTick, value => value.Kind == "ability" && value.Name == power.Name);
    }

    [Fact]
    public void AutoAbilityBehaviorHonorsForbiddenStatusCorpusShape()
    {
        var power = new SpecialPowerTemplate("AutoPower", "SPECIAL_POWER_AUTO", 0, Array.Empty<string>(), false);
        var button = Button("AutoButton", power.Name);
        var set = Set("AutoSet", button);
        // Exact seven-row corpus shape.
        var auto = new ModuleSpec(AutoAbilityBehaviorModule.TypeName,
            data: new Dictionary<string, long> { ["AllowSelf"] = 1 },
            stringData: new Dictionary<string, string>
            {
                ["ForbiddenStatus"] = "BUSY",
                ["Query"] = "ALLIES",
                ["SpecialAbility"] = button.Name,
            });
        var controller = new ModuleSpec(GenericSpecialPowerModule.TypeName, stringData:
            new Dictionary<string, string> { ["SpecialPowerTemplate"] = power.Name });
        var template = new ObjectTemplate("caster", new[] { auto, controller },
            bodyHealth: Body(100), commandSetName: set.Name);
        var world = World(new[] { template }, power, button, set);
        var caster = world.SpawnObject("caster", 0, At(0));
        caster.SetConditionToken("BUSY");

        world.Advance(2);
        Assert.DoesNotContain(world.EventsThisTick, value => value.Kind == "ability");
        caster.SetConditionToken("BUSY", false);
        world.Advance(2);

        Assert.Contains(world.EventsThisTick, value => value.Kind == "ability" && value.Name == power.Name);
    }

    [Fact]
    public void OclSpecialPowerHonorsUseOwnerObjectAndEmitsNamedOclEvent()
    {
        var power = new SpecialPowerTemplate("Summon", "SPECIAL_POWER_SUMMON", 0, Array.Empty<string>(), false);
        var button = Button("SummonButton", power.Name);
        var set = Set("SummonSet", button);
        var module = new ModuleSpec(OCLSpecialPowerModule.TypeName, stringData:
            new Dictionary<string, string> { ["CreateLocation"] = "USE_OWNER_OBJECT",
                ["OCL"] = "summon", ["SpecialPowerTemplate"] = power.Name });
        var casterTemplate = new ObjectTemplate("caster", new[] { module }, commandSetName: set.Name);
        var summon = new ObjectTemplate("summon", Array.Empty<ModuleSpec>());
        var world = World(new[] { casterTemplate, summon }, power, button, set);
        var caster = world.SpawnObject("caster", 0, At(4));

        SubmitPower(world, caster.Id, power.Name, At(20));
        world.Tick();

        Assert.Contains(world.EventsThisTick, value => value.Kind == "ability" && value.Name == "summon");
        Assert.Equal(At(4), world.Objects.Values.Single(value => value.TemplateName == "summon").Position);
    }

    [Fact]
    public void RespawnUpdateUsesTrainCommandLevelTableAndRetainsLevel()
    {
        var respawn = new ModuleSpec(RespawnUpdateModule.TypeName,
            data: new Dictionary<string, long> { ["DeathAnimationTime"] = 33,
                ["RespawnAnimationTime"] = 33 },
            stringData: new Dictionary<string, string>
            {
                ["AutoRespawnAtObjectFilter"] = "NONE +CASTLE_KEEP",
                ["ButtonImage"] = "SyntheticButton",
                ["DeathAnim"] = "DYING",
                ["DeathFX"] = "SyntheticDeathFx",
                ["InitialSpawnFX"] = "SyntheticInitialFx",
                ["RespawnAnim"] = "RESPAWN",
                ["RespawnRules"] = "AutoSpawn:No Cost:100 Time:66 Health:100%",
                ["RespawnEntry"] = "Level:2 Cost:200 Time:66\nLevel:3 Cost:300 Time:66",
                ["RespawnFX"] = "SyntheticRespawnFx",
            });
        var hero = new ObjectTemplate("hero", new ModuleSpec[]
        {
            new(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 100 }),
            new(ExperienceLevelModule.TypeName), respawn,
        }, bodyHealth: Body(100), economy: new EconomyTemplate(900, 99));
        var button = new CommandButtonTemplate("ReviveHero", "UNIT_BUILD", hero.Name, "", "", "");
        var set = Set("FortressSet", button);
        var fortress = new ObjectTemplate("fortress", new[] { new ModuleSpec(ProductionModule.TypeName) },
            commandSetName: set.Name, kindOf: new[] { "STRUCTURE", "CASTLE_KEEP" });
        var world = new SimWorld(new SimConfig(new[] { fortress, hero }, 31, 2, tech:
            new TechCatalog(commandButtons: new[] { button }, commandSets: new[] { set })),
            ModuleRegistry.CreateDefault(), 33);
        world.AddTeamResources(0, 1_000);
        var producer = world.SpawnObject("fortress", 0, At(12));
        var deadHero = world.SpawnObject("hero", 0, At(0));
        deadHero.FindModule<ExperienceLevelModule>()!.GrantLevels(2);
        world.DealDamage(deadHero, 100);
        world.Tick();
        Assert.True(deadHero.FindModule<RespawnUpdateModule>()!.IsWaiting);

        Assert.True(world.SubmitCommand(TestWorlds.Command(2, 0, 0, "train",
            ("objects", CommandValue.OfLongList(new long[] { producer.Id })),
            ("template", CommandValue.OfString("hero")), ("count", CommandValue.OfLong(1)))));
        world.Advance(2);

        var revived = Assert.Single(world.Objects.Values, value => value.TemplateName == "hero");
        Assert.Equal(3, revived.FindModule<ExperienceLevelModule>()!.Level);
        Assert.Equal(At(12), revived.Position);
        Assert.Equal(700, world.TeamResources(0));
    }

    [Fact]
    public void InvisibilityUpdateHonorsNuggetDetectionAndForbiddenConditions()
    {
        var nugget = new BundleBlock("InvisibilityNugget", "",
            new Dictionary<string, BundleValue>
            {
                ["DetectionRange"] = BundleValue.Whole(5),
                ["ForbiddenConditions"] = BundleValue.Text("MOVING FIRING_ANY"),
                ["InvisibilityType"] = BundleValue.Text("CAMOUFLAGE"),
            }, Array.Empty<BundleBlock>());
        var module = new ModuleSpec(InvisibilityUpdateModule.TypeName,
            data: new Dictionary<string, long> { ["StartsActive"] = 1, ["UpdatePeriod"] = 33 },
            blocks: new[] { nugget });
        var hiddenTemplate = new ObjectTemplate("hidden", new[] { module }, bodyHealth: Body(100));
        var observerTemplate = new ObjectTemplate("observer", Array.Empty<ModuleSpec>(), bodyHealth: Body(100));
        var world = new SimWorld(new SimConfig(new[] { hiddenTemplate, observerTemplate }, 7, 2),
            ModuleRegistry.CreateDefault(), 33);
        var hidden = world.SpawnObject("hidden", 0, At(0));
        world.SpawnObject("observer", 1, At(20));

        world.Tick();
        Assert.True(hidden.FindModule<InvisibilityUpdateModule>()!.IsInvisible);
        hidden.SetConditionToken("MOVING");
        world.Tick();
        Assert.False(hidden.FindModule<InvisibilityUpdateModule>()!.IsInvisible);
    }

    [Fact]
    public void InvisibilityUpdateHonorsRequiredUpgradesCorpusShape()
    {
        var nugget = new BundleBlock("InvisibilityNugget", "",
            new Dictionary<string, BundleValue>
            {
                ["DetectionRange"] = BundleValue.Whole(0),
                ["ForbiddenConditions"] = BundleValue.Text("MOVING"),
                ["InvisibilityType"] = BundleValue.Text("CAMOUFLAGE"),
            }, Array.Empty<BundleBlock>());
        // Exact one-row corpus shape.
        var module = new ModuleSpec(InvisibilityUpdateModule.TypeName,
            data: new Dictionary<string, long>
            {
                ["Broadcast"] = 0,
                ["BroadcastRangeRaw"] = Fixed64.FromInt(100).Raw,
                ["StartsActive"] = 1,
                ["UpdatePeriod"] = 33,
            },
            stringData: new Dictionary<string, string>
            {
                ["BroadcastObjectFilter"] = "ANY +INFANTRY",
                ["RequiredUpgrades"] = "Upgrade_Stealth",
            }, blocks: new[] { nugget });
        var template = new ObjectTemplate("hidden", new[] { module }, bodyHealth: Body(100), techEnabled: true);
        var world = new SimWorld(new SimConfig(new[] { template }, 7, 2), ModuleRegistry.CreateDefault(), 33);
        var hidden = world.SpawnObject("hidden", 0, At(0));

        world.Tick();
        Assert.False(hidden.FindModule<InvisibilityUpdateModule>()!.IsInvisible);
        Assert.True(world.GrantObjectUpgrade(hidden, "Upgrade_Stealth"));
        world.Tick();

        Assert.True(hidden.FindModule<InvisibilityUpdateModule>()!.IsInvisible);
    }

    [Fact]
    public void GiveUpgradeUpdateDeliversAuthoredUpgradeThroughEvaluator()
    {
        var power = new SpecialPowerTemplate("Deliver", "SPECIAL_POWER_DELIVER", 0, Array.Empty<string>(), false);
        var button = Button("DeliverButton", power.Name);
        var set = Set("DeliverSet", button);
        var delivery = new ModuleSpec(GiveUpgradeUpdateModule.TypeName,
            data: new Dictionary<string, long> { ["StartAbilityRangeRaw"] = Fixed64.FromInt(30).Raw,
                ["ApproachRequiresLOS"] = 1, ["FadeOutSpeedRaw"] = Fixed64.FromInt(1).Raw,
                ["PackTime"] = 100, ["PersistentPrepTime"] = 0, ["PreparationTime"] = 0,
                ["UnpackTime"] = 0 },
            stringData: new Dictionary<string, string> { ["DeliverUpgrade"] = "Upgrade_Gift",
                ["SpawnOutFX"] = "SyntheticSpawnOut", ["SpecialPowerTemplate"] = power.Name });
        var receiverUpgrade = new ModuleSpec(ModelConditionUpgradeModule.TypeName,
            stringData: new Dictionary<string, string> { ["AddConditionFlags"] = "GIFTED",
                ["TriggeredBy"] = "Upgrade_Gift" });
        var courier = new ObjectTemplate("courier", new[] { delivery }, commandSetName: set.Name);
        var receiver = new ObjectTemplate("receiver", new[] { receiverUpgrade }, bodyHealth: Body(100));
        var world = World(new[] { courier, receiver }, power, button, set);
        var caster = world.SpawnObject("courier", 0, At(0));
        var target = world.SpawnObject("receiver", 0, At(10));

        SubmitPower(world, caster.Id, power.Name, target.Id);
        world.Tick();

        Assert.Contains("Upgrade_Gift", target.OwnedUpgrades);
        Assert.True(target.HasConditionToken("GIFTED"));
        Assert.DoesNotContain(caster.Id, world.Objects.Keys);
    }

    private static SimWorld World(
        IEnumerable<ObjectTemplate> templates,
        SpecialPowerTemplate power,
        CommandButtonTemplate button,
        CommandSetTemplate set) => new(new SimConfig(templates, 13, 2,
            tech: new TechCatalog(specialPowers: new[] { power }, commandButtons: new[] { button },
                commandSets: new[] { set })), ModuleRegistry.CreateDefault(), 33);

    private static CommandButtonTemplate Button(string name, string power) =>
        new(name, "SPECIAL_POWER", "", "", "", power);
    private static CommandSetTemplate Set(string name, CommandButtonTemplate button) =>
        new(name, new[] { new CommandSetEntryTemplate(1, button.Name, button) });
    private static BodyHealthTemplate Body(int health) => new(Fixed64.FromInt(health));
    private static FixedVector2 At(int x, int y = 0) => new(Fixed64.FromInt(x), Fixed64.FromInt(y));

    private static void SubmitPower(SimWorld world, int caster, string name, FixedVector2 position) =>
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "power",
            ("objects", CommandValue.OfLongList(new long[] { caster })), ("name", CommandValue.OfString(name)),
            ("x", CommandValue.OfFixed(position.X)), ("y", CommandValue.OfFixed(position.Y)))));

    private static void SubmitPower(SimWorld world, int caster, string name, int target) =>
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "power",
            ("objects", CommandValue.OfLongList(new long[] { caster })), ("name", CommandValue.OfString(name)),
            ("target", CommandValue.OfLong(target)))));
}
