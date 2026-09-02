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
        // The shared golden fixture grows whenever a cook lane adds a table, so
        // compare the reader's counts against the fixture itself, not constants.
        using var raw = System.Text.Json.JsonDocument.Parse(
            File.ReadAllBytes(MatchLaunchTests.RepoPath("contracts", "fixtures", "bundle-v1.json")));
        var rawRoot = raw.RootElement;
        Assert.Equal(rawRoot.GetProperty("source").GetProperty("paths").GetArrayLength(), document.Source.Paths.Count);
        Assert.Equal(rawRoot.GetProperty("defines").EnumerateObject().Count(), document.Defines.Count);
        Assert.Equal(rawRoot.GetProperty("diagnostics").GetArrayLength(), document.Diagnostics.Count);
        Assert.True(document.Source.Paths.Count >= 6);
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
    public void LoaderReplacesInheritedModuleByTagAndReportsCarrierTieredGapsAndAbsentTables()
    {
        var result = BundleTemplateLoader.Load(
            BundleDocument.Load(FixturePath()), ModuleRegistry.CreateDefault(), tickMilliseconds: 33);

        Assert.Equal(7, result.Report.TemplatesLoaded);
        Assert.Empty(result.Report.TemplatesFailed);
        Assert.Contains("unknown Body carriers are Structural", result.Report.ModuleTierPolicy,
            StringComparison.Ordinal);
        Assert.Contains(result.Report.Gaps,
            gap => gap.Template == "CookMystery" && gap.Type == "MadeUpBehavior"
                && gap.Carrier == "Behavior");
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
    public void AuthoredRemoveReplaceAndAddModuleDirectivesAreHonoredInOrder()
    {
        var document = BundleDocument.Parse(MinimalBundle("""
            {"name":"Parent","kind":"object","parent":null,"kindof":[],"geometry":{},"fields":{},"blocks":[],
             "modules":[
               {"carrier":"Body","type":"ActiveBody","tag":"ModuleTag_Body","fields":{"MaxHealth":100},"blocks":[],"gap":false},
               {"carrier":"Behavior","type":"ProductionUpdate","tag":"ModuleTag_Production","fields":{"QueueMax":1},"blocks":[],"gap":false},
               {"carrier":"Draw","type":"W3DScriptedModelDraw","tag":"ModuleTag_Draw","fields":{},"blocks":[],"gap":false}]},
            {"name":"Child","kind":"child","parent":"Parent","kindof":[],"geometry":{},"fields":{},"blocks":[],
             "modules":[
               {"carrier":"Behavior","type":"DestroyDie","tag":"ModuleTag_Destroy","fields":{"RemoveModule":"ModuleTag_Draw"},"blocks":[],"gap":false},
               {"carrier":"Body","type":"ActiveBody","tag":"ModuleTag_NewBody","fields":{"ReplaceModule":"ModuleTag_Body","MaxHealth":200},"blocks":[],"gap":false},
               {"carrier":"Behavior","type":"ProductionUpdate","tag":"ModuleTag_Production","fields":{"AddModule":true,"QueueMax":4},"blocks":[],"gap":false}]}
            """));

        var result = BundleTemplateLoader.Load(document, ModuleRegistry.CreateDefault(), 33);
        var child = Assert.Single(result.Templates, template => template.Name == "Child");

        Assert.DoesNotContain(child.Modules, module => module.Tag == "ModuleTag_Draw");
        var body = Assert.Single(child.Modules, module => module.TypeName == ActiveBodyModule.TypeName);
        Assert.Equal("ModuleTag_NewBody", body.Tag);
        Assert.Equal(200, body.GetLong("MaxHealth", -1));
        Assert.Equal(2, child.Modules.Count(module => module.TypeName == ProductionModule.TypeName));
        Assert.Contains(child.Modules,
            module => module.TypeName == DestroyDieModule.TypeName && module.Tag == "ModuleTag_Destroy");
        Assert.DoesNotContain(result.Report.Notes,
            note => note.Contains("not authored", StringComparison.Ordinal));
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
        var expectedIndices = document.Templates
            .Where(row => loadedNames.Contains(row.Name, StringComparer.Ordinal))
            .Select(row => row.Index)
            .ToArray();
        Assert.Equal(expectedIndices, snapshot.RootElement.GetProperty("objects")
            .GetProperty("template").EnumerateArray().Select(item => item.GetInt32()).ToArray());
    }

    [Fact]
    public void UnknownBodyFailsOnlyItsTemplateWhileUnknownBehaviorIsQueryableGap()
    {
        var document = BundleDocument.Parse(MinimalBundle("""
            {"name":"Bad","kind":"object","parent":null,"kindof":[],"geometry":{},"fields":{},"blocks":[],
             "modules":[{"carrier":"Body","type":"MissingBody","tag":"ModuleTag_Bad","fields":{},"blocks":[],"gap":false}]},
            {"name":"Good","kind":"object","parent":null,"kindof":[],"geometry":{},"fields":{},"blocks":[],
             "modules":[
               {"carrier":"Body","type":"ActiveBody","tag":"ModuleTag_Body","fields":{"MaxHealth":10},"blocks":[],"gap":false},
               {"carrier":"Behavior","type":"FutureModBehavior","tag":"ModuleTag_Mod","fields":{},"blocks":[],"gap":false},
               {"carrier":"FutureCarrier","type":"FutureCarrierModule","tag":"ModuleTag_Future","fields":{},"blocks":[],"gap":false}]}
            """));
        var launch = MatchLaunch.Load(MatchLaunchTests.RepoPath(
            "contracts", "fixtures", "match-launch-v1.json"));

        var world = SimWorld.FromBundle(launch, document);

        var failure = Assert.Single(world.BundleLoadReport!.TemplatesFailed);
        Assert.Equal("Bad", failure.Template);
        Assert.Contains("MissingBody", failure.Reason, StringComparison.Ordinal);
        Assert.Contains("(Body)", failure.Reason, StringComparison.Ordinal);
        Assert.Equal(1, world.BundleLoadReport.TemplatesLoaded);
        Assert.Equal(2, world.BundleLoadReport.Gaps.Count);
        Assert.Contains(new BundleModuleGap("Good", "FutureModBehavior", "Behavior"),
            world.BundleLoadReport.Gaps);
        Assert.Contains(new BundleModuleGap("Good", "FutureCarrierModule", "FutureCarrier"),
            world.BundleLoadReport.Gaps);
        Assert.Equal(1, world.BundleLoadReport.GapRowsByType["FutureModBehavior"]);
        Assert.Throws<KeyNotFoundException>(() => world.SpawnObject("Bad", 0, FixedVector2.Zero));
        world.SpawnObject("Good", 0, FixedVector2.Zero);
    }

    [Fact]
    public void RegisteredImplementationTierOverridesItsCarrierFallback()
    {
        var registry = ModuleRegistry.CreateDefault();
        registry.Register("RegisteredCosmetic", spec => new RegisteredCosmeticModule(spec),
            ModuleTier.Cosmetic);
        var document = BundleDocument.Parse(MinimalBundle("""
            {"name":"Known","kind":"object","parent":null,"kindof":[],"geometry":{},"fields":{},"blocks":[],
             "modules":[
               {"carrier":"Body","type":"RegisteredCosmetic","tag":"ModuleTag_Cosmetic","fields":{},"blocks":[],"gap":false},
               {"carrier":"Behavior","type":"ActiveBody","tag":"ModuleTag_Body","fields":{"MaxHealth":10},"blocks":[],"gap":false}]}
            """));

        var result = BundleTemplateLoader.Load(document, registry, 33);

        var template = Assert.Single(result.Templates);
        Assert.Equal(ModuleTier.Cosmetic,
            Assert.Single(template.Modules, module => module.TypeName == "RegisteredCosmetic").Tier);
        Assert.Equal(ModuleTier.Structural,
            Assert.Single(template.Modules, module => module.TypeName == ActiveBodyModule.TypeName).Tier);
        Assert.Empty(result.Report.Gaps);
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
            "workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");
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
            "workspace", "logs", "lane-kernel-5", "corpus-load-report.json");
        Directory.CreateDirectory(Path.GetDirectoryName(reportPath)!);
        File.WriteAllText(reportPath, JsonSerializer.Serialize(first.Report,
            new JsonSerializerOptions { WriteIndented = true }));
        _output.WriteLine(
            $"corpus bundle: loaded={first.Report.TemplatesLoaded} failed={first.Report.TemplatesFailed.Count} " +
            $"unresolved={string.Join(',', first.Report.UnresolvedReferences.Select(pair => $"{pair.Key}:{pair.Value.Count}"))} " +
            $"gap_rows={first.Report.Gaps.Count} gap_types={first.Report.GapRowsByType.Count} " +
            $"elapsed_ms={stopwatch.ElapsedMilliseconds} report={reportPath}");
    }

    [Fact]
    public void FullCorpusFighterHordesSpawnFightDieAndTwinRunIdentically()
    {
        var corpusPath = FullCorpusPath();
        if (!File.Exists(corpusPath))
        {
            _output.WriteLine($"SKIP: full corpus bundle absent at {corpusPath}; CI has no workspace corpus");
            return;
        }

        var document = BundleDocument.Load(corpusPath);
        var launch = MatchLaunch.Load(MatchLaunchTests.RepoPath(
            "contracts", "fixtures", "match-launch-v1.json"));

        (SimWorld World, string LeftName, string RightName, int LeftMembers, int RightMembers) Build()
        {
            var world = SimWorld.FromBundle(launch, document);
            var loaded = document.Templates
                .Where(row => world.BundleLoadReport!.TemplatesFailed.All(failure => failure.Template != row.Name))
                .Select(row => row.Name)
                .ToHashSet(StringComparer.OrdinalIgnoreCase);
            var left = SelectHorde(document, loaded, "GondorFighterHorde");
            var right = SelectHorde(document, loaded, "MordorFighterHorde");
            var leftMembers = SpawnHorde(world, document, left, 0, 100_000, Fixed64.FromInt(20));
            var rightMembers = SpawnHorde(world, document, right, 1, 100_001, Fixed64.FromInt(80));
            Assert.True(world.SubmitCommand(AttackMove(100_000, 0)));
            Assert.True(world.SubmitCommand(AttackMove(100_001, 1)));
            return (world, left.Name, right.Name, leftMembers, rightMembers);
        }

        var first = Build();
        var second = Build();
        Assert.Equal(first.LeftName, second.LeftName);
        Assert.Equal(first.RightName, second.RightName);
        Assert.Equal(first.LeftMembers, Assert.Single(first.World.Hordes, horde => horde.Id == 100_000).Members.Count);
        Assert.Equal(first.RightMembers, Assert.Single(first.World.Hordes, horde => horde.Id == 100_001).Members.Count);

        var damageEvents = 0;
        var deathEvents = 0;
        for (var tick = 1; tick <= 600; tick++)
        {
            first.World.Tick();
            second.World.Tick();
            damageEvents += first.World.EventsThisTick.Count(value => value.Kind == "damage");
            deathEvents += first.World.EventsThisTick.Count(value => value.Kind == "death");
            Assert.Equal(first.World.StateHash(), second.World.StateHash());
        }

        _output.WriteLine($"retail spawn proof: left={first.LeftName} members={first.LeftMembers} " +
            $"right={first.RightName} members={first.RightMembers} damage_events={damageEvents} " +
            $"death_events={deathEvents} ticks=600 twin_hash_equal=true");
        foreach (var horde in first.World.Hordes)
        {
            var member = first.World.Objects[horde.Members[0]];
            var locomotor = member.FindModule<LocomotorModule>();
            _output.WriteLine($"retail horde state: id={horde.Id} first_member={member.TemplateName} " +
                $"position={member.Position.X},{member.Position.Y} weapons={member.Template.WeaponSets.Count} " +
                $"locomotor={(locomotor == null ? "none" : locomotor.DataForTick(first.World.TickMilliseconds).Speed.ToString())}");
        }
        foreach (var diagnostic in first.World.Diagnostics.Take(10))
            _output.WriteLine($"retail diagnostic: {diagnostic.Code} {diagnostic.Message}");
        Assert.True(damageEvents > 0, "retail hordes produced no damage events");
        Assert.True(deathEvents > 0, "retail hordes produced no death events");
    }

    private static BundleHordeRow SelectHorde(
        BundleDocument document,
        IReadOnlySet<string> loaded,
        string preferred)
    {
        var candidates = document.Hordes!
            .Where(horde => loaded.Contains(horde.Name)
                && horde.RankInfo.Count > 0
                && horde.RankInfo.All(rank => loaded.Contains(rank.UnitType)))
            .OrderBy(horde => horde.Name, StringComparer.Ordinal)
            .ToArray();
        return candidates.FirstOrDefault(horde => horde.Name.Equals(preferred, StringComparison.OrdinalIgnoreCase))
            ?? candidates.First();
    }

    private static int SpawnHorde(
        SimWorld world,
        BundleDocument document,
        BundleHordeRow horde,
        int team,
        int hordeId,
        Fixed64 originX)
    {
        var members = new List<int>();
        foreach (var rank in horde.RankInfo.OrderBy(value => value.Rank))
        {
            foreach (var position in rank.Positions)
            {
                var xOffset = Fixed64.FromRaw(position.X.Raw / 10);
                var yOffset = Fixed64.FromRaw(position.Y.Raw / 10);
                var x = team == 0 ? originX + xOffset : originX - xOffset;
                members.Add(world.SpawnObject(rank.UnitType, team,
                    new FixedVector2(x, Fixed64.FromInt(50) + yOffset)).Id);
            }
        }
        var templateIndex = document.Templates.Single(row =>
            row.Name.Equals(horde.Name, StringComparison.OrdinalIgnoreCase)).Index;
        world.AddHorde(new SnapshotHorde(hordeId, team, templateIndex, members, 0));
        return members.Count;
    }

    private static SimCommand AttackMove(int hordeId, int team) =>
        TestWorlds.Command(1, team, 0, "attack_move",
            ("objects", CommandValue.OfLongList(new long[] { hordeId })),
            ("x", CommandValue.OfFixed(Fixed64.FromInt(50))),
            ("y", CommandValue.OfFixed(Fixed64.FromInt(50))));

    private sealed class RegisteredCosmeticModule : ModuleBase
    {
        public RegisteredCosmeticModule(ModuleSpec spec) : base(spec) { }
    }

    private static string FullCorpusPath() => MatchLaunchTests.RepoPath(
        "workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");

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
