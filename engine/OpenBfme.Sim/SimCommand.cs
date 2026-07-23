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
    public IReadOnlyDictionary<string, CommandValue> Args => _args;

    private readonly SortedDictionary<string, CommandValue> _args;

    public SimCommand(int tick, int team, int seq, string type, IEnumerable<KeyValuePair<string, CommandValue>>? args = null)
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
}

/// <summary>Canonical command argument: integer, fixed-point scalar, or string. Nothing else.</summary>
public readonly struct CommandValue
{
    public CommandValueKind Kind { get; }
    public long LongValue { get; }
    public string? StringValue { get; }

    private CommandValue(CommandValueKind kind, long longValue, string? stringValue)
    {
        Kind = kind;
        LongValue = longValue;
        StringValue = stringValue;
    }

    public static CommandValue OfLong(long value) => new(CommandValueKind.Long, value, null);
    public static CommandValue OfFixed(Fixed64 value) => new(CommandValueKind.Fixed, value.Raw, null);
    public static CommandValue OfString(string value) => new(CommandValueKind.String, 0, value ?? throw new ArgumentNullException(nameof(value)));

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
            _ => throw new InvalidDataException($"Unknown command value kind {kind}"),
        };
    }
}
