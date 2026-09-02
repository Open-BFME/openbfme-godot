using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Nodes;
using OpenBfme.Sim.Map;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

[Collection(TickBudgetCollection.Name)]
public sealed class MapDocumentCorpusTests
{
    private readonly ITestOutputHelper _output;

    public MapDocumentCorpusTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void StartlessScenarioDocumentLoadsWithoutInventingAStart()
    {
        var fixture = JsonNode.Parse(File.ReadAllText(RepoPath(
            "contracts", "fixtures", "map-v1.json")))!.AsObject();
        fixture["start_positions"] = new JsonObject();
        fixture["plots"] = new JsonArray();

        var map = MapDocument.Parse(fixture.ToJsonString());

        Assert.Empty(map.StartPositions);
        Assert.Empty(map.Plots);
    }

    [Fact]
    public void EveryNativeSelectionMapLoadsAndBuildsAWorld()
    {
        var selectionPath = RepoPath("workspace", "content-packs", "native", "selection.json");
        if (!File.Exists(selectionPath))
        {
            _output.WriteLine($"SKIP: native selection is absent: {selectionPath}");
            return;
        }

        var contentRoot = Directory.GetParent(Path.GetDirectoryName(selectionPath)!)!.FullName;
        using var selection = JsonDocument.Parse(File.ReadAllText(selectionPath));
        var selectionRoot = selection.RootElement;
        var bundleRelative = selectionRoot.GetProperty("bundle").GetString()!;
        var bundlePath = ContentPath(contentRoot, bundleRelative);
        var bundle = BundleDocument.Load(bundlePath);
        var rows = new List<Dictionary<string, object?>>();
        var failures = new List<string>();
        var totalAuthored = 0;
        var totalSpawned = 0;
        var totalUnknown = 0;
        var totalStarts = 0;
        var totalPlots = 0;
        var totalLoadMilliseconds = 0d;
        var mapFilter = Environment.GetEnvironmentVariable("OPENBFME_MAP_LOAD_FILTER");

        foreach (var selectionMap in selectionRoot.GetProperty("maps").EnumerateArray())
        {
            var relative = selectionMap.GetProperty("path").GetString()!;
            if (!string.IsNullOrWhiteSpace(mapFilter)
                && !relative.Contains(mapFilter, StringComparison.OrdinalIgnoreCase)) continue;
            var row = new Dictionary<string, object?>
            {
                ["name"] = selectionMap.GetProperty("name").GetString(),
                ["slug"] = selectionMap.GetProperty("slug").GetString(),
                ["kind"] = selectionMap.GetProperty("kind").GetString(),
                ["players"] = selectionMap.GetProperty("players").GetInt32(),
                ["mapV1"] = relative,
            };
            var stopwatch = Stopwatch.StartNew();
            try
            {
                var map = MapDocument.Load(ContentPath(contentRoot, relative));
                var world = MapWorldBuilder.Build(LaunchFor(map), bundle, map);
                stopwatch.Stop();
                var report = world.MapLoadReport!;
                var unknown = report.UnknownTemplates.Values.Sum();
                var elapsed = Math.Round(stopwatch.Elapsed.TotalMilliseconds, 3);
                row["status"] = "ok";
                row["source"] = map.Source.Path;
                row["grid"] = new Dictionary<string, object>
                {
                    ["width"] = map.PassabilityGrid.Width,
                    ["height"] = map.PassabilityGrid.Height,
                    ["cellSize"] = map.World.CellSize,
                };
                row["objects"] = new Dictionary<string, object>
                {
                    ["authored"] = report.MapObjectCount,
                    ["spawned"] = report.SpawnedObjectCount,
                    ["unknown"] = unknown,
                };
                row["starts"] = map.StartPositions.Count;
                row["plots"] = map.Plots.Count;
                row["loadMs"] = elapsed;
                totalAuthored += report.MapObjectCount;
                totalSpawned += report.SpawnedObjectCount;
                totalUnknown += unknown;
                totalStarts += map.StartPositions.Count;
                totalPlots += map.Plots.Count;
                totalLoadMilliseconds += elapsed;
            }
            catch (Exception exception)
            {
                stopwatch.Stop();
                var root = RootException(exception);
                row["status"] = "failed";
                row["failureClass"] = root.GetType().Name;
                row["message"] = root.Message;
                row["loadMs"] = Math.Round(stopwatch.Elapsed.TotalMilliseconds, 3);
                failures.Add($"{relative}: {root.GetType().Name}: {root.Message}");
            }
            rows.Add(row);
        }

        var reportPath = RepoPath("workspace", "logs", "lane-maps-sweep", "map-load-report.json");
        Directory.CreateDirectory(Path.GetDirectoryName(reportPath)!);
        var output = new Dictionary<string, object?>
        {
            ["schema"] = "openbfme.native-map-load-report",
            ["schemaVersion"] = 1,
            ["selection"] = selectionPath,
            ["bundle"] = bundlePath,
            ["attempted"] = rows.Count,
            ["loaded"] = rows.Count - failures.Count,
            ["failed"] = failures.Count,
            ["totals"] = new Dictionary<string, object>
            {
                ["authoredObjects"] = totalAuthored,
                ["spawnedObjects"] = totalSpawned,
                ["unknownObjects"] = totalUnknown,
                ["starts"] = totalStarts,
                ["plots"] = totalPlots,
                ["loadMs"] = Math.Round(totalLoadMilliseconds, 3),
            },
            ["maps"] = rows,
        };
        File.WriteAllText(
            reportPath,
            JsonSerializer.Serialize(output, new JsonSerializerOptions { WriteIndented = true }) + "\n");
        _output.WriteLine(
            $"native maps attempted={rows.Count} loaded={rows.Count - failures.Count} failed={failures.Count} " +
            $"authored_objects={totalAuthored} spawned={totalSpawned} unknown={totalUnknown} " +
            $"starts={totalStarts} plots={totalPlots} load_ms={totalLoadMilliseconds:F3} report={reportPath}");

        Assert.True(failures.Count == 0, string.Join(Environment.NewLine, failures));
    }

    private static Exception RootException(Exception exception)
    {
        while (exception.InnerException is not null) exception = exception.InnerException;
        return exception;
    }

    private static MatchLaunch LaunchFor(MapDocument map) => new(
        MatchLaunch.SchemaName,
        1,
        new MatchLaunchPack("native-corpus", new string('0', 64)),
        new MatchLaunchMap(map.Source.Path, map.Source.Sha256),
        new MatchLaunchRules(
            33,
            1_000,
            Fixed64.One,
            false,
            Fixed64.One,
            "annihilation",
            false,
            new Dictionary<string, bool>()),
        new[]
        {
            new MatchLaunchPlayer(
                0, 0, "FactionMen", "human", null, null, null, null, null, "Corpus Loader"),
        },
        "skirmish",
        null);

    private static string ContentPath(string contentRoot, string relative) =>
        Path.GetFullPath(Path.Combine(
            contentRoot,
            relative.Replace('/', Path.DirectorySeparatorChar)));

    private static string RepoPath(params string[] parts) =>
        Path.Combine(new[] { AppContext.BaseDirectory, "..", "..", "..", "..", ".." }
            .Concat(parts).ToArray());
}
