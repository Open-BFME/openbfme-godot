using Xunit;

namespace OpenBfme.Sim.Tests;

public sealed class TechSystemTests
{
    [Fact]
    public void PlayerUpgradeChargesCompletesAndSwitchesTwoOwnedWeaponSets()
    {
        var upgrade = new UpgradeTemplate("ForgedBlades", UpgradeType.Player, 100, 33, Array.Empty<string>());
        var button = Button("BuyBlades", "PLAYER_UPGRADE", upgrade: upgrade.Name);
        var tech = Catalog(new[] { upgrade }, buttons: new[] { button }, sets: new[] { Set("FortressSet", button) });
        var upgradeSpec = Trigger(WeaponSetUpgradeModule.TypeName, "ForgedBlades",
            strings: new Dictionary<string, string> { ["WeaponSetFlags"] = "UPGRADED" });
        var soldier = new ObjectTemplate(
            "soldier",
            new[] { new ModuleSpec(DestroyDieModule.TypeName), upgradeSpec },
            new[] { WeaponSet("Weak"), WeaponSet("Strong", "UPGRADED") },
            bodyHealth: Body(100));
        var target = new ObjectTemplate(
            "target",
            new[] { new ModuleSpec(DestroyDieModule.TypeName) },
            armorSets: new[] { new ArmorSet(null, "PierceProof") },
            bodyHealth: Body(100));
        var world = World(
            new[] { Producer("fortress", "FortressSet"), soldier, target },
            tech,
            weapons: new[] { Weapon("Weak", 5, DamageType.PIERCE), Weapon("Strong", 20, DamageType.SLASH) },
            armors: new[] { new ArmorTemplate("PierceProof", new Dictionary<DamageType, Fixed64>
            {
                [DamageType.DEFAULT] = Fixed64.One,
                [DamageType.PIERCE] = Fixed64.Zero,
                [DamageType.SLASH] = Fixed64.One,
            }) },
            resources: 500);
        var fortress = world.SpawnObject("fortress", 0, At(20));
        world.SpawnObject("soldier", 0, At(0));
        world.SpawnObject("soldier", 0, At(0));
        var victim = world.SpawnObject("target", 1, At(1));
        Submit(world, Upgrade(1, 0, fortress.Id, upgrade.Name));

        world.Tick();
        Assert.Equal(400, world.TeamResources(0));
        Assert.Equal(Fixed64.FromInt(100), victim.Health);
        world.Tick();
        Assert.True(world.TeamHasUpgrade(0, upgrade.Name));
        Assert.Equal(Fixed64.FromInt(100), victim.Health);
        world.Tick();

        Assert.Equal(Fixed64.FromInt(60), victim.Health);
        Assert.Contains(world.EventsThisTick, value => value.Kind == "fire" && value.Name == "Strong");
    }

    [Fact]
    public void ObjectUpgradeFiresOnlyOnThePurchaser()
    {
        var upgrade = new UpgradeTemplate("Veterancy", UpgradeType.Object, 25, 33, Array.Empty<string>());
        var button = Button("BuyVeterancy", "OBJECT_UPGRADE", upgrade: upgrade.Name);
        var module = Trigger(LevelUpUpgradeModule.TypeName, upgrade.Name,
            data: new Dictionary<string, long> { ["LevelsToGain"] = 1 });
        var objectTemplate = new ObjectTemplate("barracks", new[]
        {
            new ModuleSpec(ProductionModule.TypeName),
            new ModuleSpec(ExperienceLevelModule.TypeName, new Dictionary<string, long> { ["LevelCap"] = 5 }),
            module,
        }, commandSetName: "BarracksSet");
        var world = World(new[] { objectTemplate },
            Catalog(new[] { upgrade }, buttons: new[] { button }, sets: new[] { Set("BarracksSet", button) }),
            resources: 100);
        var first = world.SpawnObject("barracks", 0, At(0));
        var second = world.SpawnObject("barracks", 0, At(10));
        Submit(world, Upgrade(1, 0, first.Id, upgrade.Name));

        world.Advance(2);

        Assert.Contains(upgrade.Name, first.OwnedUpgrades);
        Assert.DoesNotContain(upgrade.Name, second.OwnedUpgrades);
        Assert.Equal(2, first.FindModule<ExperienceLevelModule>()!.Level);
        Assert.Equal(1, second.FindModule<ExperienceLevelModule>()!.Level);
    }

    [Fact]
    public void RequiresAllTriggersWaitsForBothUpgrades()
    {
        var firstUpgrade = new UpgradeTemplate("First", UpgradeType.Object, 0, 33, Array.Empty<string>());
        var secondUpgrade = new UpgradeTemplate("Second", UpgradeType.Object, 0, 33, Array.Empty<string>());
        var firstButton = Button("FirstButton", "OBJECT_UPGRADE", upgrade: firstUpgrade.Name);
        var secondButton = Button("SecondButton", "OBJECT_UPGRADE", upgrade: secondUpgrade.Name);
        var trigger = Trigger(LevelUpUpgradeModule.TypeName, "First Second",
            data: new Dictionary<string, long> { ["RequiresAllTriggers"] = 1, ["LevelsToGain"] = 1 });
        var template = new ObjectTemplate("forge", new[]
        {
            new ModuleSpec(ProductionModule.TypeName),
            new ModuleSpec(ExperienceLevelModule.TypeName),
            trigger,
        }, commandSetName: "ForgeSet");
        var world = World(new[] { template }, Catalog(
            new[] { firstUpgrade, secondUpgrade }, buttons: new[] { firstButton, secondButton },
            sets: new[] { Set("ForgeSet", firstButton, secondButton) }));
        var forge = world.SpawnObject("forge", 0, At(0));
        Submit(world, Upgrade(1, 0, forge.Id, firstUpgrade.Name));
        Submit(world, Upgrade(1, 1, forge.Id, secondUpgrade.Name));

        world.Advance(2);
        Assert.Equal(1, forge.FindModule<ExperienceLevelModule>()!.Level);
        world.Advance(2);
        Assert.Equal(2, forge.FindModule<ExperienceLevelModule>()!.Level);
    }

    [Fact]
    public void ConflictsWithConsumesAndSuppressesTheModule()
    {
        var conflict = new UpgradeTemplate("Conflict", UpgradeType.Object, 0, 33, Array.Empty<string>());
        var triggerUpgrade = new UpgradeTemplate("Trigger", UpgradeType.Object, 0, 33, Array.Empty<string>());
        var conflictButton = Button("ConflictButton", "OBJECT_UPGRADE", upgrade: conflict.Name);
        var triggerButton = Button("TriggerButton", "OBJECT_UPGRADE", upgrade: triggerUpgrade.Name);
        var module = Trigger(LevelUpUpgradeModule.TypeName, triggerUpgrade.Name,
            data: new Dictionary<string, long> { ["LevelsToGain"] = 1 },
            strings: new Dictionary<string, string> { ["ConflictsWith"] = conflict.Name });
        var template = new ObjectTemplate("forge", new[]
        {
            new ModuleSpec(ProductionModule.TypeName), new ModuleSpec(ExperienceLevelModule.TypeName), module,
        }, commandSetName: "ForgeSet");
        var world = World(new[] { template }, Catalog(new[] { conflict, triggerUpgrade },
            buttons: new[] { conflictButton, triggerButton },
            sets: new[] { Set("ForgeSet", conflictButton, triggerButton) }));
        var forge = world.SpawnObject("forge", 0, At(0));
        Submit(world, Upgrade(1, 0, forge.Id, conflict.Name));
        Submit(world, Upgrade(1, 1, forge.Id, triggerUpgrade.Name));

        world.Advance(5);

        Assert.Equal(1, forge.FindModule<ExperienceLevelModule>()!.Level);
        Assert.True(forge.FindModule<LevelUpUpgradeModule>()!.Consumed);
    }

    [Fact]
    public void SciencePurchaseChecksPrerequisitesSpendsPointsAndUnlocksPower()
    {
        var root = new ScienceTemplate("Root", Array.Empty<string>(), 1, true);
        var advanced = new ScienceTemplate("Advanced", new[] { root.Name }, 3, true);
        var power = new SpecialPowerTemplate("Beacon", "SPECIAL_POWER_BEACON", 0, new[] { advanced.Name }, false);
        var rootButton = Button("RootButton", "PURCHASE_SCIENCE", science: root.Name);
        var advancedButton = Button("AdvancedButton", "PURCHASE_SCIENCE", science: advanced.Name);
        var powerButton = Button("PowerButton", "SPECIAL_POWER", power: power.Name);
        var tech = Catalog(sciences: new[] { root, advanced }, powers: new[] { power },
            buttons: new[] { rootButton, advancedButton, powerButton },
            sets: new[] { Set("Spellbook", rootButton, advancedButton, powerButton) });
        var casterTemplate = new ObjectTemplate("caster",
            new[] { new ModuleSpec(GenericSpecialPowerModule.TypeName) }, commandSetName: "Spellbook");
        var world = World(new[] { casterTemplate }, tech, powerPoints: 5);
        var caster = world.SpawnObject("caster", 0, At(0));
        Submit(world, Power(1, 0, advanced.Name, caster.Id));
        Submit(world, Power(2, 1, root.Name, caster.Id));
        Submit(world, Power(3, 2, advanced.Name, caster.Id));
        Submit(world, Power(4, 3, power.Name, caster.Id));

        world.Advance(3);
        Assert.Contains(world.Diagnostics, value => value.Code == "science_prerequisite_missing");
        Assert.True(world.TeamHasScience(0, advanced.Name));
        Assert.Equal(1, world.TeamPowerPoints(0));
        world.Tick();
        Assert.Contains(world.EventsThisTick, value => value.Kind == "ability" && value.Name == power.Name);
    }

    [Fact]
    public void SixtySecondReloadAtThirtyThreeMillisecondsReopensOnRoundedTick()
    {
        var science = new ScienceTemplate("Root", Array.Empty<string>(), 0, true);
        var power = new SpecialPowerTemplate("Storm", "SPECIAL_POWER_STORM", 60_000, new[] { science.Name }, true);
        var scienceButton = Button("ScienceButton", "PURCHASE_SCIENCE", science: science.Name);
        var powerButton = Button("PowerButton", "SPECIAL_POWER", power: power.Name);
        var tech = Catalog(sciences: new[] { science }, powers: new[] { power },
            buttons: new[] { scienceButton, powerButton }, sets: new[] { Set("Spellbook", scienceButton, powerButton) });
        var template = new ObjectTemplate("caster",
            new[] { new ModuleSpec(GenericSpecialPowerModule.TypeName) }, commandSetName: "Spellbook");
        var world = World(new[] { template }, tech);
        var caster = world.SpawnObject("caster", 0, At(0));
        Submit(world, Power(1, 0, science.Name, caster.Id));
        Submit(world, Power(2, 1, power.Name, caster.Id));
        var reloadTicks = EconomyTemplate.MillisecondsToTicks(60_000, 33);
        Submit(world, Power(2 + reloadTicks - 1, 2, power.Name, caster.Id));
        Submit(world, Power(2 + reloadTicks, 3, power.Name, caster.Id));

        world.Advance(2);
        Assert.Equal(2 + reloadTicks, world.PowerReadyTick(0, power.Name));
        world.Advance(reloadTicks - 1);
        Assert.Contains(world.Diagnostics, value => value.Tick == 2 + reloadTicks - 1 && value.Code == "power_reloading");
        world.Tick();
        Assert.Contains(world.EventsThisTick, value => value.Kind == "ability" && value.Name == power.Name);
    }

    [Fact]
    public void OclSpecialPowerSpawnsAuthoredTemplatesForCasterAtTargetPosition()
    {
        var power = new SpecialPowerTemplate("Summon", "SPECIAL_POWER_SUMMON", 0, Array.Empty<string>(), false);
        var button = Button("SummonButton", "SPECIAL_POWER", power: power.Name);
        var module = new ModuleSpec(OCLSpecialPowerModule.TypeName, stringData: new Dictionary<string, string>
        {
            ["SpecialPowerTemplate"] = power.Name,
            ["ObjectNames"] = "wolf raven",
        });
        var caster = new ObjectTemplate("caster", new[] { module }, commandSetName: "Spellbook");
        var wolf = new ObjectTemplate("wolf", Array.Empty<ModuleSpec>());
        var raven = new ObjectTemplate("raven", Array.Empty<ModuleSpec>());
        var world = World(new[] { caster, wolf, raven }, Catalog(powers: new[] { power },
            buttons: new[] { button }, sets: new[] { Set("Spellbook", button) }));
        var casterObject = world.SpawnObject("caster", 0, At(0));
        Submit(world, Power(1, 0, power.Name, casterObject.Id, At(12, -7)));

        world.Tick();

        Assert.Equal(new[] { "raven", "wolf" }, world.Objects.Values
            .Where(value => value.Id != casterObject.Id).Select(value => value.TemplateName)
            .OrderBy(value => value, StringComparer.Ordinal));
        Assert.All(world.Objects.Values.Where(value => value.Id != casterObject.Id), value =>
        {
            Assert.Equal(0, value.Team);
            Assert.Equal(At(12, -7), value.Position);
        });
    }

    [Fact]
    public void TrainOutsideCurrentCommandSetIsRefused()
    {
        var allowed = Button("AllowedButton", "UNIT_BUILD", objectName: "allowed");
        var tech = Catalog(buttons: new[] { allowed }, sets: new[] { Set("ProducerSet", allowed) });
        var world = World(new[]
        {
            Producer("producer", "ProducerSet"),
            new ObjectTemplate("allowed", Array.Empty<ModuleSpec>(), economy: new EconomyTemplate(buildTimeMilliseconds: 33)),
            new ObjectTemplate("forbidden", Array.Empty<ModuleSpec>(), economy: new EconomyTemplate(buildTimeMilliseconds: 33)),
        }, tech, resources: 100);
        var producer = world.SpawnObject("producer", 0, At(0));
        Submit(world, Train(1, 0, producer.Id, "forbidden"));

        world.Tick();

        Assert.Equal(0, producer.FindModule<ProductionModule>()!.QueueLength);
        Assert.Contains(world.Diagnostics, value => value.Code == "not_in_command_set");
    }

    [Fact]
    public void MixedTechAndEconomyCommandLogIsTwinRunDeterministicForTwelveHundredTicks()
    {
        static SimWorld Build()
        {
            var upgrade = new UpgradeTemplate("Upgrade", UpgradeType.Player, 10, 66, Array.Empty<string>());
            var science = new ScienceTemplate("Science", Array.Empty<string>(), 1, true);
            var power = new SpecialPowerTemplate("Power", "SPECIAL_POWER_TEST", 330, new[] { science.Name }, false);
            var buttons = new[]
            {
                Button("Train", "UNIT_BUILD", objectName: "unit"),
                Button("Build", "DOZER_CONSTRUCT", objectName: "farm"),
                Button("Upgrade", "PLAYER_UPGRADE", upgrade: upgrade.Name),
                Button("Science", "PURCHASE_SCIENCE", science: science.Name),
                Button("Power", "SPECIAL_POWER", power: power.Name),
            };
            var tech = Catalog(new[] { upgrade }, new[] { science }, new[] { power }, buttons, new[] { Set("All", buttons) });
            var baseObject = new ObjectTemplate("base", new[]
            {
                new ModuleSpec(ProductionModule.TypeName), new ModuleSpec(GenericSpecialPowerModule.TypeName),
            }, commandSetName: "All");
            var farm = new ObjectTemplate("farm", new[]
            {
                new ModuleSpec(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 100 }),
                new ModuleSpec(GettingBuiltModule.TypeName),
            }, bodyHealth: Body(100), economy: new EconomyTemplate(20, 66, buildKind: "resource"));
            var unit = new ObjectTemplate("unit", Array.Empty<ModuleSpec>(), economy: new EconomyTemplate(5, 66));
            var world = World(new[] { baseObject, farm, unit }, tech, resources: 200, powerPoints: 5);
            var producer = world.SpawnObject("base", 0, At(0));
            world.SetBuildPlots(new[] { new BuildPlot(producer.Id, 0, At(10), new[] { "resource" }) });
            Submit(world, BuildCommand(1, 0, producer.Id, "farm", 0));
            Submit(world, Train(2, 1, producer.Id, "unit"));
            Submit(world, Upgrade(3, 2, producer.Id, upgrade.Name));
            Submit(world, Power(4, 3, science.Name, producer.Id));
            Submit(world, Power(5, 4, power.Name, producer.Id, At(20)));
            return world;
        }

        var first = Build();
        var second = Build();
        for (var tick = 1; tick <= 1_200; tick++)
        {
            first.Tick();
            second.Tick();
            Assert.Equal(first.StateHash(), second.StateHash());
        }
    }

    [Fact]
    public void GoldenBundleRunsUpgradeScienceAndPowerPathsEndToEnd()
    {
        var document = BundleDocument.Load(MatchLaunchTests.RepoPath("contracts", "fixtures", "bundle-v1.json"));
        var launch = MatchLaunch.Load(MatchLaunchTests.RepoPath("contracts", "fixtures", "match-launch-v1.json"));
        var world = SimWorld.FromBundle(launch, document);
        world.SetPlayerEconomy(0, 0, world.CommandPointsMaximum(0), 10);
        world.GrantScience(0, "SCIENCE_CookRoot");
        world.GrantScience(0, "SCIENCE_CookAlly");
        var producer = world.SpawnObject("CookBase", 0, At(0));
        Submit(world, Upgrade(1, 0, producer.Id, "CookUpgrade"));
        Submit(world, Power(2, 1, "SCIENCE_CookKnowledge"));
        Submit(world, Power(3, 2, "SpecialPowerCookBeacon", producer.Id, At(5)));

        world.Advance(2);
        Assert.True(world.TeamHasScience(0, "SCIENCE_CookKnowledge"));
        world.Tick();
        Assert.Contains(world.EventsThisTick, value => value.Kind == "ability" && value.Name == "SpecialPowerCookBeacon");
        world.Advance(400);

        Assert.Contains("CookUpgrade", producer.OwnedUpgrades);
        Assert.Contains(world.BundleLoadReport!.AbsentTables, value => value == "object_creation_lists");
    }

    private static SimWorld World(
        IEnumerable<ObjectTemplate> templates,
        TechCatalog tech,
        IEnumerable<WeaponTemplate>? weapons = null,
        IEnumerable<ArmorTemplate>? armors = null,
        long resources = 0,
        long powerPoints = 0)
    {
        var world = new SimWorld(new SimConfig(templates, 77, 2,
            weaponTemplates: weapons, armorTemplates: armors, maxCommandPoints: 1_000, tech: tech),
            ModuleRegistry.CreateDefault(), 33);
        world.AddTeamResources(0, resources);
        world.SetPlayerEconomy(0, 0, 1_000, powerPoints);
        return world;
    }

    private static ObjectTemplate Producer(string name, string commandSet) => new(
        name, new[] { new ModuleSpec(ProductionModule.TypeName) }, commandSetName: commandSet);

    private static TechCatalog Catalog(
        IEnumerable<UpgradeTemplate>? upgrades = null,
        IEnumerable<ScienceTemplate>? sciences = null,
        IEnumerable<SpecialPowerTemplate>? powers = null,
        IEnumerable<CommandButtonTemplate>? buttons = null,
        IEnumerable<CommandSetTemplate>? sets = null) =>
        new(upgrades, sciences, powers, buttons, sets);

    private static CommandButtonTemplate Button(
        string name,
        string command,
        string objectName = "",
        string upgrade = "",
        string science = "",
        string power = "") => new(name, command, objectName, upgrade, science, power);

    private static CommandSetTemplate Set(string name, params CommandButtonTemplate[] buttons) =>
        new(name, buttons.Select((button, index) => new CommandSetEntryTemplate(index + 1, button.Name, button)).ToArray());

    private static ModuleSpec Trigger(
        string type,
        string triggeredBy,
        IReadOnlyDictionary<string, long>? data = null,
        IReadOnlyDictionary<string, string>? strings = null)
    {
        var stringData = new Dictionary<string, string>(strings ?? new Dictionary<string, string>())
        {
            ["TriggeredBy"] = triggeredBy,
        };
        return new ModuleSpec(type, data, stringData);
    }

    private static WeaponTemplate Weapon(string name, int damage, DamageType damageType) => new(
        name, Fixed64.FromInt(2), Fixed64.Zero, 1, 0, PreAttackType.PER_SHOT, 0, 0, 0,
        new[] { new DamageNugget(Fixed64.FromInt(damage), Fixed64.Zero, 0, damageType, "", "NORMAL") });

    private static WeaponSet WeaponSet(string name, params string[] conditions) =>
        new(conditions, new Dictionary<WeaponSlot, string> { [WeaponSlot.PRIMARY] = name });

    private static BodyHealthTemplate Body(int health) => new(Fixed64.FromInt(health));
    private static FixedVector2 At(int x, int y = 0) => new(Fixed64.FromInt(x), Fixed64.FromInt(y));
    private static void Submit(SimWorld world, SimCommand command) => Assert.True(world.SubmitCommand(command));

    private static SimCommand Upgrade(int tick, int seq, int producer, string name) =>
        TestWorlds.Command(tick, 0, seq, "upgrade",
            ("objects", CommandValue.OfLongList(new long[] { producer })), ("name", CommandValue.OfString(name)));

    private static SimCommand Power(int tick, int seq, string name, int? caster = null, FixedVector2? position = null)
    {
        var args = new List<(string, CommandValue)> { ("name", CommandValue.OfString(name)) };
        if (caster.HasValue) args.Add(("objects", CommandValue.OfLongList(new long[] { caster.Value })));
        if (position.HasValue)
        {
            args.Add(("x", CommandValue.OfFixed(position.Value.X)));
            args.Add(("y", CommandValue.OfFixed(position.Value.Y)));
        }
        return TestWorlds.Command(tick, 0, seq, "power", args.ToArray());
    }

    private static SimCommand Train(int tick, int seq, int producer, string template) =>
        TestWorlds.Command(tick, 0, seq, "train",
            ("objects", CommandValue.OfLongList(new long[] { producer })),
            ("template", CommandValue.OfString(template)), ("count", CommandValue.OfLong(1)));

    private static SimCommand BuildCommand(int tick, int seq, int producer, string template, int index) =>
        TestWorlds.Command(tick, 0, seq, "build",
            ("objects", CommandValue.OfLongList(new long[] { producer })),
            ("template", CommandValue.OfString(template)), ("index", CommandValue.OfLong(index)));
}
