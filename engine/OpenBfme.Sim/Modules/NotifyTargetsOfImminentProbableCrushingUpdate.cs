namespace OpenBfme.Sim;

/// <summary>
/// No-field SAGE crushing notifier. While the carrier has an authoritative
/// combat target, marks that target IMMINENT_CRUSH and clears the previous mark
/// when engagement changes. Geometry-specific warning distance is deferred to
/// the locomotor/crush collision seam.
/// </summary>
[SageModule("NotifyTargetsOfImminentProbableCrushingUpdate", ModuleTier.Structural)]
public sealed class NotifyTargetsOfImminentProbableCrushingUpdateModule : ModuleBase
{
    public const string TypeName = "NotifyTargetsOfImminentProbableCrushingUpdate";
    private int _notifiedTargetId;

    public NotifyTargetsOfImminentProbableCrushingUpdateModule(ModuleSpec spec) : base(spec) { }
    public int NotifiedTargetId => _notifiedTargetId;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        var current = self.Combat?.EngagedTargetId ?? 0;
        if (current == _notifiedTargetId) return;
        if (_notifiedTargetId != 0 && world.Objects.TryGetValue(_notifiedTargetId, out var oldTarget))
            oldTarget.SetConditionToken("IMMINENT_CRUSH", false);
        _notifiedTargetId = current;
        if (current != 0 && world.Objects.TryGetValue(current, out var target) && target.Team != self.Team)
            target.SetConditionToken("IMMINENT_CRUSH");
        else _notifiedTargetId = 0;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_notifiedTargetId);
    public override void ReadState(CanonicalReader reader) => _notifiedTargetId = reader.ReadInt();
}
