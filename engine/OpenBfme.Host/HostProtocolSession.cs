using System.Text;
using System.Text.Json;
using System.Globalization;
using System.Numerics;
using OpenBfme.Sim;
using OpenBfme.Sim.Pathing;

namespace OpenBfme.Host;

/// <summary>
/// One deterministic line-delimited JSON host session. Each input line is
/// isolated: a malformed request emits one error reply and cannot terminate or
/// poison the next request.
/// </summary>
public sealed class HostProtocolSession
{
    private const int MaplessGridWidth = 256;
    private const int MaplessGridHeight = 192;
    private const int MaplessGridCellSize = 8;
    private SimWorld? _world;
    private MatchLaunch? _launch;
    private IReadOnlyList<ObjectTemplate>? _templates;
    private SimConfig? _restoreConfig;
    private PassabilityGrid? _restoreGrid;
    private BundleDocument? _bundle;
    private IReadOnlyDictionary<string, BundleHordeRow> _hordes =
        new Dictionary<string, BundleHordeRow>();
    private IReadOnlySet<string> _loadedTemplateNames = new HashSet<string>();
    private IReadOnlyDictionary<int, int> _seatTeams = new Dictionary<int, int>();
    private int _nextHordeId = 100_000;
    private string? _launchJson;
    private string? _sourceKind;
    private string? _sourcePath;
    private ReplayRecorder? _recorder;

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
                "templates" => Templates(),
                "spawn" => Spawn(root),
                "commands" => Commands(root),
                "step" => Step(root),
                "hash" => Hash(),
                "save" => Save(),
                "load" => Load(root),
                "record" => Record(root),
                "replay" => Replay(root),
                "join" => Join(root),
                "diff" => Diff(root),
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
        var hasBundle = root.TryGetProperty("bundle", out var bundlePath);
        if (hasTemplates == hasBundle)
        {
            throw new ProtocolException("launch requires exactly one of 'bundle' or 'templates'");
        }

        var launchJson = match.GetRawText();
        var launch = MatchLaunch.Parse(launchJson);
        SimWorld world;
        int templatesLoaded;
        int templatesFailed;
        if (hasBundle)
        {
            if (bundlePath.ValueKind != JsonValueKind.String
                || string.IsNullOrWhiteSpace(bundlePath.GetString()))
            {
                throw new ProtocolException("launch requires non-empty string field 'bundle'");
            }
            var path = Path.GetFullPath(bundlePath.GetString()!);
            var document = BundleDocument.Load(path);
            world = SimWorld.FromBundle(
                launch,
                document,
                PassabilityGrid.Uniform(
                    MaplessGridWidth, MaplessGridHeight, cellSize: MaplessGridCellSize));
            var report = world.BundleLoadReport
                ?? throw new ProtocolException("bundle launch produced no load report");
            var loaded = BundleTemplateLoader.Load(
                document, ModuleRegistry.CreateDefault(), launch.Rules.TickMilliseconds);
            _restoreConfig = BuildConfig(
                launch, loaded, MaplessGridWidth, MaplessGridHeight);
            _restoreGrid = PassabilityGrid.Uniform(
                MaplessGridWidth, MaplessGridHeight, cellSize: MaplessGridCellSize);
            _bundle = document;
            _hordes = (document.Hordes ?? Array.Empty<BundleHordeRow>())
                .ToDictionary(row => row.Name, StringComparer.OrdinalIgnoreCase);
            _loadedTemplateNames = loaded.Templates.Select(template => template.Name)
                .ToHashSet(StringComparer.OrdinalIgnoreCase);
            _templates = loaded.Templates;
            templatesLoaded = report.TemplatesLoaded;
            templatesFailed = report.TemplatesFailed.Count;
            _sourceKind = "bundle";
            _sourcePath = path;
        }
        else
        {
            if (templatesPath.ValueKind != JsonValueKind.String
                || string.IsNullOrWhiteSpace(templatesPath.GetString()))
            {
                throw new ProtocolException("launch requires non-empty string field 'templates'");
            }
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
            world = new SimWorld(launch, loaded.Templates);
            BootstrapTemplates(world, launch, loaded.Templates);
            _restoreConfig = BuildConfig(launch, loaded.Templates);
            _restoreGrid = null;
            _bundle = null;
            _hordes = new Dictionary<string, BundleHordeRow>();
            _loadedTemplateNames = loaded.Templates.Select(template => template.Name)
                .ToHashSet(StringComparer.OrdinalIgnoreCase);
            _templates = loaded.Templates;
            templatesLoaded = loaded.Templates.Count;
            templatesFailed = 0;
            _sourceKind = "templates";
            _sourcePath = path;
        }

        _launch = launch;
        _seatTeams = launch.Players.ToDictionary(player => player.Seat, player => player.Team);
        _world = world;
        _nextHordeId = 100_000;
        _launchJson = launchJson;
        _recorder = null;

        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "launched");
            writer.WriteNumber("tick_ms", launch.Rules.TickMilliseconds);
            writer.WriteNumber("players", launch.Players.Count);
            writer.WriteNumber("templates_loaded", templatesLoaded);
            writer.WriteNumber("templates_failed", templatesFailed);
        }) };
    }

    private IReadOnlyList<string> Templates()
    {
        _ = RequireWorld();
        if (_bundle == null)
        {
            throw new ProtocolException("templates requires a bundle-v1 launched session");
        }
        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "templates");
            writer.WriteStartArray("templates");
            foreach (var row in _bundle.Templates.Where(row => _loadedTemplateNames.Contains(row.Name)))
            {
                writer.WriteStartObject();
                writer.WriteNumber("index", row.Index);
                writer.WriteString("name", row.Name);
                writer.WriteString("kind", row.Kind);
                writer.WriteString("side", row.Side ?? "");
                writer.WriteBoolean("horde", _hordes.ContainsKey(row.Name));
                writer.WriteStartArray("kindof");
                foreach (var value in row.KindOf) writer.WriteStringValue(value);
                writer.WriteEndArray();
                writer.WriteEndObject();
            }
            writer.WriteEndArray();
        }) };
    }

    private IReadOnlyList<string> Spawn(JsonElement root)
    {
        var world = RequireWorld();
        var document = _bundle
            ?? throw new ProtocolException("spawn requires a bundle-v1 launched session");
        var templateName = RequiredString(root, "template");
        var player = RequiredInt(root, "player", minimum: 0);
        var team = TeamForSeat(player);
        var origin = new FixedVector2(RequiredFixed(root, "x"), RequiredFixed(root, "y"));
        if (!_loadedTemplateNames.Contains(templateName))
        {
            throw new ProtocolException($"unknown or unloaded template '{templateName}'");
        }

        if (!_hordes.TryGetValue(templateName, out var horde))
        {
            var gameObject = world.SpawnObject(templateName, team, origin);
            _recorder?.RecordSetup(root.GetRawText());
            return new[] { SpawnReply(gameObject.Id, new[] { gameObject.Id }) };
        }
        var missingMember = horde.RankInfo.Select(rank => rank.UnitType)
            .FirstOrDefault(name => !_loadedTemplateNames.Contains(name));
        if (missingMember != null)
        {
            throw new ProtocolException(
                $"horde template '{templateName}' requires unloaded member '{missingMember}'");
        }

        var members = new List<int>();
        foreach (var rank in horde.RankInfo.OrderBy(value => value.Rank))
        {
            foreach (var position in rank.Positions)
            {
                var offset = new FixedVector2(
                    Fixed64.FromRaw(position.X.Raw / 10),
                    Fixed64.FromRaw(position.Y.Raw / 10));
                members.Add(world.SpawnObject(rank.UnitType, team, origin + offset).Id);
            }
        }
        if (members.Count == 0)
        {
            throw new ProtocolException($"horde template '{templateName}' has no authored members");
        }
        var hordeId = _nextHordeId++;
        var templateIndex = document.Templates.Single(row =>
            row.Name.Equals(templateName, StringComparison.OrdinalIgnoreCase)).Index;
        world.AddHorde(new SnapshotHorde(hordeId, team, templateIndex, members, Formation: 0));
        _recorder?.RecordSetup(root.GetRawText());
        return new[] { SpawnReply(hordeId, members) };
    }

    private static string SpawnReply(int id, IReadOnlyList<int> members) => Reply(writer =>
    {
        writer.WriteString("op", "spawned");
        writer.WriteNumber("id", id);
        writer.WriteStartArray("members");
        foreach (var member in members) writer.WriteNumberValue(member);
        writer.WriteEndArray();
    });

    private IReadOnlyList<string> Commands(JsonElement root)
    {
        var world = RequireWorld();
        if (!root.TryGetProperty("bundle", out var bundleElement)
            || bundleElement.ValueKind != JsonValueKind.Object)
        {
            throw new ProtocolException("commands requires object field 'bundle'");
        }
        var bundleJson = bundleElement.GetRawText();
        var bundle = SimCommandBundle.Parse(bundleJson);
        SubmitCommandBundle(world, bundle);
        _recorder?.RecordCommand(bundleJson);
        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "ack");
            writer.WriteNumber("tick", bundle.Tick);
        }) };
    }

    private void SubmitCommandBundle(SimWorld world, SimCommandBundle bundle)
    {
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
            _recorder?.RecordHash(world);
            replies[index] = "{\"op\":\"snapshot\",\"snapshot\":"
                + SnapshotWriter.WriteJson(world) + "}";
        }
        _recorder?.Flush(world);
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
        if (_launch == null || _templates == null || _restoreConfig == null)
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
        _world = SimWorld.Restore(
            payload, _restoreConfig, ModuleRegistry.CreateDefault(), _restoreGrid);
        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "loaded");
            writer.WriteNumber("tick", _world.TickIndex);
        }) };
    }

    private IReadOnlyList<string> Record(JsonElement root)
    {
        var world = RequireWorld();
        if (_launchJson == null || _sourceKind == null || _sourcePath == null)
        {
            throw new ProtocolException("record requires a launched session");
        }
        var path = Path.GetFullPath(RequiredString(root, "path"));
        _recorder = new ReplayRecorder(
            path,
            _launchJson,
            _sourceKind,
            _sourcePath,
            _bundle?.Source.EffectiveTreeSha256,
            world);
        _recorder.Flush(world);
        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "recording");
            writer.WriteString("path", path);
        }) };
    }

    private IReadOnlyList<string> Replay(JsonElement root)
    {
        if (_world != null)
        {
            throw new ProtocolException("replay requires a fresh host session");
        }
        var replay = ReplayFile.Load(RequiredString(root, "path"));
        var verify = !root.TryGetProperty("verify", out var verifyElement)
            || verifyElement.ValueKind != JsonValueKind.False;
        if (replay.SourceKind == "bundle")
        {
            var bundle = BundleDocument.Load(replay.SourcePath);
            if (!string.Equals(bundle.Source.EffectiveTreeSha256, replay.BundleHash,
                    StringComparison.Ordinal))
            {
                throw new ProtocolException("replay bundle effective tree identity does not match");
            }
        }
        else if (replay.SourceKind != "templates")
        {
            throw new ProtocolException($"unsupported replay source kind '{replay.SourceKind}'");
        }
        var launchRequest = new Dictionary<string, object?>
        {
            ["op"] = "launch",
            ["match"] = replay.Match,
            [replay.SourceKind] = replay.SourcePath,
        };
        using (var launchDocument = JsonDocument.Parse(JsonSerializer.Serialize(launchRequest)))
        {
            _ = Launch(launchDocument.RootElement);
        }
        var world = RequireWorld();
        if (replay.Setup.Count == 0)
        {
            RestoreState(replay.InitialState);
            world = RequireWorld();
        }
        else
        {
            foreach (var setup in replay.Setup) _ = Spawn(setup);
        }
        if (world.TickIndex != replay.InitialTick)
        {
            throw new ProtocolException("replay initial tick does not match its canonical state");
        }
        foreach (var bundleElement in replay.Commands
                     .OrderBy(CommandTick)
                     .ThenBy(CommandTeam)
                     .ThenBy(CommandSequence)
                     .ThenBy(CommandSeat))
        {
            SubmitCommandBundle(world, SimCommandBundle.Parse(bundleElement.GetRawText()));
        }
        var replies = new List<string>();
        int? divergenceTick = null;
        while (world.TickIndex < replay.FinalTick)
        {
            world.Tick();
            var actual = world.StateHash();
            var hashOk = !verify || !replay.Hashes.TryGetValue(world.TickIndex, out var expected)
                || string.Equals(actual, expected, StringComparison.Ordinal);
            if (!hashOk && divergenceTick == null) divergenceTick = world.TickIndex;
            replies.Add("{\"op\":\"replay_progress\",\"tick\":"
                + world.TickIndex.ToString(CultureInfo.InvariantCulture)
                + ",\"hash_ok\":" + (hashOk ? "true" : "false")
                + ",\"snapshot\":" + SnapshotWriter.WriteJson(world) + "}");
        }
        if (verify && divergenceTick == null
            && !string.Equals(world.StateHash(), replay.FinalHash, StringComparison.Ordinal))
        {
            divergenceTick = world.TickIndex;
        }
        replies.Add(Reply(writer =>
        {
            writer.WriteString("op", "replay_done");
            writer.WriteNumber("ticks", world.TickIndex);
            if (divergenceTick == null) writer.WriteNull("divergence_tick");
            else writer.WriteNumber("divergence_tick", divergenceTick.Value);
        }));
        return replies;
    }

    private IReadOnlyList<string> Join(JsonElement root)
    {
        _ = RequireWorld();
        var stateTick = RequiredInt(root, "tick", minimum: 0);
        RestoreState(RequiredString(root, "state"));
        var world = RequireWorld();
        if (world.TickIndex != stateTick)
        {
            throw new ProtocolException(
                $"join tick {stateTick} does not match state tick {world.TickIndex}");
        }
        if (!root.TryGetProperty("catchup", out var catchup)
            || catchup.ValueKind != JsonValueKind.Array)
        {
            throw new ProtocolException("join requires array field 'catchup'");
        }
        var bundles = catchup.EnumerateArray().Select(value => value.Clone())
            .OrderBy(CommandTick)
            .ThenBy(CommandTeam)
            .ThenBy(CommandSequence)
            .ThenBy(CommandSeat)
            .ToArray();
        foreach (var element in bundles)
        {
            SubmitCommandBundle(world, SimCommandBundle.Parse(element.GetRawText()));
        }
        var finalTick = bundles.Length == 0 ? stateTick : bundles.Max(CommandTick);
        while (world.TickIndex < finalTick) world.Tick();
        return new[] { Reply(writer =>
        {
            writer.WriteString("op", "joined");
            writer.WriteNumber("tick", world.TickIndex);
            writer.WriteString("hash", world.StateHash());
        }) };
    }

    private IReadOnlyList<string> Diff(JsonElement root)
    {
        var world = RequireWorld();
        var other = RestoreWorld(RequiredString(root, "state"));
        using var localJson = JsonDocument.Parse(SnapshotWriter.WriteJson(world));
        using var otherJson = JsonDocument.Parse(SnapshotWriter.WriteJson(other));
        var difference = CanonicalStateDifference.First(
            localJson.RootElement, otherJson.RootElement);
        difference ??= CanonicalStateDifference.FirstBytes(world.Snapshot(), other.Snapshot());
        var reply = Reply(writer =>
        {
            writer.WriteString("op", "diff");
            writer.WriteNumber("tick", world.TickIndex);
            writer.WriteNumber("other_tick", other.TickIndex);
            if (difference == null)
            {
                writer.WriteNull("difference");
            }
            else
            {
                writer.WriteStartObject("difference");
                writer.WriteString("path", difference.Path);
                writer.WritePropertyName("local");
                writer.WriteRawValue(difference.LocalJson);
                writer.WritePropertyName("other");
                writer.WriteRawValue(difference.OtherJson);
                writer.WriteEndObject();
            }
        });
        if (root.TryGetProperty("path", out var pathElement))
        {
            if (pathElement.ValueKind != JsonValueKind.String
                || string.IsNullOrWhiteSpace(pathElement.GetString()))
            {
                throw new ProtocolException("diff field 'path' must be a non-empty string");
            }
            var path = Path.GetFullPath(pathElement.GetString()!);
            var directory = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
            File.WriteAllText(path, reply + Environment.NewLine);
        }
        return new[] { reply };
    }

    private void RestoreState(string state) => _world = RestoreWorld(state);

    private SimWorld RestoreWorld(string state)
    {
        if (_launch == null || _templates == null || _restoreConfig == null)
        {
            throw new ProtocolException("canonical state restore requires a launched session");
        }
        byte[] payload;
        try
        {
            payload = Convert.FromBase64String(state);
        }
        catch (FormatException exception)
        {
            throw new ProtocolException("field 'state' is not valid base64", exception);
        }
        return SimWorld.Restore(
            payload, _restoreConfig, ModuleRegistry.CreateDefault(), _restoreGrid);
    }

    private int CommandTeam(JsonElement element) => TeamForSeat(CommandSeat(element));
    private static int CommandTick(JsonElement element) => RequiredInt(element, "tick", 0);
    private static int CommandSeat(JsonElement element) => RequiredInt(element, "seat", 0);
    private static int CommandSequence(JsonElement element) => RequiredInt(element, "seq", 0);

    private IReadOnlyList<string> Quit()
    {
        if (_world != null) _recorder?.Flush(_world);
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

    private static SimConfig BuildConfig(
        MatchLaunch launch,
        BundleTemplateLoadResult loaded,
        int mapWidthCells,
        int mapHeightCells)
    {
        var teamCount = launch.Players.Max(player => player.Team) + 1;
        var commandPoints = EconomyTemplate.ScaleInteger(
            SimConfig.DefaultMaxCommandPoints, launch.Rules.CommandPointMultiplier);
        return new SimConfig(
            loaded.Templates,
            launch.Seed,
            teamCount,
            mapWidthCells,
            mapHeightCells,
            weaponTemplates: loaded.WeaponTemplates,
            armorTemplates: loaded.ArmorTemplates,
            maxCommandPoints: commandPoints,
            templateIndices: loaded.TemplateIndices);
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

    private static int RequiredInt(JsonElement root, string name, int minimum)
    {
        if (!root.TryGetProperty(name, out var value) || !value.TryGetInt32(out var result)
            || result < minimum)
        {
            throw new ProtocolException(
                $"request field '{name}' must be an integer greater than or equal to {minimum}");
        }
        return result;
    }

    private static Fixed64 RequiredFixed(JsonElement root, string name)
    {
        if (!root.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.Number
            || !decimal.TryParse(value.GetRawText(), NumberStyles.Float,
                CultureInfo.InvariantCulture, out var parsed))
        {
            throw new ProtocolException($"request field '{name}' must be a finite number");
        }
        var bits = decimal.GetBits(parsed);
        var scale = (bits[3] >> 16) & 0xff;
        var negative = (bits[3] & unchecked((int)0x80000000)) != 0;
        var numerator = ((BigInteger)(uint)bits[2] << 64)
            | ((BigInteger)(uint)bits[1] << 32)
            | (uint)bits[0];
        if (negative) numerator = -numerator;
        var raw = (numerator << Fixed64.FractionBits) / BigInteger.Pow(10, scale);
        if (raw > long.MaxValue || raw < long.MinValue)
        {
            throw new ProtocolException($"request field '{name}' is outside the simulation range");
        }
        return Fixed64.FromRaw((long)raw);
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
