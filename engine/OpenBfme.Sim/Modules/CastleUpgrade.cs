namespace OpenBfme.Sim;

/// <summary>
/// When TriggeredBy fires, grants each authored Upgrade through the standard
/// evaluator to the castle object and, when WallUpgradeRadius is authored, all
/// allied structures/walls inside that exact fixed-point radius. Selection is
/// ascending object id. Castle-piece topology beyond radius membership is
/// deferred until the castle graph is represented in snapshots.
/// </summary>
[SageModule("CastleUpgrade", ModuleTier.Structural)]
public sealed class CastleUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "CastleUpgrade";
    private readonly string[] _upgrades;
    private readonly Fixed64 _radius;

    public CastleUpgradeModule(ModuleSpec spec) : base(spec)
    {
        _upgrades = Tokens(spec.GetString("Upgrade", ""));
        _radius = spec.GetFixed("WallUpgradeRadiusRaw", Fixed64.Zero);
    }

    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        foreach (var upgrade in _upgrades) world.GrantObjectUpgrade(self, upgrade);
        if (_radius <= Fixed64.Zero) return;
        var radiusSquared = _radius * _radius;
        foreach (var target in world.Objects.Values)
        {
            if (target.Id == self.Id || target.Team != self.Team || target.IsDying
                || self.Position.DistanceSquaredTo(target.Position) > radiusSquared) continue;
            if (!target.Template.KindOf.Any(kind => kind.Equals("STRUCTURE", StringComparison.OrdinalIgnoreCase)
                || kind.Equals("WALL", StringComparison.OrdinalIgnoreCase))) continue;
            foreach (var upgrade in _upgrades) world.GrantObjectUpgrade(target, upgrade);
        }
    }
}
