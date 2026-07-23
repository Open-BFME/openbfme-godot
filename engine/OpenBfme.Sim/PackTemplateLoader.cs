using System.Globalization;
using System.Numerics;
using System.Text.Json;

namespace OpenBfme.Sim;

/// <summary>
/// Loads ObjectTemplates from a content pack's data/objects.json document
/// (schema "openbfme.objects", rows like bfme2.object.gondor-fighter).
///
/// FLOAT-AVOIDANCE STRATEGY (the hard determinism requirement):
/// numeric JSON is never read through GetDouble/GetSingle. Every number is
/// taken as its raw JSON text (JsonElement.GetRawText()), parsed with
/// decimal.Parse(InvariantCulture) — which is exact base-10, no binary
/// float round-trip — then decomposed via decimal.GetBits into an exact
/// integer numerator and power-of-ten denominator. That rational feeds
/// Fixed64.FromFraction (itself BigInteger-based). The same JSON text
/// therefore yields bit-identical Fixed64 raw values on every platform.
/// No float/double arithmetic exists anywhere on the loader path.
///
/// STRUCTURE-VS-UNIT PREDICATE: the pack row's "kind" field is authoritative.
/// kind == "structure" gets a StructureBody module; the mobile kinds
/// ("member", "builder", "battalion") get an ActiveBody module. Any other
/// kind is skipped fail-closed with a typed reason — never guessed.
/// </summary>
public static class PackTemplateLoader
{
    public const string ExpectedSchema = "openbfme.objects";

    /// <summary>Ticks used to convert pack speed (units/second) to per-tick movement.</summary>
    public const int TicksPerSecond = SimWorld.TicksPerSecond;

    private static readonly string[] MobileKinds = { "member", "builder", "battalion" };

    public static PackTemplateLoadResult LoadFromObjectsDocument(string json)
    {
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(json);
        }
        catch (JsonException exception)
        {
            throw new PackObjectsDocumentException("objects document is not valid JSON", exception);
        }

        using (document)
        {
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                throw new PackObjectsDocumentException("objects document root is not a JSON object");
            }
            if (root.TryGetProperty("schema", out var schema)
                && (schema.ValueKind != JsonValueKind.String || schema.GetString() != ExpectedSchema))
            {
                throw new PackObjectsDocumentException(
                    $"objects document schema is '{schema}' (expected '{ExpectedSchema}')");
            }
            if (!root.TryGetProperty("objects", out var rows) || rows.ValueKind != JsonValueKind.Array)
            {
                throw new PackObjectsDocumentException("objects document has no 'objects' array");
            }

            var templates = new List<ObjectTemplate>();
            var seenIds = new HashSet<string>(StringComparer.Ordinal);
            var skipped = new List<SkippedRow>();
            var unmapped = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var notes = new List<string>();

            var index = -1;
            foreach (var row in rows.EnumerateArray())
            {
                index++;
                LoadRow(row, index, templates, seenIds, skipped, unmapped, notes);
            }

            var report = new LoadReport(templates.Count, skipped, unmapped, notes);
            return new PackTemplateLoadResult(templates, report);
        }
    }

    private static void LoadRow(
        JsonElement row,
        int index,
        List<ObjectTemplate> templates,
        HashSet<string> seenIds,
        List<SkippedRow> skipped,
        SortedDictionary<string, int> unmapped,
        List<string> notes)
    {
        if (row.ValueKind != JsonValueKind.Object)
        {
            skipped.Add(new SkippedRow(index, "", RowSkipReason.NotAnObject, $"row kind is {row.ValueKind}"));
            return;
        }
        if (!row.TryGetProperty("id", out var idElement) || idElement.ValueKind != JsonValueKind.String
            || string.IsNullOrEmpty(idElement.GetString()))
        {
            skipped.Add(new SkippedRow(index, "", RowSkipReason.MissingId, "row has no non-empty string 'id'"));
            return;
        }
        var id = idElement.GetString()!;
        if (!row.TryGetProperty("kind", out var kindElement) || kindElement.ValueKind != JsonValueKind.String)
        {
            skipped.Add(new SkippedRow(index, id, RowSkipReason.MissingKind, "row has no string 'kind'"));
            return;
        }
        var kind = kindElement.GetString()!;
        var isStructure = kind == "structure";
        if (!isStructure && Array.IndexOf(MobileKinds, kind) < 0)
        {
            skipped.Add(new SkippedRow(index, id, RowSkipReason.UnknownKind, $"kind '{kind}' is not a known object kind"));
            return;
        }
        if (!seenIds.Add(id))
        {
            skipped.Add(new SkippedRow(index, id, RowSkipReason.DuplicateId, "a previous row already used this id"));
            return;
        }

        var bodyData = new SortedDictionary<string, long>(StringComparer.Ordinal);
        var bodyStrings = new SortedDictionary<string, string>(StringComparer.Ordinal);
        Fixed64? speedPerTick = null;
        var invalid = false;

        foreach (var property in row.EnumerateObject())
        {
            switch (property.Name)
            {
                case "id":
                case "kind":
                    break; // consumed above
                case "displayName":
                    if (property.Value.ValueKind == JsonValueKind.String)
                    {
                        bodyStrings["DisplayName"] = property.Value.GetString()!;
                    }
                    else
                    {
                        CountUnmapped(unmapped, "displayName");
                    }
                    break;
                case "memberObjectId":
                    if (property.Value.ValueKind == JsonValueKind.String)
                    {
                        bodyStrings["MemberObjectId"] = property.Value.GetString()!;
                    }
                    else
                    {
                        CountUnmapped(unmapped, "memberObjectId");
                    }
                    break;
                case "memberCount":
                    invalid |= !TryMapPositiveLong(property.Value, "memberCount", "MemberCount", bodyData, id, index, skipped);
                    break;
                case "commandPoints":
                    invalid |= !TryMapPositiveLong(property.Value, "commandPoints", "CommandPoints", bodyData, id, index, skipped);
                    break;
                case "simulation":
                    if (property.Value.ValueKind != JsonValueKind.Object)
                    {
                        skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidNumericField, "'simulation' is not an object"));
                        invalid = true;
                        break;
                    }
                    foreach (var simProperty in property.Value.EnumerateObject())
                    {
                        switch (simProperty.Name)
                        {
                            case "health":
                                invalid |= !TryMapPositiveLong(simProperty.Value, "simulation.health", "MaxHealth", bodyData, id, index, skipped);
                                break;
                            case "speed":
                                if (TryReadFraction(simProperty.Value, out var numerator, out var denominator)
                                    && numerator > 0 && denominator <= long.MaxValue / TicksPerSecond)
                                {
                                    // Pack speed is units/second; the sim moves per tick.
                                    speedPerTick = Fixed64.FromFraction(numerator, denominator * TicksPerSecond);
                                }
                                else
                                {
                                    skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidNumericField,
                                        $"'simulation.speed' is not a positive number: {simProperty.Value.GetRawText()}"));
                                    invalid = true;
                                }
                                break;
                            default:
                                CountUnmapped(unmapped, "simulation." + simProperty.Name);
                                break;
                        }
                        if (invalid)
                        {
                            break;
                        }
                    }
                    break;
                default:
                    // presentation, animationCapabilityId, sourceTypeName, formations, ...
                    CountUnmapped(unmapped, property.Name);
                    break;
            }
            if (invalid)
            {
                break;
            }
        }
        if (invalid)
        {
            return;
        }

        if (!bodyData.ContainsKey("MaxHealth"))
        {
            notes.Add($"{id}: no simulation.health field; {(isStructure ? StructureBodyModule.TypeName : ActiveBodyModule.TypeName)} uses its module default");
        }

        var modules = new List<ModuleSpec>
        {
            new(isStructure ? StructureBodyModule.TypeName : ActiveBodyModule.TypeName, bodyData, bodyStrings),
        };
        if (speedPerTick is { } speed)
        {
            modules.Add(new ModuleSpec(LinearMoverModule.TypeName, new Dictionary<string, long>
            {
                ["SpeedPerTickRaw"] = speed.Raw,
            }));
        }
        templates.Add(new ObjectTemplate(id, modules));
    }

    private static bool TryMapPositiveLong(
        JsonElement element,
        string fieldName,
        string dataKey,
        SortedDictionary<string, long> data,
        string id,
        int index,
        List<SkippedRow> skipped)
    {
        if (TryReadFraction(element, out var numerator, out var denominator)
            && denominator == 1 && numerator > 0)
        {
            data[dataKey] = numerator;
            return true;
        }
        skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidNumericField,
            $"'{fieldName}' is not a positive integer: {(element.ValueKind == JsonValueKind.Number ? element.GetRawText() : element.ValueKind.ToString())}"));
        return false;
    }

    /// <summary>
    /// Exact rational read of a JSON number: raw text -> decimal (exact base-10)
    /// -> integer numerator over power-of-ten denominator, reduced. Returns false
    /// for non-numbers and values that cannot fit the long-based rational.
    /// Never touches float/double.
    /// </summary>
    private static bool TryReadFraction(JsonElement element, out long numerator, out long denominator)
    {
        numerator = 0;
        denominator = 1;
        if (element.ValueKind != JsonValueKind.Number)
        {
            return false;
        }
        decimal value;
        try
        {
            value = decimal.Parse(element.GetRawText(), NumberStyles.Float, CultureInfo.InvariantCulture);
        }
        catch (OverflowException)
        {
            return false;
        }
        catch (FormatException)
        {
            return false;
        }
        var bits = decimal.GetBits(value);
        var scale = (bits[3] >> 16) & 0xFF;
        var negative = (bits[3] & unchecked((int)0x80000000)) != 0;
        var magnitude = ((BigInteger)(uint)bits[2] << 64)
            | ((BigInteger)(uint)bits[1] << 32)
            | (uint)bits[0];
        var num = negative ? -magnitude : magnitude;
        var den = BigInteger.Pow(10, scale);
        if (!num.IsZero)
        {
            var gcd = BigInteger.GreatestCommonDivisor(BigInteger.Abs(num), den);
            num /= gcd;
            den /= gcd;
        }
        else
        {
            den = BigInteger.One;
        }
        if (num > long.MaxValue || num < long.MinValue || den > long.MaxValue)
        {
            return false;
        }
        numerator = (long)num;
        denominator = (long)den;
        return true;
    }

    private static void CountUnmapped(SortedDictionary<string, int> unmapped, string field) =>
        unmapped[field] = unmapped.TryGetValue(field, out var count) ? count + 1 : 1;
}

/// <summary>Templates plus the fail-closed accounting for one load pass.</summary>
public sealed class PackTemplateLoadResult
{
    public IReadOnlyList<ObjectTemplate> Templates { get; }
    public LoadReport Report { get; }

    public PackTemplateLoadResult(IReadOnlyList<ObjectTemplate> templates, LoadReport report)
    {
        Templates = templates;
        Report = report;
    }
}

/// <summary>
/// Fail-closed load accounting: every row that did not become a template is in
/// SkippedRows with a typed reason; every source field the loader saw but does
/// not map is enumerated (with occurrence counts) in UnmappedFields; defaults
/// applied in place of absent data are spelled out in Notes. Nothing is silent.
/// </summary>
public sealed class LoadReport
{
    public int LoadedCount { get; }
    public IReadOnlyList<SkippedRow> SkippedRows { get; }
    public IReadOnlyDictionary<string, int> UnmappedFields { get; }
    public IReadOnlyList<string> Notes { get; }

    public LoadReport(
        int loadedCount,
        IReadOnlyList<SkippedRow> skippedRows,
        IReadOnlyDictionary<string, int> unmappedFields,
        IReadOnlyList<string> notes)
    {
        LoadedCount = loadedCount;
        SkippedRows = skippedRows;
        UnmappedFields = unmappedFields;
        Notes = notes;
    }
}

public enum RowSkipReason
{
    NotAnObject,
    MissingId,
    DuplicateId,
    MissingKind,
    UnknownKind,
    InvalidNumericField,
}

public sealed record SkippedRow(int Index, string Id, RowSkipReason Reason, string Detail);

/// <summary>Typed error for a structurally unusable objects document.</summary>
public sealed class PackObjectsDocumentException : Exception
{
    public PackObjectsDocumentException(string message) : base(message)
    {
    }

    public PackObjectsDocumentException(string message, Exception inner) : base(message, inner)
    {
    }
}
