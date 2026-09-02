using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class ReplaceSelfUpgradeTests
{
    [Fact]
    public void ReplacementPreservesOwnerTransformAndHealthFraction()
    {
        var oldTemplate = ModuleBatchBTestSupport.Template("old", new[]
        {
            ModuleBatchBTestSupport.Spec(ReplaceSelfUpgradeModule.TypeName,
                strings: new Dictionary<string, string> { ["TriggeredBy"] = "Morph", ["ReplaceWith"] = "new" }),
        });
        var newTemplate = ModuleBatchBTestSupport.Template("new", Array.Empty<ModuleSpec>(), health: 200);
        var world = ModuleBatchBTestSupport.World(new[] { oldTemplate, newTemplate });
        var old = world.SpawnObject("old", 1, ModuleBatchBTestSupport.At(7));
        world.DealDamage(old, 50);

        Assert.True(world.GrantUpgrade(old, "Morph"));

        var replacement = Assert.Single(world.Objects.Values, value => value.TemplateName == "new");
        Assert.Equal(1, replacement.Team);
        Assert.Equal(old.Position, replacement.Position);
        Assert.Equal(Fixed64.FromInt(100), replacement.Health);
    }
}
