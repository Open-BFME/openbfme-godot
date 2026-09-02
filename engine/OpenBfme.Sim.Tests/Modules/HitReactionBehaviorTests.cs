using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class HitReactionBehaviorTests
{
    [Fact]
    public void ThresholdSelectsLifeTimerAndPausesMovement()
    {
        var template = ModuleBatchBTestSupport.Template("unit", new[]
        {
            ModuleBatchBTestSupport.Spec(HitReactionBehaviorModule.TypeName,
                new Dictionary<string, long> { ["HitReactionThreshold1"] = 5, ["HitReactionLifeTimer1"] = 200 }),
            ModuleBatchBTestSupport.Spec(LinearMoverModule.TypeName,
                new Dictionary<string, long> { ["SpeedPerTickRaw"] = Fixed64.One.Raw }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var unit = world.SpawnObject("unit", 0, ModuleBatchBTestSupport.At(0));
        unit.FindModule<LinearMoverModule>()!.SetDestination(ModuleBatchBTestSupport.At(5));

        world.DealDamage(unit, 5);
        world.Tick();

        Assert.True(unit.FindModule<HitReactionBehaviorModule>()!.IsStaggered);
        Assert.Equal(FixedVector2.Zero, unit.Position);
    }
}
