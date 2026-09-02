using System.Collections.ObjectModel;
using System.Globalization;
using System.Numerics;
using System.Text;
using System.Text.Json;

namespace OpenBfme.Sim;

/// <summary>Validated wire bundle from contracts/command-v1.schema.json.</summary>
public sealed record SimCommandBundle
{
    private static readonly HashSet<string> AllowedTypes = new(StringComparer.Ordinal)
    {
        "move", "attack_move", "attack", "stop", "hold", "stance",
        "formation", "build", "train", "cancel", "ability", "rally",
        "sell", "garrison", "evacuate", "upgrade", "power",
    };

    private static readonly HashSet<string> IntegerArgs = new(StringComparer.Ordinal)
    {
        "object", "target", "index", "count",
    };

    private static readonly HashSet<string> FixedArgs = new(StringComparer.Ordinal)
    {
        "x", "y",
    };

    private static readonly HashSet<string> StringArgs = new(StringComparer.Ordinal)
    {
        "name", "template", "stance", "formation", "ability", "upgrade", "power",
    };

    public const string SchemaName = "openbfme.command.v1";

    public string Schema { get; }
    public int Tick { get; }
    public int Seat { get; }
    public int Seq { get; }
    public IReadOnlyList<SimCommand> Commands { get; }

    public SimCommandBundle(
        string schema,
        int tick,
        int seat,
        int seq,
        IReadOnlyList<SimCommand> commands)
    {
        if (schema != SchemaName)
        {
            throw new CommandContractException($"field 'schema' must be '{SchemaName}'");
        }
        if (tick < 0 || seat is < 0 or > 7 || seq < 0)
        {
            throw new CommandContractException("tick, seat, and seq are outside the command-v1 range");
        }
        if (commands.Count == 0)
        {
            throw new CommandContractException("field 'commands' must not be empty");
        }
        Schema = schema;
        Tick = tick;
        Seat = seat;
        Seq = seq;
        Commands = new ReadOnlyCollection<SimCommand>(commands.ToArray());
    }

    public static SimCommandBundle Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        return Parse(File.ReadAllBytes(path));
    }

    public static SimCommandBundle Parse(string json)
    {
        ArgumentNullException.ThrowIfNull(json);
        return Parse(Encoding.UTF8.GetBytes(json));
    }

    public static SimCommandBundle Parse(ReadOnlySpan<byte> utf8Json)
    {
        try
        {
            using var document = JsonDocument.Parse(utf8Json.ToArray());
            return ParseRoot(document.RootElement);
        }
        catch (JsonException exception)
        {
            throw new CommandContractException("command bundle JSON is invalid", exception);
        }
    }

    public byte[] ToJsonBytes()
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream))
        {
            writer.WriteStartObject();
            writer.WriteString("schema", SchemaName);
            writer.WriteNumber("tick", Tick);
            writer.WriteNumber("seat", Seat);
            writer.WriteNumber("seq", Seq);
            writer.WritePropertyName("commands");
            writer.WriteStartArray();
            foreach (var command in Commands)
            {
                writer.WriteStartObject();
                writer.WriteString("type", command.Type);
                writer.WritePropertyName("args");
                writer.WriteStartObject();
                foreach (var (name, value) in command.Args)
                {
                    writer.WritePropertyName(name);
                    WriteValue(writer, value);
                }
                writer.WriteEndObject();
                writer.WriteEndObject();
            }
            writer.WriteEndArray();
            writer.WriteEndObject();
        }
        stream.WriteByte((byte)'\n');
        return stream.ToArray();
    }

    public string ToJson() => Encoding.UTF8.GetString(ToJsonBytes());

    public IReadOnlyList<SimCommand> ResolveTeams(Func<int, int> teamForSeat)
    {
        ArgumentNullException.ThrowIfNull(teamForSeat);
        var team = teamForSeat(Seat);
        if (team < 0)
        {
            throw new CommandContractException($"seat {Seat} resolved to invalid team {team}");
        }
        return Commands.Select(command => command.WithTeam(team)).ToArray();
    }

    public static IReadOnlyList<SimCommand> MergeForTick(
        IEnumerable<SimCommandBundle> bundles,
        Func<int, int>? teamForSeat = null)
    {
        ArgumentNullException.ThrowIfNull(bundles);
        teamForSeat ??= static seat => seat;
        var rows = new List<(SimCommand Command, int BundleOrder, int CommandOrder)>();
        int? tick = null;
        var bundleOrder = 0;
        foreach (var bundle in bundles)
        {
            tick ??= bundle.Tick;
            if (bundle.Tick != tick.Value)
            {
                throw new CommandContractException("all merged bundles must target the same tick");
            }
            var resolved = bundle.ResolveTeams(teamForSeat);
            for (var commandOrder = 0; commandOrder < resolved.Count; commandOrder++)
            {
                rows.Add((resolved[commandOrder], bundleOrder, commandOrder));
            }
            bundleOrder++;
        }
        rows.Sort(static (left, right) =>
        {
            var byTeam = left.Command.Team.CompareTo(right.Command.Team);
            if (byTeam != 0) return byTeam;
            var bySeq = left.Command.Seq.CompareTo(right.Command.Seq);
            if (bySeq != 0) return bySeq;
            var byBundle = left.BundleOrder.CompareTo(right.BundleOrder);
            return byBundle != 0 ? byBundle : left.CommandOrder.CompareTo(right.CommandOrder);
        });
        return rows.Select(row => row.Command).ToArray();
    }

    private static SimCommandBundle ParseRoot(JsonElement root)
    {
        RequireObject(root, "$", new[] { "schema", "tick", "seat", "seq", "commands" });
        var schema = RequireString(root, "schema", "schema");
        if (schema != SchemaName)
        {
            throw new CommandContractException($"field 'schema' must be '{SchemaName}', got '{schema}'");
        }
        var tick = RequireInt(root, "tick", "tick", 0);
        var seat = RequireInt(root, "seat", "seat", 0, 7);
        var seq = RequireInt(root, "seq", "seq", 0);
        var commandArray = Require(root, "commands", "commands");
        if (commandArray.ValueKind != JsonValueKind.Array || commandArray.GetArrayLength() == 0)
        {
            throw new CommandContractException("field 'commands' must be a non-empty array");
        }
        var commands = new List<SimCommand>(commandArray.GetArrayLength());
        var index = 0;
        foreach (var commandElement in commandArray.EnumerateArray())
        {
            var path = $"commands[{index}]";
            RequireObject(commandElement, path, new[] { "type", "args" });
            var type = RequireString(commandElement, "type", path + ".type");
            if (!AllowedTypes.Contains(type))
            {
                throw new CommandContractException($"field '{path}.type' has unknown command type '{type}'");
            }
            var argsElement = Require(commandElement, "args", path + ".args");
            if (argsElement.ValueKind != JsonValueKind.Object)
            {
                throw new CommandContractException($"field '{path}.args' must be an object");
            }
            var args = ParseArgs(argsElement, path + ".args");
            commands.Add(new SimCommand(tick, seat, seq, type, args, sourceSeat: seat));
            index++;
        }
        return new SimCommandBundle(schema, tick, seat, seq, commands);
    }

    private static IReadOnlyList<KeyValuePair<string, CommandValue>> ParseArgs(JsonElement args, string path)
    {
        var result = new List<KeyValuePair<string, CommandValue>>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in args.EnumerateObject())
        {
            if (!seen.Add(property.Name))
            {
                throw new CommandContractException($"field '{path}.{property.Name}' occurs more than once");
            }
            CommandValue value;
            if (property.Name == "objects")
            {
                value = CommandValue.OfLongList(ReadObjectIds(property.Value, path + ".objects"));
            }
            else if (IntegerArgs.Contains(property.Name))
            {
                if (property.Value.ValueKind != JsonValueKind.Number
                    || !property.Value.TryGetInt64(out var number)
                    || number < (property.Name is "index" ? 0 : 1))
                {
                    throw new CommandContractException($"field '{path}.{property.Name}' must be a valid integer");
                }
                value = CommandValue.OfLong(number);
            }
            else if (FixedArgs.Contains(property.Name))
            {
                if (property.Value.ValueKind != JsonValueKind.Number
                    || !FixedJson.TryParse(property.Value.GetRawText(), out var number))
                {
                    throw new CommandContractException($"field '{path}.{property.Name}' must be a finite number");
                }
                value = CommandValue.OfFixed(number);
            }
            else if (StringArgs.Contains(property.Name))
            {
                if (property.Value.ValueKind != JsonValueKind.String
                    || string.IsNullOrEmpty(property.Value.GetString()))
                {
                    throw new CommandContractException($"field '{path}.{property.Name}' must be a non-empty string");
                }
                value = CommandValue.OfString(property.Value.GetString()!);
            }
            else
            {
                throw new CommandContractException($"field '{path}.{property.Name}' is not a command-v1 argument");
            }
            result.Add(new KeyValuePair<string, CommandValue>(property.Name, value));
        }
        return result;
    }

    private static long[] ReadObjectIds(JsonElement value, string path)
    {
        if (value.ValueKind != JsonValueKind.Array || value.GetArrayLength() == 0)
        {
            throw new CommandContractException($"field '{path}' must be a non-empty integer array");
        }
        var result = new long[value.GetArrayLength()];
        var seen = new HashSet<long>();
        var index = 0;
        foreach (var item in value.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.Number
                || !item.TryGetInt64(out var id)
                || id < 1
                || !seen.Add(id))
            {
                throw new CommandContractException($"field '{path}[{index}]' must be a unique positive integer");
            }
            result[index++] = id;
        }
        return result;
    }

    private static void WriteValue(Utf8JsonWriter writer, CommandValue value)
    {
        switch (value.Kind)
        {
            case CommandValueKind.Long:
                writer.WriteNumberValue(value.LongValue);
                break;
            case CommandValueKind.Fixed:
                writer.WriteRawValue(FixedJson.Format(Fixed64.FromRaw(value.LongValue)));
                break;
            case CommandValueKind.String:
                writer.WriteStringValue(value.StringValue);
                break;
            case CommandValueKind.LongList:
                writer.WriteStartArray();
                foreach (var item in value.LongListValue!) writer.WriteNumberValue(item);
                writer.WriteEndArray();
                break;
            default:
                throw new CommandContractException($"unsupported command value kind {value.Kind}");
        }
    }

    private static void RequireObject(JsonElement value, string path, IReadOnlyCollection<string> allowed)
    {
        if (value.ValueKind != JsonValueKind.Object)
        {
            throw new CommandContractException($"field '{path}' must be an object");
        }
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in value.EnumerateObject())
        {
            if (!allowed.Contains(property.Name) || !seen.Add(property.Name))
            {
                throw new CommandContractException($"field '{path}.{property.Name}' is unknown or duplicated");
            }
        }
    }

    private static JsonElement Require(JsonElement owner, string name, string path) =>
        owner.TryGetProperty(name, out var value)
            ? value
            : throw new CommandContractException($"missing required field '{path}'");

    private static string RequireString(JsonElement owner, string name, string path)
    {
        var value = Require(owner, name, path);
        return value.ValueKind == JsonValueKind.String
            ? value.GetString()!
            : throw new CommandContractException($"field '{path}' must be a string");
    }

    private static int RequireInt(JsonElement owner, string name, string path, int minimum, int maximum = int.MaxValue)
    {
        var value = Require(owner, name, path);
        if (value.ValueKind != JsonValueKind.Number
            || !value.TryGetInt32(out var result)
            || result < minimum
            || result > maximum)
        {
            throw new CommandContractException($"field '{path}' must be an integer in {minimum}..{maximum}");
        }
        return result;
    }
}

internal static class FixedJson
{
    public static bool TryParse(string raw, out Fixed64 value)
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
        if (negative) numerator = -numerator;
        var rawFixed = (numerator << Fixed64.FractionBits) / BigInteger.Pow(10, scale);
        if (rawFixed > long.MaxValue || rawFixed < long.MinValue) return false;
        value = Fixed64.FromRaw((long)rawFixed);
        return true;
    }

    public static string Format(Fixed64 value)
    {
        if (value.Raw == 0) return "0";
        var negative = value.Raw < 0;
        var magnitude = BigInteger.Abs(new BigInteger(value.Raw));
        var whole = magnitude >> Fixed64.FractionBits;
        var fraction = magnitude & (Fixed64.OneRaw - 1);
        if (fraction.IsZero) return (negative ? "-" : "") + whole.ToString(CultureInfo.InvariantCulture);
        var decimalFraction = fraction * BigInteger.Pow(5, Fixed64.FractionBits);
        var digits = decimalFraction.ToString(CultureInfo.InvariantCulture).PadLeft(Fixed64.FractionBits, '0').TrimEnd('0');
        return $"{(negative ? "-" : "")}{whole.ToString(CultureInfo.InvariantCulture)}.{digits}";
    }
}

public sealed class CommandContractException : Exception
{
    public CommandContractException(string message) : base(message) { }
    public CommandContractException(string message, Exception inner) : base(message, inner) { }
}
