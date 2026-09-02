namespace OpenBfme.Sim;

/// <summary>SAGE damage identifiers used by weapon nuggets and armor tables.</summary>
public enum DamageType : byte
{
    DEFAULT,
    SLASH,
    PIERCE,
    CRUSH,
    FLAME,
    MAGIC,
    HERO,
    HERO_RANGED,
    SIEGE,
    SPECIALIST,
    URUK,
    CAVALRY,
    CAVALRY_RANGED,
    STRUCTURAL,
    WATER,
    POISON,
    FLY_INTO,
    UNRESISTABLE,
}

public static class DamageTypeNames
{
    public static DamageType Parse(string value)
    {
        if (TryParse(value, out var damageType))
        {
            return damageType;
        }
        throw new ArgumentException($"Unknown SAGE damage type '{value}'", nameof(value));
    }

    public static bool TryParse(string? value, out DamageType damageType)
    {
        var normalized = value?.Trim().Replace('-', '_').ToUpperInvariant();
        return Enum.TryParse(normalized, ignoreCase: false, out damageType)
            && Enum.IsDefined(damageType);
    }
}
