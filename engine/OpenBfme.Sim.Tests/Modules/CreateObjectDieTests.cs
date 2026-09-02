using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class CreateObjectDieTests
{
    [Fact]
    public void DirectCreationListTemplateSpawnsOnceForOwnerOnDeath()
    {
        var debris = new ObjectTemplate("debris", new[] { new ModuleSpec(InactiveBodyModule.TypeName) });
        var source = new ObjectTemplate("source", new ModuleSpec[]
        {
            new(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 10 }),
            new(CreateObjectDieModule.TypeName, null,
                new Dictionary<string, string> { ["CreationList"] = "debris" }),
        });
        var world = new SimWorld(new SimConfig(new[] { source, debris }, 16, 2), ModuleRegistry.CreateDefault());
        var dying = world.SpawnObject("source", 1,
            new FixedVector2(Fixed64.FromInt(3), Fixed64.FromInt(2)));

        world.DealDamage(dying, 10);
        world.Tick();

        var created = Assert.Single(world.Objects.Values);
        Assert.Equal("debris", created.TemplateName);
        Assert.Equal(1, created.Team);
        Assert.Equal(dying.Position, created.Position);
    }
}
