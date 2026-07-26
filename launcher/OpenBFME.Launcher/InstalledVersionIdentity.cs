using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

internal sealed record InstalledFileIdentity(
    [property: JsonPropertyName("path")] string Path,
    [property: JsonPropertyName("size")] long Size,
    [property: JsonPropertyName("sha256")] string Sha256);

internal sealed record InstalledVersionIdentity(
    [property: JsonPropertyName("schema")] string Schema,
    [property: JsonPropertyName("schemaVersion")] int SchemaVersion,
    [property: JsonPropertyName("version")] string Version,
    [property: JsonPropertyName("commit")] string Commit,
    [property: JsonPropertyName("packageSha256")] string PackageSha256,
    [property: JsonPropertyName("files")] IReadOnlyList<InstalledFileIdentity> Files)
{
    internal const string FileName = ".openbfme-version.json";
    private const string ExpectedSchema = "openbfme.installed-version";

    internal static void Write(string root, ReleaseManifest manifest, ReleasePackage package)
    {
        var rows = Inventory(root).Select(pair =>
        {
            var info = new FileInfo(pair.Value);
            return new InstalledFileIdentity(
                pair.Key,
                info.Length,
                Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(pair.Value))).ToLowerInvariant());
        }).ToArray();
        var identity = new InstalledVersionIdentity(
            ExpectedSchema, 1, manifest.Version, manifest.Commit, package.Sha256, rows);
        var path = Path.Combine(root, FileName);
        File.WriteAllBytes(path, JsonSerializer.SerializeToUtf8Bytes(identity,
            new JsonSerializerOptions { WriteIndented = true }));
    }

    internal static void Verify(
        string root,
        string? expectedVersion = null,
        string? expectedCommit = null,
        string? expectedPackageSha256 = null)
    {
        var path = Path.Combine(root, FileName);
        if (!File.Exists(path)) throw new InvalidDataException("Installed version identity is missing.");
        var identity = JsonSerializer.Deserialize<InstalledVersionIdentity>(File.ReadAllBytes(path),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = false })
            ?? throw new InvalidDataException("Installed version identity is empty.");
        if (identity.Schema != ExpectedSchema || identity.SchemaVersion != 1 ||
            !SafeVersion(identity.Version) || !FullSha1(identity.Commit) || !Sha256(identity.PackageSha256))
            throw new InvalidDataException("Installed version identity is invalid.");
        if (expectedVersion is not null && identity.Version != expectedVersion ||
            expectedCommit is not null && identity.Commit != expectedCommit ||
            expectedPackageSha256 is not null && identity.PackageSha256 != expectedPackageSha256)
            throw new InvalidDataException("Installed version identity does not match the selected release.");

        var expected = new Dictionary<string, InstalledFileIdentity>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in identity.Files)
            if (!SafePath(entry.Path) || entry.Size < 0 || !Sha256(entry.Sha256) ||
                !expected.TryAdd(entry.Path, entry))
                throw new InvalidDataException("Installed file identity is invalid.");
        var observed = Inventory(root);
        if (observed.Count != expected.Count)
            throw new InvalidDataException("Installed version file set changed.");
        foreach (var pair in observed)
        {
            if (!expected.TryGetValue(pair.Key, out var entry))
                throw new InvalidDataException("Installed version contains an unlisted file.");
            var info = new FileInfo(pair.Value);
            if (info.Length != entry.Size)
                throw new InvalidDataException("Installed version file size changed.");
            var hash = SHA256.HashData(File.ReadAllBytes(pair.Value));
            if (!CryptographicOperations.FixedTimeEquals(hash, Convert.FromHexString(entry.Sha256)))
                throw new InvalidDataException("Installed version file hash changed.");
        }
    }

    private static Dictionary<string, string> Inventory(string root)
    {
        var fullRoot = Path.GetFullPath(root);
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var pending = new Stack<string>();
        pending.Push(fullRoot);
        while (pending.Count > 0)
        {
            foreach (var item in Directory.EnumerateFileSystemEntries(pending.Pop()))
            {
                var attributes = File.GetAttributes(item);
                if ((attributes & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Installed version contains a reparse point.");
                if ((attributes & FileAttributes.Directory) != 0)
                {
                    pending.Push(item);
                    continue;
                }
                var relative = Path.GetRelativePath(fullRoot, item).Replace('\\', '/');
                if (relative.Equals(FileName, StringComparison.OrdinalIgnoreCase)) continue;
                if (!result.TryAdd(relative, item))
                    throw new InvalidDataException("Installed version contains duplicate Windows paths.");
            }
        }
        return result;
    }

    private static bool SafePath(string path) =>
        !string.IsNullOrWhiteSpace(path) && !path.Contains('\\') && !Path.IsPathRooted(path) &&
        path.Split('/').All(segment => segment.Length > 0 && segment is not "." and not "..");
    private static bool SafeVersion(string value) =>
        System.Text.RegularExpressions.Regex.IsMatch(value, "^[0-9A-Za-z][0-9A-Za-z._-]{0,63}$");
    private static bool FullSha1(string value) =>
        System.Text.RegularExpressions.Regex.IsMatch(value, "^[0-9a-f]{40}$");
    private static bool Sha256(string value) =>
        System.Text.RegularExpressions.Regex.IsMatch(value, "^[0-9a-f]{64}$");
}
