namespace OpenBfme.Sim;

/// <summary>
/// Authoritative gate state machine honoring OpenByDefault,
/// PercentOpenForPathing, ResetTimeInMilliseconds, and RepelCollidingUnits.
/// RequestOpen/RequestClose and SetAnimationOpenPercent are the collision and
/// W3D-animation seams; the named collision switch occurs at the authored
/// threshold. Automatic reset and conditions are deterministic and serialized.
/// Sound timing/names are presentation-only.
/// </summary>
[SageModule("GateOpenAndCloseBehavior", ModuleTier.Structural)]
public sealed class GateOpenAndCloseBehaviorModule : ModuleBase
{
    public const string TypeName = "GateOpenAndCloseBehavior";
    private readonly long _resetMilliseconds;
    private readonly long _pathingPercent;
    private readonly bool _repel;
    private bool _open;
    private long _openPercent;
    private int _resetTicksRemaining;

    public GateOpenAndCloseBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _open = spec.GetLong("OpenByDefault", 0) != 0;
        _openPercent = _open ? 100 : 0;
        _resetMilliseconds = Math.Max(0, spec.GetLong("ResetTimeInMilliseconds", 0));
        _pathingPercent = Math.Clamp(spec.GetLong("PercentOpenForPathing", 50), 0, 100);
        _repel = spec.GetLong("RepelCollidingUnits", 0) != 0;
    }

    public bool IsOpen => _open;
    public long OpenPercent => _openPercent;
    public bool IsPathable => _open && _openPercent >= _pathingPercent;
    public bool RepelsCollidingUnits => _repel;

    public void RequestOpen(SimWorld world, GameObject self)
    {
        _open = true;
        _resetTicksRemaining = IniValueReader.MillisecondsToTicks(_resetMilliseconds, world.TickMilliseconds);
        ApplyConditions(self);
    }

    public void RequestClose(GameObject self)
    {
        _open = false;
        _openPercent = 0;
        _resetTicksRemaining = 0;
        ApplyConditions(self);
    }

    /// <summary>Receives the deterministic W3D animation percentage (0..100).</summary>
    public void SetAnimationOpenPercent(GameObject self, long percent)
    {
        _openPercent = Math.Clamp(percent, 0, 100);
        ApplyConditions(self);
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        ApplyConditions(self);
        if (!_open || _resetTicksRemaining <= 0) return;
        _resetTicksRemaining--;
        if (_resetTicksRemaining == 0) RequestClose(self);
    }

    private void ApplyConditions(GameObject self)
    {
        self.SetConditionToken("GATE_OPEN", _open);
        self.SetConditionToken("GATE_CLOSED", !_open);
        self.SetConditionToken("GATE_PATHABLE", IsPathable);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_open);
        writer.WriteLong(_openPercent);
        writer.WriteInt(_resetTicksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _open = reader.ReadBool();
        _openPercent = reader.ReadLong();
        _resetTicksRemaining = reader.ReadInt();
    }
}
