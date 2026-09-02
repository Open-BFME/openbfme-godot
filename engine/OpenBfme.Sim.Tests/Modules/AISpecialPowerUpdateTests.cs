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
        var power = new SpecialPowerTemplate("SpellBookWarChant", "SPELL_BOOK", 1_000,
            Array.Empty<string>(), false);
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
        var enemy = world.SpawnObject("enemy", 1, ModuleBatchBTestSupport.At(5));
        var state = AiPlayerState.Create(0,
            new MatchLaunchPlayer(0, 0, "Men", "ai", "hard", null, null, null, null, null));
        var commands = new List<SimCommand>();

        Assert.True(gimli.FindModule<AISpecialPowerUpdateModule>()!.TryPlan(world, gimli, state, 1, commands));
        Assert.True(world.SubmitCommand(Assert.Single(commands)));
        world.Tick();

        Assert.Equal(Fixed64.FromInt(75), enemy.Health);
    }
}
