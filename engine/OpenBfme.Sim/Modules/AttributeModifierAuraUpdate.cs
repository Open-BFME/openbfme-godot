namespace OpenBfme.Sim;

/// <summary>
/// AttributeModifierAuraUpdate-lite (227 objects in the union corpus): a radius
/// aura granting a flat ARMOR basis-point modifier to allied objects in range
/// (the single chosen integration for this tier — the damage-bonus side arrives
/// with the weapon-modifier lane). Every RecomputeTicks the aura rescans
/// world.Objects in ascending id order (deterministic) and caches the allied
/// ids within RadiusRaw; at the END of each world tick, SimWorld rebuilds its
/// aura armor table from every living, non-dying, constructed carrier's cache,
/// so the table is stable for the whole following tick (commands and the module
/// sweep alike). world.DealDamage consults the table after the per-module
/// ModifyIncomingDamage chain.
///
/// STACKING RULE (documented contract): contributions from multiple aura
/// carriers ADD; the summed basis points are clamped to [0, 10000] at damage
/// application, so stacked auras can reach full immunity but never heal.
/// The table is derived state — rebuilt deterministically from module caches —
/// so it is NOT serialized; SimWorld.Restore rebuilds it after loading.
/// Design data: RadiusRaw (Fixed64 raw, default 10), RecomputeTicks (default 5),
/// ArmorBonusBp (default 0; 1000 = 10% less incoming damage).
/// Aura carriers do not buff themselves.
/// </summary>
[SageModule("AttributeModifierAuraUpdate", ModuleTier.Structural)]
public sealed class AttributeModifierAuraModule : ModuleBase
{
    public const string TypeName = "AttributeModifierAuraUpdate";

    private readonly Fixed64 _radius;
    private readonly int _recomputeTicks;
    private readonly long _armorBonusBp;

    private int _ticksUntilScan;
    private readonly List<int> _affectedIds = new();

    public AttributeModifierAuraModule(ModuleSpec spec) : base(spec)
    {
        _radius = spec.GetFixed("RadiusRaw", Fixed64.FromInt(10));
        _recomputeTicks = (int)Math.Max(1, spec.GetLong("RecomputeTicks", 5));
        _armorBonusBp = Math.Clamp(spec.GetLong("ArmorBonusBp", 0), 0, 10_000);
        _ticksUntilScan = 1; // first update scans immediately
    }

    public long ArmorBonusBp => _armorBonusBp;
    public IReadOnlyList<int> AffectedIds => _affectedIds;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying)
        {
            return;
        }
        _ticksUntilScan--;
        if (_ticksUntilScan > 0)
        {
            return;
        }
        _ticksUntilScan = _recomputeTicks;
        _affectedIds.Clear();
        var radiusSquared = _radius * _radius;
        foreach (var candidate in world.Objects.Values) // ascending id — deterministic
        {
            if (candidate.Id == self.Id || candidate.Team != self.Team
                || candidate.IsDead || candidate.IsDying)
            {
                continue;
            }
            if (self.Position.DistanceSquaredTo(candidate.Position) <= radiusSquared)
            {
                _affectedIds.Add(candidate.Id);
            }
        }
    }

    /// <summary>Adds this aura's cached contributions into the world table (additive stacking).</summary>
    internal void ContributeTo(SortedDictionary<int, long> armorBonusBpByObjectId, SimWorld world)
    {
        if (_armorBonusBp == 0)
        {
            return;
        }
        foreach (var id in _affectedIds)
        {
            if (!world.Objects.ContainsKey(id))
            {
                continue; // member died since the last rescan
            }
            armorBonusBpByObjectId[id] =
                armorBonusBpByObjectId.TryGetValue(id, out var existing) ? existing + _armorBonusBp : _armorBonusBp;
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksUntilScan);
        writer.WriteInt(_affectedIds.Count);
        foreach (var id in _affectedIds)
        {
            writer.WriteInt(id);
        }
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksUntilScan = reader.ReadInt();
        _affectedIds.Clear();
        var count = reader.ReadInt();
        for (var i = 0; i < count; i++)
        {
            _affectedIds.Add(reader.ReadInt());
        }
    }
}
