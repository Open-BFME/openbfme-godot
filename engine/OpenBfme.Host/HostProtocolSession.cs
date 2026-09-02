using System.Text;
using System.Text.Json;
using OpenBfme.Sim;

namespace OpenBfme.Host;

/// <summary>
/// One deterministic line-delimited JSON host session. Each input line is
/// isolated: a malformed request emits one error reply and cannot terminate or
/// poison the next request.
/// </summary>
public sealed class HostProtocolSession
{
    private SimWorld? _world;
    private MatchLaunch? _launch;
    private IReadOnlyList<ObjectTemplate>? _templates;
    private IReadOnlyDictionary<int, int> _seatTeams = new Dictionary<int, int>();

    public bool IsRunning { get; private set; } = true;

    public IReadOnlyList<string> HandleLine(string line)
    {
        try
        {
            using var document = JsonDocument.Parse(line);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                throw new ProtocolException("request must be a JSON object");
            }
            var op = RequiredString(root, "op");
            return op switch
            {
                "launch" => Launch(root),
                "commands" => Commands(root),
                "step" => Step(root),
                "hash" => Hash(),
                "save" => Save(),
                "load" => Load(root),
                "quit" => Quit(),
                _ => throw new ProtocolException($"unsupported op '{op}'"),
            };
        }
        catch (Exception exception)
        {
            return new[] { Reply(writer =>
            {
                writer.WriteString("op", "error");
                writer.WriteString("message", SafeMessage(exception));
            }) };
        }
    }

    private IReadOnlyList<string> Launch(JsonElement root)
    {
        if (!root.TryGetProperty("match", out var match) || match.ValueKind != JsonValueKind.Object)
        {
            throw new ProtocolException("launch requires object field 'match'");
        }
        var hasTemplates = root.TryGetProperty("templates", out var templatesPath);
        var hasBundle = root.TryGetProperty("bundle", out _);
        if (!hasTemplates && hasBundle)
        {
            throw new ProtocolException("unsupported: bundle-v1 loading is not available on this branch");
        }
        if (!hasTemplates || templatesPath.ValueKind != JsonValueKind.String
            || string.IsNullOrWhiteSpace(templatesPath.GetString()))
        {
            throw new ProtocolException("launch requires non-empty string field 'templates'");
        }

        var launch = MatchLaunch.Parse(match.GetRawText());
        var path = Path.GetFullPath(templatesPath.GetString()!);
        var loaded = PackTemplateLoader.LoadFromObjectsDocument(File.ReadAllText(path, Encoding.UTF8));
        if (loaded.Templates.Count == 0)
        {
            throw new ProtocolException("templates document produced no simulation templates");
        }
        if (loaded.Report.SkippedRows.Count != 0)
        {
            var first = loaded.Report.SkippedRows[0];
            throw new ProtocolException(
                $"templates document skipped {loaded.Report.SkippedRows.Count} row(s); first: {first.Id} {first.Reason}: {first.Detail}");
        }

        var world = new SimWorld(launch, loaded.Templates);
        BootstrapTemplates(world, launch, loaded.Templates);
        _launch = launch;
        _templates = loaded.Templates;
        _seatTeams = launch.Players.ToDictionary(player => player.Seat, player => player.Team);
        _world = world;

        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "launched");
            writer.WriteNumber("tick_ms", launch.Rules.TickMilliseconds);
            writer.WriteNumber("players", launch.Players.Count);
        }) };
    }

    private IReadOnlyList<string> Commands(JsonElement root)
    {
        var world = RequireWorld();
        if (!root.TryGetProperty("bundle", out var bundleElement)
            || bundleElement.ValueKind != JsonValueKind.Object)
        {
            throw new ProtocolException("commands requires object field 'bundle'");
        }
        var bundle = SimCommandBundle.Parse(bundleElement.GetRawText());
        if (bundle.Tick <= world.TickIndex)
        {
            throw new ProtocolException(
                $"command tick {bundle.Tick} must be greater than current tick {world.TickIndex}");
        }
        foreach (var command in bundle.ResolveTeams(TeamForSeat))
        {
            if (!world.SubmitCommand(command))
            {
                throw new ProtocolException($"command bundle for tick {bundle.Tick} was refused");
            }
        }
        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "ack");
            writer.WriteNumber("tick", bundle.Tick);
        }) };
    }

    private IReadOnlyList<string> Step(JsonElement root)
    {
        var world = RequireWorld();
        if (!root.TryGetProperty("ticks", out var ticksElement)
            || !ticksElement.TryGetInt32(out var ticks) || ticks < 0 || ticks > 100_000)
        {
            throw new ProtocolException("step field 'ticks' must be an integer in 0..100000");
        }
        var replies = new string[ticks];
        for (var index = 0; index < ticks; index++)
        {
            world.Tick();
            replies[index] = "{\"op\":\"snapshot\",\"snapshot\":"
                + SnapshotWriter.WriteJson(world) + "}";
        }
        return replies;
    }

    private IReadOnlyList<string> Hash()
    {
        var world = RequireWorld();
        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "hash");
            writer.WriteNumber("tick", world.TickIndex);
            writer.WriteString("hash", world.StateHash());
        }) };
    }

    private IReadOnlyList<string> Save()
    {
        var world = RequireWorld();
        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "save");
            writer.WriteNumber("tick", world.TickIndex);
            writer.WriteString("state", Convert.ToBase64String(world.Snapshot()));
        }) };
    }

    private IReadOnlyList<string> Load(JsonElement root)
    {
        _ = RequireWorld();
        if (_launch == null || _templates == null)
        {
            throw new ProtocolException("load requires a launched session");
        }
        var state = RequiredString(root, "state");
        byte[] payload;
        try
        {
            payload = Convert.FromBase64String(state);
        }
        catch (FormatException exception)
        {
            throw new ProtocolException("load field 'state' is not valid base64", exception);
        }
        var config = BuildConfig(_launch, _templates);
        _world = SimWorld.Restore(payload, config, ModuleRegistry.CreateDefault());
        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "loaded");
            writer.WriteNumber("tick", _world.TickIndex);
        }) };
    }

    private IReadOnlyList<string> Quit()
    {
        IsRunning = false;
        return new[] { Reply(writer => writer.WriteString("op", "quit")) };
    }

    private SimWorld RequireWorld() =>
        _world ?? throw new ProtocolException("launch must succeed before this operation");

    private int TeamForSeat(int seat) =>
        _seatTeams.TryGetValue(seat, out var team)
            ? team
            : throw new ProtocolException($"seat {seat} is not present in the launched match");

    private static SimConfig BuildConfig(MatchLaunch launch, IReadOnlyList<ObjectTemplate> templates)
    {
        var teamCount = launch.Players.Max(player => player.Team) + 1;
        var commandPoints = EconomyTemplate.ScaleInteger(
            SimConfig.DefaultMaxCommandPoints, launch.Rules.CommandPointMultiplier);
        return new SimConfig(templates, launch.Seed, teamCount, maxCommandPoints: commandPoints);
    }

    private static void BootstrapTemplates(
        SimWorld world,
        MatchLaunch launch,
        IReadOnlyList<ObjectTemplate> templates)
    {
        var templateIndices = templates
            .Select(template => template.Name)
            .OrderBy(name => name, StringComparer.Ordinal)
            .Select((name, index) => (name, index))
            .ToDictionary(row => row.name, row => row.index, StringComparer.Ordinal);
        for (var index = 0; index < templates.Count; index++)
        {
            var player = launch.Players[index % launch.Players.Count];
            var position = new FixedVector2(
                Fixed64.FromInt(64 + index * 24),
                Fixed64.FromInt(64 + player.Seat * 96));
            var carrier = world.SpawnObject(
                templates[index].Name,
                player.Team,
                position);
            var body = templates[index].Modules.FirstOrDefault(
                module => module.TypeName == ActiveBodyModule.TypeName);
            var memberCount = body?.GetLong("MemberCount", 0) ?? 0;
            var memberTemplate = body?.GetString("MemberObjectId", "") ?? "";
            if (memberCount <= 0 || string.IsNullOrWhiteSpace(memberTemplate))
            {
                continue;
            }
            if (memberCount > 64 || !templates.Any(template => template.Name == memberTemplate))
            {
                throw new ProtocolException(
                    $"battalion template '{templates[index].Name}' has an invalid member contract");
            }
            var members = new int[memberCount];
            for (var memberIndex = 0; memberIndex < members.Length; memberIndex++)
            {
                var column = memberIndex % 4;
                var row = memberIndex / 4;
                members[memberIndex] = world.SpawnObject(
                    memberTemplate,
                    player.Team,
                    new FixedVector2(
                        position.X + Fixed64.FromInt(column * 6),
                        position.Y + Fixed64.FromInt(row * 6))).Id;
            }
            world.AddHorde(new SnapshotHorde(
                carrier.Id,
                player.Team,
                templateIndices[templates[index].Name],
                members,
                Formation: 0));
        }
    }

    private static string RequiredString(JsonElement root, string name)
    {
        if (!root.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.String
            || string.IsNullOrEmpty(value.GetString()))
        {
            throw new ProtocolException($"request requires non-empty string field '{name}'");
        }
        return value.GetString()!;
    }

    private static string Reply(Action<Utf8JsonWriter> body)
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream))
        {
            writer.WriteStartObject();
            body(writer);
            writer.WriteEndObject();
        }
        return Encoding.UTF8.GetString(stream.ToArray());
    }

    private static string SafeMessage(Exception exception)
    {
        var message = exception.Message.Replace('\r', ' ').Replace('\n', ' ').Trim();
        return message.Length == 0 ? exception.GetType().Name : message;
    }
}

public sealed class ProtocolException : Exception
{
    public ProtocolException(string message) : base(message) { }
    public ProtocolException(string message, Exception inner) : base(message, inner) { }
}
