namespace OpenBfme.Sim;

/// <summary>Projects authored weapon/status conditions into model-condition tokens and the presentation bitset.</summary>
[SageModule("MonitorConditionUpdate", ModuleTier.Structural)]
public sealed class MonitorConditionUpdateModule : ModuleBase, IPresentationStateModule
{
    public const string TypeName = "MonitorConditionUpdate";
    public const int PresentationMonitoredCondition = 1 << 6;
    private readonly string[] _watched;
    private readonly string[] _modelFlags;
    private bool _active;

    public MonitorConditionUpdateModule(ModuleSpec spec) : base(spec)
    {
        _watched = ModuleRuntime.Tokens(spec.GetString("WeaponSetFlags",
            spec.GetString("StatusBits", spec.GetString("RequiredStatus", ""))));
        _modelFlags = ModuleRuntime.Tokens(spec.GetString("ModelConditionFlags", ""));
    }

    public bool IsActive => _active;
    public int PresentationStateBits => _active ? PresentationMonitoredCondition : 0;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        _active = _watched.Length > 0 && _watched.All(self.ConditionTokens.Contains);
        foreach (var flag in _modelFlags) self.TrySetConditionToken("MODEL:" + flag, _active);
        var commandSet = Spec.GetString("ModelConditionCommandSet", "");
        if (_active && commandSet.Length > 0) self.TrySetCurrentCommandSet(commandSet);
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteBool(_active);
    public override void ReadState(CanonicalReader reader) => _active = reader.ReadBool();
}
