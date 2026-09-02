namespace OpenBfme.Sim;

/// <summary>
/// Applies deterministic resource crowding to AutoDepositUpdate. An authored
/// per-sibling penalty wins; otherwise overlapping siblings share the claim.
/// </summary>
[SageModule("TerrainResourceBehavior", ModuleTier.Cosmetic)]
public sealed class TerrainResourceBehaviorModule : ModuleBase
{
    public const string TypeName = "TerrainResourceBehavior";
    private readonly Fixed64 _radius;
    private readonly Fixed64? _penaltyPerSibling;
    private int _siblingCount;

    public TerrainResourceBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _radius = Fixed64.Max(ReadFixed(spec, "Radius", Fixed64.Zero), Fixed64.Zero);
        _penaltyPerSibling = ReadPenalty(spec);
    }

    public Fixed64 Radius => _radius;
    public int SiblingCount => _siblingCount;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        var deposit = self.FindModule<AutoDepositUpdateModule>();
        if (deposit == null || _radius <= Fixed64.Zero) return;
        var radiusSquared = _radius * _radius;
        var siblings = 0;
        foreach (var candidate in world.Objects.Values)
        {
            if (candidate.Id == self.Id || candidate.Team != self.Team
                || candidate.IsDead || candidate.IsDying || candidate.IsUnderConstruction
                || candidate.FindModule<TerrainResourceBehaviorModule>() == null) continue;
            if (candidate.Position.DistanceSquaredTo(self.Position) <= radiusSquared) siblings++;
        }
        _siblingCount = siblings;
        var multiplier = _penaltyPerSibling.HasValue
            ? Fixed64.Max(Fixed64.Zero, Fixed64.One - _penaltyPerSibling.Value * Fixed64.FromInt(siblings))
            : Fixed64.FromFraction(1, siblings + 1);
        deposit.SetCrowdingMultiplier(multiplier);
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_siblingCount);
    public override void ReadState(CanonicalReader reader) => _siblingCount = reader.ReadInt();

    private static Fixed64? ReadPenalty(ModuleSpec spec)
    {
        foreach (var key in new[] { "CrowdingPenalty", "CrowdingPenaltyPercent", "PenaltyPercent" })
        {
            if (spec.Data.TryGetValue(key + "Raw", out var raw)) return Fixed64.FromRaw(raw);
            if (spec.Data.TryGetValue(key, out var integer))
                return key.EndsWith("Percent", StringComparison.Ordinal)
                    ? Fixed64.FromFraction(integer, 100)
                    : Fixed64.FromInt64(integer);
            var text = spec.GetString(key, "").Trim();
            if (text.EndsWith('%') && long.TryParse(text[..^1], out var percent))
                return Fixed64.FromFraction(percent, 100);
        }
        return null;
    }

    private static Fixed64 ReadFixed(ModuleSpec spec, string name, Fixed64 fallback)
    {
        if (spec.Data.TryGetValue(name + "Raw", out var raw)) return Fixed64.FromRaw(raw);
        if (spec.Data.TryGetValue(name, out var integer)) return Fixed64.FromInt64(integer);
        return fallback;
    }
}
