using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

/// <summary>
/// The publish/update target, baked into the executable at build time from
/// <c>config/release-source.json</c> — the single place the target is written down.
///
/// No owner/name literal may appear in launcher code. The repository has already
/// moved twice, and both retired targets are listed — and actively refused — in that
/// config, so there is deliberately no built-in default and no fallback. An
/// unconfigured build fails loudly at the first update check instead of quietly
/// pointing at a dead or attacker-chosen host.
///
/// This is embedded rather than read from disk on purpose: the launcher ships as a
/// signed single-file executable, and a runtime-editable target would let anyone who
/// can write next to the exe redirect the update feed. Retargeting requires a rebuild.
/// (Redirection alone still could not install foreign code — every manifest is RSA
/// signature-verified against the pinned key in <see cref="ManifestSignature"/> before
/// it is even parsed — but the update feed should not be swappable regardless.)
/// </summary>
public static class ReleaseSource
{
    private const string ResourceName = "OpenBFME.Launcher.release-source.json";

    /// <summary>Hosts this launcher will talk to. Extend deliberately, never from config alone.</summary>
    private static readonly HashSet<string> AllowedHosts =
        new(StringComparer.OrdinalIgnoreCase) { "github.com" };

    private static readonly Lazy<Resolved> Value = new(Load, isThreadSafe: true);

    /// <summary>owner/name, as GitHub accepts them.</summary>
    public static string Repository => Value.Value.Repository;

    public static string Host => Value.Value.Host;

    public static string DefaultChannel => Value.Value.Channel;

    /// <summary>
    /// The URL path prefix every release URL must sit under, e.g. <c>/owner/name/releases/</c>.
    /// </summary>
    public static string ReleasePathPrefix => $"/{Repository}/releases/";

    /// <summary>The channel-independent "newest release" manifest URL.</summary>
    public static Uri LatestManifestUri =>
        new($"https://{Host}{ReleasePathPrefix}latest/download/release-manifest.json");

    /// <summary>
    /// Targets that must never be used again, read from the config rather than repeated
    /// here. A retired GitHub name can be claimed by someone else, so silently updating
    /// from one would be a supply-chain problem, not merely a broken link.
    /// </summary>
    public static IReadOnlyList<string> RetiredRepositories => Value.Value.Retired;

    private sealed record Document(
        [property: JsonPropertyName("host")] string? Host,
        [property: JsonPropertyName("repository")] string? Repository,
        [property: JsonPropertyName("channel")] string? Channel,
        [property: JsonPropertyName("retiredRepositories")] IReadOnlyList<string>? RetiredRepositories);

    private sealed record Resolved(
        string Host, string Repository, string Channel, IReadOnlyList<string> Retired);

    private static Resolved Load()
    {
        using var stream = typeof(ReleaseSource).GetTypeInfo().Assembly
            .GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException(
                "This launcher was built without an embedded release target. " +
                "config/release-source.json must be embedded at build time; " +
                "rebuild the launcher from a complete checkout.");

        using var buffer = new MemoryStream();
        stream.CopyTo(buffer);

        Document document;
        try
        {
            document = JsonSerializer.Deserialize<Document>(buffer.ToArray())
                ?? throw new InvalidDataException("The embedded release target is empty.");
        }
        catch (JsonException error)
        {
            throw new InvalidDataException(
                $"The embedded release target is not valid JSON: {error.Message}", error);
        }

        var host = (document.Host ?? "github.com").Trim();
        var channel = (document.Channel ?? "stable").Trim();
        var repository = (document.Repository ?? string.Empty).Trim();

        if (repository.Length == 0)
            throw new InvalidDataException(
                "The publish/update target is not configured. Fill in the \"repository\" " +
                "field of config/release-source.json and rebuild the launcher. There is no " +
                "default on purpose: updating from the wrong repository is not recoverable.");

        // Anchored: owner/name only — no scheme, host, or extra path segments.
        if (!System.Text.RegularExpressions.Regex.IsMatch(
                repository, @"^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$"))
            throw new InvalidDataException(
                $"Invalid release repository '{repository}'; expected '<owner>/<name>'.");

        if (!AllowedHosts.Contains(host))
            throw new InvalidDataException(
                $"Release host '{host}' is not in the launcher's allow-list. " +
                "Add it deliberately in ReleaseSource.cs if that is intended.");

        if (channel is not ("stable" or "playtest" or "nightly"))
            throw new InvalidDataException(
                $"Release channel '{channel}' is not one of stable, playtest, nightly.");

        var retired = document.RetiredRepositories ?? Array.Empty<string>();
        foreach (var dead in retired)
            if (string.Equals(dead, repository, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException(
                    $"The configured release repository '{repository}' is on the retired list " +
                    "in config/release-source.json. A retired name can be claimed by someone " +
                    "else, so updating from it is a supply-chain risk. Set \"repository\" to " +
                    "the current target and rebuild the launcher.");

        return new Resolved(host, repository, channel, retired);
    }
}
