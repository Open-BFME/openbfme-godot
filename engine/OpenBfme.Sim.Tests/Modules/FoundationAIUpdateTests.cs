using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class FoundationAIUpdateTests
{
    [Fact]
    public void FoundationRegistersPlotAndAcceptsNormalBuildCommand()
    {
        var foundation = ModuleBatchBTestSupport.Template("foundation", new[]
        {
            ModuleBatchBTestSupport.Spec(FoundationAIUpdateModule.TypeName),
        }, economy: new EconomyTemplate(commandSet: new[] { "tower" }));
        var tower = ModuleBatchBTestSupport.Template("tower", new[]
        {
            ModuleBatchBTestSupport.Spec(GettingBuiltModule.TypeName),
        }, economy: new EconomyTemplate(buildTimeMilliseconds: 100));
        var world = ModuleBatchBTestSupport.World(new[] { foundation, tower });
        var plot = world.SpawnObject("foundation", 0, ModuleBatchBTestSupport.At(3));
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "build",
            ("objects", CommandValue.OfLongList(new long[] { plot.Id })),
            ("template", CommandValue.OfString("tower")),
            ("index", CommandValue.OfLong(0)))));

        world.Tick();

        Assert.Contains(world.Objects.Values, value => value.TemplateName == "tower");
        Assert.DoesNotContain(plot.Id, world.Objects.Keys);
        Assert.Empty(world.BuildPlots);
    }
}
