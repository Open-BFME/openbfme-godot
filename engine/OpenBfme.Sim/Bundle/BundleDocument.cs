using System.Numerics;
using System.Text.Json;

namespace OpenBfme.Sim;

public enum BundleValueKind
{
    Null,
    String,
    Integer,
    Fixed,
    Boolean,
    List,
}

/// <summary>An immutable bundle scalar or repeated-field list.</summary>
public sealed class BundleValue
{
    private BundleValue(
        BundleValueKind kind,
        string? text = null,
        long integer = 0,
        Fixed64 fixedValue = default,
        bool boolean = false,
        IReadOnlyList<BundleValue>? items = null)
    {
        Kind = kind;
        String = text;
        Integer = integer;
        Fixed = fixedValue;
        Boolean = boolean;
        Items = items ?? Array.Empty<BundleValue>();
    }

    public BundleValueKind Kind { get; }
    public string? String { get; }
    public long Integer { get; }
    public Fixed64 Fixed { get; }
    public bool Boolean { get; }
    public IReadOnlyList<BundleValue> Items { get; }

    internal static BundleValue Null() => new(BundleValueKind.Null);
    internal static BundleValue Text(string value) => new(BundleValueKind.String, text: value);
    internal static BundleValue Whole(long value) => new(BundleValueKind.Integer, integer: value);
    internal static BundleValue Exact(Fixed64 value) => new(BundleValueKind.Fixed, fixedValue: value);
    internal static BundleValue Flag(bool value) => new(BundleValueKind.Boolean, boolean: value);
    internal static BundleValue Repeated(IEnumerable<BundleValue> values) =>
        new(BundleValueKind.List, items: values.ToArray());

    internal object? ToObject() => Kind switch
    {
        BundleValueKind.Null => null,
        BundleValueKind.String => String,
        BundleValueKind.Integer => Integer,
        BundleValueKind.Fixed => Fixed,
        BundleValueKind.Boolean => Boolean,
        BundleValueKind.List => Items.Select(item => item.ToObject()).ToArray(),
        _ => throw new InvalidOperationException($"Unknown bundle value kind {Kind}"),
    };
}

public sealed record BundleBlock(
    string Type,
    string Tag,
    IReadOnlyDictionary<string, BundleValue> Fields,
    IReadOnlyList<BundleBlock> Blocks);

public sealed record BundleModuleRow(
    string Carrier,
    string Type,
    string Tag,
    IReadOnlyDictionary<string, BundleValue> Fields,
    IReadOnlyList<BundleBlock> Blocks,
    bool Gap);

public sealed record BundleGeometry(
    string? Shape,
    Fixed64? MajorRadius,
    Fixed64? MinorRadius,
    Fixed64? Height);

public sealed record BundleTemplateRow(
    int Index,
    string Name,
    string Kind,
    string? Parent,
    string? Side,
    IReadOnlyList<string> KindOf,
    BundleGeometry Geometry,
    Fixed64? BuildCost,
    Fixed64? BuildTime,
    long? CommandPoints,
    Fixed64? Health,
    IReadOnlyDictionary<string, BundleValue> Fields,
    IReadOnlyList<BundleBlock> Blocks,
    IReadOnlyList<BundleModuleRow> Modules);

public sealed record BundleSourcePath(string Path, string Sha256);
public sealed record BundleSource(string EffectiveTreeSha256, IReadOnlyList<BundleSourcePath> Paths);
public sealed record BundleDiagnostic(string Template, string Message);
public sealed record BundleNamedRow(
    string Name,
    IReadOnlyDictionary<string, BundleValue> Fields);
public sealed record BundleWeaponNugget(
    string Kind,
    IReadOnlyDictionary<string, BundleValue> Fields);
public sealed record BundleWeaponRow(
    string Name,
    IReadOnlyDictionary<string, BundleValue> Fields,
    IReadOnlyList<BundleWeaponNugget> Nuggets);
public sealed record BundleArmorEntry(string DamageType, Fixed64 Percent);
public sealed record BundleArmorRow(
    string Name,
    IReadOnlyList<BundleArmorEntry> Entries,
    IReadOnlyDictionary<string, BundleValue> Fields);
public sealed record BundleRankPosition(Fixed64 X, Fixed64 Y);
public sealed record BundleRankInfo(
    long Rank,
    string UnitType,
    IReadOnlyList<BundleRankPosition> Positions);
public sealed record BundleHordeRow(
    string Name,
    IReadOnlyList<BundleRankInfo> RankInfo,
    IReadOnlyDictionary<string, BundleValue> Fields);

/// <summary>Strict immutable reader for contracts/bundle-v1.</summary>
public sealed class BundleDocument
{
    public const string ExpectedSchema = "openbfme.bundle.v1";

    private BundleDocument(
        string schema,
        BundleSource source,
        IReadOnlyList<BundleTemplateRow> templates,
        IReadOnlyDictionary<string, BundleValue> defines,
        IReadOnlyList<BundleDiagnostic> diagnostics,
        IReadOnlyList<BundleWeaponRow>? weapons,
        IReadOnlyList<BundleArmorRow>? armors,
        IReadOnlyList<BundleNamedRow>? damageFx,
        IReadOnlyList<BundleNamedRow>? locomotors,
        IReadOnlyList<BundleNamedRow>? locomotorSets,
        IReadOnlyList<BundleHordeRow>? hordes)
    {
        Schema = schema;
        Source = source;
        Templates = templates;
        Defines = defines;
        Diagnostics = diagnostics;
        Weapons = weapons;
        Armors = armors;
        DamageFx = damageFx;
        Locomotors = locomotors;
        LocomotorSets = locomotorSets;
        Hordes = hordes;
    }

    public string Schema { get; }
    public BundleSource Source { get; }
    public IReadOnlyList<BundleTemplateRow> Templates { get; }
    public IReadOnlyDictionary<string, BundleValue> Defines { get; }
    public IReadOnlyList<BundleDiagnostic> Diagnostics { get; }
    public IReadOnlyList<BundleWeaponRow>? Weapons { get; }
    public IReadOnlyList<BundleArmorRow>? Armors { get; }
    public IReadOnlyList<BundleNamedRow>? DamageFx { get; }
    public IReadOnlyList<BundleNamedRow>? Locomotors { get; }
    public IReadOnlyList<BundleNamedRow>? LocomotorSets { get; }
    public IReadOnlyList<BundleHordeRow>? Hordes { get; }

    public static BundleDocument Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        try
        {
            return Parse(File.ReadAllText(path));
        }
        catch (BundleDocumentException)
        {
            throw;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            throw new BundleDocumentException($"Could not read bundle '{path}'", exception);
        }
    }

    public static BundleDocument Parse(string json)
    {
        JsonDocument parsed;
        try
        {
            parsed = JsonDocument.Parse(json, new JsonDocumentOptions
            {
                AllowTrailingCommas = false,
                CommentHandling = JsonCommentHandling.Disallow,
                MaxDepth = 128,
            });
        }
        catch (JsonException exception)
        {
            throw new BundleDocumentException("Bundle is not valid JSON", exception);
        }

        using (parsed)
        {
            var root = RequireKind(parsed.RootElement, JsonValueKind.Object, "$", "object");
            CheckProperties(root, "$", "schema", "source", "templates", "defines", "diagnostics",
                "weapons", "armors", "damage_fx", "locomotors", "locomotor_sets", "hordes");
            var schema = RequireString(root, "schema", "$", nonEmpty: true);
            if (schema != ExpectedSchema)
            {
                throw Error("$.schema", $"is '{schema}' (expected '{ExpectedSchema}')");
            }
            var source = ReadSource(Require(root, "source", "$"));
            var templatesElement = RequireKind(Require(root, "templates", "$"),
                JsonValueKind.Array, "$.templates", "array");
            var templates = new List<BundleTemplateRow>();
            var index = 0;
            foreach (var row in templatesElement.EnumerateArray())
            {
                templates.Add(ReadTemplate(row, index));
                index++;
            }
            var defines = ReadFields(RequireKind(Require(root, "defines", "$"),
                JsonValueKind.Object, "$.defines", "object"), "$.defines", listsAllowed: false);
            var diagnostics = ReadDiagnostics(Require(root, "diagnostics", "$"));
            return new BundleDocument(
                schema, source, templates.ToArray(), defines, diagnostics,
                ReadOptionalWeapons(root),
                ReadOptionalArmors(root),
                ReadOptionalNamedTable(root, "damage_fx"),
                ReadOptionalNamedTable(root, "locomotors"),
                ReadOptionalNamedTable(root, "locomotor_sets"),
                ReadOptionalHordes(root));
        }
    }

    private static BundleTemplateRow ReadTemplate(JsonElement row, int index)
    {
        var path = $"$.templates[{index}]";
        RequireKind(row, JsonValueKind.Object, path, "object");
        CheckProperties(row, path, "name", "kind", "parent", "side", "kindof", "geometry",
            "build_cost", "build_time", "command_points", "health", "fields", "blocks", "modules");
        var name = RequireString(row, "name", path, nonEmpty: true);
        var kind = RequireString(row, "kind", path, nonEmpty: true);
        if (kind is not ("object" or "child" or "reskin")) throw Error(path + ".kind", $"unknown kind '{kind}'");
        var parent = OptionalString(row, "parent", path);
        if (kind is "child" or "reskin" && string.IsNullOrWhiteSpace(parent))
        {
            throw Error(path + ".parent", $"{kind} requires a non-empty parent");
        }
        var side = OptionalString(row, "side", path);
        var kindOf = ReadStrings(Require(row, "kindof", path), path + ".kindof");
        var geometry = ReadGeometry(Require(row, "geometry", path), path + ".geometry");
        var fields = ReadFields(RequireKind(Require(row, "fields", path),
            JsonValueKind.Object, path + ".fields", "object"), path + ".fields", listsAllowed: true);
        var blocks = ReadBlocks(Require(row, "blocks", path), path + ".blocks");
        var modules = ReadModules(Require(row, "modules", path), path + ".modules");
        return new BundleTemplateRow(
            index, name, kind, parent, side, kindOf, geometry,
            OptionalFixed(row, "build_cost", path),
            OptionalFixed(row, "build_time", path),
            OptionalLong(row, "command_points", path),
            OptionalFixed(row, "health", path),
            fields, blocks, modules);
    }

    private static BundleGeometry ReadGeometry(JsonElement element, string path)
    {
        RequireKind(element, JsonValueKind.Object, path, "object");
        CheckProperties(element, path, "shape", "major_radius", "minor_radius", "height");
        return new BundleGeometry(
            OptionalString(element, "shape", path),
            OptionalFixed(element, "major_radius", path),
            OptionalFixed(element, "minor_radius", path),
            OptionalFixed(element, "height", path));
    }

    private static IReadOnlyList<BundleModuleRow> ReadModules(JsonElement element, string path)
    {
        RequireKind(element, JsonValueKind.Array, path, "array");
        var result = new List<BundleModuleRow>();
        var index = 0;
        foreach (var row in element.EnumerateArray())
        {
            var itemPath = $"{path}[{index}]";
            RequireKind(row, JsonValueKind.Object, itemPath, "object");
            CheckProperties(row, itemPath, "carrier", "type", "tag", "fields", "blocks", "gap");
            var carrier = RequireString(row, "carrier", itemPath, nonEmpty: true);
            if (carrier is not ("Behavior" or "Body" or "Draw" or "ClientUpdate"
                or "ClientBehavior" or "Flasher" or "other"))
            {
                throw Error(itemPath + ".carrier", $"unknown carrier '{carrier}'");
            }
            result.Add(new BundleModuleRow(
                carrier,
                RequireString(row, "type", itemPath, nonEmpty: true),
                RequireString(row, "tag", itemPath, nonEmpty: false),
                ReadFields(RequireKind(Require(row, "fields", itemPath), JsonValueKind.Object,
                    itemPath + ".fields", "object"), itemPath + ".fields", listsAllowed: true),
                ReadBlocks(Require(row, "blocks", itemPath), itemPath + ".blocks"),
                RequireBoolean(row, "gap", itemPath)));
            index++;
        }
        return result.ToArray();
    }

    private static IReadOnlyList<BundleBlock> ReadBlocks(JsonElement element, string path)
    {
        RequireKind(element, JsonValueKind.Array, path, "array");
        var result = new List<BundleBlock>();
        var index = 0;
        foreach (var row in element.EnumerateArray())
        {
            var itemPath = $"{path}[{index}]";
            RequireKind(row, JsonValueKind.Object, itemPath, "object");
            CheckProperties(row, itemPath, "type", "tag", "fields", "blocks");
            result.Add(new BundleBlock(
                RequireString(row, "type", itemPath, nonEmpty: true),
                RequireString(row, "tag", itemPath, nonEmpty: false),
                ReadFields(RequireKind(Require(row, "fields", itemPath), JsonValueKind.Object,
                    itemPath + ".fields", "object"), itemPath + ".fields", listsAllowed: true),
                ReadBlocks(Require(row, "blocks", itemPath), itemPath + ".blocks")));
            index++;
        }
        return result.ToArray();
    }

    private static IReadOnlyDictionary<string, BundleValue> ReadFields(
        JsonElement element,
        string path,
        bool listsAllowed)
    {
        var result = new SortedDictionary<string, BundleValue>(StringComparer.Ordinal);
        foreach (var property in element.EnumerateObject())
        {
            result.Add(property.Name, ReadValue(property.Value, path + "." + property.Name, listsAllowed));
        }
        return result;
    }

    private static BundleValue ReadValue(JsonElement element, string path, bool listsAllowed)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.Null:
                return BundleValue.Null();
            case JsonValueKind.String:
            {
                var text = element.GetString()!;
                return TryReadExactNumber(text, out var number) ? number : BundleValue.Text(text);
            }
            case JsonValueKind.Number:
                return TryReadExactNumber(element.GetRawText(), out var numericValue)
                    ? numericValue
                    : throw Error(path, "number is outside Fixed64/long range");
            case JsonValueKind.True:
                return BundleValue.Flag(true);
            case JsonValueKind.False:
                return BundleValue.Flag(false);
            case JsonValueKind.Array when listsAllowed:
            {
                var values = element.EnumerateArray().Select((item, index) =>
                    ReadValue(item, $"{path}[{index}]", listsAllowed: false)).ToArray();
                if (values.Length < 2) throw Error(path, "field lists require at least two scalar values");
                return BundleValue.Repeated(values);
            }
            default:
                throw Error(path, listsAllowed
                    ? "must be a scalar or scalar list"
                    : "must be a scalar");
        }
    }

    private static bool TryReadExactNumber(string text, out BundleValue value)
    {
        value = null!;
        var span = text.AsSpan().Trim();
        if (span.IsEmpty) return false;
        var negative = false;
        if (span[0] is '+' or '-')
        {
            negative = span[0] == '-';
            span = span[1..];
            if (span.IsEmpty) return false;
        }
        var exponent = 0;
        var exponentIndex = span.IndexOfAny('e', 'E');
        if (exponentIndex >= 0)
        {
            if (!int.TryParse(span[(exponentIndex + 1)..],
                    System.Globalization.NumberStyles.AllowLeadingSign,
                    System.Globalization.CultureInfo.InvariantCulture, out exponent)) return false;
            span = span[..exponentIndex];
        }
        var point = span.IndexOf('.');
        var fractionDigits = point < 0 ? 0 : span.Length - point - 1;
        var digits = point < 0 ? span.ToString() : string.Concat(span[..point], span[(point + 1)..]);
        if (digits.Length == 0 || digits.Any(character => character is < '0' or > '9')) return false;
        if (point < 0 && exponentIndex < 0
            && long.TryParse((negative ? "-" : "") + digits,
                System.Globalization.NumberStyles.AllowLeadingSign,
                System.Globalization.CultureInfo.InvariantCulture, out var integer))
        {
            value = BundleValue.Whole(integer);
            return true;
        }
        if (Math.Abs((long)exponent) + fractionDigits > 1000) return false;
        var numerator = BigInteger.Parse(digits, System.Globalization.CultureInfo.InvariantCulture);
        if (negative) numerator = -numerator;
        var scale = fractionDigits - exponent;
        var denominator = BigInteger.One;
        if (scale > 0) denominator = BigInteger.Pow(10, scale);
        else if (scale < 0) numerator *= BigInteger.Pow(10, -scale);
        var raw = (numerator << Fixed64.FractionBits) / denominator;
        if (raw < long.MinValue || raw > long.MaxValue) return false;
        value = BundleValue.Exact(Fixed64.FromRaw((long)raw));
        return true;
    }

    private static BundleSource ReadSource(JsonElement element)
    {
        const string path = "$.source";
        RequireKind(element, JsonValueKind.Object, path, "object");
        CheckProperties(element, path, "effective_tree_sha256", "paths");
        var treeHash = RequireString(element, "effective_tree_sha256", path, nonEmpty: true);
        ValidateSha(treeHash, path + ".effective_tree_sha256");
        var pathsElement = RequireKind(Require(element, "paths", path), JsonValueKind.Array,
            path + ".paths", "array");
        var paths = new List<BundleSourcePath>();
        var index = 0;
        foreach (var row in pathsElement.EnumerateArray())
        {
            var itemPath = $"{path}.paths[{index}]";
            RequireKind(row, JsonValueKind.Object, itemPath, "object");
            CheckProperties(row, itemPath, "path", "sha256");
            var sourcePath = RequireString(row, "path", itemPath, nonEmpty: true);
            var sha = RequireString(row, "sha256", itemPath, nonEmpty: true);
            ValidateSha(sha, itemPath + ".sha256");
            paths.Add(new BundleSourcePath(sourcePath, sha));
            index++;
        }
        return new BundleSource(treeHash, paths.ToArray());
    }

    private static IReadOnlyList<BundleDiagnostic> ReadDiagnostics(JsonElement element)
    {
        const string path = "$.diagnostics";
        RequireKind(element, JsonValueKind.Array, path, "array");
        var result = new List<BundleDiagnostic>();
        var index = 0;
        foreach (var row in element.EnumerateArray())
        {
            var itemPath = $"{path}[{index}]";
            RequireKind(row, JsonValueKind.Object, itemPath, "object");
            CheckProperties(row, itemPath, "template", "message");
            result.Add(new BundleDiagnostic(
                RequireString(row, "template", itemPath, nonEmpty: false),
                RequireString(row, "message", itemPath, nonEmpty: true)));
            index++;
        }
        return result.ToArray();
    }

    private static IReadOnlyList<BundleNamedRow>? ReadOptionalNamedTable(JsonElement root, string name)
    {
        if (!root.TryGetProperty(name, out var element)) return null;
        RequireKind(element, JsonValueKind.Array, "$." + name, "array");
        var result = new List<BundleNamedRow>();
        var index = 0;
        foreach (var row in element.EnumerateArray())
        {
            result.Add(ReadNamedRow(row, $"$.{name}[{index}]"));
            index++;
        }
        return result.ToArray();
    }

    private static BundleNamedRow ReadNamedRow(JsonElement row, string path)
    {
        RequireKind(row, JsonValueKind.Object, path, "object");
        CheckProperties(row, path, "name", "fields");
        return new BundleNamedRow(
            RequireString(row, "name", path, nonEmpty: true),
            ReadFields(RequireKind(Require(row, "fields", path), JsonValueKind.Object,
                path + ".fields", "object"), path + ".fields", listsAllowed: true));
    }

    private static IReadOnlyList<BundleWeaponRow>? ReadOptionalWeapons(JsonElement root)
    {
        if (!root.TryGetProperty("weapons", out var element)) return null;
        const string path = "$.weapons";
        RequireKind(element, JsonValueKind.Array, path, "array");
        var result = new List<BundleWeaponRow>();
        var index = 0;
        foreach (var row in element.EnumerateArray())
        {
            var itemPath = $"{path}[{index}]";
            RequireKind(row, JsonValueKind.Object, itemPath, "object");
            CheckProperties(row, itemPath, "name", "fields", "nuggets");
            var nuggetsElement = RequireKind(Require(row, "nuggets", itemPath),
                JsonValueKind.Array, itemPath + ".nuggets", "array");
            var nuggets = new List<BundleWeaponNugget>();
            var nuggetIndex = 0;
            foreach (var nugget in nuggetsElement.EnumerateArray())
            {
                var nuggetPath = $"{itemPath}.nuggets[{nuggetIndex}]";
                RequireKind(nugget, JsonValueKind.Object, nuggetPath, "object");
                CheckProperties(nugget, nuggetPath, "kind", "fields");
                var kind = RequireString(nugget, "kind", nuggetPath, nonEmpty: true);
                if (kind is not ("DamageNugget" or "MetaImpactNugget" or "ProjectileNugget" or "other"))
                    throw Error(nuggetPath + ".kind", $"unknown nugget kind '{kind}'");
                nuggets.Add(new BundleWeaponNugget(kind,
                    ReadFields(RequireKind(Require(nugget, "fields", nuggetPath), JsonValueKind.Object,
                        nuggetPath + ".fields", "object"), nuggetPath + ".fields", listsAllowed: true)));
                nuggetIndex++;
            }
            result.Add(new BundleWeaponRow(
                RequireString(row, "name", itemPath, nonEmpty: true),
                ReadFields(RequireKind(Require(row, "fields", itemPath), JsonValueKind.Object,
                    itemPath + ".fields", "object"), itemPath + ".fields", listsAllowed: true),
                nuggets.ToArray()));
            index++;
        }
        return result.ToArray();
    }

    private static IReadOnlyList<BundleArmorRow>? ReadOptionalArmors(JsonElement root)
    {
        if (!root.TryGetProperty("armors", out var element)) return null;
        const string path = "$.armors";
        RequireKind(element, JsonValueKind.Array, path, "array");
        var result = new List<BundleArmorRow>();
        var index = 0;
        foreach (var row in element.EnumerateArray())
        {
            var itemPath = $"{path}[{index}]";
            RequireKind(row, JsonValueKind.Object, itemPath, "object");
            CheckProperties(row, itemPath, "name", "entries", "fields");
            var entriesElement = RequireKind(Require(row, "entries", itemPath), JsonValueKind.Array,
                itemPath + ".entries", "array");
            var entries = new List<BundleArmorEntry>();
            var entryIndex = 0;
            foreach (var entry in entriesElement.EnumerateArray())
            {
                var entryPath = $"{itemPath}.entries[{entryIndex}]";
                RequireKind(entry, JsonValueKind.Object, entryPath, "object");
                CheckProperties(entry, entryPath, "damage_type", "percent");
                entries.Add(new BundleArmorEntry(
                    RequireString(entry, "damage_type", entryPath, nonEmpty: true),
                    OptionalFixed(entry, "percent", entryPath)
                        ?? throw Error(entryPath + ".percent", "is required")));
                entryIndex++;
            }
            if (entries.Count == 0) throw Error(itemPath + ".entries", "requires at least one entry");
            result.Add(new BundleArmorRow(
                RequireString(row, "name", itemPath, nonEmpty: true),
                entries.ToArray(),
                ReadFields(RequireKind(Require(row, "fields", itemPath), JsonValueKind.Object,
                    itemPath + ".fields", "object"), itemPath + ".fields", listsAllowed: true)));
            index++;
        }
        return result.ToArray();
    }

    private static IReadOnlyList<BundleHordeRow>? ReadOptionalHordes(JsonElement root)
    {
        if (!root.TryGetProperty("hordes", out var element)) return null;
        const string path = "$.hordes";
        RequireKind(element, JsonValueKind.Array, path, "array");
        var result = new List<BundleHordeRow>();
        var index = 0;
        foreach (var row in element.EnumerateArray())
        {
            var itemPath = $"{path}[{index}]";
            RequireKind(row, JsonValueKind.Object, itemPath, "object");
            CheckProperties(row, itemPath, "name", "rank_info", "fields");
            var ranksElement = RequireKind(Require(row, "rank_info", itemPath), JsonValueKind.Array,
                itemPath + ".rank_info", "array");
            var ranks = new List<BundleRankInfo>();
            var rankIndex = 0;
            foreach (var rank in ranksElement.EnumerateArray())
            {
                var rankPath = $"{itemPath}.rank_info[{rankIndex}]";
                RequireKind(rank, JsonValueKind.Object, rankPath, "object");
                CheckProperties(rank, rankPath, "rank", "unit_type", "position");
                var rankNumber = OptionalLong(rank, "rank", rankPath)
                    ?? throw Error(rankPath + ".rank", "is required");
                if (rankNumber < 1) throw Error(rankPath + ".rank", "must be positive");
                var positionsElement = RequireKind(Require(rank, "position", rankPath), JsonValueKind.Array,
                    rankPath + ".position", "array");
                var positions = new List<BundleRankPosition>();
                var positionIndex = 0;
                foreach (var position in positionsElement.EnumerateArray())
                {
                    var positionPath = $"{rankPath}.position[{positionIndex}]";
                    RequireKind(position, JsonValueKind.Object, positionPath, "object");
                    CheckProperties(position, positionPath, "x", "y");
                    positions.Add(new BundleRankPosition(
                        OptionalFixed(position, "x", positionPath) ?? throw Error(positionPath + ".x", "is required"),
                        OptionalFixed(position, "y", positionPath) ?? throw Error(positionPath + ".y", "is required")));
                    positionIndex++;
                }
                if (positions.Count == 0) throw Error(rankPath + ".position", "requires at least one position");
                ranks.Add(new BundleRankInfo(
                    rankNumber,
                    RequireString(rank, "unit_type", rankPath, nonEmpty: true),
                    positions.ToArray()));
                rankIndex++;
            }
            result.Add(new BundleHordeRow(
                RequireString(row, "name", itemPath, nonEmpty: true),
                ranks.ToArray(),
                ReadFields(RequireKind(Require(row, "fields", itemPath), JsonValueKind.Object,
                    itemPath + ".fields", "object"), itemPath + ".fields", listsAllowed: true)));
            index++;
        }
        return result.ToArray();
    }

    private static IReadOnlyList<string> ReadStrings(JsonElement element, string path)
    {
        RequireKind(element, JsonValueKind.Array, path, "array");
        var result = new List<string>();
        var index = 0;
        foreach (var item in element.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.String || string.IsNullOrEmpty(item.GetString()))
                throw Error($"{path}[{index}]", "must be a non-empty string");
            result.Add(item.GetString()!);
            index++;
        }
        return result.ToArray();
    }

    private static Fixed64? OptionalFixed(JsonElement row, string name, string path)
    {
        if (!row.TryGetProperty(name, out var element)) return null;
        if (!TryReadExactNumber(element.ValueKind == JsonValueKind.String
                ? element.GetString()!
                : element.GetRawText(), out var value)
            || value.Kind is not (BundleValueKind.Integer or BundleValueKind.Fixed))
            throw Error(path + "." + name, "must be an exact number");
        return value.Kind == BundleValueKind.Integer ? Fixed64.FromInt64(value.Integer) : value.Fixed;
    }

    private static long? OptionalLong(JsonElement row, string name, string path)
    {
        if (!row.TryGetProperty(name, out var element)) return null;
        if (!TryReadExactNumber(element.ValueKind == JsonValueKind.String
                ? element.GetString()!
                : element.GetRawText(), out var value)
            || value.Kind != BundleValueKind.Integer)
            throw Error(path + "." + name, "must be an integer");
        return value.Integer;
    }

    private static string? OptionalString(JsonElement row, string name, string path)
    {
        if (!row.TryGetProperty(name, out var value) || value.ValueKind == JsonValueKind.Null) return null;
        if (value.ValueKind != JsonValueKind.String) throw Error(path + "." + name, "must be a string or null");
        return value.GetString();
    }

    private static string RequireString(JsonElement row, string name, string path, bool nonEmpty)
    {
        var value = Require(row, name, path);
        if (value.ValueKind != JsonValueKind.String || (nonEmpty && string.IsNullOrEmpty(value.GetString())))
            throw Error(path + "." + name, nonEmpty ? "must be a non-empty string" : "must be a string");
        return value.GetString()!;
    }

    private static bool RequireBoolean(JsonElement row, string name, string path)
    {
        var value = Require(row, name, path);
        if (value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            throw Error(path + "." + name, "must be a boolean");
        return value.GetBoolean();
    }

    private static JsonElement Require(JsonElement row, string name, string path) =>
        row.TryGetProperty(name, out var value)
            ? value
            : throw Error(path + "." + name, "is required");

    private static JsonElement RequireKind(JsonElement value, JsonValueKind kind, string path, string description) =>
        value.ValueKind == kind ? value : throw Error(path, $"must be a JSON {description}");

    private static void CheckProperties(JsonElement row, string path, params string[] allowed)
    {
        var set = new HashSet<string>(allowed, StringComparer.Ordinal);
        foreach (var property in row.EnumerateObject())
        {
            if (!set.Contains(property.Name)) throw Error(path + "." + property.Name, "is not allowed by bundle-v1");
        }
    }

    private static void ValidateSha(string value, string path)
    {
        if (value.Length != 64 || value.Any(character => character is not (>= '0' and <= '9')
                and not (>= 'a' and <= 'f')))
            throw Error(path, "must be 64 lowercase hexadecimal characters");
    }

    private static BundleDocumentException Error(string path, string detail) =>
        new($"Bundle field {path} {detail}");
}

public sealed class BundleDocumentException : Exception
{
    public BundleDocumentException(string message) : base(message) { }
    public BundleDocumentException(string message, Exception innerException) : base(message, innerException) { }
}
