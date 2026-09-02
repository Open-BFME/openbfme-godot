using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
using OpenBfme.Sim;

namespace OpenBfme.Host;

internal sealed class ReplayRecorder
{
    private const int CheckpointIntervalTicks = 30;
    private readonly string _path;
    private readonly JsonObject _root;
    private readonly JsonArray _commands = new();
    private readonly JsonArray _hashLog = new();
    private readonly JsonArray _setup = new();
    private int _lastFlushedTick;

    public ReplayRecorder(
        string path,
        string matchJson,
        string sourceKind,
        string sourcePath,
        string? bundleHash,
        SimWorld world)
    {
        _path = System.IO.Path.GetFullPath(path);
        var match = JsonNode.Parse(matchJson)
            ?? throw new ProtocolException("record could not retain the match-launch document");
        var source = new JsonObject
        {
            ["kind"] = sourceKind,
            ["path"] = sourcePath,
        };
        if (bundleHash != null) source["effective_tree_sha256"] = bundleHash;
        if (match["map"] is JsonNode map) source["map"] = map.DeepClone();
        _root = new JsonObject
        {
            ["schema"] = "openbfme.replay.v1",
            ["match"] = match,
            ["source"] = source,
            ["initial_tick"] = world.TickIndex,
            ["initial_state"] = Convert.ToBase64String(world.Snapshot()),
            ["setup"] = _setup,
            ["command_bundles"] = _commands,
            ["hash_log_interval"] = 1,
            ["hash_log"] = _hashLog,
            ["final_tick"] = world.TickIndex,
            ["final_hash"] = world.StateHash(),
            ["build"] = new JsonObject { ["identity"] = BuildIdentity() },
        };
        _lastFlushedTick = world.TickIndex;
    }

    public string Path => _path;

    public void RecordCommand(string bundleJson)
    {
        var node = JsonNode.Parse(bundleJson)
            ?? throw new ProtocolException("record could not retain a command bundle");
        var insertAt = 0;
        while (insertAt < _commands.Count
               && CompareCommandKeys(_commands[insertAt]!, node) <= 0)
        {
            insertAt++;
        }
        _commands.Insert(insertAt, node);
    }

    public void RecordSetup(string requestJson)
    {
        _setup.Add(JsonNode.Parse(requestJson)
            ?? throw new ProtocolException("record could not retain a setup operation"));
    }

    public void RecordHash(SimWorld world)
    {
        _hashLog.Add(new JsonObject
        {
            ["tick"] = world.TickIndex,
            ["hash"] = world.StateHash(),
        });
    }

    public void Flush(SimWorld world)
    {
        _root["final_tick"] = world.TickIndex;
        _root["final_hash"] = world.StateHash();
        var directory = System.IO.Path.GetDirectoryName(_path);
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
        File.WriteAllText(_path, _root.ToJsonString(new JsonSerializerOptions
        {
            WriteIndented = true,
        }) + Environment.NewLine);
        _lastFlushedTick = world.TickIndex;
    }

    public void FlushCheckpoint(SimWorld world)
    {
        if (world.TickIndex - _lastFlushedTick >= CheckpointIntervalTicks)
        {
            Flush(world);
        }
    }

    private static string BuildIdentity()
    {
        var assembly = typeof(HostProtocolSession).Assembly;
        return assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()
                   ?.InformationalVersion
               ?? assembly.GetName().Version?.ToString()
               ?? "unknown";
    }

    private static int CompareCommandKeys(JsonNode left, JsonNode right)
    {
        foreach (var key in new[] { "tick", "seat", "seq" })
        {
            var comparison = left[key]!.GetValue<int>().CompareTo(right[key]!.GetValue<int>());
            if (comparison != 0) return comparison;
        }
        return 0;
    }
}

internal sealed class ReplayFile
{
    private ReplayFile(
        JsonElement match,
        string sourceKind,
        string sourcePath,
        string? bundleHash,
        string initialState,
        int initialTick,
        IReadOnlyList<JsonElement> commands,
        IReadOnlyList<JsonElement> setup,
        IReadOnlyDictionary<int, string> hashes,
        int finalTick,
        string finalHash)
    {
        Match = match;
        SourceKind = sourceKind;
        SourcePath = sourcePath;
        BundleHash = bundleHash;
        InitialState = initialState;
        InitialTick = initialTick;
        Commands = commands;
        Setup = setup;
        Hashes = hashes;
        FinalTick = finalTick;
        FinalHash = finalHash;
    }

    public JsonElement Match { get; }
    public string SourceKind { get; }
    public string SourcePath { get; }
    public string? BundleHash { get; }
    public string InitialState { get; }
    public int InitialTick { get; }
    public IReadOnlyList<JsonElement> Commands { get; }
    public IReadOnlyList<JsonElement> Setup { get; }
    public IReadOnlyDictionary<int, string> Hashes { get; }
    public int FinalTick { get; }
    public string FinalHash { get; }

    public static ReplayFile Load(string path)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(Path.GetFullPath(path)));
        var root = document.RootElement;
        if (RequiredString(root, "schema") != "openbfme.replay.v1")
        {
            throw new ProtocolException("replay schema must be 'openbfme.replay.v1'");
        }
        if (!root.TryGetProperty("match", out var match) || match.ValueKind != JsonValueKind.Object)
        {
            throw new ProtocolException("replay requires object field 'match'");
        }
        if (!root.TryGetProperty("source", out var source) || source.ValueKind != JsonValueKind.Object)
        {
            throw new ProtocolException("replay requires object field 'source'");
        }
        var commands = RequiredArray(root, "command_bundles")
            .Select(value => value.Clone()).ToArray();
        var setup = RequiredArray(root, "setup").Select(value => value.Clone()).ToArray();
        var hashes = new SortedDictionary<int, string>();
        foreach (var row in RequiredArray(root, "hash_log"))
        {
            hashes.Add(RequiredInt(row, "tick"), RequiredString(row, "hash"));
        }
        return new ReplayFile(
            match.Clone(),
            RequiredString(source, "kind"),
            RequiredString(source, "path"),
            source.TryGetProperty("effective_tree_sha256", out var hash)
                ? hash.GetString() : null,
            RequiredString(root, "initial_state"),
            RequiredInt(root, "initial_tick"),
            commands,
            setup,
            hashes,
            RequiredInt(root, "final_tick"),
            RequiredString(root, "final_hash"));
    }

    private static IEnumerable<JsonElement> RequiredArray(JsonElement root, string name)
    {
        if (!root.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.Array)
        {
            throw new ProtocolException($"replay requires array field '{name}'");
        }
        return value.EnumerateArray();
    }

    private static string RequiredString(JsonElement root, string name)
    {
        if (!root.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.String
            || string.IsNullOrEmpty(value.GetString()))
        {
            throw new ProtocolException($"replay requires non-empty string field '{name}'");
        }
        return value.GetString()!;
    }

    private static int RequiredInt(JsonElement root, string name)
    {
        if (!root.TryGetProperty(name, out var value) || !value.TryGetInt32(out var result)
            || result < 0)
        {
            throw new ProtocolException($"replay field '{name}' must be a non-negative integer");
        }
        return result;
    }
}
