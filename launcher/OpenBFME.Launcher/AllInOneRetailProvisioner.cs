using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

/// <summary>
/// Installs BFME II / Rise of the Witch-king game files the same way the community
/// All-in-One Launcher does: fetch the base-game package description from the BFME
/// Ladder workshop API, then download every required file into a local install
/// directory.
///
/// OpenBFME never redistributes those bytes from our GitHub releases. This path is
/// how a player who already uses (or is willing to use) the All-in-One ecosystem
/// gets a working local install for conversion. Installs land under the OpenBFME
/// install root so no elevated registry write is required for the happy path.
///
/// The player must own a legal copy of the games. This provisioner is a convenience
/// transport over the same public workshop host the All-in-One Launcher uses; it is
/// not a substitute for a licence. <see cref="ThirdPartyDownloadDisclosure"/> says so
/// in the player's own words, once, before the first download.
///
/// <para><b>Trust model.</b> The upstream listing supplies transport only — a URL for
/// a file name. What may be installed, how large it is, and what it must hash to come
/// from <see cref="RetailPayloadManifest"/>, a repo-tracked pinned manifest. Every
/// downloaded file is SHA-256 verified against that manifest before it is moved into
/// the install tree; a mismatch aborts the run, deletes the partial, and reports the
/// expected and actual digests. Files the host lists but the manifest does not pin are
/// never written. When the manifest is empty the provisioner refuses to run: a
/// substituted or poisoned mirror payload must not be installable, and size alone
/// never proved anything about content.</para>
/// </summary>
public sealed class AllInOneRetailProvisioner
{
    public const string WorkshopServerHost = "https://bfmeladder.com";
    public const string DefaultLanguageCode = "EN";

    /// <summary>
    /// Hosts workshop metadata and file downloads may touch. Mirrors the discipline of
    /// <see cref="ReleaseUriPolicy"/>: a poisoned package list must not pull from an
    /// arbitrary HTTPS host into the player's install tree.
    /// </summary>
    private static readonly HashSet<string> AllowedHosts = new(StringComparer.OrdinalIgnoreCase)
    {
        "bfmeladder.com",
        "www.bfmeladder.com",
        "workshop-files.bfmeladder.com",
        "32d721493580a1de3fa984779848a30d.r2.cloudflarestorage.com"
    };

    /// <summary>Relative install folders under the OpenBFME install root.</summary>
    public const string Bfme2RelativeInstall = @"workspace\retail-installs\bfme2";
    public const string RotwkRelativeInstall = @"workspace\retail-installs\rotwk";

    private readonly HttpClient _http;

    public TimeSpan MetadataTimeout { get; set; } = TimeSpan.FromSeconds(90);
    public TimeSpan StallTimeout { get; set; } = TimeSpan.FromSeconds(90);
    public TimeSpan ResponseTimeout { get; set; } = TimeSpan.FromSeconds(60);

    private RetailPayloadManifest? _payloadManifest;

    /// <summary>
    /// Pinned digests every installed file must match. Defaults to the manifest
    /// compiled into the launcher; tests inject their own. Resolved lazily so simply
    /// constructing a launcher service never depends on the embedded resource.
    /// </summary>
    public RetailPayloadManifest PayloadManifest
    {
        get => _payloadManifest ??= RetailPayloadManifest.Embedded;
        init => _payloadManifest = value;
    }

    public AllInOneRetailProvisioner(HttpClient? http = null)
    {
        _http = http ?? new HttpClient(new SocketsHttpHandler
        {
            ConnectTimeout = TimeSpan.FromSeconds(30),
            PooledConnectionLifetime = TimeSpan.FromMinutes(10),
            // Workshop file host is a CDN; allow redirects within HTTPS.
            AllowAutoRedirect = true,
            MaxAutomaticRedirections = 5
        })
        {
            Timeout = Timeout.InfiniteTimeSpan
        };
        if (!_http.DefaultRequestHeaders.UserAgent.Any())
            _http.DefaultRequestHeaders.UserAgent.ParseAdd("OpenBFME-Launcher/1.0 (AllInOne-compatible)");
    }

    public sealed record ProvisionProgress(
        string Phase, string Message, double Percent, long BytesCompleted, long BytesTotal);

    public sealed record ProvisionResult(
        string Game, string InstallPath, string PackageName, string PackageVersion,
        int FilesInstalled, int FilesSkipped, long BytesDownloaded);

    public static string DefaultInstallPath(string openBfmeInstallRoot, string game) =>
        Path.GetFullPath(Path.Combine(openBfmeInstallRoot, game switch
        {
            "bfme2" => Bfme2RelativeInstall,
            "rotwk" => RotwkRelativeInstall,
            _ => throw new ArgumentOutOfRangeException(nameof(game), game, "Expected bfme2 or rotwk.")
        }));

    public static string WorkshopGuid(string game) => game switch
    {
        "bfme2" => "original-BFME2",
        "rotwk" => "original-RotWK",
        _ => throw new ArgumentOutOfRangeException(nameof(game), game, "Expected bfme2 or rotwk.")
    };

    /// <summary>
    /// Install (or resume) the base game into <paramref name="installDirectory"/>.
    /// Files already present with the pinned SHA-256 are skipped so a cancelled run can
    /// continue without re-downloading multi-gigabyte archives.
    /// </summary>
    public async Task<ProvisionResult> ProvisionAsync(
        string game,
        string installDirectory,
        IProgress<ProvisionProgress>? progress,
        CancellationToken cancellationToken,
        string languageCode = DefaultLanguageCode)
    {
        if (game is not ("bfme2" or "rotwk"))
            throw new ArgumentOutOfRangeException(nameof(game), game, "Expected bfme2 or rotwk.");
        if (string.IsNullOrWhiteSpace(installDirectory))
            throw new ArgumentException("Install directory is required.", nameof(installDirectory));

        // Fail closed before the first network call: with no pinned digests there is
        // nothing to verify against, and an unverifiable multi-gigabyte payload must
        // not reach the player's disk.
        var pinned = PayloadManifest.RequirePayload(game, languageCode);
        var pinnedByPath = RetailPayloadManifest.Index(pinned);

        var target = Path.GetFullPath(installDirectory);
        RejectDangerousInstallRoot(target);
        Directory.CreateDirectory(target);

        progress?.Report(new ProvisionProgress("Workshop", $"Fetching {game} package list…", 0, 0, 0));
        var package = await FetchPackageAsync(WorkshopGuid(game), cancellationToken);
        var listed = SelectLanguageFiles(package.Files, languageCode);
        if (listed.Count == 0)
            throw new InvalidDataException(
                $"The workshop package for {game} listed no files for language '{languageCode}'.");

        // The host proposes; the pinned manifest disposes. Only pinned paths are
        // installed, and every pinned path must be on offer — a listing that has gone
        // missing a file is a changed upstream package, not a partial install.
        var files = new List<WorkshopFile>();
        var offered = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var file in listed)
        {
            var relative = RetailPayloadManifest.NormalizeRelativePath(file.Name);
            if (relative is null || !pinnedByPath.ContainsKey(relative)) continue;
            if (!offered.Add(relative)) continue;
            files.Add(file);
        }
        var absent = pinnedByPath.Keys.Where(p => !offered.Contains(p)).OrderBy(p => p).ToList();
        if (absent.Count != 0)
            throw new InvalidDataException(
                $"The workshop package for {game} no longer offers {absent.Count} pinned " +
                $"file(s) — first missing: {absent[0]}. The upstream package changed; " +
                "nothing was installed. Update the pinned retail payload manifest from " +
                "payloads you have verified, or use your own lawfully owned installation.");

        long totalBytes = pinnedByPath.Values.Sum(e => e.Size);
        long completedBytes = 0;
        var installed = 0;
        var skipped = 0;
        long downloaded = 0;

        // Fail before the first byte when the disk cannot hold the package.
        var required = totalBytes + 256L * 1024 * 1024;
        var drive = new DriveInfo(Path.GetPathRoot(target)
            ?? throw new InvalidOperationException("Install path has no drive root."));
        if (drive.AvailableFreeSpace < required)
            throw new IOException(
                $"Downloading {game} needs about {FormatBytes(required)} free on {drive.Name}, " +
                $"but only {FormatBytes(drive.AvailableFreeSpace)} is available.");

        progress?.Report(new ProvisionProgress(
            "Planning",
            $"{package.Name} {package.Version} — {files.Count} files, {FormatBytes(totalBytes)}",
            0, 0, totalBytes));

        foreach (var file in files.OrderBy(f => f.Size))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var relative = RetailPayloadManifest.NormalizeRelativePath(file.Name)
                ?? throw new InvalidDataException(
                    $"Workshop package listed an unsafe file path '{file.Name}'. " +
                    "The download was refused rather than writing outside the install folder.");
            var entry = pinnedByPath[relative];
            var destination = Path.Combine(target, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);

            if (await IsAlreadyCorrectAsync(destination, entry, cancellationToken))
            {
                skipped++;
                completedBytes += entry.Size;
                Report(progress, "Skipping", relative, completedBytes, totalBytes);
                continue;
            }

            if (string.IsNullOrWhiteSpace(file.Url) ||
                !Uri.TryCreate(file.Url, UriKind.Absolute, out var url))
                throw new InvalidDataException($"Workshop file '{file.Name}' has no download URL.");
            ValidateWorkshopUri(url, file.Name);

            await DownloadFileAsync(
                url, destination, relative, entry, progress, completedBytes, totalBytes, cancellationToken);
            downloaded += entry.Size;
            installed++;
            completedBytes += entry.Size;
            Report(progress, "Installed", relative, completedBytes, totalBytes);
        }

        // Require the same edition-aware fingerprint as browse/discovery. A package
        // that downloads a marker-only, incomplete, or wrong-edition tree is a
        // broken provision, not a success.
        if (!RetailDiscovery.IsRetailInstall(game, target))
        {
            var rejection = RetailDiscovery.ExplainRejection(game, target)
                            ?? "the retail fingerprint did not match";
            throw new InvalidDataException(
                $"The {game} download finished, but {rejection} " +
                "The workshop package may have changed; nothing was marked ready.");
        }

        WriteReceipt(target, game, package, languageCode, installed, skipped);
        progress?.Report(new ProvisionProgress(
            "Ready",
            $"{package.Name} installed at {target}",
            100, totalBytes, totalBytes));

        return new ProvisionResult(
            game, target, package.Name, package.Version, installed, skipped, downloaded);
    }

    /// <summary>
    /// Resolve a usable install path for <paramref name="game"/>: existing discovery
    /// first, then the default provision location if it already has game.dat.
    /// </summary>
    public static string? ResolveExisting(string game, string openBfmeInstallRoot)
    {
        var found = RetailDiscovery.Discover(game);
        if (found is not null && RetailDiscovery.IsRetailInstall(game, found)) return found;
        var provisioned = DefaultInstallPath(openBfmeInstallRoot, game);
        return RetailDiscovery.IsRetailInstall(game, provisioned) ? provisioned : null;
    }

    private async Task<WorkshopPackage> FetchPackageAsync(string guid, CancellationToken cancellationToken)
    {
        var uri = new Uri(
            $"{WorkshopServerHost}/api/workshop/download?guid={Uri.EscapeDataString(guid)}");
        ValidateWorkshopUri(uri, guid);
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(MetadataTimeout);
        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, uri);
            // Match the All-in-One Launcher's unauthenticated workshop client.
            request.Headers.TryAddWithoutValidation("AuthAccountUuid", "unauthenticated");
            request.Headers.TryAddWithoutValidation("AuthAccountPassword", "");
            using var response = await _http.SendAsync(
                request, HttpCompletionOption.ResponseHeadersRead, deadline.Token);
            if (!response.IsSuccessStatusCode)
                throw new InvalidDataException(
                    $"Workshop package '{guid}' could not be fetched " +
                    $"({(int)response.StatusCode} {response.StatusCode}). " +
                    "The BFME Ladder / All-in-One workshop host may be down.");
            await using var stream = await response.Content.ReadAsStreamAsync(deadline.Token);
            var package = await JsonSerializer.DeserializeAsync<WorkshopPackage>(
                stream, cancellationToken: deadline.Token)
                ?? throw new InvalidDataException($"Workshop package '{guid}' was empty.");
            if (package.Files is null || package.Files.Count == 0)
                throw new InvalidDataException($"Workshop package '{guid}' listed no files.");
            return package;
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"Timed out after {MetadataTimeout.TotalSeconds:0}s fetching the workshop " +
                $"package list from {WorkshopServerHost}.");
        }
    }

    private async Task DownloadFileAsync(
        Uri url,
        string destination,
        string relativePath,
        RetailPayloadManifest.Entry entry,
        IProgress<ProvisionProgress>? progress,
        long completedBefore,
        long totalBytes,
        CancellationToken cancellationToken)
    {
        var temporary = destination + $".{Guid.NewGuid():N}.part";
        try
        {
            using var digest = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            using var responseCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            responseCts.CancelAfter(ResponseTimeout);
            using var response = await _http.GetAsync(
                url, HttpCompletionOption.ResponseHeadersRead, responseCts.Token);
            if (!response.IsSuccessStatusCode)
                throw new InvalidDataException(
                    $"Download of {relativePath} failed with {(int)response.StatusCode}.");
            // Re-validate after redirects so a 302 cannot leave the allow-list.
            if (response.RequestMessage?.RequestUri is Uri finalUri)
                ValidateWorkshopUri(finalUri, relativePath);

            await using var remote = await response.Content.ReadAsStreamAsync(cancellationToken);
            await using var local = new FileStream(
                temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None, 128 * 1024,
                FileOptions.Asynchronous | FileOptions.SequentialScan);

            var buffer = new byte[128 * 1024];
            long written = 0;
            var expected = entry.Size;

            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                int read;
                try
                {
                    using var readCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                    readCts.CancelAfter(StallTimeout);
                    read = await remote.ReadAsync(buffer.AsMemory(0, buffer.Length), readCts.Token);
                }
                catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
                {
                    throw new TimeoutException(
                        $"Download of {relativePath} stalled: no data for {StallTimeout.TotalSeconds:0}s.");
                }
                if (read == 0) break;
                // Refuse to keep buffering a response that has already overrun the
                // pinned size: a digest check at the end cannot bound disk use.
                if (written + read > expected)
                    throw new InvalidDataException(
                        $"{relativePath} sent more than the pinned {expected} bytes. " +
                        "The download was cut off and discarded.");
                await local.WriteAsync(buffer.AsMemory(0, read), cancellationToken);
                digest.AppendData(buffer.AsSpan(0, read));
                written += read;
                if (written % (2L * 1024 * 1024) < read || written == expected)
                {
                    var overall = completedBefore + written;
                    var percent = totalBytes <= 0 ? 0 :
                        Math.Clamp(overall * 100.0 / totalBytes, 0, 99.5);
                    progress?.Report(new ProvisionProgress(
                        "Downloading",
                        $"{relativePath} ({FormatBytes(written)} / {FormatBytes(expected)})",
                        percent, overall, totalBytes));
                }
            }

            await local.FlushAsync(cancellationToken);
            local.Close();

            if (written != expected)
                throw new InvalidDataException(
                    $"{relativePath} downloaded {written} bytes but the pinned manifest " +
                    $"declares {expected}. Nothing was installed.");

            // The check that actually matters. Size proves nothing about content: a
            // substituted payload of the right length would have passed the old
            // check. Compare the pinned SHA-256 before the file exists under its real
            // name, and report both digests so a mismatch is diagnosable rather than
            // just alarming.
            var actual = Convert.ToHexString(digest.GetHashAndReset()).ToLowerInvariant();
            var expectedDigest = entry.Sha256.ToLowerInvariant();
            if (!actual.Equals(expectedDigest, StringComparison.Ordinal))
                throw new InvalidDataException(
                    $"{relativePath} failed SHA-256 verification and was deleted. " +
                    $"Expected {expectedDigest}, got {actual}. The file served by " +
                    $"{url.DnsSafeHost} is not the pinned payload; nothing was installed.");

            if (File.Exists(destination)) File.Delete(destination);
            File.Move(temporary, destination);
        }
        finally
        {
            // Covers every abort path: mismatch, size overrun, stall, cancellation.
            if (File.Exists(temporary))
                try { File.Delete(temporary); }
                catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        }
    }

    /// <summary>
    /// A resume is only a resume when the bytes on disk are the pinned bytes. Size is a
    /// cheap pre-filter; the digest is the answer.
    /// </summary>
    private static async Task<bool> IsAlreadyCorrectAsync(
        string path, RetailPayloadManifest.Entry entry, CancellationToken cancellationToken)
    {
        if (!File.Exists(path)) return false;
        try
        {
            if (new FileInfo(path).Length != entry.Size) return false;
            var actual = await HashSha256HexAsync(path, cancellationToken);
            return actual.Equals(entry.Sha256, StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    public static async Task<string> HashSha256HexAsync(string path, CancellationToken cancellationToken)
    {
        await using var stream = new FileStream(
            path, FileMode.Open, FileAccess.Read, FileShare.Read, 128 * 1024,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        var hash = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    /// <summary>
    /// Keep one file per relative destination. Prefer a language-specific match over
    /// ALL when both exist (English game.dat vs a generic one).
    /// </summary>
    public static IReadOnlyList<WorkshopFile> SelectLanguageFiles(
        IReadOnlyList<WorkshopFile> files, string languageCode)
    {
        var code = languageCode.Trim().ToUpperInvariant();
        if (code.Length == 0) code = DefaultLanguageCode;

        var candidates = new List<WorkshopFile>();
        foreach (var file in files)
        {
            if (string.IsNullOrWhiteSpace(file.Name)) continue;
            var lang = (file.Language ?? "ALL").Trim().ToUpperInvariant();
            if (lang is "" or "ALL") { candidates.Add(file); continue; }
            var tokens = lang.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            if (tokens.Any(t => t == code || t == "EN" && code == "EN"))
                candidates.Add(file);
        }

        // Collapse to unique relative paths, preferring specific language over ALL.
        var best = new Dictionary<string, WorkshopFile>(StringComparer.OrdinalIgnoreCase);
        foreach (var file in candidates)
        {
            var key = NormalizeRelative(file.Name);
            if (key is null) continue;
            if (!best.TryGetValue(key, out var existing))
            {
                best[key] = file;
                continue;
            }
            var existingLang = (existing.Language ?? "ALL").Trim().ToUpperInvariant();
            var nextLang = (file.Language ?? "ALL").Trim().ToUpperInvariant();
            var existingSpecific = existingLang != "ALL" && existingLang.Length > 0;
            var nextSpecific = nextLang != "ALL" && nextLang.Length > 0;
            if (nextSpecific && !existingSpecific) best[key] = file;
        }
        return best.Values.ToList();
    }

    /// <summary>
    /// Single normalisation rule shared with the pinned manifest, so a listed file and
    /// a pinned entry are keyed identically and a path cannot escape the install root.
    /// </summary>
    private static string? NormalizeRelative(string name) =>
        RetailPayloadManifest.NormalizeRelativePath(name);

    public static void ValidateWorkshopUri(Uri uri, string fileName)
    {
        if (uri.Scheme != Uri.UriSchemeHttps || uri.UserInfo.Length != 0 ||
            !string.IsNullOrEmpty(uri.Fragment))
            throw new InvalidDataException(
                $"Workshop file '{fileName}' must use credential-free HTTPS.");
        if (!AllowedHosts.Contains(uri.DnsSafeHost))
            throw new InvalidDataException(
                $"Workshop file '{fileName}' points at unapproved host '{uri.DnsSafeHost}'. " +
                "Only BFME Ladder / All-in-One workshop hosts are allowed.");
    }

    private static void RejectDangerousInstallRoot(string target)
    {
        // Same guard the All-in-One sync uses: installing into C:\ or D:\ would let a
        // cleanup pass delete the drive root.
        var full = Path.GetFullPath(target).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        var root = Path.GetPathRoot(full)?.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        if (string.Equals(full, root, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException(
                $"Refusing to install game files into drive root '{full}'. " +
                "Choose a folder such as a dedicated BFME2 or RotWK directory.");
        var parts = full.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            .Where(p => p.Length > 0).ToArray();
        if (parts.Length < 2)
            throw new InvalidOperationException(
                $"Install path '{full}' is too shallow and was refused for safety.");
    }

    private static void WriteReceipt(
        string target, string game, WorkshopPackage package, string language,
        int installed, int skipped)
    {
        var receipt = new
        {
            schema = "openbfme.retail-provision-receipt",
            schemaVersion = 1,
            game,
            packageGuid = WorkshopGuid(game),
            packageName = package.Name,
            packageVersion = package.Version,
            language,
            installed,
            skipped,
            source = WorkshopServerHost,
            verification = "sha256-pinned",
            verificationManifest = "launcher/OpenBFME.Launcher/retail-payload-manifest.json",
            completedAtUtc = DateTime.UtcNow.ToString("o")
        };
        var path = Path.Combine(target, "openbfme-provision.json");
        File.WriteAllText(path, JsonSerializer.Serialize(receipt, new JsonSerializerOptions
        {
            WriteIndented = true
        }) + "\n");
    }

    private static void Report(
        IProgress<ProvisionProgress>? progress, string phase, string name,
        long completed, long total)
    {
        var percent = total <= 0 ? 0 : Math.Clamp(completed * 100.0 / total, 0, 100);
        progress?.Report(new ProvisionProgress(phase, name, percent, completed, total));
    }

    private static string FormatBytes(long bytes)
    {
        if (bytes < 1024) return $"{bytes} B";
        if (bytes < 1024 * 1024) return $"{bytes / 1024.0:0.0} KiB";
        if (bytes < 1024L * 1024 * 1024) return $"{bytes / (1024.0 * 1024):0.0} MiB";
        return $"{bytes / (1024.0 * 1024 * 1024):0.00} GiB";
    }

    public sealed record WorkshopPackage(
        [property: JsonPropertyName("Guid")] string? Guid,
        [property: JsonPropertyName("Name")] string Name,
        [property: JsonPropertyName("Version")] string Version,
        [property: JsonPropertyName("Game")] int Game,
        [property: JsonPropertyName("Files")] List<WorkshopFile> Files);

    public sealed record WorkshopFile(
        [property: JsonPropertyName("Name")] string Name,
        [property: JsonPropertyName("Language")] string? Language,
        [property: JsonPropertyName("Md5")] string? Md5,
        [property: JsonPropertyName("Size")] long Size,
        [property: JsonPropertyName("Url")] string? Url);
}
