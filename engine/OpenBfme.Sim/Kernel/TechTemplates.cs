using System.Numerics;

namespace OpenBfme.Sim;

public enum UpgradeType : byte
{
    Player = 1,
    Object = 2,
}

public sealed record UpgradeTemplate(
    string Name,
    UpgradeType Type,
    long BuildCost,
    long BuildTimeMilliseconds,
    IReadOnlyList<string> Prerequisites)
{
    public int BuildTicks(int tickMilliseconds) =>
        IniValueReader.MillisecondsToTicks(BuildTimeMilliseconds, tickMilliseconds);
}

public sealed record ScienceTemplate(
    string Name,
    IReadOnlyList<string> PrerequisiteSciences,
    long PurchasePointCost,
    bool IsGrantable);

public sealed record SpecialPowerTemplate(
    string Name,
    string Enum,
    long ReloadTimeMilliseconds,
    IReadOnlyList<string> RequiredSciences,
    bool PublicTimer)
{
    public int ReloadTicks(int tickMilliseconds) =>
        IniValueReader.MillisecondsToTicks(ReloadTimeMilliseconds, tickMilliseconds);
}

public sealed record CommandButtonTemplate(
    string Name,
    string Command,
    string Object,
    string Upgrade,
    string Science,
    string SpecialPower,
    string Stances = "",
    string FlagsUsedForToggle = "",
    string WeaponSlot = "");

public sealed record BundleCommandSetEntry(int Slot, string? Button);

public sealed record BundleCommandSetRow(
    string Name,
    IReadOnlyList<BundleCommandSetEntry> Entries,
    IReadOnlyDictionary<string, BundleValue> Fields);

public sealed record CommandSetEntryTemplate(
    int Slot,
    string ButtonName,
    CommandButtonTemplate Button);

public sealed record CommandSetTemplate(
    string Name,
    IReadOnlyList<CommandSetEntryTemplate> Entries);

/// <summary>Immutable, name-indexed match technology catalog.</summary>
public sealed class TechCatalog
{
    public static TechCatalog Empty { get; } = new();

    public TechCatalog(
        IEnumerable<UpgradeTemplate>? upgrades = null,
        IEnumerable<ScienceTemplate>? sciences = null,
        IEnumerable<SpecialPowerTemplate>? specialPowers = null,
        IEnumerable<CommandButtonTemplate>? commandButtons = null,
        IEnumerable<CommandSetTemplate>? commandSets = null)
    {
        Upgrades = Index(upgrades, value => value.Name);
        Sciences = Index(sciences, value => value.Name);
        SpecialPowers = Index(specialPowers, value => value.Name);
        CommandButtons = Index(commandButtons, value => value.Name);
        CommandSets = Index(commandSets, value => value.Name);
    }

    public IReadOnlyDictionary<string, UpgradeTemplate> Upgrades { get; }
    public IReadOnlyDictionary<string, ScienceTemplate> Sciences { get; }
    public IReadOnlyDictionary<string, SpecialPowerTemplate> SpecialPowers { get; }
    public IReadOnlyDictionary<string, CommandButtonTemplate> CommandButtons { get; }
    public IReadOnlyDictionary<string, CommandSetTemplate> CommandSets { get; }
    public bool IsEmpty => Upgrades.Count == 0 && Sciences.Count == 0
        && SpecialPowers.Count == 0 && CommandButtons.Count == 0 && CommandSets.Count == 0;

    internal static UpgradeTemplate ParseUpgrade(BundleNamedRow row)
    {
        var type = TechField.String(row.Fields, "Type", "OBJECT").ToUpperInvariant() switch
        {
            "PLAYER" => UpgradeType.Player,
            "OBJECT" => UpgradeType.Object,
            var value => throw new FormatException($"upgrade {row.Name}: unknown Type '{value}'"),
        };
        return new UpgradeTemplate(
            row.Name,
            type,
            TechField.Integer(row.Fields, "BuildCost"),
            TechField.SecondsToMilliseconds(row.Fields, "BuildTime"),
            TechField.Tokens(row.Fields, "Prerequisites"));
    }

    internal static ScienceTemplate ParseScience(BundleNamedRow row) => new(
        row.Name,
        TechField.Tokens(row.Fields, "PrerequisiteSciences"),
        TechField.Integer(row.Fields, "SciencePurchasePointCost"),
        TechField.Boolean(row.Fields, "IsGrantable", true));

    internal static SpecialPowerTemplate ParseSpecialPower(BundleNamedRow row) => new(
        row.Name,
        TechField.String(row.Fields, "Enum", row.Name),
        TechField.Integer(row.Fields, "ReloadTime"),
        TechField.Tokens(row.Fields, "RequiredScience"),
        TechField.Boolean(row.Fields, "PublicTimer"));

    internal static CommandButtonTemplate ParseCommandButton(BundleNamedRow row) => new(
        row.Name,
        TechField.String(row.Fields, "Command"),
        TechField.String(row.Fields, "Object"),
        TechField.String(row.Fields, "Upgrade"),
        TechField.String(row.Fields, "Science"),
        TechField.String(row.Fields, "SpecialPower"),
        TechField.String(row.Fields, "Stances"),
        TechField.String(row.Fields, "FlagsUsedForToggle"),
        TechField.String(row.Fields, "WeaponSlot"));

    private static IReadOnlyDictionary<string, T> Index<T>(IEnumerable<T>? values, Func<T, string> name)
    {
        var result = new SortedDictionary<string, T>(StringComparer.Ordinal);
        if (values != null)
        {
            foreach (var value in values) result.Add(name(value), value);
        }
        return result;
    }
}

internal static class TechField
{
    public static string String(
        IReadOnlyDictionary<string, BundleValue> fields,
        string name,
        string fallback = "") =>
        fields.TryGetValue(name, out var value) && value.Kind == BundleValueKind.String
            ? value.String!
            : fallback;

    public static bool Boolean(
        IReadOnlyDictionary<string, BundleValue> fields,
        string name,
        bool fallback = false)
    {
        if (!fields.TryGetValue(name, out var value)) return fallback;
        return value.Kind switch
        {
            BundleValueKind.Boolean => value.Boolean,
            BundleValueKind.Integer => value.Integer != 0,
            BundleValueKind.String when bool.TryParse(value.String, out var parsed) => parsed,
            _ => throw new FormatException($"field {name} is not boolean"),
        };
    }

    public static long Integer(
        IReadOnlyDictionary<string, BundleValue> fields,
        string name,
        long fallback = 0)
    {
        if (!fields.TryGetValue(name, out var value)) return fallback;
        return ExactLong(value, name);
    }

    public static long SecondsToMilliseconds(
        IReadOnlyDictionary<string, BundleValue> fields,
        string name)
    {
        if (!fields.TryGetValue(name, out var value)) return 0;
        var raw = value.Kind switch
        {
            BundleValueKind.Integer => checked(value.Integer * Fixed64.OneRaw),
            BundleValueKind.Fixed => value.Fixed.Raw,
            _ => throw new FormatException($"field {name} is not an exact number"),
        };
        var scaled = (BigInteger)raw * 1000;
        if (scaled % Fixed64.OneRaw != 0)
            throw new FormatException($"field {name} cannot be represented as exact milliseconds");
        var milliseconds = scaled / Fixed64.OneRaw;
        if (milliseconds < 0 || milliseconds > long.MaxValue)
            throw new OverflowException($"field {name} millisecond conversion overflow");
        return (long)milliseconds;
    }

    public static IReadOnlyList<string> Tokens(
        IReadOnlyDictionary<string, BundleValue> fields,
        string name)
    {
        if (!fields.TryGetValue(name, out var value)) return Array.Empty<string>();
        var result = new List<string>();
        AddTokens(value, result);
        return result.ToArray();
    }

    private static void AddTokens(BundleValue value, ICollection<string> result)
    {
        if (value.Kind == BundleValueKind.List)
        {
            foreach (var item in value.Items) AddTokens(item, result);
            return;
        }
        if (value.Kind != BundleValueKind.String)
            throw new FormatException("token field contains a non-string value");
        foreach (var token in value.String!.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
            result.Add(token);
    }

    private static long ExactLong(BundleValue value, string name)
    {
        if (value.Kind == BundleValueKind.Integer) return value.Integer;
        if (value.Kind == BundleValueKind.Fixed && value.Fixed.Raw % Fixed64.OneRaw == 0)
            return value.Fixed.Raw / Fixed64.OneRaw;
        throw new FormatException($"field {name} is not an exact integer");
    }
}
