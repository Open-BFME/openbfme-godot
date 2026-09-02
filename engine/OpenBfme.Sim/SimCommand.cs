namespace OpenBfme.Sim;

/// <summary>
/// The single mutation entry point for the simulation, mirroring the GDScript
/// lockstep contract: commands are scheduled for a future tick and applied at
/// tick start ordered by (team, seq). Args hold only canonical value types.
/// </summary>
public sealed class SimCommand
{
    public int Tick { get; }
    public int Team { get; }
    public int Seq { get; }
    public string Type { get; }
    public int SourceSeat { get; }
    public IReadOnlyDictionary<string, CommandValue> Args => _args;

    private readonly SortedDictionary<string, CommandValue> _args;

    public SimCommand(
        int tick,
        int team,
        int seq,
        string type,
        IEnumerable<KeyValuePair<string, CommandValue>>? args = null,
        int? sourceSeat = null)
    {
        if (tick < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(tick));
        }
        if (team < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(team));
        }
        if (seq < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(seq));
        }
        Tick = tick;
        Team = team;
        Seq = seq;
        Type = type ?? throw new ArgumentNullException(nameof(type));
        SourceSeat = sourceSeat ?? team;
        _args = new SortedDictionary<string, CommandValue>(StringComparer.Ordinal);
        if (args != null)
        {
            foreach (var pair in args)
            {
                _args.Add(pair.Key, pair.Value);
            }
        }
    }

    public long GetLong(string key) => Args.TryGetValue(key, out var value) && value.Kind == CommandValueKind.Long
        ? value.LongValue
        : throw new KeyNotFoundException($"Command '{Type}' missing long arg '{key}'");

    public Fixed64 GetFixed(string key) => Args.TryGetValue(key, out var value) && value.Kind == CommandValueKind.Fixed
        ? Fixed64.FromRaw(value.LongValue)
        : throw new KeyNotFoundException($"Command '{Type}' missing fixed arg '{key}'");

    public string GetString(string key) => Args.TryGetValue(key, out var value) && value.Kind == CommandValueKind.String
        ? value.StringValue!
        : throw new KeyNotFoundException($"Command '{Type}' missing string arg '{key}'");

    public IReadOnlyList<long> GetLongList(string key) =>
        Args.TryGetValue(key, out var value) && value.Kind == CommandValueKind.LongList
            ? value.LongListValue!
            : throw new KeyNotFoundException($"Command '{Type}' missing integer-list arg '{key}'");

    public static SimCommandBundle ParseBundle(string json) => SimCommandBundle.Parse(json);

    public static SimCommandBundle ParseBundle(ReadOnlySpan<byte> utf8Json) =>
        SimCommandBundle.Parse(utf8Json);

    public static byte[] SerializeBundle(SimCommandBundle bundle) => bundle.ToJsonBytes();

    internal SimCommand WithTeam(int team) =>
        new(Tick, team, Seq, Type, _args, SourceSeat);

    internal void WriteTo(CanonicalWriter writer)
    {
        writer.WriteInt(Tick);
        writer.WriteInt(Team);
        writer.WriteInt(Seq);
        writer.WriteString(Type);
        writer.WriteInt(_args.Count);
        foreach (var (key, value) in _args)
        {
            writer.WriteString(key);
            value.WriteTo(writer);
        }
    }

    internal static SimCommand ReadFrom(CanonicalReader reader)
    {
        var tick = reader.ReadInt();
        var team = reader.ReadInt();
        var seq = reader.ReadInt();
        var type = reader.ReadString();
        var count = reader.ReadInt();
        var args = new List<KeyValuePair<string, CommandValue>>(count);
        for (var i = 0; i < count; i++)
        {
            var key = reader.ReadString();
            args.Add(new KeyValuePair<string, CommandValue>(key, CommandValue.ReadFrom(reader)));
        }
        return new SimCommand(tick, team, seq, type, args);
    }
}

public enum CommandValueKind : byte
{
    Long = 1,
    Fixed = 2,
    String = 3,
    LongList = 4,
}

/// <summary>Canonical command argument: integer, fixed-point scalar, string, or ordered object-id list.</summary>
public readonly struct CommandValue
{
    public CommandValueKind Kind { get; }
    public long LongValue { get; }
    public string? StringValue { get; }
    public IReadOnlyList<long>? LongListValue { get; }

    private CommandValue(
        CommandValueKind kind,
        long longValue,
        string? stringValue,
        IReadOnlyList<long>? longListValue = null)
    {
        Kind = kind;
        LongValue = longValue;
        StringValue = stringValue;
        LongListValue = longListValue;
    }

    public static CommandValue OfLong(long value) => new(CommandValueKind.Long, value, null);
    public static CommandValue OfFixed(Fixed64 value) => new(CommandValueKind.Fixed, value.Raw, null);
    public static CommandValue OfString(string value) => new(CommandValueKind.String, 0, value ?? throw new ArgumentNullException(nameof(value)));
    public static CommandValue OfLongList(IEnumerable<long> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        return new CommandValue(CommandValueKind.LongList, 0, null, values.ToArray());
    }

    internal void WriteTo(CanonicalWriter writer)
    {
        writer.WriteByte((byte)Kind);
        switch (Kind)
        {
            case CommandValueKind.Long:
            case CommandValueKind.Fixed:
                writer.WriteLong(LongValue);
                break;
            case CommandValueKind.String:
                writer.WriteString(StringValue!);
                break;
            case CommandValueKind.LongList:
                writer.WriteInt(LongListValue!.Count);
                foreach (var value in LongListValue)
                {
                    writer.WriteLong(value);
                }
                break;
            default:
                throw new InvalidOperationException($"Unserializable command value kind {Kind}");
        }
    }

    internal static CommandValue ReadFrom(CanonicalReader reader)
    {
        var kind = (CommandValueKind)reader.ReadByte();
        return kind switch
        {
            CommandValueKind.Long => OfLong(reader.ReadLong()),
            CommandValueKind.Fixed => OfFixed(Fixed64.FromRaw(reader.ReadLong())),
            CommandValueKind.String => OfString(reader.ReadString()),
            CommandValueKind.LongList => ReadLongList(reader),
            _ => throw new InvalidDataException($"Unknown command value kind {kind}"),
        };
    }

    private static CommandValue ReadLongList(CanonicalReader reader)
    {
        var count = reader.ReadInt();
        if (count < 0)
        {
            throw new InvalidDataException("Negative command integer-list length");
        }
        var values = new long[count];
        for (var index = 0; index < count; index++)
        {
            values[index] = reader.ReadLong();
        }
        return OfLongList(values);
    }
}
