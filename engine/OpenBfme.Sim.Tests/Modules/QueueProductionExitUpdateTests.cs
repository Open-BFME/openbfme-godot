using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class QueueProductionExitUpdateTests
{
    [Fact]
    public void AuthoredCreatePointAndNaturalRallyStartProducedPath()
    {
        var producerTemplate = new ObjectTemplate("barracks", new ModuleSpec[]
        {
            new(QueueProductionExitUpdateModule.TypeName, null,
                new Dictionary<string, string>
                {
                    ["UnitCreatePoint"] = "X:2.0 Y:0.0 Z:0.0",
                    ["NaturalRallyPoint"] = "X:4.0 Y:0.0 Z:0.0",
                }),
            new(ProductionModule.TypeName, new Dictionary<string, long> { ["Build:unit"] = 1 }),
        });
        var unitTemplate = new ObjectTemplate("unit", new[]
        {
            new ModuleSpec(LinearMoverModule.TypeName,
                new Dictionary<string, long> { ["SpeedPerTickRaw"] = Fixed64.One.Raw }),
        });
        var world = new SimWorld(
            new SimConfig(new[] { producerTemplate, unitTemplate }, 15, 2),
            ModuleRegistry.CreateDefault());
        var barracks = world.SpawnObject("barracks", 0,
            new FixedVector2(Fixed64.FromInt(10), Fixed64.Zero));
        Assert.True(barracks.FindModule<ProductionModule>()!.TryQueue(world, barracks, "unit"));

        world.Advance(2);

        var unit = Assert.Single(world.Objects.Values, value => value.TemplateName == "unit");
        Assert.Equal(Fixed64.FromInt(12), unit.Position.X);
        world.Tick();
        Assert.Equal(Fixed64.FromInt(13), unit.Position.X);
    }
}
