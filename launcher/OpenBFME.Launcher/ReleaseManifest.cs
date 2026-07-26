using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

public sealed record ReleasePackage(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("url")] string Url,
    [property: JsonPropertyName("sha256")] string Sha256,
    [property: JsonPropertyName("size")] long Size,
    [property: JsonPropertyName("expandedSize")] long ExpandedSize,
    [property: JsonPropertyName("kind")] string Kind);

public sealed record ReleaseManifest(
    [property: JsonPropertyName("schema")] string Schema,
    [property: JsonPropertyName("schemaVersion")] int SchemaVersion,
    [property: JsonPropertyName("repository")] string Repository,
    [property: JsonPropertyName("version")] string Version,
    [property: JsonPropertyName("channel")] string Channel,
    [property: JsonPropertyName("commit")] string Commit,
    [property: JsonPropertyName("packages")] IReadOnlyList<ReleasePackage> Packages)
{
    public const string ExpectedSchema = "openbfme.release-manifest";

    /// <summary>
    /// The version shape the launcher can actually order.
    ///
    /// This is enforced at parse time because the installer compares versions to decide
    /// whether an update is a downgrade, and that comparison requires SemVer. A manifest
    /// carrying, say, "beta1" used to validate here and install fine on a clean machine,
    /// and then every later update threw "Installed release version is not SemVer" —
    /// permanently, because the unusable version was by then written into the install
    /// state. Rejecting it while nothing has been installed keeps the failure in the one
    /// place it is recoverable. Components are digit-bounded so the parse below cannot
    /// overflow on a manifest that declares a 40-digit major.
    /// </summary>
    private static readonly System.Text.RegularExpressions.Regex OrderableVersion =
        new(@"^[0-9]{1,9}\.[0-9]{1,9}\.[0-9]{1,9}(?:-[0-9A-Za-z.-]+)?$",
            System.Text.RegularExpressions.RegexOptions.CultureInvariant);

    public static bool IsOrderableVersion(string value) => OrderableVersion.IsMatch(value);

    public static ReleaseManifest Parse(ReadOnlySpan<byte> json)
    {
        var manifest = JsonSerializer.Deserialize<ReleaseManifest>(json,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = false })
            ?? throw new InvalidDataException("Release manifest is empty.");
        manifest.Validate();
        return manifest;
    }

    public void Validate()
    {
        if (Schema != ExpectedSchema || SchemaVersion != 1)
            throw new InvalidDataException("Unsupported release manifest schema.");
        // The target is baked in from config/release-source.json, never written as a
        // literal here; retired targets are listed and refused there too.
        if (!string.Equals(Repository, ReleaseSource.Repository, StringComparison.Ordinal))
            throw new InvalidDataException(
                $"Release manifest is for repository '{Repository}', but this launcher " +
                $"updates from '{ReleaseSource.Repository}'.");
        if (!System.Text.RegularExpressions.Regex.IsMatch(Version, @"^[0-9A-Za-z][0-9A-Za-z._-]{0,63}$"))
            throw new InvalidDataException("Unsafe release version.");
        if (!IsOrderableVersion(Version))
            throw new InvalidDataException(
                $"Release version '{Version}' is not SemVer (major.minor.patch[-prerelease]). " +
                "The launcher orders releases by version to refuse downgrades, so a release " +
                "it cannot order is refused before anything is installed.");
        if (!System.Text.RegularExpressions.Regex.IsMatch(Commit, @"^[0-9a-f]{40}$"))
            throw new InvalidDataException("Release commit must be a full lowercase SHA-1.");
        if (Channel is not ("stable" or "playtest" or "nightly"))
            throw new InvalidDataException("Release channel is invalid.");
        if (Packages.Count != 2)
            throw new InvalidDataException("Release manifest must contain one game and one launcher package.");
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var kinds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var package in Packages)
        {
            if (!System.Text.RegularExpressions.Regex.IsMatch(package.Name, @"^[0-9A-Za-z][0-9A-Za-z._-]{0,127}$") ||
                package.Kind is not ("game-windows-x64" or "launcher-windows-x64"))
                throw new InvalidDataException("Release package identity is invalid.");
            if (!names.Add(package.Name) || !kinds.Add(package.Kind))
                throw new InvalidDataException("Release package names and kinds must be unique.");
            if (package.Size is <= 0 or > 8L * 1024 * 1024 * 1024)
                throw new InvalidDataException("Release package size is outside the supported bound.");
            if (package.ExpandedSize is <= 0 or > 16L * 1024 * 1024 * 1024)
                throw new InvalidDataException("Release package expanded size is outside the supported bound.");
            if (!System.Text.RegularExpressions.Regex.IsMatch(package.Sha256, @"^[0-9a-f]{64}$"))
                throw new InvalidDataException("Release package SHA-256 is invalid.");
            if (!Uri.TryCreate(package.Url, UriKind.Absolute, out var uri))
                throw new InvalidDataException("Release package URL is invalid.");
            ReleaseUriPolicy.Validate(uri);
        }
        if (!kinds.SetEquals(new[] { "game-windows-x64", "launcher-windows-x64" }))
            throw new InvalidDataException("Release manifest package kinds are incomplete.");
    }
}
