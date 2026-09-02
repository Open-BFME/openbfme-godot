namespace OpenBfme.Sim;

/// <summary>
/// Adds/removes authored model-condition flags through the upgrade evaluator.
/// AddTempConditionFlag expires after TempConditionTime using deterministic
/// tick rounding and canonical state. Permanent has no distinct removal path
/// because bundle-v1 has no upgrade-revocation command; visual transitions are
/// deferred to presentation.
/// </summary>
[SageModule("ModelConditionUpgrade", ModuleTier.Structural)]
public sealed class ModelConditionUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "ModelConditionUpgrade";
    private readonly string[] _add;
    private readonly string[] _remove;
    private readonly string[] _temporary;
    private readonly long _temporaryMilliseconds;
    private int _temporaryTicksRemaining;

    public ModelConditionUpgradeModule(ModuleSpec spec) : base(spec)
    {
        _add = Tokens(spec.GetString("AddConditionFlags", ""));
        _remove = Tokens(spec.GetString("RemoveConditionFlags", ""));
        _temporary = Tokens(spec.GetString("AddTempConditionFlag", ""));
        _temporaryMilliseconds = Math.Max(0, spec.GetLong("TempConditionTime", 0));
    }

    public int TemporaryTicksRemaining => _temporaryTicksRemaining;

    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        foreach (var condition in _remove) self.SetConditionToken(condition, false);
        foreach (var condition in _add) self.SetConditionToken(condition);
        foreach (var condition in _temporary) self.SetConditionToken(condition);
        if (_temporary.Length > 0 && _temporaryMilliseconds > 0)
            _temporaryTicksRemaining = IniValueReader.MillisecondsToTicks(
                _temporaryMilliseconds, world.TickMilliseconds);
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_temporaryTicksRemaining <= 0) return;
        _temporaryTicksRemaining--;
        if (_temporaryTicksRemaining != 0) return;
        foreach (var condition in _temporary) self.SetConditionToken(condition, false);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        base.WriteState(writer);
        writer.WriteInt(_temporaryTicksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        base.ReadState(reader);
        _temporaryTicksRemaining = reader.ReadInt();
    }
}
