namespace OpenBfme.Sim;

/// <summary>Persistent structure foundation, rebuilding, and authored window/fire presentation bindings.</summary>
[SageModule("BuildingBehavior", ModuleTier.Structural)]
public sealed class BuildingBehaviorModule : ModuleBase, IPresentationStateModule
{
    public const string TypeName = "BuildingBehavior";
    public const int PresentationRebuildHole = 1 << 7;
    public const int PresentationNightWindows = 1 << 8;
    public const int PresentationFireWindows = 1 << 9;
    private readonly long _rebuildMilliseconds;
    private readonly string[] _nightWindows;
    private readonly string[] _fireWindows;
    private readonly string[] _glowWindows;
    private readonly string[] _fires;
    private int _ticksRemaining;
    private bool _hole;
    private bool _firePresentation;

    public BuildingBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _rebuildMilliseconds = Math.Max(0, spec.GetLong("RebuildTime", 0));
        _nightWindows = ModuleRuntime.Tokens(spec.GetString("NightWindowName", ""));
        _fireWindows = ModuleRuntime.Tokens(spec.GetString("FireWindowName", ""));
        _glowWindows = ModuleRuntime.Tokens(spec.GetString("GlowWindowName", ""));
        _fires = ModuleRuntime.Tokens(spec.GetString("FireName", ""));
    }

    public bool IsRebuildHole => _hole;
    public IReadOnlyList<string> NightWindows => _nightWindows;
    public IReadOnlyList<string> FireWindows => _fireWindows;
    public IReadOnlyList<string> GlowWindows => _glowWindows;
    public IReadOnlyList<string> FireSubObjects => _fires;
    public int PresentationStateBits =>
        (_hole ? PresentationRebuildHole : 0)
        | (_nightWindows.Length > 0 ? PresentationNightWindows : 0)
        | (_firePresentation ? PresentationFireWindows : 0);

    public override void OnCreated(SimWorld world, GameObject self, GameObject? creator)
    {
        foreach (var window in _nightWindows) self.TrySetConditionToken("MODEL:NIGHT:" + window);
    }

    public override void OnDamageReceived(SimWorld world, GameObject self, Fixed64 amount, string damageType)
    {
        if (damageType.Equals("WATER", StringComparison.OrdinalIgnoreCase))
        {
            SetFirePresentation(self, false);
            return;
        }
        if (amount <= Fixed64.Zero
            || (!damageType.Equals("FLAME", StringComparison.OrdinalIgnoreCase)
                && !damageType.Equals("LOGICAL_FIRE", StringComparison.OrdinalIgnoreCase))) return;
        if (_fireWindows.Length == 0 && _glowWindows.Length == 0 && _fires.Length == 0) return;
        SetFirePresentation(self, true);
        foreach (var fire in _fires) world.RaiseEvent(new SimEvent("fire", self.Id, Name: fire));
    }

    private void SetFirePresentation(GameObject self, bool enabled)
    {
        _firePresentation = enabled;
        foreach (var window in _fireWindows) self.TrySetConditionToken("MODEL:FIRE:" + window, enabled);
        foreach (var window in _glowWindows) self.TrySetConditionToken("MODEL:GLOW:" + window, enabled);
    }

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (_hole) return true;
        _hole = true;
        self.MarkDying();
        self.TrySetConditionToken("RUBBLE");
        _ticksRemaining = _rebuildMilliseconds == 0
            ? 0
            : ModuleRuntime.MillisecondsToTicks(_rebuildMilliseconds, world.TickMilliseconds);
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_hole || _ticksRemaining == 0) return;
        if (--_ticksRemaining > 0) return;
        _hole = false;
        self.TrySetConditionToken("RUBBLE", false);
        self.RestoreFromRebuild(self.MaxHealth > Fixed64.Zero ? self.MaxHealth : Fixed64.One);
        world.RaiseEvent(new SimEvent("build_done", self.Id, Name: self.TemplateName));
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksRemaining);
        writer.WriteBool(_hole);
        writer.WriteBool(_firePresentation);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksRemaining = reader.ReadInt();
        _hole = reader.ReadBool();
        _firePresentation = reader.ReadBool();
    }
}
