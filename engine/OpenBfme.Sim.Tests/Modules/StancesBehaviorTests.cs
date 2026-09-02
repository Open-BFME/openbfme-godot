using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class StancesBehaviorTests
{
    [Fact]
    public void StanceCommandMapsHoldAndAggressiveProfilesIntoIdleAi()
    {
        var template = new ObjectTemplate("unit", new ModuleSpec[]
        {
            new(AIUpdateInterfaceModule.TypeName,
                new Dictionary<string, long> { ["AutoAcquireEnemiesWhenIdle"] = 1 }),
            new(StancesBehaviorModule.TypeName,
                new Dictionary<string, long> { ["HoldGroundGuardRadius"] = 3 }),
        }, bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(20)));
        var world = new SimWorld(new SimConfig(new[] { template }, 12, 2), ModuleRegistry.CreateDefault());
        var unit = world.SpawnObject("unit", 0, FixedVector2.Zero);
        Assert.True(world.SubmitCommand(Stance(1, unit.Id, "hold_ground")));
        world.Tick();

        var ai = unit.FindModule<AIUpdateInterfaceModule>()!;
        Assert.False(ai.AutoAcquireEnabled);
        Assert.Equal(Fixed64.FromInt(3), ai.GuardRadius);

        Assert.True(world.SubmitCommand(Stance(2, unit.Id, "aggressive")));
        world.Tick();
        Assert.True(ai.AutoAcquireEnabled);
    }

    private static SimCommand Stance(int tick, int id, string stance) =>
        TestWorlds.Command(tick, 0, 0, "stance",
            ("objects", CommandValue.OfLongList(new long[] { id })),
            ("stance", CommandValue.OfString(stance)));
}
