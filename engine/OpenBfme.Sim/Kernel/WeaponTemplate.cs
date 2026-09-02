namespace OpenBfme.Sim;

public enum WeaponSlot : byte
{
    PRIMARY,
    SECONDARY,
    TERTIARY,
}

public enum PreAttackType : byte
{
    PER_SHOT,
    PER_CLIP,
    PER_ATTACK,
    PER_POSITION,
}

[Flags]
public enum WeaponTargetFlags : ushort
{
    None = 0,
    AntiGround = 1 << 0,
    AntiAirborne = 1 << 1,
    AntiAirborneVehicle = 1 << 2,
    AntiAirborneInfantry = 1 << 3,
    AntiProjectile = 1 << 4,
    AntiSmallMissile = 1 << 5,
    AntiBallisticMissile = 1 << 6,
    AntiMine = 1 << 7,
    AntiParachute = 1 << 8,
}

public sealed record DamageNugget(
    Fixed64 Damage,
    Fixed64 Radius,
    int DelayTicks,
    DamageType DamageType,
    string DamageFXType,
    string DeathType,
    bool FriendlyFire = false);

public sealed record MetaImpactNugget(Fixed64 Amount, Fixed64 Radius, Fixed64 TaperOff);

public sealed record ProjectileNugget(string ProjectileTemplateName);

/// <summary>Immutable, tick-resolved data from one SAGE Weapon block.</summary>
public sealed class WeaponTemplate
{
    private static readonly string[] AntiFlagNames =
    {
        "AntiGround", "AntiAirborne", "AntiAirborneVehicle", "AntiAirborneInfantry",
        "AntiProjectile", "AntiSmallMissile", "AntiBallisticMissile", "AntiMine", "AntiParachute",
    };

    public WeaponTemplate(
        string name,
        Fixed64 attackRange,
        Fixed64 minimumAttackRange,
        int delayBetweenShotsTicks,
        int preAttackDelayTicks,
        PreAttackType preAttackType,
        int firingDurationTicks,
        int clipSize,
        int clipReloadTimeTicks,
        IReadOnlyList<DamageNugget> damageNuggets,
        MetaImpactNugget? metaImpact = null,
        ProjectileNugget? projectile = null,
        WeaponTargetFlags targetFlags = WeaponTargetFlags.AntiGround)
    {
        if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("Weapon name is required", nameof(name));
        if (attackRange < Fixed64.Zero || minimumAttackRange < Fixed64.Zero || minimumAttackRange > attackRange)
        {
            throw new ArgumentOutOfRangeException(nameof(attackRange));
        }
        if (delayBetweenShotsTicks < 0 || preAttackDelayTicks < 0 || firingDurationTicks < 0
            || clipSize < 0 || clipReloadTimeTicks < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(delayBetweenShotsTicks));
        }
        Name = name;
        AttackRange = attackRange;
        MinimumAttackRange = minimumAttackRange;
        DelayBetweenShotsTicks = delayBetweenShotsTicks;
        PreAttackDelayTicks = preAttackDelayTicks;
        PreAttackType = preAttackType;
        FiringDurationTicks = firingDurationTicks;
        ClipSize = clipSize;
        ClipReloadTimeTicks = clipReloadTimeTicks;
        DamageNuggets = damageNuggets?.ToArray() ?? throw new ArgumentNullException(nameof(damageNuggets));
        MetaImpact = metaImpact;
        Projectile = projectile;
        TargetFlags = targetFlags;
    }

    public string Name { get; }
    public Fixed64 AttackRange { get; }
    public Fixed64 MinimumAttackRange { get; }
    public int DelayBetweenShotsTicks { get; }
    public int PreAttackDelayTicks { get; }
    public PreAttackType PreAttackType { get; }
    public int FiringDurationTicks { get; }
    public int ClipSize { get; }
    public int ClipReloadTimeTicks { get; }
    public IReadOnlyList<DamageNugget> DamageNuggets { get; }
    public MetaImpactNugget? MetaImpact { get; }
    public ProjectileNugget? Projectile { get; }
    public WeaponTargetFlags TargetFlags { get; }

    public static WeaponTemplate Parse(ModuleSpec spec, int tickMilliseconds)
    {
        ArgumentNullException.ThrowIfNull(spec);
        var name = spec.GetString("Name", spec.TypeName);
        var attackRange = ReadFixed(spec, "AttackRange", Fixed64.Zero);
        var minimumRange = ReadFixed(spec, "MinimumAttackRange", Fixed64.Zero);
        var delay = Ticks(spec, "DelayBetweenShots", tickMilliseconds);
        var preAttack = Ticks(spec, "PreAttackDelay", tickMilliseconds);
        var firing = Ticks(spec, "FiringDuration", tickMilliseconds);
        var reload = Ticks(spec, "ClipReloadTime", tickMilliseconds);
        var preAttackType = ParsePreAttack(spec.GetString("PreAttackType", "PER_SHOT"));
        var clipSize = checked((int)Math.Max(0, spec.GetLong("ClipSize", 0)));
        var nuggets = ParseModuleNuggets(spec, tickMilliseconds);
        var meta = ParseModuleMeta(spec);
        var projectileName = spec.GetString("ProjectileTemplateName", spec.GetString("ProjectileNugget", ""));
        var projectile = string.IsNullOrWhiteSpace(projectileName) ? null : new ProjectileNugget(projectileName);
        return new WeaponTemplate(
            name, attackRange, minimumRange, delay, preAttack, preAttackType, firing,
            clipSize, reload, nuggets, meta, projectile, ParseModuleFlags(spec));
    }

    public static WeaponTemplate Parse(
        string name,
        IReadOnlyDictionary<string, object?> row,
        int tickMilliseconds)
    {
        var fields = IniValueReader.Fields(row);
        var attackRange = IniValueReader.Fixed(fields, "AttackRange", Fixed64.Zero);
        var minimumRange = IniValueReader.Fixed(fields, "MinimumAttackRange", Fixed64.Zero);
        var delay = IniValueReader.MillisecondsToTicks(
            IniValueReader.Milliseconds(fields, "DelayBetweenShots"), tickMilliseconds);
        var preAttack = IniValueReader.MillisecondsToTicks(
            IniValueReader.Milliseconds(fields, "PreAttackDelay"), tickMilliseconds);
        var firing = IniValueReader.MillisecondsToTicks(
            IniValueReader.Milliseconds(fields, "FiringDuration"), tickMilliseconds);
        var reload = IniValueReader.MillisecondsToTicks(
            IniValueReader.Milliseconds(fields, "ClipReloadTime"), tickMilliseconds);
        var preAttackType = ParsePreAttack(IniValueReader.String(fields, "PreAttackType", "PER_SHOT"));
        var clipSize = IniValueReader.Integer(fields, "ClipSize");
        var nuggets = new List<DamageNugget>();
        foreach (var item in IniValueReader.List(IniValueReader.Value(fields, "DamageNugget")))
        {
            if (!IniValueReader.TryDictionary(item, out var nuggetRow))
            {
                throw new FormatException("DamageNugget row must be an object");
            }
            nuggets.Add(ParseNestedNugget(IniValueReader.Fields(nuggetRow), tickMilliseconds));
        }
        if (nuggets.Count == 0 && fields.ContainsKey("Damage"))
        {
            nuggets.Add(ParseNestedNugget(fields, tickMilliseconds));
        }
        var meta = ParseNestedMeta(IniValueReader.Value(fields, "MetaImpactNugget"));
        var projectile = ParseNestedProjectile(IniValueReader.Value(fields, "ProjectileNugget"));
        return new WeaponTemplate(
            name, attackRange, minimumRange, delay, preAttack, preAttackType, firing,
            clipSize, reload, nuggets, meta, projectile, ParseNestedFlags(fields));
    }

    private static IReadOnlyList<DamageNugget> ParseModuleNuggets(ModuleSpec spec, int tickMilliseconds)
    {
        var count = checked((int)Math.Max(0, spec.GetLong("DamageNuggetCount", 0)));
        var result = new List<DamageNugget>();
        if (count == 0 && (spec.Data.ContainsKey("Damage") || spec.Data.ContainsKey("DamageRaw")))
        {
            result.Add(ParseModuleNugget(spec, "", tickMilliseconds));
            return result;
        }
        for (var index = 0; index < count; index++)
        {
            result.Add(ParseModuleNugget(spec, $"DamageNugget:{index}:", tickMilliseconds));
        }
        return result;
    }

    private static DamageNugget ParseModuleNugget(ModuleSpec spec, string prefix, int tickMilliseconds)
    {
        var damage = ReadFixed(spec, prefix + "Damage", Fixed64.Zero);
        var radius = ReadFixed(spec, prefix + "Radius", Fixed64.Zero);
        var delayMs = spec.GetLong(prefix + "DelayTimeMs", spec.GetLong(prefix + "DelayTime", 0));
        var damageType = DamageTypeNames.Parse(spec.GetString(prefix + "DamageType", "DEFAULT"));
        return new DamageNugget(
            damage,
            radius,
            IniValueReader.MillisecondsToTicks(delayMs, tickMilliseconds),
            damageType,
            spec.GetString(prefix + "DamageFXType", ""),
            spec.GetString(prefix + "DeathType", "NORMAL"),
            spec.GetLong(prefix + "FriendlyFire", 0) != 0);
    }

    private static DamageNugget ParseNestedNugget(
        IReadOnlyDictionary<string, object?> fields,
        int tickMilliseconds) => new(
        IniValueReader.Fixed(fields, "Damage", Fixed64.Zero),
        IniValueReader.Fixed(fields, "Radius", Fixed64.Zero),
        IniValueReader.MillisecondsToTicks(
            IniValueReader.Milliseconds(fields, "DelayTime"), tickMilliseconds),
        DamageTypeNames.Parse(IniValueReader.String(fields, "DamageType", "DEFAULT")),
        IniValueReader.String(fields, "DamageFXType"),
        IniValueReader.String(fields, "DeathType", "NORMAL"),
        IniValueReader.Boolean(fields, "FriendlyFire"));

    private static MetaImpactNugget? ParseModuleMeta(ModuleSpec spec)
    {
        if (!spec.Data.ContainsKey("MetaImpactAmount") && !spec.Data.ContainsKey("MetaImpactAmountRaw")) return null;
        return new MetaImpactNugget(
            ReadFixed(spec, "MetaImpactAmount", Fixed64.Zero),
            ReadFixed(spec, "MetaImpactRadius", Fixed64.Zero),
            ReadFixed(spec, "MetaImpactTaperOff", Fixed64.Zero));
    }

    private static MetaImpactNugget? ParseNestedMeta(object? value)
    {
        var item = IniValueReader.List(value).FirstOrDefault();
        if (item == null) return null;
        if (!IniValueReader.TryDictionary(item, out var row)) throw new FormatException("MetaImpactNugget row must be an object");
        var fields = IniValueReader.Fields(row);
        return new MetaImpactNugget(
            IniValueReader.Fixed(fields, "Amount", Fixed64.Zero),
            IniValueReader.Fixed(fields, "Radius", Fixed64.Zero),
            IniValueReader.Fixed(fields, "TaperOff", Fixed64.Zero));
    }

    private static ProjectileNugget? ParseNestedProjectile(object? value)
    {
        var item = IniValueReader.List(value).FirstOrDefault();
        if (item == null) return null;
        if (item is string name) return new ProjectileNugget(name);
        if (!IniValueReader.TryDictionary(item, out var row)) throw new FormatException("ProjectileNugget row must be an object");
        var fields = IniValueReader.Fields(row);
        var projectileName = IniValueReader.String(fields, "ProjectileTemplateName",
            IniValueReader.String(fields, "ProjectileObject", "PROJECTILE"));
        return new ProjectileNugget(projectileName);
    }

    private static WeaponTargetFlags ParseModuleFlags(ModuleSpec spec)
    {
        var flags = WeaponTargetFlags.None;
        foreach (var name in AntiFlagNames)
        {
            if (spec.GetLong(name, 0) != 0) flags |= Flag(name);
        }
        return flags == WeaponTargetFlags.None ? WeaponTargetFlags.AntiGround : flags;
    }

    private static WeaponTargetFlags ParseNestedFlags(IReadOnlyDictionary<string, object?> fields)
    {
        var flags = WeaponTargetFlags.None;
        foreach (var name in AntiFlagNames)
        {
            if (IniValueReader.Boolean(fields, name)) flags |= Flag(name);
        }
        return flags == WeaponTargetFlags.None ? WeaponTargetFlags.AntiGround : flags;
    }

    private static WeaponTargetFlags Flag(string name) => name switch
    {
        "AntiGround" => WeaponTargetFlags.AntiGround,
        "AntiAirborne" => WeaponTargetFlags.AntiAirborne,
        "AntiAirborneVehicle" => WeaponTargetFlags.AntiAirborneVehicle,
        "AntiAirborneInfantry" => WeaponTargetFlags.AntiAirborneInfantry,
        "AntiProjectile" => WeaponTargetFlags.AntiProjectile,
        "AntiSmallMissile" => WeaponTargetFlags.AntiSmallMissile,
        "AntiBallisticMissile" => WeaponTargetFlags.AntiBallisticMissile,
        "AntiMine" => WeaponTargetFlags.AntiMine,
        "AntiParachute" => WeaponTargetFlags.AntiParachute,
        _ => WeaponTargetFlags.None,
    };

    private static PreAttackType ParsePreAttack(string value) =>
        Enum.TryParse<PreAttackType>(value.Trim().ToUpperInvariant(), out var parsed)
            ? parsed
            : throw new FormatException($"Unknown PreAttackType '{value}'");

    private static Fixed64 ReadFixed(ModuleSpec spec, string name, Fixed64 fallback)
    {
        if (spec.Data.TryGetValue(name + "Raw", out var raw)) return Fixed64.FromRaw(raw);
        if (spec.Data.TryGetValue(name, out var integer)) return Fixed64.FromInt64(integer);
        return fallback;
    }

    private static int Ticks(ModuleSpec spec, string name, int tickMilliseconds)
    {
        var milliseconds = spec.GetLong(name + "Ms", spec.GetLong(name, 0));
        return IniValueReader.MillisecondsToTicks(milliseconds, tickMilliseconds);
    }
}
