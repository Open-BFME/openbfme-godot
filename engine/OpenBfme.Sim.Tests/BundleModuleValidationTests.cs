using Xunit;

namespace OpenBfme.Sim.Tests;

public sealed class BundleModuleValidationTests
{
    [Fact]
    public void RegisteredStructuralModulesWithMissingOptionalDataLoadFailOpen()
    {
        // Missing optional data on a registered structural module is not a broken
        // module: retail authors BannerCarrierUpdate rows without BannerTemplate and
        // fortress CastleBehavior rows whose layout lives in a .bse file. Both load;
        // the missing facts are visible on the module, never a template failure.
        var document = BundleDocument.Parse(MinimalBundle("""
            {"name":"BareBanner","kind":"object","parent":null,"kindof":[],"geometry":{},
             "fields":{},"blocks":[],"modules":[
               {"carrier":"Behavior","type":"BannerCarrierUpdate","tag":"ModuleTag_Banner",
                "fields":{},"blocks":[],"gap":false}]},
            {"name":"BareCastle","kind":"object","parent":null,"kindof":[],"geometry":{},
             "fields":{},"blocks":[],"modules":[
               {"carrier":"Behavior","type":"CastleBehavior","tag":"ModuleTag_Castle",
                "fields":{},"blocks":[],"gap":false}]}
            """));

        var result = BundleTemplateLoader.Load(document, ModuleRegistry.CreateDefault(), 33);

        Assert.Empty(result.Report.TemplatesFailed);
        Assert.Equal(new[] { "BareBanner", "BareCastle" }, result.Templates.Select(template => template.Name).OrderBy(name => name, StringComparer.Ordinal));
        var banner = new BannerCarrierModule(Assert.Single(result.Templates.Single(t => t.Name == "BareBanner").Modules));
        Assert.False(banner.HasBannerTemplate);
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
