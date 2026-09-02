namespace OpenBfme.Sim;

/// <summary>
/// Generic SAGE special-power controller. StartsPaused gates the command-v1
/// power path until UnpauseSpecialPowerUpgrade fires. Authored model-condition
/// and attribute-modifier names become authoritative condition tokens on cast;
/// SetModelConditionTime is rounded at the tick boundary and serialized.
/// Audio/FX strings and requirements filters are presentation/selection concerns
/// and are deliberately deferred to their owning lanes.
/// </summary>
[SageModule("SpecialPowerModule", ModuleTier.Structural)]
public sealed class GenericSpecialPowerModule : SpecialPowerEffectModuleBase
{
    public const string TypeName = "SpecialPowerModule";
    private readonly string _powerName;
    private readonly string _attributeModifier;
    private readonly string[] _modelConditions;
    private readonly long _conditionMilliseconds;
    private bool _paused;
    private int _conditionTicksRemaining;

    public GenericSpecialPowerModule(ModuleSpec spec) : base(spec)
    {
        _powerName = spec.GetString("SpecialPowerTemplate", "");
        _attributeModifier = spec.GetString("AttributeModifier", "");
        _modelConditions = UpgradeTriggeredModuleBase.Tokens(spec.GetString("SetModelCondition", ""));
        _conditionMilliseconds = Math.Max(0, spec.GetLong("SetModelConditionTime", 0));
        _paused = spec.GetLong("StartsPaused", 0) != 0 && spec.GetLong("AvailableAtStart", 0) == 0;
    }

    public bool IsPaused => _paused;
    public int ConditionTicksRemaining => _conditionTicksRemaining;

    internal bool Controls(string name) => _powerName.Length == 0
        || _powerName.Equals(name, StringComparison.Ordinal);

    internal void Unpause() => _paused = false;

    internal override bool CanCast(SimWorld world, GameObject caster, SpecialPowerTemplate power) => !_paused;

    internal override void Cast(
        SimWorld world,
        GameObject caster,
        int targetId,
        FixedVector2 targetPosition)
    {
        if (_attributeModifier.Length > 0) caster.SetConditionToken(_attributeModifier);
        foreach (var condition in _modelConditions) caster.SetConditionToken(condition);
        if (_modelConditions.Length > 0 && _conditionMilliseconds > 0)
            _conditionTicksRemaining = IniValueReader.MillisecondsToTicks(
                _conditionMilliseconds, world.TickMilliseconds);
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_conditionTicksRemaining <= 0) return;
        _conditionTicksRemaining--;
        if (_conditionTicksRemaining != 0) return;
        foreach (var condition in _modelConditions) self.SetConditionToken(condition, false);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_paused);
        writer.WriteInt(_conditionTicksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _paused = reader.ReadBool();
        _conditionTicksRemaining = reader.ReadInt();
    }
}
