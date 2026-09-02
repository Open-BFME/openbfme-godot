using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class InheritUpgradeCreateTests
{
    [Fact]
    public void ChildInheritsNamedObjectUpgradeFromProducer()
    {
        var producerTemplate = ModuleBatchBTestSupport.Template("fort", new[]
        {
            ModuleBatchBTestSupport.Spec(GrantUpgradeCreateModule.TypeName,
                strings: new Dictionary<string, string> { ["UpgradeToGrant"] = "IceWalls" }),
        }, kindOf: new[] { "CASTLE_KEEP" });
        var childTemplate = ModuleBatchBTestSupport.Template("wall", new[]
        {
            ModuleBatchBTestSupport.Spec(InheritUpgradeCreateModule.TypeName,
                new Dictionary<string, long> { ["Radius"] = 100 },
                new Dictionary<string, string> { ["Upgrade"] = "IceWalls", ["ObjectFilter"] = "ANY +CASTLE_KEEP" }),
        });
        var world = ModuleBatchBTestSupport.World(new[] { producerTemplate, childTemplate });
        var producer = world.SpawnObject("fort", 0, ModuleBatchBTestSupport.At(0));

        var child = world.SpawnObjectFrom("wall", 0, ModuleBatchBTestSupport.At(1), producer);

        Assert.Contains("IceWalls", child.OwnedUpgrades);
        Assert.True(child.FindModule<InheritUpgradeCreateModule>()!.HasInherited);
    }
}
