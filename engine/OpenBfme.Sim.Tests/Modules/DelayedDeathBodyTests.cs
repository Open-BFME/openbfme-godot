using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class DelayedDeathBodyTests
{
    [Fact]
    public void AuthoredMillisecondsDelayThenResumeNormalDeathPipeline()
    {
        var template = new ObjectTemplate("mumak", new[]
        {
            new ModuleSpec(DelayedDeathBodyModule.TypeName,
                new Dictionary<string, long> { ["MaxHealth"] = 20, ["DelayedDeathTime"] = 66 }),
            new ModuleSpec(DestroyDieModule.TypeName),
        });
        var world = new SimWorld(new SimConfig(new[] { template }, 3, 2), ModuleRegistry.CreateDefault(), 33);
        var victim = world.SpawnObject("mumak", 1, FixedVector2.Zero);

        world.DealDamage(victim, 20);
        world.Tick();
        Assert.True(victim.IsDying);
        Assert.Contains(victim.Id, world.Objects.Keys);

        world.Tick();
        Assert.DoesNotContain(victim.Id, world.Objects.Keys);
    }
}
