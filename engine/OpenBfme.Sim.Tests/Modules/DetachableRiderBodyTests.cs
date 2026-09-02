using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class DetachableRiderBodyTests
{
    [Fact]
    public void DeathSpawnsLoadedAuthoredRiderTemplateForOwner()
    {
        var rider = new ObjectTemplate("orc-rider", new[] { new ModuleSpec(InactiveBodyModule.TypeName) });
        var mount = new ObjectTemplate("warg", new[]
        {
            new ModuleSpec(DetachableRiderBodyModule.TypeName,
                new Dictionary<string, long> { ["MaxHealth"] = 10 },
                new Dictionary<string, string> { ["RiderTemplate"] = "orc-rider" }),
        });
        var world = new SimWorld(new SimConfig(new[] { mount, rider }, 6, 2), ModuleRegistry.CreateDefault());
        var warg = world.SpawnObject("warg", 1,
            new FixedVector2(Fixed64.FromInt(7), Fixed64.FromInt(2)));

        world.DealDamage(warg, 10);
        world.Tick();

        var detached = Assert.Single(world.Objects.Values);
        Assert.Equal("orc-rider", detached.TemplateName);
        Assert.Equal(1, detached.Team);
        Assert.Equal(warg.Position, detached.Position);
    }
}
