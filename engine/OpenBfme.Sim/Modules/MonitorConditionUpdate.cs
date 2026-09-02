namespace OpenBfme.Sim;

/// <summary>Maps authored model-condition and weapon-set flags to their corresponding command sets.</summary>
[SageModule("MonitorConditionUpdate", ModuleTier.Structural)]
public sealed class MonitorConditionUpdateModule : ModuleBase, IPresentationStateModule
{
    public const string TypeName = "MonitorConditionUpdate";
    public const int PresentationMonitoredCondition = 1 << 6;
    private readonly string[] _modelFlags;
    private readonly string[] _weaponFlags;
    private readonly string _modelCommandSet;
    private readonly string _weaponCommandSet;
    private bool _modelActive;
    private bool _weaponActive;

    public MonitorConditionUpdateModule(ModuleSpec spec) : base(spec)
    {
        _modelFlags = ModuleRuntime.Tokens(spec.GetString("ModelConditionFlags", ""));
        _weaponFlags = ModuleRuntime.Tokens(spec.GetString("WeaponSetFlags", ""));
        _modelCommandSet = spec.GetString("ModelConditionCommandSet", "");
        _weaponCommandSet = spec.GetString("WeaponToggleCommandSet", "");
    }

    public bool IsActive => _modelActive || _weaponActive;
    public bool IsModelConditionActive => _modelActive;
    public bool IsWeaponToggleActive => _weaponActive;
    public int PresentationStateBits => IsActive ? PresentationMonitoredCondition : 0;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        _modelActive = Matches(self, _modelFlags, modelCondition: true);
        _weaponActive = Matches(self, _weaponFlags, modelCondition: false);
        if (_modelActive && _modelCommandSet.Length > 0) self.TrySetCurrentCommandSet(_modelCommandSet);
        if (_weaponActive && _weaponCommandSet.Length > 0) self.TrySetCurrentCommandSet(_weaponCommandSet);
    }

    private static bool Matches(GameObject self, IReadOnlyList<string> flags, bool modelCondition) =>
        flags.Count > 0 && flags.All(flag => self.ConditionTokens.Contains(flag)
            || (modelCondition && self.ConditionTokens.Contains("MODEL:" + flag)));

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_modelActive);
        writer.WriteBool(_weaponActive);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _modelActive = reader.ReadBool();
        _weaponActive = reader.ReadBool();
    }
}
