using Xunit;

namespace OpenBfme.Sim.Tests;

public sealed class BundleModuleValidationTests
{
    [Fact]
    public void InvalidRegisteredStructuralModulesFailTheirTemplatesAtBundleLoad()
    {
        var document = BundleDocument.Parse(MinimalBundle("""
            {"name":"BadBanner","kind":"object","parent":null,"kindof":[],"geometry":{},
             "fields":{},"blocks":[],"modules":[
               {"carrier":"Behavior","type":"BannerCarrierUpdate","tag":"ModuleTag_Banner",
                "fields":{},"blocks":[],"gap":false}]},
            {"name":"BadCastle","kind":"object","parent":null,"kindof":[],"geometry":{},
             "fields":{},"blocks":[],"modules":[
               {"carrier":"Behavior","type":"CastleBehavior","tag":"ModuleTag_Castle",
                "fields":{},"blocks":[],"gap":false}]}
            """));

        var result = BundleTemplateLoader.Load(document, ModuleRegistry.CreateDefault(), 33);

        Assert.Empty(result.Templates);
        Assert.Collection(
            result.Report.TemplatesFailed.OrderBy(row => row.Template),
            row =>
            {
                Assert.Equal("BadBanner", row.Template);
                Assert.Contains("BannerTemplate", row.Reason, StringComparison.Ordinal);
            },
            row =>
            {
                Assert.Equal("BadCastle", row.Template);
                Assert.Contains("BSE piece", row.Reason, StringComparison.Ordinal);
            });
    }

    private static string MinimalBundle(string templates) => $$"""
        {
          "schema":"openbfme.bundle.v1",
          "source":{"effective_tree_sha256":"{{new string('0', 64)}}","paths":[]},
          "templates":[{{templates}}],
          "defines":{},
          "diagnostics":[]
        }
        """;
}
