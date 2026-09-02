using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class GeometryUpgradeTests
{
    [Fact]
    public void TriggeredByAppliesCorpusAuthoredShowHideGeometry()
    {
        var template = ModuleBatchBTestSupport.Template("fort", new[]
        {
            ModuleBatchBTestSupport.Spec(GeometryUpgradeModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["TriggeredBy"] = "Level2", ["ShowGeometry"] = "Geom_V2", ["HideGeometry"] = "Geom_Orig",
                }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { template });
        var fort = world.SpawnObject("fort", 0, ModuleBatchBTestSupport.At(0));

        Assert.True(world.GrantUpgrade(fort, "Level2"));
        var module = fort.FindModule<GeometryUpgradeModule>()!;
        Assert.Equal("Geom_V2", module.ShownGeometry);
        Assert.Equal("Geom_Orig", module.HiddenGeometry);
        Assert.Contains("GEOMETRY_SHOW:Geom_V2", fort.ConditionTokens);
        Assert.Contains("GEOMETRY_HIDE:Geom_Orig", fort.ConditionTokens);
    }
}
