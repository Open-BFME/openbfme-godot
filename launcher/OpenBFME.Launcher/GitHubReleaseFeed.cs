using System.Net.Http;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

/// <summary>
/// Finds the current signed release-manifest URL for this launcher's channel.
///
/// GitHub's <c>/releases/latest</c> route deliberately skips pre-releases. Playtest
/// and nightly builds are published as pre-releases (and often as the only release
/// while the project is still in alpha), so a launcher that only asks for "latest"
/// will report "no release" exactly when players need an update the most.
///
/// Discovery order:
/// 1. Channel-independent latest, when the channel is stable.
/// 2. GitHub Releases API newest-first, preferring assets named
///    <c>release-manifest.json</c> on a release whose tag or name suggests the
///    requested channel (or any non-draft release for stable fallback).
/// 3. Clear, actionable failure when nothing is published yet.
/// </summary>
public sealed class GitHubReleaseFeed
{
    private readonly HttpClient _http;

    public TimeSpan RequestTimeout { get; set; } = TimeSpan.FromSeconds(45);

    public GitHubReleaseFeed(HttpClient? http = null)
    {
        _http = http ?? new HttpClient(new SocketsHttpHandler
        {
            ConnectTimeout = TimeSpan.FromSeconds(20),
            PooledConnectionLifetime = TimeSpan.FromMinutes(5)
        })
        {
            Timeout = Timeout.InfiniteTimeSpan
        };
        if (!_http.DefaultRequestHeaders.UserAgent.Any())
            _http.DefaultRequestHeaders.UserAgent.ParseAdd("OpenBFME-Launcher/1.0");
        // GitHub requires an Accept header for the JSON API.
        if (!_http.DefaultRequestHeaders.Accept.Any())
            _http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
    }

    public sealed record ReleaseCandidate(
        string Tag,
        string Name,
        bool PreRelease,
        Uri ManifestUri,
        DateTimeOffset? PublishedAt,
        string Source);

    /// <summary>
    /// Resolve the best manifest URL for <paramref name="channel"/>, or throw with a
    /// player-readable reason when none is available.
    /// </summary>
    public async Task<ReleaseCandidate> ResolveAsync(string channel, CancellationToken cancellationToken)
    {
        if (channel is not ("stable" or "playtest" or "nightly"))
            throw new ArgumentOutOfRangeException(nameof(channel), channel, "Expected stable, playtest, or nightly.");

        // Stable still prefers the dedicated "latest" asset URL: it is the one the
        // release workflow documents, and it avoids an extra API round-trip when a
        // full release exists.
        if (channel == "stable")
        {
            var latest = ReleaseSource.LatestManifestUri;
            if (await HeadExistsAsync(latest, cancellationToken))
            {
                return new ReleaseCandidate(
                    "latest", "Latest stable release", PreRelease: false,
                    latest, PublishedAt: null, Source: "releases/latest");
            }
        }

        var releases = await ListReleasesAsync(cancellationToken);
        if (releases.Count == 0)
            throw new InvalidOperationException(
                $"No GitHub Releases exist yet for {ReleaseSource.Repository}. " +
                "Nothing is wrong with your machine — a signed release simply has not " +
                "been published. When one is, this launcher will find it automatically.");

        var ranked = Rank(releases, channel).ToList();
        if (ranked.Count == 0)
        {
            var newest = releases[0];
            throw new InvalidOperationException(
                $"No {channel} release candidate with a release-manifest.json was found " +
                $"for {ReleaseSource.Repository}. The newest GitHub Release is " +
                $"'{newest.TagName}' (prerelease={newest.Prerelease}). " +
                "If you were given a direct manifest link, start with --manifest-url.");
        }

        var winner = ranked[0];
        var asset = winner.Assets.First(a =>
            a.Name.Equals("release-manifest.json", StringComparison.OrdinalIgnoreCase));
        // Prefer the browser download URL under /releases/download/… so it still
        // passes ReleaseUriPolicy (api.github.com is not on the allow-list).
        var manifestUri = new Uri(asset.BrowserDownloadUrl);
        ReleaseUriPolicy.Validate(manifestUri);
        return new ReleaseCandidate(
            winner.TagName,
            string.IsNullOrWhiteSpace(winner.Name) ? winner.TagName : winner.Name,
            winner.Prerelease,
            manifestUri,
            winner.PublishedAt,
            Source: "github-releases-api");
    }

    private async Task<bool> HeadExistsAsync(Uri uri, CancellationToken cancellationToken)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(RequestTimeout);
        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Head, uri);
            using var response = await _http.SendAsync(
                request, HttpCompletionOption.ResponseHeadersRead, deadline.Token);
            if (response.IsSuccessStatusCode) return true;
            // Some hosts refuse HEAD; try a ranged GET of one byte as a soft probe.
            if (response.StatusCode is System.Net.HttpStatusCode.MethodNotAllowed
                or System.Net.HttpStatusCode.NotImplemented)
            {
                using var get = new HttpRequestMessage(HttpMethod.Get, uri);
                get.Headers.Range = new System.Net.Http.Headers.RangeHeaderValue(0, 0);
                using var body = await _http.SendAsync(
                    get, HttpCompletionOption.ResponseHeadersRead, deadline.Token);
                return body.IsSuccessStatusCode ||
                       body.StatusCode == System.Net.HttpStatusCode.PartialContent;
            }
            return false;
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return false;
        }
        catch (HttpRequestException)
        {
            return false;
        }
    }

    private async Task<IReadOnlyList<GhRelease>> ListReleasesAsync(CancellationToken cancellationToken)
    {
        // Public API, no credentials: the launcher never ships tokens. Rate limit is
        // 60 unauthenticated requests/hour/IP — one list per update check is fine.
        var uri = new Uri(
            $"https://api.github.com/repos/{ReleaseSource.Repository}/releases?per_page=20");
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(RequestTimeout);
        try
        {
            using var response = await _http.GetAsync(
                uri, HttpCompletionOption.ResponseHeadersRead, deadline.Token);
            if (response.StatusCode == System.Net.HttpStatusCode.NotFound)
                throw new InvalidOperationException(
                    $"GitHub repository {ReleaseSource.Repository} was not found or is private. " +
                    "The launcher only updates from public releases.");
            if (response.StatusCode == System.Net.HttpStatusCode.Forbidden ||
                (int)response.StatusCode == 429)
                throw new InvalidOperationException(
                    "GitHub rate-limited the release check. Wait a few minutes and try again.");
            if (!response.IsSuccessStatusCode)
                throw new InvalidOperationException(
                    $"GitHub returned {(int)response.StatusCode} while listing releases for " +
                    $"{ReleaseSource.Repository}.");

            await using var stream = await response.Content.ReadAsStreamAsync(deadline.Token);
            var releases = await JsonSerializer.DeserializeAsync<List<GhRelease>>(
                stream, cancellationToken: deadline.Token) ?? new List<GhRelease>();
            // Drafts are never installable by players.
            return releases.Where(r => !r.Draft).ToList();
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"Timed out after {RequestTimeout.TotalSeconds:0}s asking GitHub for releases " +
                $"of {ReleaseSource.Repository}. Check your internet connection and try again.");
        }
    }

    private static IEnumerable<GhRelease> Rank(IReadOnlyList<GhRelease> releases, string channel)
    {
        bool HasManifest(GhRelease r) =>
            r.Assets.Any(a => a.Name.Equals("release-manifest.json", StringComparison.OrdinalIgnoreCase));

        static int ChannelScore(GhRelease r, string channel)
        {
            var hay = $"{r.TagName} {r.Name}".ToLowerInvariant();
            return channel switch
            {
                "playtest" => hay.Contains("playtest") ||
                              System.Text.RegularExpressions.Regex.IsMatch(hay, @"(^|[^a-z])rc([0-9]|\b|\.|-)") ||
                              r.Prerelease
                    ? 2
                    : hay.Contains("alpha") || hay.Contains("beta") ? 1 : 0,
                "nightly" => hay.Contains("nightly") ? 2 : r.Prerelease ? 1 : 0,
                // stable prefers non-prerelease; still allow a lone prerelease so alpha
                // ships are reachable until the first full release exists.
                "stable" => r.Prerelease ? 0 : 2,
                _ => 0
            };
        }

        return releases
            .Where(HasManifest)
            .Where(r => ChannelScore(r, channel) > 0)
            .OrderByDescending(r => ChannelScore(r, channel))
            .ThenByDescending(r => r.PublishedAt ?? DateTimeOffset.MinValue);
    }

    private sealed record GhRelease(
        [property: JsonPropertyName("tag_name")] string TagName,
        [property: JsonPropertyName("name")] string? Name,
        [property: JsonPropertyName("draft")] bool Draft,
        [property: JsonPropertyName("prerelease")] bool Prerelease,
        [property: JsonPropertyName("published_at")] DateTimeOffset? PublishedAt,
        [property: JsonPropertyName("assets")] List<GhAsset> Assets);

    private sealed record GhAsset(
        [property: JsonPropertyName("name")] string Name,
        [property: JsonPropertyName("browser_download_url")] string BrowserDownloadUrl);
}
