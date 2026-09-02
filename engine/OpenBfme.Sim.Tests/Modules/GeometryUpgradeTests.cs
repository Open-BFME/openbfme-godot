using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class GeometryUpgradeTests
{
    [Fact]
    public void TriggeredByAppliesAuthoredRadiiAndShowHideGeometry()
    {
        var template = ModuleBatchBTestSupport.Template("fort", new[]
        {
            ModuleBatchBTestSupport.Spec(GeometryUpgradeModule.TypeName,
                new Dictionary<string, long> { ["GeometryMajorRadius"] = 25, ["GeometryMinorRadius"] = 10 },
                new Dictionary<string, string>
                {
                    ["TriggeredBy"] = "Level2", ["ShowGeometry"] = "Geom_V2", ["HideGeometry"] = "Geom_Orig",
                }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var fort = world.SpawnObject("fort", 0, ModuleBatchBTestSupport.At(0));

        Assert.True(world.GrantUpgrade(fort, "Level2"));
        var module = fort.FindModule<GeometryUpgradeModule>()!;
        Assert.Equal(Fixed64.FromInt(25), module.MajorRadius);
        Assert.Equal("Geom_V2", module.ShownGeometry);
    }
}
