namespace OpenBfme.Sim;

/// <summary>Shared deterministic trigger mux for SAGE *Upgrade modules.</summary>
public abstract class UpgradeTriggeredModuleBase : ModuleBase
{
    private readonly string[] _triggeredBy;
    private readonly string[] _conflictsWith;
    private readonly bool _requiresAll;
    private bool _consumed;

    protected UpgradeTriggeredModuleBase(ModuleSpec spec) : base(spec)
    {
        _triggeredBy = Tokens(spec.GetString("TriggeredBy", ""));
        _conflictsWith = Tokens(spec.GetString("ConflictsWith", ""));
        _requiresAll = spec.GetLong("RequiresAllTriggers", 0) != 0;
    }

    public bool Consumed => _consumed;

    internal void Evaluate(SimWorld world, GameObject self)
    {
        if (_consumed || _triggeredBy.Length == 0) return;
        if (_conflictsWith.Any(name => world.ObjectHasUpgrade(self, name)))
        {
            _consumed = true;
            return;
        }
        var satisfied = _requiresAll
            ? _triggeredBy.All(name => world.ObjectHasUpgrade(self, name))
            : _triggeredBy.Any(name => world.ObjectHasUpgrade(self, name));
        if (!satisfied) return;
        _consumed = true;
        ApplyEffect(world, self);
    }

    protected abstract void ApplyEffect(SimWorld world, GameObject self);

    protected static string FirstToken(ModuleSpec spec, params string[] names)
    {
        foreach (var name in names)
        {
            var tokens = Tokens(spec.GetString(name, ""));
            if (tokens.Length > 0) return tokens[0];
        }
        return "";
    }

    protected internal static string[] Tokens(string value) =>
        value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);

    public override void WriteState(CanonicalWriter writer) => writer.WriteBool(_consumed);
    public override void ReadState(CanonicalReader reader) => _consumed = reader.ReadBool();
}

internal sealed class ObjectTechState
{
    private readonly SortedSet<string> _upgrades = new(StringComparer.Ordinal);

    public ObjectTechState(string commandSet) => CurrentCommandSet = commandSet;

    public string CurrentCommandSet { get; private set; }
    public IReadOnlySet<string> Upgrades => _upgrades;
    public bool AddUpgrade(string name) => _upgrades.Add(name);
    public bool HasUpgrade(string name) => _upgrades.Contains(name);
    public void SetCommandSet(string name) => CurrentCommandSet = name;

    public void Write(CanonicalWriter writer)
    {
        writer.WriteString(CurrentCommandSet);
        writer.WriteInt(_upgrades.Count);
        foreach (var upgrade in _upgrades) writer.WriteString(upgrade);
    }

    public void Read(CanonicalReader reader)
    {
        CurrentCommandSet = reader.ReadString();
        _upgrades.Clear();
        var count = reader.ReadInt();
        if (count < 0) throw new InvalidDataException("Negative object upgrade count");
        for (var index = 0; index < count; index++) _upgrades.Add(reader.ReadString());
    }
}
