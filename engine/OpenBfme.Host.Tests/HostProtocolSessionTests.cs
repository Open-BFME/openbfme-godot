using System.Text.Json;
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

    private static string CommandsLine() =>
        "{\"op\":\"commands\",\"bundle\":{"
        + "\"schema\":\"openbfme.command.v1\",\"tick\":1,\"seat\":0,\"seq\":0,"
        + "\"commands\":[{\"type\":\"move\",\"args\":{\"objects\":[1],\"x\":200,\"y\":40}}]}}";

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
