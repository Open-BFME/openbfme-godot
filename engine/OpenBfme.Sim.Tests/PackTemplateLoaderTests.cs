using System.Text.Json;
using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class PackTemplateLoaderTests
{
    private static string Document(params string[] rows) =>
        "{\"schema\":\"openbfme.objects\",\"schemaVersion\":0,\"objects\":[" + string.Join(",", rows) + "]}";

    private const string UnitRow = """
        {
          "id": "test.object.fighter",
          "kind": "member",
          "displayName": "Fighter",
          "simulation": { "health": 200, "speed": 55 }
        }
        """;

    private const string StructureRow = """
        {
          "id": "test.object.barracks",
          "kind": "structure",
          "displayName": "Barracks",
          "simulation": { "health": 3000 }
        }
        """;

    private const string BattalionRow = """
        {
          "id": "test.object.fighter-horde",
          "kind": "battalion",
          "displayName": "Fighter Horde",
          "memberCount": 15,
          "commandPoints": 60,
          "memberObjectId": "test.object.fighter"
        }
        """;

    private const string BannerRow = """
        {
          "id": "test.object.fighter-banner",
          "sourceTypeName": "RetailFighterBanner",
          "kind": "member",
          "simulation": { "health": 80, "speed": 50 },
          "gameplay": {
            "bannerCarrierUpdate": {
              "diedRespawnTime": { "milliseconds": 10000, "sourceIni": "banner.ini", "line": 10 },
              "meleeFreeBannerRespawnTime": { "milliseconds": 20000, "sourceIni": "banner.ini", "line": 11 }
            }
          }
        }
        """;

    private const string RankedBannerBattalionRow = """
        {
          "id": "test.object.ranked-horde",
          "sourceTypeName": "RetailRankedHorde",
          "kind": "battalion",
          "memberCount": 15,
          "gameplay": {
            "bannerCarrier": {
              "allowedObjectIds": ["RetailFighterBanner"],
              "positions": [{ "unitType": "RetailFighter", "x": 70.125, "y": -0.25 }],
              "minLevel": 2,
              "minLevelDefaulted": true,
              "destroyHordeOnBannerDeath": false
            }
          },
          "experience": {
            "status": "compiled",
            "initialRank": 1,
            "maxLevel": 3,
            "levels": [
              { "rank": 1, "requiredExperience": 1 },
              { "rank": 2, "requiredExperience": 50 },
              { "rank": 3, "requiredExperience": 100 }
            ]
          }
        }
        """;

    private const string CastlePieceRow = """
        {
          "id": "test.object.retail-pad",
          "sourceTypeName": "RetailPad",
          "kind": "structure",
          "simulation": { "health": 500 }
        }
        """;

    private const string FortressRow = """
        {
          "id": "test.object.fortress",
          "kind": "structure",
          "simulation": { "health": 5000 },
          "gameplay": {
            "castleBehavior": {
              "faction": "Dwarves",
              "castleTemplateToken": "Fortress_Dwarven",
              "pieces": [{
                "index": 0,
                "objectId": "RetailPad",
                "offset": [94.800048828125, -61.75, 0.5],
                "offsetRawQ32": [407163109376, -265214230528, 2147483648],
                "angleRadians": 0.7853981852531433,
                "angleRadiansRawQ32": 3373259520,
                "priority": 40,
                "phase": 1
              }]
            }
          }
        }
        """;

    [Fact]
    public void UnitRowMapsToActiveBodyAndMover()
    {
        var result = PackTemplateLoader.LoadFromObjectsDocument(Document(UnitRow));

        var template = Assert.Single(result.Templates);
        Assert.Equal("test.object.fighter", template.Name);
        Assert.Equal(2, template.Modules.Count);

        var body = template.Modules[0];
        Assert.Equal(ActiveBodyModule.TypeName, body.TypeName);
        Assert.Equal(200, body.GetLong("MaxHealth", -1));
        Assert.Equal("Fighter", body.GetString("DisplayName", ""));

        var mover = template.Modules[1];
        Assert.Equal(LinearMoverModule.TypeName, mover.TypeName);
        Assert.Equal(Fixed64.FromFraction(55, SimWorld.TicksPerSecond).Raw, mover.GetLong("SpeedPerTickRaw", -1));

        Assert.Empty(result.Report.SkippedRows);
    }

    [Fact]
    public void StructureRowMapsToStructureBodyWithoutMover()
    {
        var result = PackTemplateLoader.LoadFromObjectsDocument(Document(StructureRow));

        var template = Assert.Single(result.Templates);
        var body = Assert.Single(template.Modules);
        Assert.Equal(StructureBodyModule.TypeName, body.TypeName);
        Assert.Equal(3000, body.GetLong("MaxHealth", -1));
    }

    [Fact]
    public void RetailCastlePiecesMapExactTransformAndExecuteInSimulation()
    {
        var result = PackTemplateLoader.LoadFromObjectsDocument(
            Document(FortressRow, CastlePieceRow));

        Assert.Empty(result.Report.SkippedRows);
        var fortressTemplate = result.Templates.Single(
            template => template.Name == "test.object.fortress");
        var castleSpec = Assert.Single(
            fortressTemplate.Modules,
            module => module.TypeName == CastleBehaviorModule.TypeName);
        Assert.Equal(1, castleSpec.GetLong("PieceCount", -1));
        Assert.Equal("test.object.retail-pad", castleSpec.GetString("PieceTemplate:0", ""));
        Assert.Equal(
            Fixed64.FromFraction(6212816, 65536).Raw,
            castleSpec.GetLong("OffsetXRaw:0", 0));
        Assert.Equal(
            Fixed64.FromFraction(-247, 4).Raw,
            castleSpec.GetLong("OffsetYRaw:0", 0));
        Assert.Equal(
            Fixed64.FromFraction(1, 2).Raw,
            castleSpec.GetLong("OffsetZRaw:0", 0));
        Assert.Equal(
            3373259520,
            castleSpec.GetLong("AngleRadiansRaw:0", 0));

        var world = new SimWorld(
            new SimConfig(result.Templates, 19, 2), ModuleRegistry.CreateDefault());
        world.SpawnObject(
            fortressTemplate.Name,
            0,
            FixedVector2.Zero,
            Fixed64.FromInt(2),
            Fixed64.FromFraction(1, 8));
        world.Tick();
        var pad = Assert.Single(
            world.Objects.Values,
            value => value.TemplateName == "test.object.retail-pad");
        Assert.Equal(Fixed64.FromFraction(6212816, 65536), pad.Position.X);
        Assert.Equal(Fixed64.FromFraction(-247, 4), pad.Position.Y);
        Assert.Equal(Fixed64.FromFraction(5, 2), pad.Elevation);
        Assert.Equal(
            Fixed64.FromFraction(1, 8)
                + Fixed64.FromRaw(3373259520),
            pad.HeadingRadians);
    }

    [Fact]
    public void RetailCastleMissingPieceTargetSkipsFortressFailClosed()
    {
        var result = PackTemplateLoader.LoadFromObjectsDocument(Document(FortressRow));

        Assert.Empty(result.Templates);
        var skipped = Assert.Single(result.Report.SkippedRows);
        Assert.Equal(RowSkipReason.InvalidGameplayField, skipped.Reason);
        Assert.Contains("target", skipped.Detail, StringComparison.Ordinal);
    }

    [Fact]
    public void BattalionRowMapsMemberCountAndCommandPoints()
    {
        var result = PackTemplateLoader.LoadFromObjectsDocument(Document(BattalionRow));

        var template = Assert.Single(result.Templates);
        var body = Assert.Single(template.Modules);
        Assert.Equal(ActiveBodyModule.TypeName, body.TypeName);
        Assert.Equal(15, body.GetLong("MemberCount", -1));
        Assert.Equal(60, body.GetLong("CommandPoints", -1));
        Assert.Equal("test.object.fighter", body.GetString("MemberObjectId", ""));
        // No health field -> module default, spelled out in the report.
        Assert.Contains(result.Report.Notes, note => note.StartsWith("test.object.fighter-horde:", StringComparison.Ordinal));
    }

    [Fact]
    public void GameplayBannerAndExperienceMapToExecutableModuleSpecs()
    {
        var result = PackTemplateLoader.LoadFromObjectsDocument(
            Document(RankedBannerBattalionRow, BannerRow));

        Assert.Equal(2, result.Templates.Count);
        var horde = result.Templates.Single(template => template.Name == "test.object.ranked-horde");
        var experience = Assert.Single(horde.Modules, module => module.TypeName == ExperienceLevelModule.TypeName);
        Assert.Equal(3, experience.GetLong("LevelCap", -1));
        Assert.Equal(1, experience.GetLong("InitialLevel", -1));
        Assert.Equal(50, experience.GetLong("RequiredExperience:2", -1));

        var banner = Assert.Single(horde.Modules, module => module.TypeName == BannerCarrierModule.TypeName);
        Assert.Equal("test.object.fighter-banner", banner.GetString("BannerTemplate", ""));
        Assert.Equal(2, banner.GetLong("MinLevel", -1));
        Assert.Equal(Fixed64.FromFraction(561, 8).Raw, banner.GetLong("OffsetXRaw", 0));
        Assert.Equal(Fixed64.FromFraction(-1, 4).Raw, banner.GetLong("OffsetYRaw", 0));
        Assert.Equal(0, banner.GetLong("DestroyHordeOnBannerDeath", -1));
        Assert.Equal(600, banner.GetLong("RespawnTicks", -1));
        Assert.Empty(result.Report.SkippedRows);

        var world = new SimWorld(new SimConfig(result.Templates, 17, 2), ModuleRegistry.CreateDefault());
        var objectInstance = world.SpawnObject(horde.Name, 0, FixedVector2.Zero);
        objectInstance.FindModule<ExperienceLevelModule>()!.GrantExperience(50);
        world.Tick();
        var spawned = Assert.Single(world.Objects.Values, value => value.TemplateName == "test.object.fighter-banner");
        Assert.Equal(Fixed64.FromFraction(561, 8), spawned.Position.X);
        Assert.Equal(Fixed64.FromFraction(-1, 4), spawned.Position.Y);
    }

    [Fact]
    public void BannerTargetMissingFromPackSkipsHordeFailClosed()
    {
        var result = PackTemplateLoader.LoadFromObjectsDocument(Document(RankedBannerBattalionRow));

        Assert.Empty(result.Templates);
        var skipped = Assert.Single(result.Report.SkippedRows);
        Assert.Equal(RowSkipReason.InvalidGameplayField, skipped.Reason);
        Assert.Contains("target", skipped.Detail, StringComparison.Ordinal);
    }

    [Fact]
    public void DestroyHordeFlagMapsOnlyFromAuthoredBoolean()
    {
        var authoredDestroy = RankedBannerBattalionRow.Replace(
            "\"destroyHordeOnBannerDeath\": false",
            "\"destroyHordeOnBannerDeath\": true",
            StringComparison.Ordinal);
        var result = PackTemplateLoader.LoadFromObjectsDocument(Document(authoredDestroy, BannerRow));

        var horde = result.Templates.Single(template => template.Name == "test.object.ranked-horde");
        var banner = Assert.Single(horde.Modules, module => module.TypeName == BannerCarrierModule.TypeName);
        Assert.Equal(1, banner.GetLong("DestroyHordeOnBannerDeath", 0));
    }

    [Fact]
    public void MalformedAuthoredBannerRespawnSkipsBannerAndDependentHordeFailClosed()
    {
        var malformedBanner = BannerRow.Replace(
            "\"milliseconds\": 10000",
            "\"milliseconds\": \"ten seconds\"",
            StringComparison.Ordinal);
        var result = PackTemplateLoader.LoadFromObjectsDocument(
            Document(RankedBannerBattalionRow, malformedBanner));

        Assert.Empty(result.Templates);
        Assert.Equal(2, result.Report.SkippedRows.Count);
        Assert.All(result.Report.SkippedRows,
            skipped => Assert.Equal(RowSkipReason.InvalidGameplayField, skipped.Reason));
    }

    [Fact]
    public void FractionalSpeedIsExactRationalNotFloat()
    {
        var row = """{ "id": "test.object.creep", "kind": "member", "simulation": { "health": 10, "speed": 1.15 } }""";
        var result = PackTemplateLoader.LoadFromObjectsDocument(Document(row));

        var template = Assert.Single(result.Templates);
        var mover = template.Modules[1];
        // 1.15 units/second == 115/100 -> per tick 115 / (100 * 30) == 23/600, exactly.
        Assert.Equal(Fixed64.FromFraction(23, 600).Raw, mover.GetLong("SpeedPerTickRaw", 0));
    }

    [Fact]
    public void UnknownFieldsAreEnumeratedNeverSilent()
    {
        var row = """
            {
              "id": "test.object.odd",
              "kind": "member",
              "presentation": { "model": "x.glb" },
              "someFutureField": true,
              "simulation": { "health": 10, "vision": 175, "cost": 200 }
            }
            """;
        var result = PackTemplateLoader.LoadFromObjectsDocument(Document(row));

        Assert.Single(result.Templates);
        var unmapped = result.Report.UnmappedFields;
        Assert.Equal(1, unmapped["presentation"]);
        Assert.Equal(1, unmapped["someFutureField"]);
        Assert.Equal(1, unmapped["simulation.vision"]);
        Assert.Equal(1, unmapped["simulation.cost"]);
    }

    [Fact]
    public void MalformedRowsAreSkippedWithTypedReasons()
    {
        var result = PackTemplateLoader.LoadFromObjectsDocument(Document(
            "42",                                                                          // not an object
            """{ "kind": "member" }""",                                                    // no id
            """{ "id": "test.object.nokind" }""",                                          // no kind
            """{ "id": "test.object.weird", "kind": "hologram" }""",                       // unknown kind
            """{ "id": "test.object.badhp", "kind": "member", "simulation": { "health": 1.5 } }""",
            UnitRow,
            UnitRow));                                                                     // duplicate id

        Assert.Single(result.Templates);
        Assert.Equal(6, result.Report.SkippedRows.Count);
        Assert.Equal(RowSkipReason.NotAnObject, result.Report.SkippedRows[0].Reason);
        Assert.Equal(RowSkipReason.MissingId, result.Report.SkippedRows[1].Reason);
        Assert.Equal(RowSkipReason.MissingKind, result.Report.SkippedRows[2].Reason);
        Assert.Equal(RowSkipReason.UnknownKind, result.Report.SkippedRows[3].Reason);
        Assert.Equal(RowSkipReason.InvalidNumericField, result.Report.SkippedRows[4].Reason);
        Assert.Equal(RowSkipReason.DuplicateId, result.Report.SkippedRows[5].Reason);
        Assert.Equal(6, result.Report.SkippedRows[5].Index);
    }

    [Fact]
    public void UnusableDocumentThrowsTypedError()
    {
        Assert.Throws<PackObjectsDocumentException>(() => PackTemplateLoader.LoadFromObjectsDocument("not json"));
        Assert.Throws<PackObjectsDocumentException>(() => PackTemplateLoader.LoadFromObjectsDocument("[]"));
        Assert.Throws<PackObjectsDocumentException>(() => PackTemplateLoader.LoadFromObjectsDocument("{\"objects\":7}"));
        Assert.Throws<PackObjectsDocumentException>(
            () => PackTemplateLoader.LoadFromObjectsDocument("{\"schema\":\"wrong.schema\",\"objects\":[]}"));
    }

    [Fact]
    public void LoadingTwiceYieldsHashIdenticalTwinRunsOver500Ticks()
    {
        var json = Document(UnitRow, StructureRow, BattalionRow);
        var hashA = RunWorld(PackTemplateLoader.LoadFromObjectsDocument(json).Templates, ticks: 500);
        var hashB = RunWorld(PackTemplateLoader.LoadFromObjectsDocument(json).Templates, ticks: 500);
        Assert.Equal(hashA, hashB);
    }

    [Fact]
    public void RealPackObjectsLoadSpawnAndStayDeterministicOver300Ticks()
    {
        var objectsJsonPath = FindActivePackObjectsJson();
        if (objectsJsonPath == null)
        {
            return; // pack bundle absent on this machine — integration coverage skipped
        }
        var json = File.ReadAllText(objectsJsonPath);
        var result = PackTemplateLoader.LoadFromObjectsDocument(json);

        Assert.True(result.Templates.Count > 0, "real pack produced no templates");
        Assert.Empty(result.Report.SkippedRows);

        var hashA = RunWorld(result.Templates, ticks: 300);
        var hashB = RunWorld(PackTemplateLoader.LoadFromObjectsDocument(json).Templates, ticks: 300);
        Assert.Equal(hashA, hashB);
    }

    /// <summary>Spawns one of each template, nudges every mover, advances, returns the state hash.</summary>
    private static string RunWorld(IReadOnlyList<ObjectTemplate> templates, int ticks)
    {
        var config = new SimConfig(templates, randomSeed: 2026, teamCount: 2);
        var world = new SimWorld(config, ModuleRegistry.CreateDefault());
        var slot = 0;
        foreach (var template in templates)
        {
            var spawned = world.SpawnObject(template.Name, slot % 2,
                new FixedVector2(Fixed64.FromInt(slot * 10), Fixed64.FromInt(slot * 7)));
            spawned.FindModule<LinearMoverModule>()?.SetDestination(
                new FixedVector2(Fixed64.FromInt(slot * 10 + 900), Fixed64.FromInt(slot * 7 + 400)));
            slot++;
        }
        world.Advance(ticks);
        return world.StateHash();
    }

    /// <summary>
    /// Walks up from the test bin dir to the repo root, reads
    /// .private/content-packs/selection.json, and resolves the active pack's
    /// data/objects.json. Returns null (test skips) when anything is absent.
    /// </summary>
    private static string? FindActivePackObjectsJson()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            var selectionPath = Path.Combine(dir.FullName, ".private", "content-packs", "selection.json");
            if (File.Exists(selectionPath))
            {
                using var selection = JsonDocument.Parse(File.ReadAllText(selectionPath));
                if (!selection.RootElement.TryGetProperty("activePack", out var activePack)
                    || activePack.ValueKind != JsonValueKind.String)
                {
                    return null;
                }
                var objectsPath = Path.Combine(
                    dir.FullName, ".private", "content-packs",
                    activePack.GetString()!.Replace('/', Path.DirectorySeparatorChar),
                    "data", "objects.json");
                return File.Exists(objectsPath) ? objectsPath : null;
            }
            dir = dir.Parent;
        }
        return null;
    }
}
