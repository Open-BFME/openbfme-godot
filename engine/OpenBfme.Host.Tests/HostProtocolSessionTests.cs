using System.Text.Json;
using System.Text.Json.Nodes;
using OpenBfme.Host;
using Xunit;

namespace OpenBfme.Host.Tests;

public sealed class HostProtocolSessionTests : IDisposable
{
    private readonly string _temporaryDirectory = Path.Combine(
        Path.GetTempPath(), "openbfme-host-tests-" + Guid.NewGuid().ToString("N"));
    private readonly string _templatesPath;
    private readonly string _matchJson;

    public HostProtocolSessionTests()
    {
        Directory.CreateDirectory(_temporaryDirectory);
        _templatesPath = Path.Combine(_temporaryDirectory, "objects.json");
        File.WriteAllText(_templatesPath, TemplatesJson);
        _matchJson = File.ReadAllText(RepoPath("contracts", "fixtures", "match-launch-v1.json"));
    }

    [Fact]
    public void FullSessionPublishesContractSnapshotsAndRestoresCanonicalState()
    {
        var session = new HostProtocolSession();
        Assert.Equal("launched", Op(Single(session.HandleLine(LaunchLine()))));

        var ack = Single(session.HandleLine(CommandsLine()));
        Assert.Equal("ack", Op(ack));
        Assert.Equal(1, Parse(ack).RootElement.GetProperty("tick").GetInt32());

        var snapshots = session.HandleLine("{\"op\":\"step\",\"ticks\":3}");
        Assert.Equal(3, snapshots.Count);
        var required = SnapshotRequiredKeys();
        for (var index = 0; index < snapshots.Count; index++)
        {
            using var reply = Parse(snapshots[index]);
            Assert.Equal("snapshot", reply.RootElement.GetProperty("op").GetString());
            var snapshot = reply.RootElement.GetProperty("snapshot");
            foreach (var key in required)
            {
                Assert.True(snapshot.TryGetProperty(key, out _), key);
            }
            Assert.Equal(index + 1, snapshot.GetProperty("tick").GetInt32());
            var count = snapshot.GetProperty("object_count").GetInt32();
            foreach (var property in snapshot.GetProperty("objects").EnumerateObject())
            {
                Assert.Equal(count, property.Value.GetArrayLength());
            }
        }

        var hashBefore = Single(session.HandleLine("{\"op\":\"hash\"}"));
        Assert.Equal("hash", Op(hashBefore));
        var save = Single(session.HandleLine("{\"op\":\"save\"}"));
        var state = Parse(save).RootElement.GetProperty("state").GetString()!;
        _ = session.HandleLine("{\"op\":\"step\",\"ticks\":2}");
        var load = Single(session.HandleLine(JsonSerializer.Serialize(new { op = "load", state })));
        Assert.Equal("loaded", Op(load));
        Assert.Equal(hashBefore, Single(session.HandleLine("{\"op\":\"hash\"}")));

        Assert.Equal("quit", Op(Single(session.HandleLine("{\"op\":\"quit\"}"))));
        Assert.False(session.IsRunning);
    }

    [Fact]
    public void PackedStepUsesTheSameTickAndHashAsTheJsonProtocolPath()
    {
        var jsonSession = new HostProtocolSession();
        var packedSession = new HostProtocolSession();
        Assert.Equal("launched", Op(Single(jsonSession.HandleLine(LaunchLine()))));
        Assert.Equal("launched", Op(Single(packedSession.HandleLine(LaunchLine()))));

        using var jsonReply = Parse(Single(jsonSession.HandleLine("{\"op\":\"step\",\"ticks\":1}")));
        using var packedReply = Parse(Single(packedSession.HandleLine(
            "{\"op\":\"step\",\"ticks\":1,\"format\":\"packed\"}")));
        var json = jsonReply.RootElement.GetProperty("snapshot");
        var packed = packedReply.RootElement.GetProperty("snapshot");
        Assert.Equal(json.GetProperty("tick").GetInt32(), packed.GetProperty("tick").GetInt32());
        Assert.Equal(json.GetProperty("hash").GetString(), packed.GetProperty("hash").GetString());
        Assert.True(packed.GetProperty("objects").GetProperty("full").GetBoolean());
        Assert.Equal("packed", packedReply.RootElement.GetProperty("format").GetString());
        PackedSnapshotProtocolTests.AssertObjectColumnsEqual(json, packed);
    }

    [Fact]
    public void TwinSessionsProduceByteIdenticalProtocolOutput()
    {
        Assert.Equal(RunTranscript(), RunTranscript());
    }

    [Fact]
    public void MalformedInputReturnsErrorAndSessionSurvives()
    {
        var session = new HostProtocolSession();
        Assert.Equal("error", Op(Single(session.HandleLine("{"))));
        Assert.True(session.IsRunning);
        Assert.Equal("launched", Op(Single(session.HandleLine(LaunchLine()))));
        Assert.Equal("hash", Op(Single(session.HandleLine("{\"op\":\"hash\"}"))));
    }

    [Fact]
    public void BundleLaunchUsesFixtureAndReportsDeterministicAccounting()
    {
        var session = new HostProtocolSession();
        var launched = Single(session.HandleLine(BundleLaunchLine()));
        using var reply = Parse(launched);
        Assert.Equal("launched", reply.RootElement.GetProperty("op").GetString());
        Assert.Equal(33, reply.RootElement.GetProperty("tick_ms").GetInt32());
        Assert.Equal(2, reply.RootElement.GetProperty("players").GetInt32());
        Assert.Equal(7, reply.RootElement.GetProperty("templates_loaded").GetInt32());
        Assert.Equal(0, reply.RootElement.GetProperty("templates_failed").GetInt32());
        Assert.True(session.IsRunning);
    }

    [Fact]
    public void BundleSessionListsLoadedTemplatesAndSpawnsAuthoredHorde()
    {
        var session = new HostProtocolSession();
        Assert.Equal("launched", Op(Single(session.HandleLine(BundleLaunchLine()))));

        using var catalog = Parse(Single(session.HandleLine("{\"op\":\"templates\"}")));
        var templates = catalog.RootElement.GetProperty("templates").EnumerateArray().ToArray();
        var horde = Assert.Single(templates, row => row.GetProperty("name").GetString() == "CookHorde");
        Assert.Equal("object", horde.GetProperty("kind").GetString());
        Assert.Equal(JsonValueKind.Array, horde.GetProperty("kindof").ValueKind);

        var spawned = Single(session.HandleLine(
            "{\"op\":\"spawn\",\"template\":\"CookHorde\",\"player\":0,\"x\":1200,\"y\":800}"));
        using var spawnReply = Parse(spawned);
        Assert.Equal("spawned", spawnReply.RootElement.GetProperty("op").GetString());
        Assert.Equal(100_000, spawnReply.RootElement.GetProperty("id").GetInt32());
        Assert.Equal(3, spawnReply.RootElement.GetProperty("members").GetArrayLength());

        using var snapshot = Parse(Single(session.HandleLine("{\"op\":\"step\",\"ticks\":1}")));
        var snapshotRoot = snapshot.RootElement.GetProperty("snapshot");
        Assert.Equal(3, snapshotRoot.GetProperty("object_count").GetInt32());
        Assert.Equal(100_000, snapshotRoot.GetProperty("hordes")[0].GetProperty("id").GetInt32());
    }

    [Fact]
    public void MapLaunchPublishesTerrainStartsPlotsAndSupportsStartSpawn()
    {
        var session = new HostProtocolSession();
        using var launched = Parse(Single(session.HandleLine(MapLaunchLine())));
        Assert.Equal("launched", launched.RootElement.GetProperty("op").GetString());
        var map = launched.RootElement.GetProperty("map");
        Assert.Equal(8, map.GetProperty("grid").GetProperty("width").GetInt32());
        Assert.Equal(6, map.GetProperty("grid").GetProperty("height").GetInt32());
        Assert.Equal(10, map.GetProperty("grid").GetProperty("cell_size").GetInt32());
        Assert.Equal(2, map.GetProperty("start_positions").EnumerateObject().Count());
        Assert.Equal(2, map.GetProperty("plots_per_player").GetProperty("0").GetInt32());
        Assert.Equal(3, map.GetProperty("objects_spawned").GetInt32());
        Assert.Equal(1, map.GetProperty("objects_unknown").GetInt32());

        using var spawned = Parse(Single(session.HandleLine(
            "{\"op\":\"spawn\",\"template\":\"CookHorde\",\"player\":0,\"start\":1}")));
        Assert.Equal("spawned", spawned.RootElement.GetProperty("op").GetString());
        Assert.Equal(3, spawned.RootElement.GetProperty("members").GetArrayLength());
        using var snapshot = Parse(Single(session.HandleLine("{\"op\":\"step\",\"ticks\":1}")));
        var objects = snapshot.RootElement.GetProperty("snapshot").GetProperty("objects");
        var ids = objects.GetProperty("id").EnumerateArray().Select(value => value.GetInt32()).ToArray();
        var memberIds = spawned.RootElement.GetProperty("members").EnumerateArray()
            .Select(value => value.GetInt32()).ToArray();
        var memberSlots = memberIds.Select(id => Array.IndexOf(ids, id)).ToArray();
        Assert.All(memberSlots, slot => Assert.True(slot >= 0));
        var averageX = memberSlots.Average(slot => objects.GetProperty("x")[slot].GetDecimal());
        var averageY = memberSlots.Average(slot => objects.GetProperty("z")[slot].GetDecimal());
        Assert.InRange(averageX, 62m, 68m);
        Assert.InRange(averageY, 42m, 48m);
    }

    [Fact]
    public void CorpusFordsMapLaunchReportsFullMapAccountingWhenPrivateInputsExist()
    {
        var bundle = RepoPath("workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");
        var map = RepoPath("workspace", "logs", "lane-map-scene", "fords.map-v1.json");
        if (!File.Exists(bundle) || !File.Exists(map))
            return; // Private-input skip: the public/clean checkout has neither artifact.

        var session = new HostProtocolSession();
        using var launched = Parse(Single(session.HandleLine(JsonSerializer.Serialize(new
        {
            op = "launch",
            match = JsonSerializer.Deserialize<JsonElement>(_matchJson),
            bundle,
            map,
        }))));
        Assert.Equal("launched", launched.RootElement.GetProperty("op").GetString());
        var report = launched.RootElement.GetProperty("map");
        Assert.Equal(415, report.GetProperty("grid").GetProperty("width").GetInt32());
        Assert.Equal(353, report.GetProperty("grid").GetProperty("height").GetInt32());
        Assert.Equal(10, report.GetProperty("grid").GetProperty("cell_size").GetInt32());
        Assert.Equal(2, report.GetProperty("start_positions").EnumerateObject().Count());
        Assert.True(report.GetProperty("plots_per_player").GetProperty("0").GetInt32() > 0);
        Assert.True(report.GetProperty("plots_per_player").GetProperty("1").GetInt32() > 0);
        Assert.True(report.GetProperty("objects_spawned").GetInt32() > 1_000);
    }

    [Fact]
    public void BundleSpawnUnknownTemplateReturnsErrorAndSessionSurvives()
    {
        var session = new HostProtocolSession();
        Assert.Equal("launched", Op(Single(session.HandleLine(BundleLaunchLine()))));
        var error = Single(session.HandleLine(
            "{\"op\":\"spawn\",\"template\":\"UnknownHorde\",\"player\":0,\"x\":0,\"y\":0}"));
        Assert.Equal("error", Op(error));
        using var parsed = Parse(error);
        Assert.Equal(
            "unknown or unloaded template 'UnknownHorde'",
            parsed.RootElement.GetProperty("message").GetString());
        Assert.Equal("hash", Op(Single(session.HandleLine("{\"op\":\"hash\"}"))));
    }

    [Fact]
    public void RecordedThreeHundredTickReplayVerifiesAndTamperFindsFirstDivergence()
    {
        var replayPath = Path.Combine(_temporaryDirectory, "roundtrip.replay.json");
        var session = new HostProtocolSession();
        Assert.Equal("launched", Op(Single(session.HandleLine(LaunchLine()))));
        Assert.Equal("recording", Op(Single(session.HandleLine(JsonSerializer.Serialize(new
        {
            op = "record",
            path = replayPath,
        })))));
        Assert.Equal("ack", Op(Single(session.HandleLine(CommandLine(1, 0, 200, 40)))));
        Assert.Equal("ack", Op(Single(session.HandleLine(CommandLine(151, 1, 340, 160)))));
        Assert.Equal(300, session.HandleLine("{\"op\":\"step\",\"ticks\":300}").Count);
        Assert.Equal("quit", Op(Single(session.HandleLine("{\"op\":\"quit\"}"))));

        var replay = new HostProtocolSession().HandleLine(JsonSerializer.Serialize(new
        {
            op = "replay",
            path = replayPath,
            verify = true,
        }));
        Assert.Equal(301, replay.Count);
        Assert.All(replay.Take(300), line =>
        {
            using var progress = Parse(line);
            Assert.Equal("replay_progress", progress.RootElement.GetProperty("op").GetString());
            Assert.True(progress.RootElement.GetProperty("hash_ok").GetBoolean());
        });
        using (var done = Parse(replay[^1]))
        {
            Assert.Equal("replay_done", done.RootElement.GetProperty("op").GetString());
            Assert.Equal(300, done.RootElement.GetProperty("ticks").GetInt32());
            Assert.Equal(JsonValueKind.Null, done.RootElement.GetProperty("divergence_tick").ValueKind);
        }

        var tamperedPath = Path.Combine(_temporaryDirectory, "tampered.replay.json");
        var tampered = JsonNode.Parse(File.ReadAllText(replayPath))!.AsObject();
        tampered["command_bundles"]![0]!["commands"]![0]!["args"]!["x"] = 201;
        File.WriteAllText(tamperedPath, tampered.ToJsonString());
        var tamperedReplay = new HostProtocolSession().HandleLine(JsonSerializer.Serialize(new
        {
            op = "replay",
            path = tamperedPath,
            verify = true,
        }));
        using var tamperedDone = Parse(tamperedReplay[^1]);
        Assert.Equal(1, tamperedDone.RootElement.GetProperty("divergence_tick").GetInt32());
        using var firstProgress = Parse(tamperedReplay[0]);
        Assert.False(firstProgress.RootElement.GetProperty("hash_ok").GetBoolean());
    }

    [Fact]
    public void FreshHostJoinsTickTwoHundredAndCatchesUpToIdenticalTickFourHundredHash()
    {
        var first = new HostProtocolSession();
        Assert.Equal("launched", Op(Single(first.HandleLine(LaunchLine()))));
        Assert.Equal(200, first.HandleLine("{\"op\":\"step\",\"ticks\":200}").Count);
        using var save = Parse(Single(first.HandleLine("{\"op\":\"save\"}")));
        var state = save.RootElement.GetProperty("state").GetString()!;
        var catchup = new List<JsonElement>();
        for (var tick = 201; tick <= 400; tick++)
        {
            var line = CommandLine(tick, tick - 201, 200 + tick, 40 + tick);
            using var command = JsonDocument.Parse(line);
            catchup.Add(command.RootElement.GetProperty("bundle").Clone());
            Assert.Equal("ack", Op(Single(first.HandleLine(line))));
        }
        Assert.Equal(200, first.HandleLine("{\"op\":\"step\",\"ticks\":200}").Count);
        using var firstHash = Parse(Single(first.HandleLine("{\"op\":\"hash\"}")));

        var second = new HostProtocolSession();
        Assert.Equal("launched", Op(Single(second.HandleLine(LaunchLine()))));
        var joinedLine = Single(second.HandleLine(JsonSerializer.Serialize(new
        {
            op = "join",
            state,
            tick = 200,
            catchup,
        })));
        using var joined = Parse(joinedLine);
        Assert.Equal("joined", joined.RootElement.GetProperty("op").GetString());
        Assert.Equal(400, joined.RootElement.GetProperty("tick").GetInt32());
        Assert.Equal(
            firstHash.RootElement.GetProperty("hash").GetString(),
            joined.RootElement.GetProperty("hash").GetString());
    }

    [Fact]
    public void DiffIsDeterministicNamesFirstDifferenceAndCanWriteExactReport()
    {
        var first = new HostProtocolSession();
        var second = new HostProtocolSession();
        Assert.Equal("launched", Op(Single(first.HandleLine(LaunchLine()))));
        Assert.Equal("launched", Op(Single(second.HandleLine(LaunchLine()))));
        _ = first.HandleLine("{\"op\":\"step\",\"ticks\":1}");
        Assert.Equal("ack", Op(Single(second.HandleLine(CommandLine(1, 0, 400, 300)))));
        _ = second.HandleLine("{\"op\":\"step\",\"ticks\":1}");
        using var firstSave = Parse(Single(first.HandleLine("{\"op\":\"save\"}")));
        using var secondSave = Parse(Single(second.HandleLine("{\"op\":\"save\"}")));
        var firstState = firstSave.RootElement.GetProperty("state").GetString()!;
        var secondState = secondSave.RootElement.GetProperty("state").GetString()!;

        using (var identical = Parse(Single(first.HandleLine(JsonSerializer.Serialize(new
               { op = "diff", state = firstState })))))
        {
            Assert.Equal(JsonValueKind.Null, identical.RootElement.GetProperty("difference").ValueKind);
        }
        var reportPath = Path.Combine(_temporaryDirectory, "desync.json");
        var reportLine = Single(first.HandleLine(JsonSerializer.Serialize(new
        {
            op = "diff",
            state = secondState,
            path = reportPath,
        })));
        Assert.Equal(reportLine + Environment.NewLine, File.ReadAllText(reportPath));
        using var report = Parse(reportLine);
        Assert.Equal(1, report.RootElement.GetProperty("tick").GetInt32());
        Assert.Equal(1, report.RootElement.GetProperty("other_tick").GetInt32());
        Assert.False(report.RootElement.GetProperty("difference").GetProperty("path")
            .GetString()!.Length == 0);
    }

    public void Dispose()
    {
        if (Directory.Exists(_temporaryDirectory))
        {
            Directory.Delete(_temporaryDirectory, recursive: true);
        }
    }

    private string RunTranscript()
    {
        var session = new HostProtocolSession();
        var lines = new List<string>();
        lines.AddRange(session.HandleLine(LaunchLine()));
        lines.AddRange(session.HandleLine(CommandsLine()));
        lines.AddRange(session.HandleLine("{\"op\":\"step\",\"ticks\":12}"));
        lines.AddRange(session.HandleLine("{\"op\":\"hash\"}"));
        lines.AddRange(session.HandleLine("{\"op\":\"quit\"}"));
        return string.Join("\n", lines);
    }

    private string LaunchLine() => JsonSerializer.Serialize(new
    {
        op = "launch",
        match = JsonSerializer.Deserialize<JsonElement>(_matchJson),
        templates = _templatesPath,
    });

    private string BundleLaunchLine() => JsonSerializer.Serialize(new
    {
        op = "launch",
        match = JsonSerializer.Deserialize<JsonElement>(_matchJson),
        bundle = RepoPath("contracts", "fixtures", "bundle-v1.json"),
    });

    private string MapLaunchLine()
    {
        var match = JsonNode.Parse(_matchJson)!.AsObject();
        match["map"] = new JsonObject { ["path"] = "maps/test/wall.map" };
        return JsonSerializer.Serialize(new
        {
            op = "launch",
            match = JsonSerializer.Deserialize<JsonElement>(match.ToJsonString()),
            bundle = RepoPath("contracts", "fixtures", "bundle-v1.json"),
            map = RepoPath("contracts", "fixtures", "map-v1.json"),
        });
    }

    private static string CommandsLine() =>
        "{\"op\":\"commands\",\"bundle\":{"
        + "\"schema\":\"openbfme.command.v1\",\"tick\":1,\"seat\":0,\"seq\":0,"
        + "\"commands\":[{\"type\":\"move\",\"args\":{\"objects\":[1],\"x\":200,\"y\":40}}]}}";

    private static string CommandLine(int tick, int seq, int x, int y) =>
        JsonSerializer.Serialize(new
        {
            op = "commands",
            bundle = new
            {
                schema = "openbfme.command.v1",
                tick,
                seat = 0,
                seq,
                commands = new[]
                {
                    new { type = "move", args = new { objects = new[] { 1 }, x, y } },
                },
            },
        });

    private static IReadOnlyList<string> SnapshotRequiredKeys()
    {
        using var schema = JsonDocument.Parse(File.ReadAllText(
            RepoPath("contracts", "snapshot-v1.schema.json")));
        return schema.RootElement.GetProperty("required").EnumerateArray()
            .Select(value => value.GetString()!).ToArray();
    }

    private static string Op(string line) => Parse(line).RootElement.GetProperty("op").GetString()!;
    private static string Single(IReadOnlyList<string> lines) => Assert.Single(lines);
    private static JsonDocument Parse(string line) => JsonDocument.Parse(line);

    private static string RepoPath(params string[] parts)
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory != null && !File.Exists(Path.Combine(directory.FullName, "AGENTS.md")))
        {
            directory = directory.Parent;
        }
        Assert.NotNull(directory);
        return Path.Combine(new[] { directory!.FullName }.Concat(parts).ToArray());
    }

    private const string TemplatesJson = """
        {
          "schema": "openbfme.objects",
          "schemaVersion": 0,
          "objects": [
            {
              "id": "sim-host.horde-a",
              "kind": "battalion",
              "displayName": "Host Infantry A",
              "memberCount": 10,
              "commandPoints": 20,
              "simulation": { "health": 200, "speed": 90 }
            },
            {
              "id": "sim-host.horde-b",
              "kind": "battalion",
              "displayName": "Host Infantry B",
              "memberCount": 10,
              "commandPoints": 20,
              "simulation": { "health": 200, "speed": 90 }
            }
          ]
        }
        """;
}
