using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class BuildingBehaviorTests
{
    [Fact]
    public void AuthoredWindowsReactToFireAndRebuildTimeRestoresStructure()
    {
        var template = ModuleBatchBTestSupport.Template("tower", new[]
        {
            ModuleBatchBTestSupport.Spec(BuildingBehaviorModule.TypeName,
                new Dictionary<string, long> { ["RebuildTime"] = 200 },
                new Dictionary<string, string>
                {
                    ["NightWindowName"] = "WINDOW_N01",
                    ["FireWindowName"] = "WINDOW_F01",
                    ["GlowWindowName"] = "WINDOW_G01",
                    ["FireName"] = "FIRE01\nFIRE02",
                }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var tower = world.SpawnObject("tower", 0, ModuleBatchBTestSupport.At(0));
        var module = tower.FindModule<BuildingBehaviorModule>()!;

        Assert.Contains("MODEL:NIGHT:WINDOW_N01", tower.ConditionTokens);
        world.DealDamage(tower, 1, "FLAME");
        Assert.Contains("MODEL:FIRE:WINDOW_F01", tower.ConditionTokens);
        Assert.Contains("MODEL:GLOW:WINDOW_G01", tower.ConditionTokens);
        Assert.Equal(new[] { "FIRE01", "FIRE02" },
            world.EventsThisTick.Where(value => value.Kind == "fire").Select(value => value.Name));
        Assert.NotEqual(0, module.PresentationStateBits & BuildingBehaviorModule.PresentationFireWindows);

        world.DealDamage(tower, 100);
        Assert.True(module.IsRebuildHole);
        world.Advance(2);

        Assert.False(tower.IsDying);
        Assert.Equal(tower.MaxHealth, tower.Health);
    }
}
