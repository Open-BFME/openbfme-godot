using System.Text.Json;

namespace OpenBfme.Host;

internal sealed record CanonicalStateDifference(string Path, string LocalJson, string OtherJson)
{
    public static CanonicalStateDifference? First(JsonElement local, JsonElement other) =>
        Compare(local, other, "$");

    private static CanonicalStateDifference? Compare(
        JsonElement local, JsonElement other, string path)
    {
        if (local.ValueKind != other.ValueKind) return New(path, local, other);
        if (local.ValueKind == JsonValueKind.Object)
        {
            var remaining = other.EnumerateObject()
                .ToDictionary(row => row.Name, row => row.Value);
            foreach (var property in local.EnumerateObject())
            {
                if (property.NameEquals("hash"))
                {
                    remaining.Remove(property.Name);
                    continue;
                }
                if (!remaining.TryGetValue(property.Name, out var otherValue))
                {
                    return New(path + "." + property.Name, property.Value, null);
                }
                var difference = Compare(
                    property.Value, otherValue, path + "." + property.Name);
                if (difference != null) return difference;
                remaining.Remove(property.Name);
            }
            if (remaining.Count != 0)
            {
                var first = other.EnumerateObject().First(row => remaining.ContainsKey(row.Name));
                return New(path + "." + first.Name, null, first.Value);
            }
            return null;
        }
        if (local.ValueKind == JsonValueKind.Array)
        {
            var left = local.EnumerateArray().ToArray();
            var right = other.EnumerateArray().ToArray();
            var shared = Math.Min(left.Length, right.Length);
            for (var index = 0; index < shared; index++)
            {
                var difference = Compare(left[index], right[index], $"{path}[{index}]");
                if (difference != null) return difference;
            }
            if (left.Length != right.Length)
            {
                return New(
                    $"{path}[{shared}]",
                    shared < left.Length ? left[shared] : null,
                    shared < right.Length ? right[shared] : null);
            }
            return null;
        }
        return local.GetRawText() == other.GetRawText() ? null : New(path, local, other);
    }

    private static CanonicalStateDifference New(
        string path, JsonElement? local, JsonElement? other) =>
        new(path, local?.GetRawText() ?? "null", other?.GetRawText() ?? "null");

    public static CanonicalStateDifference? FirstBytes(byte[] local, byte[] other)
    {
        var shared = Math.Min(local.Length, other.Length);
        for (var index = 0; index < shared; index++)
        {
            if (local[index] != other[index])
            {
                return new CanonicalStateDifference(
                    $"$.canonical_bytes[{index}]", local[index].ToString(), other[index].ToString());
            }
        }
        return local.Length == other.Length
            ? null
            : new CanonicalStateDifference(
                $"$.canonical_bytes[{shared}]",
                shared < local.Length ? local[shared].ToString() : "null",
                shared < other.Length ? other[shared].ToString() : "null");
    }
}
