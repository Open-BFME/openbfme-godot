using System.Globalization;
using System.Numerics;
using System.Text.Json;

namespace OpenBfme.Sim;

/// <summary>Exact, culture-independent reads at the compiled-INI data boundary.</summary>
internal static class IniValueReader
{
    public static IReadOnlyDictionary<string, object?> Fields(IReadOnlyDictionary<string, object?> row)
    {
        ArgumentNullException.ThrowIfNull(row);
        if (row.TryGetValue("fields", out var fields)
            && TryDictionary(fields, out var nested))
        {
            return nested;
        }
        return row;
    }

    public static object? Value(IReadOnlyDictionary<string, object?> fields, string name) =>
        fields.TryGetValue(name, out var value) ? value : null;

    public static string String(IReadOnlyDictionary<string, object?> fields, string name, string fallback = "")
    {
        var value = Value(fields, name);
        return value switch
        {
            null => fallback,
            string text => text,
            JsonElement { ValueKind: JsonValueKind.String } json => json.GetString() ?? fallback,
            _ => throw new FormatException($"Field '{name}' is not a string"),
        };
    }

    public static int Integer(IReadOnlyDictionary<string, object?> fields, string name, int fallback = 0)
    {
        var value = Value(fields, name);
        if (value == null) return fallback;
        var number = Long(value, name);
        return checked((int)number);
    }

    public static long Integer64(IReadOnlyDictionary<string, object?> fields, string name, long fallback = 0)
    {
        var value = Value(fields, name);
        return value == null ? fallback : Long(value, name);
    }

    public static bool Boolean(IReadOnlyDictionary<string, object?> fields, string name, bool fallback = false)
    {
        var value = Value(fields, name);
        return value switch
        {
            null => fallback,
            bool flag => flag,
            string text when bool.TryParse(text, out var parsed) => parsed,
            JsonElement { ValueKind: JsonValueKind.True } => true,
            JsonElement { ValueKind: JsonValueKind.False } => false,
            _ => throw new FormatException($"Field '{name}' is not a boolean"),
        };
    }

    public static Fixed64 Fixed(IReadOnlyDictionary<string, object?> fields, string name, Fixed64 fallback = default)
    {
        var value = Value(fields, name);
        return value == null ? fallback : Fixed(value, name);
    }

    public static Fixed64 PercentMultiplier(object value, string name)
    {
        if (value is string text)
        {
            text = text.Trim();
            if (text.EndsWith('%')) text = text[..^1].Trim();
            return ParseDecimal(text, name) / Fixed64.FromInt(100);
        }
        if (value is JsonElement { ValueKind: JsonValueKind.String } json)
        {
            return PercentMultiplier(json.GetString()!, name);
        }
        return Fixed(value, name) / Fixed64.FromInt(100);
    }

    public static int MillisecondsToTicks(long milliseconds, int tickMilliseconds)
    {
        if (milliseconds < 0) throw new ArgumentOutOfRangeException(nameof(milliseconds));
        if (tickMilliseconds < 1) throw new ArgumentOutOfRangeException(nameof(tickMilliseconds));
        if (milliseconds == 0) return 0;
        var ticks = ((BigInteger)milliseconds * 2 + tickMilliseconds) / (2 * tickMilliseconds);
        if (ticks < 1) ticks = 1;
        if (ticks > int.MaxValue) throw new OverflowException("Authored duration exceeds the tick range");
        return (int)ticks;
    }

    public static long Milliseconds(IReadOnlyDictionary<string, object?> fields, string name, long fallback = 0)
    {
        var value = Value(fields, name);
        return value == null ? fallback : Long(value, name);
    }

    public static IReadOnlyList<object?> List(object? value)
    {
        if (value == null) return Array.Empty<object?>();
        if (value is string) return new[] { value };
        if (TryDictionary(value, out _)) return new[] { value };
        if (value is JsonElement json)
        {
            if (json.ValueKind != JsonValueKind.Array) return new object?[] { json };
            return json.EnumerateArray().Select(element => (object?)element.Clone()).ToArray();
        }
        if (value is System.Collections.IEnumerable enumerable)
        {
            var result = new List<object?>();
            foreach (var item in enumerable) result.Add(item);
            return result;
        }
        return new[] { value };
    }

    public static IReadOnlyList<string> Tokens(object? value)
    {
        var result = new List<string>();
        foreach (var item in List(value))
        {
            var text = item switch
            {
                string stringValue => stringValue,
                JsonElement { ValueKind: JsonValueKind.String } json => json.GetString()!,
                _ => throw new FormatException("Expected a token string"),
            };
            result.AddRange(text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
        }
        return result;
    }

    public static bool TryDictionary(object? value, out IReadOnlyDictionary<string, object?> dictionary)
    {
        if (value is IReadOnlyDictionary<string, object?> readOnly)
        {
            dictionary = readOnly;
            return true;
        }
        if (value is IDictionary<string, object?> mutable)
        {
            dictionary = new SortedDictionary<string, object?>(mutable, StringComparer.Ordinal);
            return true;
        }
        if (value is JsonElement { ValueKind: JsonValueKind.Object } json)
        {
            var result = new SortedDictionary<string, object?>(StringComparer.Ordinal);
            foreach (var property in json.EnumerateObject()) result.Add(property.Name, property.Value.Clone());
            dictionary = result;
            return true;
        }
        dictionary = null!;
        return false;
    }

    private static long Long(object value, string name) => value switch
    {
        byte number => number,
        short number => number,
        int number => number,
        long number => number,
        Fixed64 number when number.Raw % Fixed64.OneRaw == 0 => number.Raw / Fixed64.OneRaw,
        JsonElement json when json.ValueKind == JsonValueKind.Number && json.TryGetInt64(out var number) => number,
        _ => throw new FormatException($"Field '{name}' is not an integer"),
    };

    private static Fixed64 Fixed(object value, string name) => value switch
    {
        Fixed64 fixedValue => fixedValue,
        byte number => Fixed64.FromInt(number),
        short number => Fixed64.FromInt(number),
        int number => Fixed64.FromInt(number),
        long number => Fixed64.FromInt64(number),
        decimal number => FromDecimal(number),
        string text => ParseDecimal(text, name),
        JsonElement json when json.ValueKind == JsonValueKind.Number => ParseDecimal(json.GetRawText(), name),
        _ => throw new FormatException($"Field '{name}' is not an exact number"),
    };

    private static Fixed64 ParseDecimal(string text, string name)
    {
        if (!decimal.TryParse(text, NumberStyles.Number, CultureInfo.InvariantCulture, out var value))
        {
            throw new FormatException($"Field '{name}' is not an exact decimal");
        }
        return FromDecimal(value);
    }

    private static Fixed64 FromDecimal(decimal value)
    {
        var bits = decimal.GetBits(value);
        var scale = (bits[3] >> 16) & 0xff;
        var negative = (bits[3] & unchecked((int)0x80000000)) != 0;
        var magnitude = ((BigInteger)(uint)bits[2] << 64)
            | ((BigInteger)(uint)bits[1] << 32)
            | (uint)bits[0];
        if (negative) magnitude = -magnitude;
        var denominator = BigInteger.Pow(10, scale);
        var raw = (magnitude << Fixed64.FractionBits) / denominator;
        if (raw > long.MaxValue || raw < long.MinValue) throw new OverflowException("Fixed64 decimal conversion overflow");
        return Fixed64.FromRaw((long)raw);
    }
}
