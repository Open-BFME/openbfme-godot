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
