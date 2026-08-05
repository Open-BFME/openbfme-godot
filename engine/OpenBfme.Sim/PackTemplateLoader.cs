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
            var templateIds = IndexTemplateIds(rows);
            var bannerRespawnTicks = IndexBannerRespawnTicks(rows);

            var index = -1;
            foreach (var row in rows.EnumerateArray())
            {
                index++;
                LoadRow(row, index, templates, seenIds, skipped, unmapped, notes,
                    templateIds, bannerRespawnTicks);
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
        List<string> notes,
        IReadOnlyDictionary<string, string> templateIds,
        IReadOnlyDictionary<string, long?> bannerRespawnTicks)
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
        ModuleSpec? experienceSpec = null;
        ModuleSpec? bannerCarrierSpec = null;
        ModuleSpec? castleBehaviorSpec = null;
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
                case "sourceTypeName":
                    break; // consumed by the document-wide source-id index
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
                case "gameplay":
                    invalid |= !TryMapGameplay(property.Value, id, index, templateIds, bannerRespawnTicks,
                        out bannerCarrierSpec, out castleBehaviorSpec, skipped, unmapped);
                    break;
                case "experience":
                    invalid |= !TryMapExperience(property.Value, id, index,
                        out experienceSpec, skipped);
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
        if (experienceSpec is not null)
        {
            modules.Add(experienceSpec);
        }
        if (bannerCarrierSpec is not null)
        {
            modules.Add(bannerCarrierSpec);
        }
        if (castleBehaviorSpec is not null)
        {
            modules.Add(castleBehaviorSpec);
        }
        templates.Add(new ObjectTemplate(id, modules));
    }

    private static IReadOnlyDictionary<string, string> IndexTemplateIds(JsonElement rows)
    {
        var result = new SortedDictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var ambiguous = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var row in rows.EnumerateArray())
        {
            if (row.ValueKind != JsonValueKind.Object
                || !row.TryGetProperty("id", out var idValue)
                || idValue.ValueKind != JsonValueKind.String
                || string.IsNullOrEmpty(idValue.GetString()))
            {
                continue;
            }
            var id = idValue.GetString()!;
            Add(id, id);
            if (row.TryGetProperty("sourceTypeName", out var source)
                && source.ValueKind == JsonValueKind.String
                && !string.IsNullOrEmpty(source.GetString()))
            {
                Add(source.GetString()!, id);
            }
        }
        return result;

        void Add(string source, string id)
        {
            if (ambiguous.Contains(source)) return;
            if (result.TryGetValue(source, out var prior) && prior != id)
            {
                result.Remove(source);
                ambiguous.Add(source);
            }
            else
            {
                result[source] = id;
            }
        }
    }

    private static IReadOnlyDictionary<string, long?> IndexBannerRespawnTicks(JsonElement rows)
    {
        var result = new SortedDictionary<string, long?>(StringComparer.Ordinal);
        foreach (var row in rows.EnumerateArray())
        {
            if (row.ValueKind != JsonValueKind.Object
                || !row.TryGetProperty("id", out var idValue)
                || idValue.ValueKind != JsonValueKind.String
                || string.IsNullOrEmpty(idValue.GetString())
                || !row.TryGetProperty("gameplay", out var gameplay)
                || gameplay.ValueKind != JsonValueKind.Object
                || !gameplay.TryGetProperty("bannerCarrierUpdate", out var contract))
            {
                continue;
            }
            result[idValue.GetString()!] = TryReadBannerRespawnTicks(contract, out var ticks)
                ? ticks
                : null;
        }
        return result;
    }

    private static bool TryMapGameplay(
        JsonElement gameplay,
        string id,
        int index,
        IReadOnlyDictionary<string, string> templateIds,
        IReadOnlyDictionary<string, long?> bannerRespawnTicks,
        out ModuleSpec? bannerSpec,
        out ModuleSpec? castleSpec,
        List<SkippedRow> skipped,
        SortedDictionary<string, int> unmapped)
    {
        bannerSpec = null;
        castleSpec = null;
        if (gameplay.ValueKind != JsonValueKind.Object)
        {
            skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                "'gameplay' is not an object"));
            return false;
        }
        foreach (var property in gameplay.EnumerateObject())
        {
            if (property.Name == "bannerCarrierUpdate")
            {
                if (!TryReadBannerRespawnTicks(property.Value, out _))
                {
                    skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                        "'gameplay.bannerCarrierUpdate' has invalid authored respawn timers"));
                    return false;
                }
                continue;
            }
            if (property.Name == "castleBehavior")
            {
                if (!TryMapCastleBehavior(
                    property.Value, id, index, templateIds, out castleSpec, skipped))
                {
                    return false;
                }
                continue;
            }
            if (property.Name != "bannerCarrier")
            {
                CountUnmapped(unmapped, "gameplay." + property.Name);
                continue;
            }
            var contract = property.Value;
            if (contract.ValueKind != JsonValueKind.Object
                || !contract.TryGetProperty("allowedObjectIds", out var allowed)
                || allowed.ValueKind != JsonValueKind.Array)
            {
                skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                    "'gameplay.bannerCarrier.allowedObjectIds' is not an array"));
                return false;
            }
            string? bannerTemplate = null;
            foreach (var candidate in allowed.EnumerateArray())
            {
                if (candidate.ValueKind != JsonValueKind.String
                    || string.IsNullOrEmpty(candidate.GetString())
                    || !templateIds.TryGetValue(candidate.GetString()!, out var resolved))
                {
                    skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                        "banner carrier target is missing, ambiguous, or not a string"));
                    return false;
                }
                bannerTemplate ??= resolved;
            }
            if (bannerTemplate is null)
            {
                skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                    "banner carrier target list is empty"));
                return false;
            }
            if (!contract.TryGetProperty("minLevel", out var minLevel)
                || !TryReadInteger(minLevel, out var min) || min < 0 || min > int.MaxValue)
            {
                skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                    "'gameplay.bannerCarrier.minLevel' is not a non-negative integer"));
                return false;
            }
            var data = new SortedDictionary<string, long>(StringComparer.Ordinal)
            {
                ["MinLevel"] = min,
            };
            if (!contract.TryGetProperty("destroyHordeOnBannerDeath", out var destroy)
                || destroy.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            {
                skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                    "'gameplay.bannerCarrier.destroyHordeOnBannerDeath' is not a boolean"));
                return false;
            }
            data["DestroyHordeOnBannerDeath"] = destroy.GetBoolean() ? 1 : 0;
            if (bannerRespawnTicks.TryGetValue(bannerTemplate, out var respawnTicks))
            {
                if (!respawnTicks.HasValue)
                {
                    skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                        "banner carrier target has an invalid authored respawn contract"));
                    return false;
                }
                data["RespawnTicks"] = respawnTicks.Value;
            }
            if (contract.TryGetProperty("positions", out var positions))
            {
                if (positions.ValueKind != JsonValueKind.Array)
                {
                    skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                        "'gameplay.bannerCarrier.positions' is not an array"));
                    return false;
                }
                using var enumerator = positions.EnumerateArray();
                if (enumerator.MoveNext())
                {
                    var position = enumerator.Current;
                    if (position.ValueKind != JsonValueKind.Object
                        || !position.TryGetProperty("x", out var x)
                        || !position.TryGetProperty("y", out var y)
                        || !TryReadFraction(x, out var xn, out var xd)
                        || !TryReadFraction(y, out var yn, out var yd))
                    {
                        skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                            "first banner carrier position has invalid exact coordinates"));
                        return false;
                    }
                    data["OffsetXRaw"] = Fixed64.FromFraction(xn, xd).Raw;
                    data["OffsetYRaw"] = Fixed64.FromFraction(yn, yd).Raw;
                }
            }
            bannerSpec = new ModuleSpec(BannerCarrierModule.TypeName, data,
                new Dictionary<string, string> { ["BannerTemplate"] = bannerTemplate });
        }
        return true;
    }

    private static bool TryMapCastleBehavior(
        JsonElement contract,
        string id,
        int index,
        IReadOnlyDictionary<string, string> templateIds,
        out ModuleSpec? spec,
        List<SkippedRow> skipped)
    {
        spec = null;
        if (contract.ValueKind != JsonValueKind.Object
            || !contract.TryGetProperty("pieces", out var pieces)
            || pieces.ValueKind != JsonValueKind.Array)
        {
            skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                "'gameplay.castleBehavior.pieces' is not an array"));
            return false;
        }
        var count = pieces.GetArrayLength();
        if (count is < 1 or > 64)
        {
            skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                "'gameplay.castleBehavior.pieces' must contain 1..64 rows"));
            return false;
        }
        var data = new SortedDictionary<string, long>(StringComparer.Ordinal)
        {
            ["PieceCount"] = count,
        };
        var strings = new SortedDictionary<string, string>(StringComparer.Ordinal);
        var expectedIndex = 0;
        foreach (var piece in pieces.EnumerateArray())
        {
            if (piece.ValueKind != JsonValueKind.Object
                || !piece.TryGetProperty("index", out var pieceIndex)
                || !TryReadInteger(pieceIndex, out var authoredIndex)
                || authoredIndex != expectedIndex
                || !piece.TryGetProperty("objectId", out var objectId)
                || objectId.ValueKind != JsonValueKind.String
                || string.IsNullOrEmpty(objectId.GetString())
                || !templateIds.TryGetValue(objectId.GetString()!, out var resolved)
                || !piece.TryGetProperty("offset", out var offset)
                || offset.ValueKind != JsonValueKind.Array
                || offset.GetArrayLength() != 3
                || !piece.TryGetProperty("angleRadians", out var angle)
                || !piece.TryGetProperty("offsetRawQ32", out var offsetRaw)
                || offsetRaw.ValueKind != JsonValueKind.Array
                || offsetRaw.GetArrayLength() != 3
                || !piece.TryGetProperty("angleRadiansRawQ32", out var angleRaw))
            {
                skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                    $"castle piece {expectedIndex} identity or target is invalid"));
                return false;
            }
            var coordinates = offset.EnumerateArray().ToArray();
            var rawCoordinates = offsetRaw.EnumerateArray().ToArray();
            if (!TryReadFraction(coordinates[0], out var xn, out var xd)
                || !TryReadFraction(coordinates[1], out var yn, out var yd)
                || !TryReadFraction(coordinates[2], out var zn, out var zd)
                || !TryReadFraction(angle, out var an, out var ad)
                || !TryReadInteger(rawCoordinates[0], out var xRaw)
                || !TryReadInteger(rawCoordinates[1], out var yRaw)
                || !TryReadInteger(rawCoordinates[2], out var zRaw)
                || !TryReadInteger(angleRaw, out var authoredAngleRaw))
            {
                skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                    $"castle piece {expectedIndex} has invalid exact transform values"));
                return false;
            }
            strings[$"PieceTemplate:{expectedIndex}"] = resolved;
            data[$"OffsetXRaw:{expectedIndex}"] = xRaw;
            data[$"OffsetYRaw:{expectedIndex}"] = yRaw;
            data[$"OffsetZRaw:{expectedIndex}"] = zRaw;
            data[$"AngleRadiansRaw:{expectedIndex}"] = authoredAngleRaw;
            expectedIndex++;
        }
        spec = new ModuleSpec(CastleBehaviorModule.TypeName, data, strings);
        return true;
    }

    private static bool TryReadBannerRespawnTicks(JsonElement contract, out long ticks)
    {
        ticks = 0;
        if (contract.ValueKind != JsonValueKind.Object
            || !TryReadTimer(contract, "diedRespawnTime", required: true, out var diedMs)
            || !TryReadTimer(contract, "meleeFreeBannerRespawnTime", required: false, out var meleeMs))
        {
            return false;
        }
        var delayMs = Math.Max(diedMs, meleeMs);
        if (delayMs > (long)int.MaxValue * 1000 / TicksPerSecond)
        {
            return false;
        }
        ticks = checked((delayMs * TicksPerSecond + 999) / 1000);
        return true;

        static bool TryReadTimer(JsonElement owner, string name, bool required, out long milliseconds)
        {
            milliseconds = 0;
            if (!owner.TryGetProperty(name, out var timer))
            {
                return !required;
            }
            return timer.ValueKind == JsonValueKind.Object
                && timer.TryGetProperty("milliseconds", out var value)
                && TryReadInteger(value, out milliseconds)
                && milliseconds >= 0;
        }
    }

    private static bool TryMapExperience(
        JsonElement experience,
        string id,
        int index,
        out ModuleSpec? spec,
        List<SkippedRow> skipped)
    {
        spec = null;
        if (experience.ValueKind != JsonValueKind.Object
            || !experience.TryGetProperty("status", out var status)
            || status.ValueKind != JsonValueKind.String)
        {
            skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                "'experience' has no string status"));
            return false;
        }
        if (status.GetString() is "unauthored" or "unavailable") return true;
        if (status.GetString() != "compiled"
            || !experience.TryGetProperty("maxLevel", out var capValue)
            || !TryReadInteger(capValue, out var cap) || cap < 1 || cap > int.MaxValue
            || !experience.TryGetProperty("initialRank", out var initialValue)
            || !TryReadInteger(initialValue, out var initial) || initial < 1 || initial > cap
            || !experience.TryGetProperty("levels", out var levels)
            || levels.ValueKind != JsonValueKind.Array)
        {
            skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                "compiled experience header is invalid"));
            return false;
        }
        var data = new SortedDictionary<string, long>(StringComparer.Ordinal)
        {
            ["LevelCap"] = cap,
            ["InitialLevel"] = initial,
        };
        var priorRank = 0L;
        foreach (var level in levels.EnumerateArray())
        {
            if (level.ValueKind != JsonValueKind.Object
                || !level.TryGetProperty("rank", out var rankValue)
                || !TryReadInteger(rankValue, out var rank) || rank <= priorRank || rank > cap
                || !level.TryGetProperty("requiredExperience", out var requiredValue)
                || !TryReadInteger(requiredValue, out var required) || required < 0)
            {
                skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                    "compiled experience level row is invalid"));
                return false;
            }
            data[$"RequiredExperience:{rank}"] = required;
            priorRank = rank;
        }
        if (priorRank != cap)
        {
            skipped.Add(new SkippedRow(index, id, RowSkipReason.InvalidGameplayField,
                "compiled experience levels do not reach maxLevel"));
            return false;
        }
        spec = new ModuleSpec(ExperienceLevelModule.TypeName, data);
        return true;
    }

    private static bool TryReadInteger(JsonElement element, out long value)
    {
        if (TryReadFraction(element, out var numerator, out var denominator)
            && denominator == 1)
        {
            value = numerator;
            return true;
        }
        value = 0;
        return false;
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
    InvalidGameplayField,
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
