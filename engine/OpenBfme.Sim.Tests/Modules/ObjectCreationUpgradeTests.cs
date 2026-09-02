using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class ObjectCreationUpgradeTests
{
    [Fact]
    public void CorpusShapesSpawnThingOrGrantUpgradeToOwner()
    {
        var parent = ModuleBatchBTestSupport.Template("fort", new[]
        {
            ModuleBatchBTestSupport.Spec(ObjectCreationUpgradeModule.TypeName,
                strings: new Dictionary<string, string> { ["TriggeredBy"] = "Level2", ["ThingToSpawn"] = "spikes" }),
            ModuleBatchBTestSupport.Spec(ObjectCreationUpgradeModule.TypeName,
                strings: new Dictionary<string, string>
                {
                    ["TriggeredBy"] = "SanctumPurchased",
                    ["GrantUpgrade"] = "Upgrade_AngmarFortressSanctumReady",
                }),
        }, techEnabled: true);
        var child = ModuleBatchBTestSupport.Template("spikes", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { parent, child });
        var fort = world.SpawnObject("fort", 1, ModuleBatchBTestSupport.At(4));

        Assert.True(world.GrantUpgrade(fort, "Level2"));

        var spikes = Assert.Single(world.Objects.Values, value => value.TemplateName == "spikes");
        Assert.Equal(1, spikes.Team);
        Assert.Equal(fort.Position, spikes.Position);

        Assert.True(world.GrantUpgrade(fort, "SanctumPurchased"));
        Assert.Contains("Upgrade_AngmarFortressSanctumReady", fort.OwnedUpgrades);
    }
}
