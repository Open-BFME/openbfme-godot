using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

internal sealed record BundleInventoryEntry(
    [property: JsonPropertyName("path")] string Path,
    [property: JsonPropertyName("size")] long Size,
    [property: JsonPropertyName("sha256")] string Sha256);

internal sealed record BundleInventoryDocument(
    [property: JsonPropertyName("schema")] string Schema,
    [property: JsonPropertyName("schemaVersion")] int SchemaVersion,
    [property: JsonPropertyName("files")] IReadOnlyList<BundleInventoryEntry> Files);

internal static class BundleInventory
{
    internal const string FileName = "openbfme-bundle-inventory.json";

    internal static void Verify(string bundleRoot)
    {
        var root = Path.GetFullPath(bundleRoot);
        var inventoryPath = Path.Combine(root, FileName);
        if (!File.Exists(inventoryPath))
            throw new InvalidDataException("Bundled runtime inventory is missing.");
        if (new FileInfo(inventoryPath).Length > 4 * 1024 * 1024)
            throw new InvalidDataException("Bundled runtime inventory is too large.");
        var document = JsonSerializer.Deserialize<BundleInventoryDocument>(
            File.ReadAllBytes(inventoryPath),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = false })
            ?? throw new InvalidDataException("Bundled runtime inventory is empty.");
        if (document.Schema != "openbfme.bundle-inventory" || document.SchemaVersion != 1)
            throw new InvalidDataException("Bundled runtime inventory schema is invalid.");

        var expected = new Dictionary<string, BundleInventoryEntry>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in document.Files)
        {
            if (!IsSafeRelativePath(entry.Path) || entry.Path.Equals(FileName, StringComparison.OrdinalIgnoreCase) ||
                entry.Path.Equals(InstalledVersionIdentity.FileName, StringComparison.OrdinalIgnoreCase) ||
                entry.Size < 0 || !System.Text.RegularExpressions.Regex.IsMatch(entry.Sha256, "^[0-9a-f]{64}$") ||
                !expected.TryAdd(entry.Path, entry))
                throw new InvalidDataException("Bundled runtime inventory contains an invalid entry.");
        }

        var observed = EnumerateContainedFiles(root);
        if (observed.Count != expected.Count)
            throw new InvalidDataException("Bundled runtime file set does not match its inventory.");
        foreach (var pair in observed)
        {
            if (!expected.TryGetValue(pair.Key, out var entry))
                throw new InvalidDataException("Bundled runtime contains an unlisted file.");
            var info = new FileInfo(pair.Value);
            if (info.Length != entry.Size)
                throw new InvalidDataException("Bundled runtime file size mismatch.");
            var actual = SHA256.HashData(File.ReadAllBytes(pair.Value));
            if (!CryptographicOperations.FixedTimeEquals(actual, Convert.FromHexString(entry.Sha256)))
                throw new InvalidDataException("Bundled runtime file hash mismatch.");
        }
    }

    private static Dictionary<string, string> EnumerateContainedFiles(string root)
    {
        var files = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var pending = new Stack<string>();
        pending.Push(root);
        while (pending.Count > 0)
        {
            var directory = pending.Pop();
            foreach (var path in Directory.EnumerateFileSystemEntries(directory))
            {
                var attributes = File.GetAttributes(path);
                if ((attributes & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Bundled runtime contains a reparse point.");
                if ((attributes & FileAttributes.Directory) != 0)
                {
                    pending.Push(path);
                    continue;
                }
                var relative = Path.GetRelativePath(root, path).Replace('\\', '/');
                if (relative.Equals(FileName, StringComparison.OrdinalIgnoreCase) ||
                    relative.Equals(InstalledVersionIdentity.FileName, StringComparison.OrdinalIgnoreCase)) continue;
                if (!files.TryAdd(relative, path))
                    throw new InvalidDataException("Bundled runtime contains duplicate Windows paths.");
            }
        }
        return files;
    }

    private static bool IsSafeRelativePath(string path) =>
        !string.IsNullOrWhiteSpace(path) &&
        !path.Contains('\\') &&
        !Path.IsPathRooted(path) &&
        path.Split('/').All(segment => segment.Length > 0 && segment is not "." and not "..");
}
