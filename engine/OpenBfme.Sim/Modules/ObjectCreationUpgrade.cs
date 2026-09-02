namespace OpenBfme.Sim;

/// <summary>TriggeredBy upgrade spawns ThingToSpawn/UpgradeObject after Delay and tracks DestroyWhenSold children.</summary>
[SageModule("ObjectCreationUpgrade", ModuleTier.Structural)]
public sealed class ObjectCreationUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "ObjectCreationUpgrade";
    private readonly string _template;
    private readonly string _grantUpgrade;
    private readonly long _delayMilliseconds;
    private readonly bool _destroyWithParent;
    private int _delayTicks;
    private bool _pending;
    private readonly List<int> _children = new();

    public ObjectCreationUpgradeModule(ModuleSpec spec) : base(spec)
    {
        _template = FirstToken(spec, "ThingToSpawn", "UpgradeObject");
        _grantUpgrade = FirstToken(spec, "GrantUpgrade");
        _delayMilliseconds = Math.Max(0, spec.GetLong("Delay", 0));
        _destroyWithParent = ModuleRuntime.ReadBool(spec, "DestroyWhenSold");
    }

    public IReadOnlyList<int> CreatedObjectIds => _children;

    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        if (_delayMilliseconds == 0) Spawn(world, self);
        else
        {
            _pending = true;
            _delayTicks = ModuleRuntime.MillisecondsToTicks(_delayMilliseconds, world.TickMilliseconds);
        }
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_pending && --_delayTicks <= 0)
        {
            _pending = false;
            Spawn(world, self);
        }
    }

    public override void OnDeathStarted(SimWorld world, GameObject self)
    {
        if (!_destroyWithParent) return;
        foreach (var id in _children)
            if (world.Objects.TryGetValue(id, out var child) && !child.IsDead && !child.IsDying)
                world.HandleDeath(child);
    }

    private void Spawn(SimWorld world, GameObject self)
    {
        if (_grantUpgrade.Length > 0) world.GrantUpgrade(self, _grantUpgrade);
        if (_template.Length == 0 || !world.TryGetTemplate(_template, out _)) return;
        var child = world.SpawnObjectFrom(_template, self.Team, self.Position, self,
            self.Elevation, self.HeadingRadians);
        _children.Add(child.Id);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        base.WriteState(writer);
        writer.WriteInt(_delayTicks);
        writer.WriteBool(_pending);
        writer.WriteInt(_children.Count);
        foreach (var id in _children) writer.WriteInt(id);
    }

    public override void ReadState(CanonicalReader reader)
    {
        base.ReadState(reader);
        _delayTicks = reader.ReadInt();
        _pending = reader.ReadBool();
        _children.Clear();
        var count = reader.ReadInt();
        for (var index = 0; index < count; index++) _children.Add(reader.ReadInt());
    }
}
