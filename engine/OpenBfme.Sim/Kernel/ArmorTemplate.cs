namespace OpenBfme.Sim;

/// <summary>Immutable SAGE Armor block with DEFAULT fallback.</summary>
public sealed class ArmorTemplate
{
    private readonly SortedDictionary<DamageType, Fixed64> _multipliers;

    public ArmorTemplate(string name, IEnumerable<KeyValuePair<DamageType, Fixed64>> multipliers)
    {
        if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("Armor name is required", nameof(name));
        Name = name;
        _multipliers = new SortedDictionary<DamageType, Fixed64>();
        foreach (var (damageType, multiplier) in multipliers)
        {
            if (multiplier < Fixed64.Zero) throw new ArgumentOutOfRangeException(nameof(multipliers));
            _multipliers.Add(damageType, multiplier);
        }
        if (!_multipliers.ContainsKey(DamageType.DEFAULT))
        {
            _multipliers.Add(DamageType.DEFAULT, Fixed64.One);
        }
    }

    public string Name { get; }
    public IReadOnlyDictionary<DamageType, Fixed64> Multipliers => _multipliers;

    public Fixed64 MultiplierFor(DamageType damageType) =>
        _multipliers.TryGetValue(damageType, out var value)
            ? value
            : _multipliers[DamageType.DEFAULT];

    public static ArmorTemplate Parse(ModuleSpec spec)
    {
        ArgumentNullException.ThrowIfNull(spec);
        var values = new SortedDictionary<DamageType, Fixed64>();
        foreach (DamageType damageType in Enum.GetValues<DamageType>())
        {
            if (spec.Data.TryGetValue("ArmorRaw:" + damageType, out var raw))
            {
                values[damageType] = Fixed64.FromRaw(raw);
            }
            else if (spec.Data.TryGetValue("Armor:" + damageType, out var percent))
            {
                values[damageType] = Fixed64.FromFraction(percent, 100);
            }
        }
        if (!values.ContainsKey(DamageType.DEFAULT)
            && spec.Data.TryGetValue("ArmorDefault", out var defaultPercent))
        {
            values[DamageType.DEFAULT] = Fixed64.FromFraction(defaultPercent, 100);
        }
        return new ArmorTemplate(spec.GetString("Name", spec.TypeName), values);
    }

    public static ArmorTemplate Parse(string name, IReadOnlyDictionary<string, object?> row)
    {
        var fields = IniValueReader.Fields(row);
        var values = new SortedDictionary<DamageType, Fixed64>();
        foreach (var item in IniValueReader.List(IniValueReader.Value(fields, "Armor")))
        {
            if (IniValueReader.TryDictionary(item, out var armorRow))
            {
                var entry = IniValueReader.Fields(armorRow);
                var type = DamageTypeNames.Parse(IniValueReader.String(entry, "DamageType",
                    IniValueReader.String(entry, "Type", "DEFAULT")));
                var multiplierValue = IniValueReader.Value(entry, "Percent")
                    ?? IniValueReader.Value(entry, "Multiplier")
                    ?? throw new FormatException("Armor row has no Percent or Multiplier");
                values[type] = entry.ContainsKey("Multiplier")
                    ? IniValueReader.Fixed(entry, "Multiplier")
                    : IniValueReader.PercentMultiplier(multiplierValue, "Armor.Percent");
                continue;
            }
            var text = item switch
            {
                string stringValue => stringValue,
                System.Text.Json.JsonElement { ValueKind: System.Text.Json.JsonValueKind.String } json => json.GetString()!,
                _ => throw new FormatException("Armor entry must be a string or object"),
            };
            var split = text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
            if (split.Length != 2) throw new FormatException($"Armor entry '{text}' is not '<type> <percent>'");
            values[DamageTypeNames.Parse(split[0])] = IniValueReader.PercentMultiplier(split[1], "Armor");
        }
        foreach (DamageType damageType in Enum.GetValues<DamageType>())
        {
            if (fields.TryGetValue(damageType.ToString(), out var direct))
            {
                values[damageType] = IniValueReader.PercentMultiplier(direct!, damageType.ToString());
            }
        }
        return new ArmorTemplate(name, values);
    }
}

/// <summary>One ordered SAGE WeaponSet row. Weapon names resolve through SimConfig.</summary>
public sealed class WeaponSet
{
    private readonly SortedSet<string> _conditions;
    private readonly SortedDictionary<WeaponSlot, string> _weapons;
    private readonly SortedDictionary<WeaponSlot, IReadOnlyList<string>> _autoChooseSources;
    private readonly SortedDictionary<WeaponSlot, IReadOnlyList<string>> _preferredAgainst;

    public WeaponSet(
        IEnumerable<string>? conditions,
        IEnumerable<KeyValuePair<WeaponSlot, string>> weapons,
        IEnumerable<KeyValuePair<WeaponSlot, IReadOnlyList<string>>>? autoChooseSources = null,
        IEnumerable<KeyValuePair<WeaponSlot, IReadOnlyList<string>>>? preferredAgainst = null)
    {
        _conditions = new SortedSet<string>(conditions ?? Array.Empty<string>(), StringComparer.Ordinal);
        _weapons = new SortedDictionary<WeaponSlot, string>();
        foreach (var (slot, name) in weapons)
        {
            if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("Weapon name is required", nameof(weapons));
            _weapons.Add(slot, name);
        }
        _autoChooseSources = CopyLists(autoChooseSources);
        _preferredAgainst = CopyLists(preferredAgainst);
    }

    public IReadOnlySet<string> Conditions => _conditions;
    public IReadOnlyDictionary<WeaponSlot, string> Weapons => _weapons;
    public IReadOnlyDictionary<WeaponSlot, IReadOnlyList<string>> AutoChooseSources => _autoChooseSources;
    public IReadOnlyDictionary<WeaponSlot, IReadOnlyList<string>> PreferredAgainst => _preferredAgainst;

    public string? PrimaryWeaponName =>
        _weapons.TryGetValue(WeaponSlot.PRIMARY, out var name) ? name : null;

    public bool Matches(IReadOnlySet<string> objectConditions) =>
        _conditions.Count > 0 && _conditions.All(objectConditions.Contains);

    public static WeaponSet Parse(IReadOnlyDictionary<string, object?> row)
    {
        var fields = IniValueReader.Fields(row);
        var weapons = ParseSlotNames(IniValueReader.Value(fields, "Weapon"));
        return new WeaponSet(
            IniValueReader.Tokens(IniValueReader.Value(fields, "Conditions")),
            weapons,
            ParseSlotTokens(IniValueReader.Value(fields, "AutoChooseSources")),
            ParseSlotTokens(IniValueReader.Value(fields, "PreferredAgainst")));
    }

    public static WeaponSet Parse(ModuleSpec spec)
    {
        ArgumentNullException.ThrowIfNull(spec);
        var conditions = TokensFromSpec(spec, "Conditions");
        var weapons = new SortedDictionary<WeaponSlot, string>();
        var autoChoose = new SortedDictionary<WeaponSlot, IReadOnlyList<string>>();
        var preferred = new SortedDictionary<WeaponSlot, IReadOnlyList<string>>();
        foreach (WeaponSlot slot in Enum.GetValues<WeaponSlot>())
        {
            var weapon = spec.GetString("Weapon:" + slot, "");
            if (!string.IsNullOrWhiteSpace(weapon)) weapons.Add(slot, weapon);
            var auto = TokensFromSpec(spec, "AutoChooseSources:" + slot);
            if (auto.Count > 0) autoChoose.Add(slot, auto);
            var against = TokensFromSpec(spec, "PreferredAgainst:" + slot);
            if (against.Count > 0) preferred.Add(slot, against);
        }
        return new WeaponSet(conditions, weapons, autoChoose, preferred);
    }

    private static SortedDictionary<WeaponSlot, string> ParseSlotNames(object? value)
    {
        var result = new SortedDictionary<WeaponSlot, string>();
        foreach (var item in IniValueReader.List(value))
        {
            var tokens = TokenLine(item, "Weapon");
            if (tokens.Length != 2) throw new FormatException("Weapon row must be '<slot> <template>'");
            result.Add(ParseSlot(tokens[0]), tokens[1]);
        }
        return result;
    }

    private static SortedDictionary<WeaponSlot, IReadOnlyList<string>> ParseSlotTokens(object? value)
    {
        var result = new SortedDictionary<WeaponSlot, IReadOnlyList<string>>();
        foreach (var item in IniValueReader.List(value))
        {
            var tokens = TokenLine(item, "weapon slot tokens");
            if (tokens.Length < 2) throw new FormatException("Slot token row needs a slot and at least one token");
            result.Add(ParseSlot(tokens[0]), tokens[1..]);
        }
        return result;
    }

    private static string[] TokenLine(object? item, string name)
    {
        var text = item switch
        {
            string stringValue => stringValue,
            System.Text.Json.JsonElement { ValueKind: System.Text.Json.JsonValueKind.String } json => json.GetString()!,
            _ => throw new FormatException($"{name} row must be a string"),
        };
        return text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
    }

    private static WeaponSlot ParseSlot(string value) =>
        Enum.TryParse<WeaponSlot>(value.Trim().ToUpperInvariant(), out var slot)
            ? slot
            : throw new FormatException($"Unknown weapon slot '{value}'");

    private static SortedDictionary<WeaponSlot, IReadOnlyList<string>> CopyLists(
        IEnumerable<KeyValuePair<WeaponSlot, IReadOnlyList<string>>>? source)
    {
        var result = new SortedDictionary<WeaponSlot, IReadOnlyList<string>>();
        if (source == null) return result;
        foreach (var (slot, values) in source) result.Add(slot, values.ToArray());
        return result;
    }

    private static IReadOnlyList<string> TokensFromSpec(ModuleSpec spec, string key) =>
        spec.GetString(key, "").Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
}

/// <summary>One ordered SAGE ArmorSet row.</summary>
public sealed class ArmorSet
{
    private readonly SortedSet<string> _conditions;

    public ArmorSet(IEnumerable<string>? conditions, string armorName, string damageFX = "")
    {
        if (string.IsNullOrWhiteSpace(armorName)) throw new ArgumentException("Armor name is required", nameof(armorName));
        _conditions = new SortedSet<string>(conditions ?? Array.Empty<string>(), StringComparer.Ordinal);
        ArmorName = armorName;
        DamageFX = damageFX ?? "";
    }

    public IReadOnlySet<string> Conditions => _conditions;
    public string ArmorName { get; }
    public string DamageFX { get; }
    public bool Matches(IReadOnlySet<string> objectConditions) =>
        _conditions.Count > 0 && _conditions.All(objectConditions.Contains);

    public static ArmorSet Parse(IReadOnlyDictionary<string, object?> row)
    {
        var fields = IniValueReader.Fields(row);
        return new ArmorSet(
            IniValueReader.Tokens(IniValueReader.Value(fields, "Conditions")),
            IniValueReader.String(fields, "Armor"),
            IniValueReader.String(fields, "DamageFX"));
    }

    public static ArmorSet Parse(ModuleSpec spec)
    {
        ArgumentNullException.ThrowIfNull(spec);
        return new ArmorSet(
            spec.GetString("Conditions", "")
                .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries),
            spec.GetString("Armor", ""),
            spec.GetString("DamageFX", ""));
    }
}

public sealed record BodyHealthTemplate(Fixed64 MaxHealth, Fixed64 InitialHealth)
{
    public BodyHealthTemplate(Fixed64 maxHealth)
        : this(maxHealth, maxHealth)
    {
    }
}
