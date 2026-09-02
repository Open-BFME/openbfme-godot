using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class InactiveBodyTests
{
    [Fact]
    public void HasNoHealthIsUntargetableAndConsumesDamage()
    {
        var template = new ObjectTemplate("scenery", new[] { new ModuleSpec(InactiveBodyModule.TypeName) });
        var world = new SimWorld(new SimConfig(new[] { template }, 1, 2), ModuleRegistry.CreateDefault());
        var scenery = world.SpawnObject("scenery", 1, FixedVector2.Zero);

        world.DealDamage(scenery, 999);

        Assert.False(world.IsAttackable(scenery));
        Assert.False(scenery.IsDead);
        Assert.Equal(Fixed64.Zero, scenery.MaxHealth);
    }
}
