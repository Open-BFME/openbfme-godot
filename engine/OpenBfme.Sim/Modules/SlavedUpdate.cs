using System.Text.RegularExpressions;

namespace OpenBfme.Sim;

/// <summary>
/// Maintains an explicit master/slave relationship. DieOnMastersDeath owns the
/// dependent death path; LeashRange and GuardPositionOffset pull a slave back
/// through its existing locomotor when it strays. MarkUnselectable is an
/// authoritative condition. Guard wander, attack range, EVA, and fade visuals
/// are deferred until their respective planner/presentation seams exist.
/// </summary>
[SageModule("SlavedUpdate", ModuleTier.Structural)]
public sealed partial class SlavedUpdateModule : ModuleBase
{
    public const string TypeName = "SlavedUpdate";
    private readonly bool _dieWithMaster;
    private readonly bool _markUnselectable;
    private readonly Fixed64 _leashRange;
    private readonly FixedVector2 _guardOffset;
    private int _masterId;

    public SlavedUpdateModule(ModuleSpec spec) : base(spec)
    {
        _dieWithMaster = spec.GetLong("DieOnMastersDeath", 0) != 0;
        _markUnselectable = spec.GetLong("MarkUnselectable", 0) != 0;
        _leashRange = spec.GetFixed("LeashRangeRaw", spec.GetFixed("GuardMaxRangeRaw", Fixed64.Zero));
        _guardOffset = ParseOffset(spec.GetString("GuardPositionOffset", ""));
    }

    public int MasterId => _masterId;
    public void SetMaster(int masterId)
    {
        if (masterId < 1) throw new ArgumentOutOfRangeException(nameof(masterId));
        _masterId = masterId;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_markUnselectable) self.SetConditionToken("UNSELECTABLE");
        if (_masterId == 0) return;
        if (!world.Objects.TryGetValue(_masterId, out var master) || master.IsDead || master.IsDying)
        {
            if (_dieWithMaster) world.HandleDeath(self);
            return;
        }
        if (_leashRange <= Fixed64.Zero) return;
        var guard = master.Position + _guardOffset;
        if (self.Position.DistanceSquaredTo(guard) <= _leashRange * _leashRange) return;
        if (self.FindModule<LocomotorModule>() is { } locomotor)
            locomotor.SetOrder(guard, MoveOrderKind.Move);
        else self.FindModule<LinearMoverModule>()?.SetDestination(guard);
    }

    private static FixedVector2 ParseOffset(string value)
    {
        var match = Offset().Match(value);
        if (!match.Success) return FixedVector2.Zero;
        return new FixedVector2(Parse(match.Groups[1].Value), Parse(match.Groups[2].Value));
    }

    private static Fixed64 Parse(string value) => decimal.TryParse(value,
        System.Globalization.NumberStyles.Number, System.Globalization.CultureInfo.InvariantCulture, out var number)
        ? Fixed64.FromFraction((long)(number * 1_000_000m), 1_000_000) : Fixed64.Zero;

    [GeneratedRegex(@"X:([-+0-9.]+)\s+Y:([-+0-9.]+)", RegexOptions.IgnoreCase)]
    private static partial Regex Offset();

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_masterId);
    public override void ReadState(CanonicalReader reader) => _masterId = reader.ReadInt();
}
