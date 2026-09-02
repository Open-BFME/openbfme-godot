using System.Diagnostics;
using System.Text.Json;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

public sealed class BundleLoaderTests
{
    private readonly ITestOutputHelper _output;

    public BundleLoaderTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void FixtureReaderPreservesOrderInheritanceFieldsBlocksAndGapRows()
    {
        var document = BundleDocument.Load(FixturePath());

        Assert.Equal(BundleDocument.ExpectedSchema, document.Schema);
        Assert.Equal(7, document.Templates.Count);
        Assert.Equal("CookChild", document.Templates[3].Name);
        Assert.Equal("CookBase", document.Templates[3].Parent);
        var production = Assert.Single(
            document.Templates[2].Modules,
            module => module.Type == ProductionModule.TypeName);
        Assert.Equal(3, production.Fields["QueueMax"].Integer);
        Assert.Equal(new[] { "FIRST", "SECOND" },
            production.Fields["Bonus"].Items.Select(item => item.String));
        var draw = Assert.Single(
            document.Templates[2].Modules,
            module => module.Type == "W3DScriptedModelDraw");
        Assert.Equal("DefaultModelConditionState", Assert.Single(draw.Blocks).Type);
        Assert.Equal("COOK_MODEL", draw.Blocks[0].Fields["Model"].String);
        var gap = Assert.Single(document.Templates[6].Modules,
            module => module.Type == "ModuleTag_Malformed");
        Assert.True(gap.Gap);
        Assert.Equal("ModuleTag_Malformed", gap.Type);
        Assert.Equal(8, gap.Fields["Count"].Integer);
        Assert.Equal(6, document.Source.Paths.Count);
        Assert.Equal(10, document.Defines.Count);
        Assert.Equal(2, document.Diagnostics.Count);
    }

    [Fact]
    public void ReaderEnforcesSchemaAndConvertsNumericTextWithoutBinaryFloatingPoint()
    {
        var wrongSchema = MinimalBundle("").Replace(
            BundleDocument.ExpectedSchema, "openbfme.bundle.v2", StringComparison.Ordinal);
        var exception = Assert.Throws<BundleDocumentException>(() => BundleDocument.Parse(wrongSchema));
        Assert.Contains("schema", exception.Message, StringComparison.OrdinalIgnoreCase);

        var document = BundleDocument.Parse(MinimalBundle("""
            {"name":"Exact","kind":"object","parent":null,"kindof":[],"geometry":{},
             "fields":{"NumericString":"0.1","NumericToken":0.1},"blocks":[],"modules":[]}
            """));
        var expected = Fixed64.FromFraction(1, 10);
        Assert.Equal(expected, document.Templates[0].Fields["NumericString"].Fixed);
        Assert.Equal(expected, document.Templates[0].Fields["NumericToken"].Fixed);
    }

    [Fact]
    public void LoaderReplacesInheritedModuleByTagAndReportsUnknownsAndAbsentTables()
    {
        var result = BundleTemplateLoader.Load(
            BundleDocument.Load(FixturePath()), ModuleRegistry.CreateDefault(), tickMilliseconds: 33);

        Assert.Equal(5, result.Report.TemplatesLoaded);
        Assert.Equal(2, result.Report.TemplatesFailed.Count);
        Assert.Contains(result.Report.TemplatesFailed,
            failure => failure.Template == "CookMystery" && failure.Reason.Contains("MadeUpBehavior", StringComparison.Ordinal));
        Assert.Contains(result.Report.GapRowsByType, pair => pair.Key == "ModuleTag_Malformed");
        Assert.Empty(result.Report.AbsentTables);

        var child = Assert.Single(result.Templates, template => template.Name == "CookChild");
        Assert.Single(child.Modules, module => module.TypeName == ActiveBodyModule.TypeName);
        var production = Assert.Single(child.Modules, module => module.TypeName == ProductionModule.TypeName);
        Assert.Equal(5, production.GetLong("QueueMax", -1));
        Assert.Equal("ModuleTag_Production", production.Tag);
        Assert.Equal(800, child.Economy.BuildCost);
        Assert.Equal(15_500, child.Economy.BuildTimeMilliseconds);
        Assert.Equal(25, child.Economy.CommandPoints);
        Assert.Equal(Fixed64.FromInt(1200), child.BodyHealth!.MaxHealth);
        Assert.Equal("CookSword", Assert.Single(child.WeaponSets).PrimaryWeaponName);
    }

    [Fact]
    public void BundleWorldSpawnsEveryLoadedFixtureTemplateAndPublishesBundleIndexes()
    {
        var launch = MatchLaunch.Load(MatchLaunchTests.RepoPath(
            "contracts", "fixtures", "match-launch-v1.json"));
        var document = BundleDocument.Load(FixturePath());
        var world = SimWorld.FromBundle(launch, document);
        var loadedNames = document.Templates
            .Where(row => world.BundleLoadReport!.TemplatesFailed.All(failure => failure.Template != row.Name))
            .Select(row => row.Name)
            .ToArray();

        for (var index = 0; index < loadedNames.Length; index++)
        {
            world.SpawnObject(loadedNames[index], index % 2,
                new FixedVector2(Fixed64.FromInt(index), Fixed64.FromInt(index + 1)));
        }
        world.Advance(100);

        using var snapshot = JsonDocument.Parse(SnapshotWriter.Write(world));
        Assert.Equal(new[] { 0, 1, 2, 3, 4 }, snapshot.RootElement.GetProperty("objects")
            .GetProperty("template").EnumerateArray().Select(item => item.GetInt32()).ToArray());
    }

    [Fact]
    public void UnknownStructuralModuleFailsOnlyItsTemplateAndWorldStillConstructs()
    {
        var document = BundleDocument.Parse(MinimalBundle("""
            {"name":"Bad","kind":"object","parent":null,"kindof":[],"geometry":{},"fields":{},"blocks":[],
             "modules":[{"carrier":"Behavior","type":"MissingStructuralBehavior","tag":"ModuleTag_Bad","fields":{},"blocks":[],"gap":false}]},
            {"name":"Good","kind":"object","parent":null,"kindof":[],"geometry":{},"fields":{},"blocks":[],
             "modules":[{"carrier":"Body","type":"ActiveBody","tag":"ModuleTag_Body","fields":{"MaxHealth":10},"blocks":[],"gap":false}]}
            """));
        var launch = MatchLaunch.Load(MatchLaunchTests.RepoPath(
            "contracts", "fixtures", "match-launch-v1.json"));

        var world = SimWorld.FromBundle(launch, document);

        var failure = Assert.Single(world.BundleLoadReport!.TemplatesFailed);
        Assert.Equal("Bad", failure.Template);
        Assert.Contains("MissingStructuralBehavior", failure.Reason, StringComparison.Ordinal);
        Assert.Equal(1, world.BundleLoadReport.TemplatesLoaded);
        Assert.Throws<KeyNotFoundException>(() => world.SpawnObject("Bad", 0, FixedVector2.Zero));
        world.SpawnObject("Good", 0, FixedVector2.Zero);
    }

    [Fact]
    public void OptionalKernelTablesAreConsumedAndResolveNamedReferences()
    {
        var document = BundleDocument.Parse("""
            {
              "schema":"openbfme.bundle.v1",
              "source":{"effective_tree_sha256":"__HASH__","paths":[]},
              "templates":[
                {"name":"Soldier","kind":"object","parent":null,"kindof":[],"geometry":{},"fields":{},
                 "blocks":[
                   {"type":"WeaponSet","tag":"","fields":{"Conditions":"None","Weapon":"PRIMARY Sword"},"blocks":[]},
                   {"type":"ArmorSet","tag":"","fields":{"Conditions":"None","Armor":"Plate"},"blocks":[]},
                   {"type":"LocomotorSet","tag":"","fields":{"Locomotor":"SET_NORMAL Walker"},"blocks":[]}],
                 "modules":[{"carrier":"Body","type":"ActiveBody","tag":"ModuleTag_Body","fields":{"MaxHealth":100},"blocks":[],"gap":false}]}
              ],
              "defines":{},"diagnostics":[],
              "weapons":[{"name":"Sword","fields":{"AttackRange":12,"DelayBetweenShots":1000},
                "nuggets":[{"kind":"DamageNugget","fields":{"Damage":25,"DamageType":"SLASH"}}]}],
              "armors":[{"name":"Plate","entries":[{"damage_type":"DEFAULT","percent":50}],"fields":{}}],
              "damage_fx":[{"name":"SwordDamageFX","fields":{"ThrottleTime":100}}],
              "locomotors":[{"name":"Walker","fields":{"Speed":30,"TurnRate":90}}],
              "locomotor_sets":[{"name":"Soldier","fields":{"Condition":"SET_NORMAL","Locomotor":"Walker"}}],
              "hordes":[{"name":"SoldierHorde","rank_info":[],"fields":{}}]
            }
            """.Replace("__HASH__", new string('0', 64), StringComparison.Ordinal));

        var result = BundleTemplateLoader.Load(document, ModuleRegistry.CreateDefault(), 33);

        Assert.Empty(result.Report.AbsentTables);
        Assert.All(result.Report.UnresolvedReferences, pair => Assert.Empty(pair.Value));
        Assert.Single(result.WeaponTemplates);
        Assert.Single(result.ArmorTemplates);
        Assert.Single(document.DamageFx!);
        Assert.Single(document.LocomotorSets!);
        Assert.Single(document.Hordes!);
        var template = Assert.Single(result.Templates);
        Assert.Equal("Sword", Assert.Single(template.WeaponSets).PrimaryWeaponName);
        Assert.Equal("Plate", Assert.Single(template.ArmorSets).ArmorName);
        var locomotor = Assert.Single(template.Modules,
            module => module.TypeName == LocomotorModule.TypeName);
        Assert.Equal(30, locomotor.GetLong("Speed", -1));
    }

    [Fact]
    public void FutureCompleteCookBundleCompatibilityWhenProvided()
    {
        var path = Environment.GetEnvironmentVariable("OPENBFME_COMPLETE_BUNDLE_TEST_PATH");
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            _output.WriteLine("SKIP: OPENBFME_COMPLETE_BUNDLE_TEST_PATH does not name a complete cook bundle");
            return;
        }
        var document = BundleDocument.Load(path);
        Assert.NotNull(document.Weapons);
        Assert.NotNull(document.Armors);
        Assert.NotNull(document.DamageFx);
        Assert.NotNull(document.Locomotors);
        Assert.NotNull(document.LocomotorSets);
        Assert.NotNull(document.Hordes);
        var result = BundleTemplateLoader.Load(document, ModuleRegistry.CreateDefault(), 33);
        Assert.Equal(document.Templates.Count,
            result.Report.TemplatesLoaded + result.Report.TemplatesFailed.Count);
        _output.WriteLine(
            $"complete bundle: templates={document.Templates.Count} loaded={result.Report.TemplatesLoaded} " +
            $"failed={result.Report.TemplatesFailed.Count} weapons={document.Weapons.Count} " +
            $"armors={document.Armors.Count} locomotors={document.Locomotors.Count} hordes={document.Hordes.Count}");
    }

    [Fact]
    public void CorpusLoadsAtScaleWritesReportAndTwinLoadsAreIdentical()
    {
        var corpusPath = MatchLaunchTests.RepoPath(
            "workspace", "logs", "lane-cook", "corpus-bundle.json");
        if (!File.Exists(corpusPath))
        {
            _output.WriteLine($"SKIP: corpus bundle absent at {corpusPath}; CI has no workspace corpus");
            return;
        }

        var stopwatch = Stopwatch.StartNew();
        var document = BundleDocument.Load(corpusPath);
        Assert.Equal(4665, document.Templates.Count);
        var first = BundleTemplateLoader.Load(document, ModuleRegistry.CreateDefault(), 33);
        stopwatch.Stop();
        var second = BundleTemplateLoader.Load(
            BundleDocument.Load(corpusPath), ModuleRegistry.CreateDefault(), 33);

        Assert.Equal(document.Templates.Count,
            first.Report.TemplatesLoaded + first.Report.TemplatesFailed.Count);
        Assert.Equal(JsonSerializer.Serialize(first.Report), JsonSerializer.Serialize(second.Report));

        var reportPath = MatchLaunchTests.RepoPath(
            "workspace", "logs", "lane-kernel-4", "corpus-load-report.json");
        Directory.CreateDirectory(Path.GetDirectoryName(reportPath)!);
        File.WriteAllText(reportPath, JsonSerializer.Serialize(first.Report,
            new JsonSerializerOptions { WriteIndented = true }));
        _output.WriteLine(
            $"corpus bundle: loaded={first.Report.TemplatesLoaded} failed={first.Report.TemplatesFailed.Count} " +
            $"unresolved={string.Join(',', first.Report.UnresolvedReferences.Select(pair => $"{pair.Key}:{pair.Value.Count}"))} " +
            $"gaps={string.Join(',', first.Report.GapRowsByType.Select(pair => $"{pair.Key}:{pair.Value}"))} " +
            $"elapsed_ms={stopwatch.ElapsedMilliseconds} report={reportPath}");
    }

    private static string FixturePath() => MatchLaunchTests.RepoPath(
        "contracts", "fixtures", "bundle-v1.json");

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
