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
}
