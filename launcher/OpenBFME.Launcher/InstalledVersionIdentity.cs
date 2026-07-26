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
                pair.Key, info.Length, Convert.ToHexString(HashFile(pair.Value)).ToLowerInvariant());
        }).ToArray();
        var identity = new InstalledVersionIdentity(
            ExpectedSchema, 1, manifest.Version, manifest.Commit, package.Sha256, rows);
        var path = Path.Combine(root, FileName);
        File.WriteAllBytes(path, JsonSerializer.SerializeToUtf8Bytes(identity,
            new JsonSerializerOptions { WriteIndented = true }));
    }

    /// <summary>
    /// Read and shape-check the identity record a version directory keeps about itself.
    /// </summary>
    private static InstalledVersionIdentity Read(string root)
    {
        var path = Path.Combine(root, FileName);
        if (!File.Exists(path)) throw new InvalidDataException("Installed version identity is missing.");
        if (new FileInfo(path).Length > 64 * 1024 * 1024)
            throw new InvalidDataException("Installed version identity is too large.");
        InstalledVersionIdentity identity;
        try
        {
            identity = JsonSerializer.Deserialize<InstalledVersionIdentity>(File.ReadAllBytes(path),
                new JsonSerializerOptions { PropertyNameCaseInsensitive = false })
                ?? throw new InvalidDataException("Installed version identity is empty.");
        }
        catch (JsonException error)
        {
            // A truncated or scrambled identity file is damage like any other, and must
            // read as damage rather than escaping as a JsonException that callers
            // deciding whether a directory is repairable would not recognise.
            throw new InvalidDataException(
                $"Installed version identity is not valid JSON: {error.Message}", error);
        }
        if (identity.Schema != ExpectedSchema || identity.SchemaVersion != 1 ||
            !SafeVersion(identity.Version) || !FullSha1(identity.Commit) || !Sha256(identity.PackageSha256))
            throw new InvalidDataException("Installed version identity is invalid.");
        return identity;
    }

    /// <summary>
    /// Establish that a version directory is one the launcher installed and that it
    /// names itself as <paramref name="expectedVersion"/> — without hashing its contents.
    ///
    /// This is the gate for deleting an obsolete version. Ownership is the question there;
    /// intactness is not, because the answer is used to delete the directory either way.
    /// The reparse-point and duplicate-path scan is kept, since those do affect what a
    /// recursive delete would touch.
    /// </summary>
    internal static void VerifyOwnership(string root, string expectedVersion)
    {
        var identity = Read(root);
        if (identity.Version != expectedVersion)
            throw new InvalidDataException(
                "Installed version identity does not match the directory it is stored in.");
        Inventory(root);
    }

    internal static void Verify(
        string root,
        string? expectedVersion = null,
        string? expectedCommit = null,
        string? expectedPackageSha256 = null)
    {
        var identity = Read(root);
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
            if (!CryptographicOperations.FixedTimeEquals(
                    HashFile(pair.Value), Convert.FromHexString(entry.Sha256)))
                throw new InvalidDataException("Installed version file hash changed.");
        }
    }

    /// <summary>
    /// Hash a file without materialising it in memory.
    ///
    /// <c>File.ReadAllBytes</c> refuses outright at 2 GiB — the shipped game data file is
    /// a single Godot .pck expected to pass that — so reading whole files to hash them
    /// meant verification would start failing on exactly the installs it most needs to
    /// check, with an I/O error rather than anything a player could act on. It also made
    /// every verification allocate the largest file in the install.
    /// </summary>
    private static byte[] HashFile(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read,
            1024 * 1024, FileOptions.SequentialScan);
        return SHA256.HashData(stream);
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
