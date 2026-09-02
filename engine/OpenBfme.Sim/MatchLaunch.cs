using System.Collections.ObjectModel;
using System.Globalization;
using System.Numerics;
using System.Text.Json;

namespace OpenBfme.Sim;

public sealed record MatchLaunchPack(string Id, string Sha256);

public sealed record MatchLaunchMap(string Path, string? Sha256);

public sealed record MatchLaunchRules(
    int TickMilliseconds,
    long StartingResources,
    Fixed64 CommandPointMultiplier,
    bool FogOfWar,
    Fixed64 GameSpeed,
    string Victory,
    bool Classic,
    IReadOnlyDictionary<string, bool> Improvements);

public sealed record MatchLaunchPlayer(
    int Seat,
    int Team,
    string Faction,
    string Controller,
    string? AiDifficulty,
    int? Color,
    int? StartPosition,
    Fixed64? Handicap,
    string? CustomHero,
    string? Name);

/// <summary>Immutable, validated representation of contracts/match-launch-v1.</summary>
public sealed record MatchLaunch(
    string Schema,
    ulong Seed,
    MatchLaunchPack Pack,
    MatchLaunchMap Map,
    MatchLaunchRules Rules,
    IReadOnlyList<MatchLaunchPlayer> Players,
    string Mode,
    string? Mission)
{
    public const string SchemaName = "openbfme.match-launch.v1";

    public static MatchLaunch Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        return Parse(File.ReadAllText(path));
    }

    public static MatchLaunch Parse(string json)
    {
        ArgumentNullException.ThrowIfNull(json);
        try
        {
            using var document = JsonDocument.Parse(json);
            return ParseRoot(document.RootElement);
        }
        catch (JsonException exception)
        {
            throw new MatchLaunchException("match launch JSON is invalid", exception);
        }
    }

    private static MatchLaunch ParseRoot(JsonElement root)
    {
        RequireKind(root, JsonValueKind.Object, "$", "object");
        var schema = RequireString(root, "schema", "schema");
        if (!string.Equals(schema, SchemaName, StringComparison.Ordinal))
        {
            throw new MatchLaunchException(
                $"field 'schema' must be '{SchemaName}', got '{schema}'");
        }

        var seedElement = Require(root, "seed", "seed");
        if (seedElement.ValueKind != JsonValueKind.Number || !seedElement.TryGetUInt64(out var seed))
        {
            throw new MatchLaunchException("field 'seed' must be a non-negative integer");
        }

        var packElement = RequireObject(root, "pack", "pack");
        var pack = new MatchLaunchPack(
            RequireNonEmptyString(packElement, "id", "pack.id"),
            RequireSha256(packElement, "sha256", "pack.sha256"));

        var mapElement = RequireObject(root, "map", "map");
        var map = new MatchLaunchMap(
            RequireString(mapElement, "path", "map.path"),
            OptionalSha256(mapElement, "sha256", "map.sha256"));

        var rulesElement = RequireObject(root, "rules", "rules");
        var tickMilliseconds = RequireInt(rulesElement, "tick_ms", "rules.tick_ms", minimum: 1);
        var startingResources = RequireLong(
            rulesElement, "starting_resources", "rules.starting_resources", minimum: 0);
        var commandPointMultiplier = RequireFixed(
            rulesElement, "command_point_multiplier", "rules.command_point_multiplier", allowZero: true);
        var fogOfWar = RequireBoolean(rulesElement, "fog_of_war", "rules.fog_of_war");
        var gameSpeed = RequireFixed(rulesElement, "game_speed", "rules.game_speed", allowZero: false);
        var victory = RequireEnum(
            rulesElement, "victory", "rules.victory", "annihilation", "fortress", "timed");
        var classic = OptionalBoolean(rulesElement, "classic", "rules.classic") ?? false;
        var improvements = ReadImprovements(rulesElement, classic);
        var rules = new MatchLaunchRules(
            tickMilliseconds,
            startingResources,
            commandPointMultiplier,
            fogOfWar,
            gameSpeed,
            victory,
            classic,
            improvements);

        var playersElement = Require(root, "players", "players");
        RequireKind(playersElement, JsonValueKind.Array, "players", "array");
        if (playersElement.GetArrayLength() is < 1 or > 8)
        {
            throw new MatchLaunchException("field 'players' must contain 1 through 8 players");
        }
        var players = new List<MatchLaunchPlayer>(playersElement.GetArrayLength());
        var playerIndex = 0;
        foreach (var playerElement in playersElement.EnumerateArray())
        {
            var prefix = $"players[{playerIndex}]";
            RequireKind(playerElement, JsonValueKind.Object, prefix, "object");
            players.Add(new MatchLaunchPlayer(
                RequireInt(playerElement, "seat", prefix + ".seat", 0, 7),
                RequireInt(playerElement, "team", prefix + ".team", 0),
                RequireNonEmptyString(playerElement, "faction", prefix + ".faction"),
                RequireEnum(playerElement, "controller", prefix + ".controller", "human", "ai", "observer", "none"),
                OptionalEnum(playerElement, "ai_difficulty", prefix + ".ai_difficulty", "easy", "medium", "hard", "brutal"),
                OptionalInt(playerElement, "color", prefix + ".color", 0),
                OptionalInt(playerElement, "start_position", prefix + ".start_position", 0),
                OptionalFixed(playerElement, "handicap", prefix + ".handicap", allowZero: true, maximum: Fixed64.One),
                OptionalString(playerElement, "custom_hero", prefix + ".custom_hero"),
                OptionalString(playerElement, "name", prefix + ".name")));
            playerIndex++;
        }

        var mode = OptionalEnum(root, "mode", "mode", "skirmish", "multiplayer", "campaign", "wotr", "tutorial")
            ?? "skirmish";
        var mission = OptionalString(root, "mission", "mission");
        return new MatchLaunch(
            schema,
            seed,
            pack,
            map,
            rules,
            new ReadOnlyCollection<MatchLaunchPlayer>(players),
            mode,
            mission);
    }

    private static IReadOnlyDictionary<string, bool> ReadImprovements(JsonElement rules, bool classic)
    {
        var result = new SortedDictionary<string, bool>(StringComparer.Ordinal);
        if (!rules.TryGetProperty("improvements", out var improvements))
        {
            return new ReadOnlyDictionary<string, bool>(result);
        }
        RequireKind(improvements, JsonValueKind.Object, "rules.improvements", "object");
        foreach (var property in improvements.EnumerateObject())
        {
            if (property.Value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            {
                throw new MatchLaunchException(
                    $"field 'rules.improvements.{property.Name}' must be a boolean");
            }
            result.Add(property.Name, classic ? false : property.Value.GetBoolean());
        }
        return new ReadOnlyDictionary<string, bool>(result);
    }

    private static JsonElement Require(JsonElement owner, string name, string path) =>
        owner.TryGetProperty(name, out var value)
            ? value
            : throw new MatchLaunchException($"missing required field '{path}'");

    private static JsonElement RequireObject(JsonElement owner, string name, string path)
    {
        var value = Require(owner, name, path);
        RequireKind(value, JsonValueKind.Object, path, "object");
        return value;
    }

    private static void RequireKind(JsonElement value, JsonValueKind expected, string path, string description)
    {
        if (value.ValueKind != expected)
        {
            throw new MatchLaunchException($"field '{path}' must be an {description}");
        }
    }

    private static string RequireString(JsonElement owner, string name, string path)
    {
        var value = Require(owner, name, path);
        if (value.ValueKind != JsonValueKind.String)
        {
            throw new MatchLaunchException($"field '{path}' must be a string");
        }
        return value.GetString()!;
    }

    private static string RequireNonEmptyString(JsonElement owner, string name, string path)
    {
        var value = RequireString(owner, name, path);
        return value.Length > 0
            ? value
            : throw new MatchLaunchException($"field '{path}' must be a non-empty string");
    }

    private static string? OptionalString(JsonElement owner, string name, string path)
    {
        if (!owner.TryGetProperty(name, out var value))
        {
            return null;
        }
        if (value.ValueKind != JsonValueKind.String)
        {
            throw new MatchLaunchException($"field '{path}' must be a string");
        }
        return value.GetString();
    }

    private static string RequireEnum(JsonElement owner, string name, string path, params string[] allowed) =>
        OptionalEnum(owner, name, path, allowed)
        ?? throw new MatchLaunchException($"missing required field '{path}'");

    private static string? OptionalEnum(JsonElement owner, string name, string path, params string[] allowed)
    {
        var value = OptionalString(owner, name, path);
        if (value == null)
        {
            return null;
        }
        if (!allowed.Contains(value, StringComparer.Ordinal))
        {
            throw new MatchLaunchException(
                $"field '{path}' must be one of: {string.Join(", ", allowed)}");
        }
        return value;
    }

    private static bool RequireBoolean(JsonElement owner, string name, string path) =>
        OptionalBoolean(owner, name, path)
        ?? throw new MatchLaunchException($"missing required field '{path}'");

    private static bool? OptionalBoolean(JsonElement owner, string name, string path)
    {
        if (!owner.TryGetProperty(name, out var value))
        {
            return null;
        }
        if (value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
        {
            throw new MatchLaunchException($"field '{path}' must be a boolean");
        }
        return value.GetBoolean();
    }

    private static int RequireInt(
        JsonElement owner, string name, string path, int minimum, int maximum = int.MaxValue) =>
        OptionalInt(owner, name, path, minimum, maximum)
        ?? throw new MatchLaunchException($"missing required field '{path}'");

    private static int? OptionalInt(
        JsonElement owner, string name, string path, int minimum, int maximum = int.MaxValue)
    {
        if (!owner.TryGetProperty(name, out var value))
        {
            return null;
        }
        if (value.ValueKind != JsonValueKind.Number
            || !value.TryGetInt32(out var number)
            || number < minimum
            || number > maximum)
        {
            throw new MatchLaunchException(
                $"field '{path}' must be an integer in {minimum}..{maximum}");
        }
        return number;
    }

    private static long RequireLong(JsonElement owner, string name, string path, long minimum)
    {
        var value = Require(owner, name, path);
        if (value.ValueKind != JsonValueKind.Number
            || !value.TryGetInt64(out var number)
            || number < minimum)
        {
            throw new MatchLaunchException(
                $"field '{path}' must be an integer greater than or equal to {minimum}");
        }
        return number;
    }

    private static Fixed64 RequireFixed(
        JsonElement owner, string name, string path, bool allowZero, Fixed64? maximum = null) =>
        OptionalFixed(owner, name, path, allowZero, maximum)
        ?? throw new MatchLaunchException($"missing required field '{path}'");

    private static Fixed64? OptionalFixed(
        JsonElement owner, string name, string path, bool allowZero, Fixed64? maximum = null)
    {
        if (!owner.TryGetProperty(name, out var value))
        {
            return null;
        }
        if (value.ValueKind != JsonValueKind.Number
            || !TryParseExactFixed(value.GetRawText(), out var fixedValue)
            || fixedValue < Fixed64.Zero
            || (!allowZero && fixedValue == Fixed64.Zero)
            || (maximum.HasValue && fixedValue > maximum.Value))
        {
            throw new MatchLaunchException($"field '{path}' has an invalid numeric value");
        }
        return fixedValue;
    }

    private static bool TryParseExactFixed(string raw, out Fixed64 value)
    {
        value = Fixed64.Zero;
        if (!decimal.TryParse(raw, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed))
        {
            return false;
        }
        var bits = decimal.GetBits(parsed);
        var scale = (bits[3] >> 16) & 0xff;
        var negative = (bits[3] & unchecked((int)0x80000000)) != 0;
        var numerator = ((BigInteger)(uint)bits[2] << 64)
            | ((BigInteger)(uint)bits[1] << 32)
            | (uint)bits[0];
        if (negative)
        {
            numerator = -numerator;
        }
        var denominator = BigInteger.Pow(10, scale);
        var rawFixed = (numerator << Fixed64.FractionBits) / denominator;
        if (rawFixed > long.MaxValue || rawFixed < long.MinValue)
        {
            return false;
        }
        value = Fixed64.FromRaw((long)rawFixed);
        return true;
    }

    private static string RequireSha256(JsonElement owner, string name, string path) =>
        ValidateSha256(RequireString(owner, name, path), path);

    private static string? OptionalSha256(JsonElement owner, string name, string path)
    {
        var value = OptionalString(owner, name, path);
        return value == null ? null : ValidateSha256(value, path);
    }

    private static string ValidateSha256(string value, string path)
    {
        if (value.Length != 64 || value.Any(character => character is not (>= '0' and <= '9') and not (>= 'a' and <= 'f')))
        {
            throw new MatchLaunchException($"field '{path}' must be 64 lowercase hexadecimal characters");
        }
        return value;
    }
}

public sealed class MatchLaunchException : Exception
{
    public MatchLaunchException(string message) : base(message)
    {
    }

    public MatchLaunchException(string message, Exception inner) : base(message, inner)
    {
    }
}
