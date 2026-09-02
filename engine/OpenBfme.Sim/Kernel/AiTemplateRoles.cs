namespace OpenBfme.Sim;

[Flags]
public enum AiUnitRole : ushort
{
    None = 0,
    Structure = 1 << 0,
    Economy = 1 << 1,
    Infantry = 1 << 2,
    Pike = 1 << 3,
    Archer = 1 << 4,
    Cavalry = 1 << 5,
    Hero = 1 << 6,
    Siege = 1 << 7,
}

public enum AiBuildCategory : byte
{
    Economy = 1,
    Producer = 2,
    Unit = 3,
    Upgrade = 4,
    Science = 5,
}

/// <summary>
/// Optional deterministic ordering hook for a future SkirmishAIData adapter.
/// This unit deliberately does not parse SkirmishAIData. Implementations may
/// only reorder/filter the already command-set-authorized candidate names and
/// must return the same ordinal sequence on every peer.
/// </summary>
public interface IAiBuildListProvider
{
    IReadOnlyList<string> OrderCandidates(
        string faction,
        AiBuildCategory category,
        IReadOnlyList<string> authorizedCandidates);
}

/// <summary>
/// Retail KindOf mapping used by the planner. STRUCTURE marks buildings;
/// ECONOMY_STRUCTURE, +ECONOMY_STRUCTURE, FS_CASH_PRODUCER,
/// SUPPLY_GATHERING_CENTER, and HARVESTER mark economy; PIKE is kept distinct
/// from INFANTRY; ARCHER, CAVALRY, HERO, and SIEGEENGINE/SIEGE_TOWER/
/// SIEGE_LADDER map directly. MELEE_HORDE, ORC, and URUK are infantry
/// fallbacks for authored hordes that omit INFANTRY.
/// </summary>
public static class AiTemplateRoles
{
    public static AiUnitRole Classify(ObjectTemplate template)
    {
        ArgumentNullException.ThrowIfNull(template);
        var roles = AiUnitRole.None;
        if (Has(template, "STRUCTURE")) roles |= AiUnitRole.Structure;
        if (HasAny(template, "ECONOMY_STRUCTURE", "+ECONOMY_STRUCTURE", "FS_CASH_PRODUCER",
                "SUPPLY_GATHERING_CENTER", "HARVESTER"))
            roles |= AiUnitRole.Economy | AiUnitRole.Structure;
        if (HasAny(template, "INFANTRY", "MELEE_HORDE", "ORC", "URUK")) roles |= AiUnitRole.Infantry;
        if (Has(template, "PIKE")) roles |= AiUnitRole.Pike | AiUnitRole.Infantry;
        if (Has(template, "ARCHER")) roles |= AiUnitRole.Archer;
        if (Has(template, "CAVALRY")) roles |= AiUnitRole.Cavalry;
        if (Has(template, "HERO")) roles |= AiUnitRole.Hero;
        if (HasAny(template, "SIEGEENGINE", "SIEGE_TOWER", "SIEGE_LADDER")) roles |= AiUnitRole.Siege;
        return roles;
    }

    public static bool IsSide(ObjectTemplate template, string normalizedFaction) =>
        template.Side.Length == 0
        || string.Equals(template.Side, normalizedFaction, StringComparison.OrdinalIgnoreCase);

    public static bool IsNonCombat(ObjectTemplate template) =>
        HasAny(template, "NONCOM", "DOZER", "PORTER");

    private static bool Has(ObjectTemplate template, string token) =>
        template.KindOf.Contains(token, StringComparer.OrdinalIgnoreCase);

    private static bool HasAny(ObjectTemplate template, params string[] tokens) =>
        tokens.Any(token => Has(template, token));
}
