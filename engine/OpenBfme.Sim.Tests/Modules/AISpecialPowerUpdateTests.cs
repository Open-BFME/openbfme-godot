using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class AISpecialPowerUpdateTests
{
    [Fact]
    public void AuthoredCommandButtonPlansReadyPowerAgainstNearestEnemy()
    {
        var power = new SpecialPowerTemplate("Roar", "POWER_ROAR", 1_000, Array.Empty<string>(), false);
        var button = new CommandButtonTemplate("Command_Roar", "SPECIAL_POWER", "", "", "", power.Name);
        var set = new CommandSetTemplate("HeroSet", new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(specialPowers: new[] { power }, commandButtons: new[] { button }, commandSets: new[] { set });
        var casterTemplate = ModuleBatchBTestSupport.Template("hero", new[]
        {
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = button.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_ENEMY_TYPE_KILLER",
                }),
        }, commandSet: set.Name);
        var enemyTemplate = ModuleBatchBTestSupport.Template("enemy", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { casterTemplate, enemyTemplate }, tech: tech);
        var caster = world.SpawnObject("hero", 0, ModuleBatchBTestSupport.At(0));
        var enemy = world.SpawnObject("enemy", 1, ModuleBatchBTestSupport.At(5));
        var state = AiPlayerState.Create(0, new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        var commands = new List<SimCommand>();

        Assert.True(caster.FindModule<AISpecialPowerUpdateModule>()!.TryPlan(world, caster, state, 1, commands));
        var command = Assert.Single(commands);
        Assert.Equal("power", command.Type);
        Assert.Equal(power.Name, command.GetString("name"));
        Assert.Equal(enemy.Id, command.GetLong("target"));
    }

    [Fact]
    public void AuthoredSpellBookButtonUsesTheNormalPowerAuthorizationPath()
    {
        var power = TechCatalog.ParseSpecialPower(new BundleNamedRow("SpellBookWarChant",
            new Dictionary<string, BundleValue>
            {
                ["Enum"] = BundleValue.Text("SPELL_BOOK"),
                ["ReloadTime"] = BundleValue.Whole(1_000),
                ["RequiredSciences"] = BundleValue.Text("ScienceWarChant"),
            }));
        var button = new CommandButtonTemplate("Command_SpellBookWarChant", "SPELL_BOOK",
            "", "", "", power.Name);
        var set = new CommandSetTemplate("EvilSpellBookCommandSet",
            new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(specialPowers: new[] { power },
            commandButtons: new[] { button }, commandSets: new[] { set });
        var spellBookTemplate = ModuleBatchBTestSupport.Template("EvilSpellBook", new[]
        {
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = button.Name,
                    ["SpecialPowerAIType"] = "AI_SPELLBOOK_ASSIST_BATTLE_BUFF",
                }),
        }, commandSet: set.Name);
        var allyTemplate = ModuleBatchBTestSupport.Template("ally", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { spellBookTemplate, allyTemplate }, tech: tech);
        var spellBook = world.SpawnObject("EvilSpellBook", 0, ModuleBatchBTestSupport.At(0));
        var ally = world.SpawnObject("ally", 0, ModuleBatchBTestSupport.At(5));
        world.DealDamage(ally, 25);
        var state = AiPlayerState.Create(0,
            new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        var commands = new List<SimCommand>();

        Assert.False(spellBook.FindModule<AISpecialPowerUpdateModule>()!
            .TryPlan(world, spellBook, state, 1, commands));
        world.GrantScience(0, "ScienceWarChant");
        Assert.True(spellBook.FindModule<AISpecialPowerUpdateModule>()!
            .TryPlan(world, spellBook, state, 1, commands));
        var command = Assert.Single(commands);
        Assert.Equal("power", command.Type);
        Assert.Equal(power.Name, command.GetString("name"));
        Assert.Equal(ally.Id, command.GetLong("target"));
        Assert.True(world.SubmitCommand(command));
        world.Tick();

        Assert.Equal(11, world.PowerReadyTick(0, power.Name));
    }

    [Fact]
    public void RadiusScoresClustersWithoutBecomingRangeAndDebuffTargetsEnemy()
    {
        var blast = new SpecialPowerTemplate("Blast", "BLAST", 1_000, Array.Empty<string>(), false);
        var blastButton = new CommandButtonTemplate("Command_Blast", "SPECIAL_POWER",
            "", "", "", blast.Name);
        var debuff = new SpecialPowerTemplate("Debuff", "DEBUFF", 1_000, Array.Empty<string>(), false);
        var debuffButton = new CommandButtonTemplate("Command_Debuff", "SPECIAL_POWER",
            "", "", "", debuff.Name);
        var set = new CommandSetTemplate("PowerSet", new[]
        {
            new CommandSetEntryTemplate(1, blastButton.Name, blastButton),
            new CommandSetEntryTemplate(2, debuffButton.Name, debuffButton),
        });
        var tech = new TechCatalog(specialPowers: new[] { blast, debuff },
            commandButtons: new[] { blastButton, debuffButton }, commandSets: new[] { set });
        var casterTemplate = ModuleBatchBTestSupport.Template("caster", new[]
        {
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                new Dictionary<string, long> { ["SpecialPowerRadius"] = 10 },
                new Dictionary<string, string>
                {
                    ["CommandButtonName"] = blastButton.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_RANGED_AOE_ATTACK",
                }),
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = debuffButton.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_BASIC_SELF_DEBUFF",
                }),
        }, commandSet: set.Name);
        var targetTemplate = ModuleBatchBTestSupport.Template("target", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { casterTemplate, targetTemplate }, tech: tech);
        var caster = world.SpawnObject("caster", 0, ModuleBatchBTestSupport.At(0));
        var isolated = world.SpawnObject("target", 1, ModuleBatchBTestSupport.At(40));
        var clustered = world.SpawnObject("target", 1, ModuleBatchBTestSupport.At(80));
        world.SpawnObject("target", 1, ModuleBatchBTestSupport.At(85));
        var ally = world.SpawnObject("target", 0, ModuleBatchBTestSupport.At(1));
        world.DealDamage(ally, 10);
        var state = AiPlayerState.Create(0,
            new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        var modules = caster.Modules.OfType<AISpecialPowerUpdateModule>().ToArray();
        var commands = new List<SimCommand>();

        Assert.True(modules[0].TryPlan(world, caster, state, 1, commands));
        Assert.Equal(clustered.Id, Assert.Single(commands).GetLong("target"));
        commands.Clear();
        Assert.True(modules[1].TryPlan(world, caster, state, 2, commands));
        Assert.Equal(isolated.Id, Assert.Single(commands).GetLong("target"));
    }

    [Fact]
    public void PlannerStopsAfterFirstSuccessfulAuthoredPower()
    {
        var first = new SpecialPowerTemplate("First", "FIRST", 1_000, Array.Empty<string>(), false);
        var second = new SpecialPowerTemplate("Second", "SECOND", 1_000, Array.Empty<string>(), false);
        var firstButton = new CommandButtonTemplate("Command_First", "SPECIAL_POWER", "", "", "", first.Name);
        var secondButton = new CommandButtonTemplate("Command_Second", "SPECIAL_POWER", "", "", "", second.Name);
        var set = new CommandSetTemplate("HeroSet", new[]
        {
            new CommandSetEntryTemplate(1, firstButton.Name, firstButton),
            new CommandSetEntryTemplate(2, secondButton.Name, secondButton),
        });
        var tech = new TechCatalog(specialPowers: new[] { first, second },
            commandButtons: new[] { firstButton, secondButton }, commandSets: new[] { set });
        var casterTemplate = ModuleBatchBTestSupport.Template("hero", new[]
        {
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = firstButton.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_BASIC_SELF_BUFF",
                }),
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = secondButton.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_BASIC_SELF_BUFF",
                }),
        }, commandSet: set.Name);
        var world = ModuleBatchBTestSupport.World(new[] { casterTemplate }, tech: tech);
        world.SpawnObject("hero", 0, ModuleBatchBTestSupport.At(0));
        var state = AiPlayerState.Create(0,
            new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        var commands = new List<SimCommand>();

        AiSpecialPowerPlanner.Plan(world, state, 1, commands);

        Assert.Equal(first.Name, Assert.Single(commands).GetString("name"));
    }

    [Fact]
    public void AuthoredStanceButtonUsesNormalCommandPathForCurrentCombatOrder()
    {
        var button = new CommandButtonTemplate("Command_SetStanceAggressive", "SET_STANCE",
            "", "", "", "", Stances: "Aggressive");
        var set = new CommandSetTemplate("HordeSet", new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(commandButtons: new[] { button }, commandSets: new[] { set });
        var actorTemplate = ModuleBatchBTestSupport.Template("horde", new[]
        {
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = button.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_STANCEAGGRESSIVE",
                }),
        }, commandSet: set.Name);
        var enemyTemplate = ModuleBatchBTestSupport.Template("enemy", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { actorTemplate, enemyTemplate }, tech: tech);
        var actor = world.SpawnObject("horde", 0, ModuleBatchBTestSupport.At(0));
        var enemy = world.SpawnObject("enemy", 1, ModuleBatchBTestSupport.At(5));
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "attack",
            ("objects", CommandValue.OfLongList(new long[] { actor.Id })),
            ("target", CommandValue.OfLong(enemy.Id)))));
        world.Tick();
        var state = AiPlayerState.Create(0,
            new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        var commands = new List<SimCommand>();

        Assert.True(actor.FindModule<AISpecialPowerUpdateModule>()!.TryPlan(world, actor, state, 2, commands));
        var command = Assert.Single(commands);
        Assert.Equal("stance", command.Type);
        Assert.True(world.SubmitCommand(command));
        world.Tick();

        Assert.Equal(UnitStance.Aggressive, actor.Combat!.Stance);
    }

    [Fact]
    public void AuthoredHoldGroundRowActivatesForDefensiveAiPhase()
    {
        var button = new CommandButtonTemplate("Command_SetStanceHoldGround", "SET_STANCE",
            "", "", "", "", Stances: "HoldGround");
        var set = new CommandSetTemplate("HordeSet",
            new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(commandButtons: new[] { button }, commandSets: new[] { set });
        var actorTemplate = ModuleBatchBTestSupport.Template("horde", new[]
        {
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = button.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_STANCEHOLDGROUND",
                }),
        }, commandSet: set.Name);
        var world = ModuleBatchBTestSupport.World(new[] { actorTemplate }, tech: tech);
        var actor = world.SpawnObject("horde", 0, ModuleBatchBTestSupport.At(0));
        var state = AiPlayerState.Create(0,
            new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        state.Phase = AiPhase.Defend;
        var commands = new List<SimCommand>();

        Assert.True(actor.FindModule<AISpecialPowerUpdateModule>()!.TryPlan(world, actor, state, 1, commands));

        Assert.Equal("hold_ground", Assert.Single(commands).GetString("stance"));
    }

    [Fact]
    public void CaptureBuildingRequiresNeutralCapturableStructure()
    {
        var power = new SpecialPowerTemplate("Capture", "CAPTURE", 1_000, Array.Empty<string>(), false);
        var button = new CommandButtonTemplate("Command_Capture", "SPECIAL_POWER",
            "", "", "", power.Name);
        var set = new CommandSetTemplate("CaptureSet",
            new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(specialPowers: new[] { power }, commandButtons: new[] { button },
            commandSets: new[] { set });
        var casterTemplate = ModuleBatchBTestSupport.Template("capturer", new[]
        {
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = button.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_CAPTURE_BUILDING",
                }),
        }, commandSet: set.Name);
        var enemyStructure = ModuleBatchBTestSupport.Template("enemy-fort", Array.Empty<ModuleSpec>(),
            kindOf: new[] { "STRUCTURE" });
        var captureFlag = ModuleBatchBTestSupport.Template("CaptureFlag", Array.Empty<ModuleSpec>(),
            kindOf: new[] { "CAPTURABLE", "STRUCTURE", "CAPTUREFLAG" });
        var world = ModuleBatchBTestSupport.World(new[] { casterTemplate, enemyStructure, captureFlag }, tech: tech);
        var caster = world.SpawnObject("capturer", 0, ModuleBatchBTestSupport.At(0));
        world.SpawnObject("enemy-fort", 1, ModuleBatchBTestSupport.At(2));
        var flag = world.SpawnObject("CaptureFlag", -1, ModuleBatchBTestSupport.At(10));
        var state = AiPlayerState.Create(0,
            new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        var commands = new List<SimCommand>();

        Assert.True(caster.FindModule<AISpecialPowerUpdateModule>()!.TryPlan(world, caster, state, 1, commands));

        Assert.Equal(flag.Id, Assert.Single(commands).GetLong("target"));
    }

    [Fact]
    public void AuthoredWeaponToggleUsesAbilityCommandAndToggleFlag()
    {
        var button = new CommandButtonTemplate("Command_ToggleSpiderRiderWeapon", "TOGGLE_WEAPONSET",
            "", "", "", "", FlagsUsedForToggle: "WEAPONSET_TOGGLE_1");
        var set = new CommandSetTemplate("RiderSet", new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(commandButtons: new[] { button }, commandSets: new[] { set });
        var riderTemplate = ModuleBatchBTestSupport.Template("rider", new[]
        {
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = button.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_TOGGLE_MELEE_AND_RANGE",
                }),
        }, commandSet: set.Name);
        var enemyTemplate = ModuleBatchBTestSupport.Template("enemy", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { riderTemplate, enemyTemplate }, tech: tech);
        var rider = world.SpawnObject("rider", 0, ModuleBatchBTestSupport.At(0));
        world.SpawnObject("enemy", 1, ModuleBatchBTestSupport.At(5));
        var state = AiPlayerState.Create(0,
            new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        var commands = new List<SimCommand>();

        Assert.True(rider.FindModule<AISpecialPowerUpdateModule>()!.TryPlan(world, rider, state, 1, commands));
        var command = Assert.Single(commands);
        Assert.Equal("ability", command.Type);
        Assert.True(world.SubmitCommand(command));
        world.Tick();

        Assert.Contains("WEAPONSET_TOGGLE_1", rider.ConditionTokens);
    }

    [Fact]
    public void AuthoredFireWeaponButtonFiresItsSlotThroughCombat()
    {
        var button = new CommandButtonTemplate("Command_GimliAxeThrow", "FIRE_WEAPON",
            "", "", "", "", WeaponSlot: "TERTIARY");
        var set = new CommandSetTemplate("GimliSet", new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var tech = new TechCatalog(commandButtons: new[] { button }, commandSets: new[] { set });
        var weaponSet = new WeaponSet(Array.Empty<string>(),
            new[] { new KeyValuePair<WeaponSlot, string>(WeaponSlot.TERTIARY, "Axe") });
        var actorTemplate = ModuleBatchBTestSupport.Template("gimli", new[]
        {
            ModuleBatchBTestSupport.Spec(AISpecialPowerUpdateModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["CommandButtonName"] = button.Name,
                    ["SpecialPowerAIType"] = "AI_SPECIAL_POWER_ENEMY_TYPE_KILLER",
                }),
        }, weaponSets: new[] { weaponSet }, commandSet: set.Name);
        var enemyTemplate = ModuleBatchBTestSupport.Template("enemy", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { actorTemplate, enemyTemplate },
            new[] { ModuleBatchBTestSupport.Weapon("Axe", 25) }, tech);
        var gimli = world.SpawnObject("gimli", 0, ModuleBatchBTestSupport.At(0));
        var enemy = world.SpawnObject("enemy", 1, ModuleBatchBTestSupport.At(200));
        var state = AiPlayerState.Create(0,
            new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        var commands = new List<SimCommand>();

        var module = gimli.FindModule<AISpecialPowerUpdateModule>()!;
        Assert.False(module.TryPlan(world, gimli, state, 1, commands));
        enemy.SetPosition(ModuleBatchBTestSupport.At(5));
        Assert.True(module.TryPlan(world, gimli, state, 1, commands));
        Assert.True(world.SubmitCommand(Assert.Single(commands)));
        world.Tick();

        Assert.Equal(Fixed64.FromInt(75), enemy.Health);
    }
}
