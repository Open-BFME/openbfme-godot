using OpenBfme.Sim;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

public sealed class RetailStructureBridgeTests
{
    private readonly ITestOutputHelper _output;

    public RetailStructureBridgeTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void IsengardPorterBuildsAuthoredFurnaceOnKernelPlot()
    {
        var path = MatchLaunchTests.RepoPath(
            "workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");
        if (!File.Exists(path))
        {
            _output.WriteLine($"SKIP: corpus bundle absent at {path}; retail structure proof unavailable");
            return;
        }

        var document = BundleDocument.Load(path);
        var setup = AiRetailProofTests.SelectSetup(document, "Isengard", "Men");
        var world = AiRetailProofTests.Build(document, setup, "hard", "medium");
        var builder = Assert.Single(world.Objects.Values, value => value.Team == 0
            && value.Template.KindOf.Contains("PORTER", StringComparer.OrdinalIgnoreCase));
        Assert.Equal("IsengardPorterCommandSet", builder.CurrentCommandSet);
        var loaded = BundleTemplateLoader.Load(document, ModuleRegistry.CreateDefault(), 33);
        var furnace = Assert.Single(loaded.Templates, value => value.Name == "IsengardFurnace");
        Assert.Equal(300, furnace.Economy.BuildCost);
        Assert.Equal(15_000, furnace.Economy.BuildTimeMilliseconds);
        Assert.True((AiTemplateRoles.Classify(furnace) & AiUnitRole.Economy) != 0);
        Assert.Equal("Civilian", furnace.Side);
        Assert.Contains(furnace.Modules, value => value.TypeName == GettingBuiltModule.TypeName);
        Assert.Contains(furnace.Modules, value => value.TypeName == ProductionModule.TypeName);
        var commandSet = loaded.Tech.CommandSets[builder.CurrentCommandSet];
        Assert.Contains(commandSet.Entries, value => value.Button.Command == "DOZER_CONSTRUCT"
            && value.Button.Object == furnace.Name);
        Assert.Contains(world.BuildPlots, value => value.BaseObjectId == builder.Id
            && value.AllowedKinds.Contains(furnace.Name, StringComparer.Ordinal));

        world.Tick();

        Assert.True(world.AiCommandCounts(0).TryGetValue("build", out var count) && count > 0,
            string.Join(" | ", world.AiDiagnostics.Where(value => value.Player == 0)
                .Select(value => $"{value.Action}:{value.Detail}")));
        var built = Assert.Single(world.Objects.Values, value => value.Team == 0
            && value.TemplateName == furnace.Name);
        Assert.True(built.IsUnderConstruction,
            $"built object was not under construction: health={built.Health} maximum={world.AiHealth(built).Maximum}");
        Assert.True(built.Health < world.AiHealth(built).Maximum,
            $"getting-built ramp did not start below maximum: health={built.Health} maximum={world.AiHealth(built).Maximum}");
    }
}
