namespace OpenBfme.Sim;

/// <summary>Copies the producer's object upgrades, or the named Upgrade from the nearest authored source filter.</summary>
[SageModule("InheritUpgradeCreate", ModuleTier.Structural)]
public sealed class InheritUpgradeCreateModule : ModuleBase
{
    public const string TypeName = "InheritUpgradeCreate";
    private readonly string _upgrade;
    private readonly string _filter;
    private readonly Fixed64 _radius;
    private bool _inherited;

    public InheritUpgradeCreateModule(ModuleSpec spec) : base(spec)
    {
        _upgrade = spec.GetString("Upgrade", "");
        _filter = spec.GetString("ObjectFilter", "");
        _radius = ModuleRuntime.ReadFixed(spec, "Radius", Fixed64.Zero);
    }

    public override void OnCreated(SimWorld world, GameObject self, GameObject? creator)
    {
        var source = creator;
        if (source == null || (_filter.Length > 0 && !ModuleRuntime.MatchesKindOf(source, _filter)))
        {
            var limit = _radius * _radius;
            source = world.Objects.Values
                .Where(value => value.Id != self.Id && value.Team == self.Team
                    && !value.IsDead && !value.IsDying
                    && (_filter.Length == 0 || ModuleRuntime.MatchesKindOf(value, _filter))
                    && (_radius <= Fixed64.Zero || self.Position.DistanceSquaredTo(value.Position) <= limit))
                .OrderBy(value => self.Position.DistanceSquaredTo(value.Position))
                .ThenBy(value => value.Id)
                .FirstOrDefault();
        }
        if (source == null) return;
        if (_upgrade.Length > 0)
        {
            if (world.ObjectHasUpgrade(source, _upgrade)) _inherited = world.GrantUpgrade(self, _upgrade);
            return;
        }
        foreach (var upgrade in source.OwnedUpgrades) _inherited |= world.GrantUpgrade(self, upgrade);
    }

    public bool HasInherited => _inherited;
    public override void WriteState(CanonicalWriter writer) => writer.WriteBool(_inherited);
    public override void ReadState(CanonicalReader reader) => _inherited = reader.ReadBool();
}
