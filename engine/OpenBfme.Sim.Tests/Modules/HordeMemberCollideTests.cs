using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class HordeMemberCollideTests
{
    [Fact]
    public void DifferentHordesSeparateOnceInAscendingIdOrder()
    {
        var template = new ObjectTemplate("member", new[]
        {
            new ModuleSpec(HordeMemberCollideModule.TypeName,
                new Dictionary<string, long> { ["Radius"] = 2 }),
        });
        var world = new SimWorld(new SimConfig(new[] { template }, 17, 2), ModuleRegistry.CreateDefault());
        var first = world.SpawnObject("member", 0, FixedVector2.Zero);
        var second = world.SpawnObject("member", 1, FixedVector2.Zero);
        world.AddHorde(new SnapshotHorde(100, 0, 0, new[] { first.Id }, 0));
        world.AddHorde(new SnapshotHorde(200, 1, 0, new[] { second.Id }, 0));

        world.Tick();

        Assert.Equal(Fixed64.FromInt(-1), first.Position.X);
        Assert.Equal(Fixed64.FromInt(1), second.Position.X);
        Assert.Equal(Fixed64.FromInt(2),
            Fixed64.Sqrt(first.Position.DistanceSquaredTo(second.Position)));
    }
}
