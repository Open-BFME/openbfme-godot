namespace OpenBfme.Sim;

/// <summary>
/// On death refunds authored RefundPercent of the object's BuildCost when the
/// owning player has UpgradeRequired and an allied structure matches the
/// positive BuildingRequired KindOf tokens. The one-shot refund flag is
/// canonical state. No death FX is owned here.
/// </summary>
[SageModule("RefundDie", ModuleTier.Structural)]
public sealed class RefundDieModule : ModuleBase
{
    public const string TypeName = "RefundDie";
    private readonly Fixed64 _refundMultiplier;
    private readonly string _upgradeRequired;
    private readonly string[] _buildingKinds;
    private bool _refunded;

    public RefundDieModule(ModuleSpec spec) : base(spec)
    {
        _refundMultiplier = spec.StringData.TryGetValue("RefundPercent", out var percent)
            ? IniValueReader.PercentMultiplier(percent, "RefundPercent") : Fixed64.Zero;
        _upgradeRequired = spec.GetString("UpgradeRequired", "");
        _buildingKinds = spec.GetString("BuildingRequired", "")
            .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
            .Where(token => token.StartsWith('+')).Select(token => token[1..]).ToArray();
    }

    public bool Refunded => _refunded;

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (_refunded) return false;
        if (_upgradeRequired.Length > 0 && !world.ObjectHasUpgrade(self, _upgradeRequired)) return false;
        if (_buildingKinds.Length > 0 && !world.Objects.Values.Any(candidate => candidate.Team == self.Team
            && !candidate.IsDead && !candidate.IsDying
            && _buildingKinds.All(kind => candidate.Template.KindOf.Contains(kind,
                StringComparer.OrdinalIgnoreCase)))) return false;
        var refund = EconomyTemplate.ScaleInteger(self.Template.Economy.BuildCost, _refundMultiplier);
        if (refund > 0) world.AddTeamResources(self.Team, refund);
        _refunded = true;
        return false;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteBool(_refunded);
    public override void ReadState(CanonicalReader reader) => _refunded = reader.ReadBool();
}
