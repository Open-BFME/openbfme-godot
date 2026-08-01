using System.IO.Compression;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;

namespace OpenBFME.Launcher;

public sealed record TransferProgress(string Phase, long Completed, long Total)
{
    public double Percent => Total <= 0 ? 0 : Math.Clamp(Completed * 100.0 / Total, 0, 100);
}

public sealed class ReleaseInstaller
{
    private readonly HttpClient _http;

    /// <summary>
    /// How long the whole small-metadata fetch (manifest, signature) may take.
    /// These are kilobytes; anything slower than this is a dead network, not a slow one.
    /// </summary>
    public TimeSpan MetadataTimeout { get; set; } = TimeSpan.FromSeconds(60);

    /// <summary>How long to wait for a package server to send response headers.</summary>
    public TimeSpan ResponseTimeout { get; set; } = TimeSpan.FromSeconds(60);

    /// <summary>
    /// How long a package download may go without receiving a single byte before it is
    /// declared stalled. This is deliberately a per-read stall budget rather than an
    /// overall deadline: a multi-gigabyte download on a slow line is legitimate, a
    /// connection that has gone silent is not. Without it a half-open TCP connection
    /// wedged the launcher UI forever with no way out.
    /// </summary>
    public TimeSpan StallTimeout { get; set; } = TimeSpan.FromSeconds(90);

    public ReleaseInstaller(HttpClient? http = null)
    {
        _http = http ?? new HttpClient(new System.Net.Http.SocketsHttpHandler
        {
            // A dead host must fail here rather than hanging on a TCP connect.
            ConnectTimeout = TimeSpan.FromSeconds(30),
            PooledConnectionLifetime = TimeSpan.FromMinutes(10)
        })
        {
            // HttpClient.Timeout is an overall deadline that also covers reading the
            // response body, so the stock 100 seconds would abort every real package
            // download. Timeouts are imposed explicitly per phase instead: connect
            // above, response headers and per-read stall below.
            Timeout = Timeout.InfiniteTimeSpan
        };
        if (!_http.DefaultRequestHeaders.UserAgent.Any())
            _http.DefaultRequestHeaders.UserAgent.ParseAdd("OpenBFME-Launcher/1.0");
    }

    public async Task<ReleaseManifest> FetchManifestAsync(Uri uri, CancellationToken cancellationToken)
    {
        ReleaseUriPolicy.Validate(uri);
        var signatureUri = new Uri(uri.AbsoluteUri + ".sig");
        ReleaseUriPolicy.Validate(signatureUri);
        var bytes = await DownloadBoundedAsync(uri, 1024 * 1024, cancellationToken);
        var signature = await DownloadBoundedAsync(signatureUri, 16 * 1024, cancellationToken);
        ManifestSignature.Verify(bytes, signature);
        return ReleaseManifest.Parse(bytes);
    }

    private async Task<byte[]> DownloadBoundedAsync(
        Uri uri, int maximumBytes, CancellationToken cancellationToken)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(MetadataTimeout);
        try
        {
            using var response = await _http.GetAsync(
                uri, HttpCompletionOption.ResponseHeadersRead, deadline.Token);
            if (!response.IsSuccessStatusCode)
                throw new InvalidDataException(DescribeFetchFailure(response.StatusCode));
            ReleaseUriPolicy.ValidateResponse(response.RequestMessage?.RequestUri
                ?? throw new InvalidDataException("Release response URL is missing."));
            if (response.Content.Headers.ContentLength is long length && length > maximumBytes)
                throw new InvalidDataException("Release metadata is too large.");
            // Read against the bound rather than buffering first and measuring after.
            // ReadAsByteArrayAsync would happily allocate whatever the host sent when it
            // declared no Content-Length at all, which is the one case the header check
            // above cannot cover — the bound is meant to hold even then.
            await using var body = await response.Content.ReadAsStreamAsync(deadline.Token);
            using var buffer = new MemoryStream();
            var chunk = new byte[16 * 1024];
            while (true)
            {
                var read = await body.ReadAsync(chunk, deadline.Token);
                if (read == 0) break;
                if (buffer.Length + read > maximumBytes)
                    throw new InvalidDataException("Release metadata is too large.");
                buffer.Write(chunk, 0, read);
            }
            return buffer.ToArray();
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            // Distinguish "the user pressed Cancel" from "the network is dead".
            // Reporting the wrong one sends the player looking in the wrong place.
            throw new TimeoutException(
                $"Timed out after {MetadataTimeout.TotalSeconds:0} seconds fetching release " +
                $"information from {uri.Host}. Check your internet connection and try again.");
        }
    }

    /// <summary>
    /// Turn an HTTP status into something a playtester can act on.
    ///
    /// The bare <c>EnsureSuccessStatusCode</c> message ("Response status code does not
    /// indicate success: 404") is the least useful thing to show someone whose update
    /// just failed, and 404 here has three very different causes that look identical:
    /// no release exists yet, the release repository is private (it serves 404, not 403,
    /// to anyone without access), or the build is a pre-release — GitHub's
    /// <c>releases/latest</c> route deliberately skips pre-releases, which is exactly
    /// what a playtest channel publishes.
    /// </summary>
    private static string DescribeFetchFailure(System.Net.HttpStatusCode status) => status switch
    {
        System.Net.HttpStatusCode.NotFound =>
            $"No release information was found for {ReleaseSource.Repository}. That means one of: " +
            "no release has been published yet; the release repository is private, so it is not " +
            "readable without access; or the newest build is a pre-release, which the " +
            "\"latest release\" address never returns. If you were given a direct release link, " +
            "start the launcher with --manifest-url and that link.",
        System.Net.HttpStatusCode.Unauthorized or System.Net.HttpStatusCode.Forbidden =>
            $"Access to release information for {ReleaseSource.Repository} was refused. The " +
            "launcher deliberately sends no credentials, so the release must be publicly " +
            "readable to update automatically.",
        System.Net.HttpStatusCode.TooManyRequests =>
            "The release host is rate-limiting this machine. Wait a few minutes and try again.",
        _ => $"The release host returned {(int)status} ({status}) while fetching release " +
             "information. Nothing was installed; try again later."
    };

    public async Task InstallAsync(
        ReleaseManifest manifest,
        string installRoot,
        IProgress<TransferProgress>? progress,
        CancellationToken cancellationToken,
        Func<ReleasePackage, CancellationToken, Task<Stream>>? packageStream = null,
        string? expectedChannel = null)
    {
        manifest.Validate();
        var fullRoot = Path.GetFullPath(installRoot);
        if (expectedChannel is not null && manifest.Channel != expectedChannel)
            // Worth spelling out: a launcher built with the default channel will refuse
            // every pre-release build, which is exactly the situation during playtesting.
            // The mismatch is a real safety check, but the fix has to be obvious.
            throw new InvalidDataException(
                $"This launcher is set to the '{expectedChannel}' channel, but the newest " +
                $"release is on the '{manifest.Channel}' channel, so it was not installed. " +
                $"Start the launcher with --channel {manifest.Channel} to use that build.");
        var currentState = InstallState.Load(fullRoot);
        if (currentState is not null &&
            CompareVersions(manifest.Version, currentState.HighestVersion) < 0)
            throw new InvalidDataException("Automatic release downgrade is not allowed; use rollback.");
        var versions = Path.Combine(fullRoot, "versions");
        Directory.CreateDirectory(versions);
        // Reclaim what a killed update left behind before measuring free space: an
        // abandoned staging tree and its part-written archive can be several gigabytes.
        RemoveAbandonedScratch(versions);
        var game = manifest.Packages.SingleOrDefault(p => p.Kind == "game-windows-x64")
            ?? throw new InvalidDataException("Windows game package is missing.");
        var launcher = manifest.Packages.SingleOrDefault(p => p.Kind == "launcher-windows-x64")
            ?? throw new InvalidDataException("Windows launcher package is missing.");
        var requiredSpace = manifest.Packages.Sum(package => package.Size + package.ExpandedSize) +
                            256L * 1024 * 1024;
        var drive = new DriveInfo(Path.GetPathRoot(fullRoot)
            ?? throw new InvalidOperationException("Install root has no drive."));
        if (drive.AvailableFreeSpace < requiredSpace)
            throw new IOException(
                $"OpenBFME needs {requiredSpace / (1024 * 1024)} MiB free for this update.");
        var final = Path.Combine(versions, manifest.Version);
        if (Directory.Exists(final))
        {
            // A version directory that is present and intact is reused as-is: version
            // directories are immutable, so re-downloading it would change nothing.
            // A present-but-damaged one (antivirus quarantine, bad sector, a half-written
            // tree from a killed update) used to make this throw and stay thrown — the
            // update path was the only way to repair an install, and it refused to run
            // while the damage existed, so "check for an update to reinstall it" could
            // never succeed. It is now reinstalled over, loudly.
            var damage = DescribeVersionDamage(
                () => VerifyInstalledVersion(final, manifest.Version, manifest.Commit, game.Sha256));
            if (damage is null)
            {
                await InstallLauncherAsync(
                    manifest, launcher, fullRoot, progress, cancellationToken, packageStream);
                SelectVersion(fullRoot, manifest);
                PruneObsoleteVersions(fullRoot);
                return;
            }
            progress?.Report(new TransferProgress(
                $"Repairing {manifest.Version} ({damage})", 0, 1));
        }

        var staging = final + $".staging-{Guid.NewGuid():N}";
        Directory.CreateDirectory(staging);
        try
        {
            await using var source = packageStream is null
                ? await OpenRemotePackageAsync(game, cancellationToken)
                : await packageStream(game, cancellationToken);
            await ExtractVerifiedAsync(source, game, staging, progress, cancellationToken);
            VerifyPayloadShape(staging);
            InstalledVersionIdentity.Write(staging, manifest, game);
            VerifyInstalledVersion(staging, manifest.Version, manifest.Commit, game.Sha256);
            ReplaceVersionDirectory(staging, final);
            await InstallLauncherAsync(
                manifest, launcher, fullRoot, progress, cancellationToken, packageStream);
            SelectVersion(fullRoot, manifest);
            PruneObsoleteVersions(fullRoot);
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
        }
    }

    private static int CompareVersions(string left, string right)
    {
        static (Version Core, string? Pre) Parse(string value)
        {
            // Shape-checked against the same rule the manifest is validated with, so a
            // release can never be accepted for install and then be unorderable here.
            if (!ReleaseManifest.IsOrderableVersion(value))
                throw new InvalidDataException($"Release version '{value}' is not SemVer.");
            var match = System.Text.RegularExpressions.Regex.Match(
                value, @"^([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([0-9A-Za-z.-]+))?$");
            if (!match.Success) throw new InvalidDataException("Installed release version is not SemVer.");
            return (
                new Version(
                    int.Parse(match.Groups[1].Value),
                    int.Parse(match.Groups[2].Value),
                    int.Parse(match.Groups[3].Value)),
                match.Groups[4].Success ? match.Groups[4].Value : null);
        }
        var a = Parse(left);
        var b = Parse(right);
        var core = a.Core.CompareTo(b.Core);
        if (core != 0) return core;
        if (a.Pre is null) return b.Pre is null ? 0 : 1;
        if (b.Pre is null) return -1;
        var aa = a.Pre.Split('.');
        var bb = b.Pre.Split('.');
        for (var index = 0; index < Math.Max(aa.Length, bb.Length); index++)
        {
            if (index >= aa.Length) return -1;
            if (index >= bb.Length) return 1;
            var an = int.TryParse(aa[index], out var av);
            var bn = int.TryParse(bb[index], out var bv);
            var comparison = an && bn ? av.CompareTo(bv) :
                an ? -1 : bn ? 1 : string.CompareOrdinal(aa[index], bb[index]);
            if (comparison != 0) return comparison;
        }
        return 0;
    }

    private async Task InstallLauncherAsync(
        ReleaseManifest manifest,
        ReleasePackage package,
        string installRoot,
        IProgress<TransferProgress>? progress,
        CancellationToken cancellationToken,
        Func<ReleasePackage, CancellationToken, Task<Stream>>? packageStream)
    {
        var versions = Path.Combine(installRoot, "launcher-versions");
        Directory.CreateDirectory(versions);
        RemoveAbandonedScratch(versions);
        var final = Path.Combine(versions, manifest.Version);
        if (Directory.Exists(final))
        {
            // Same reasoning as the game payload: a damaged launcher directory must be
            // repairable by updating, not a permanent refusal to update.
            var damage = DescribeVersionDamage(() => VerifyLauncherVersion(final, manifest, package));
            if (damage is null)
            {
                LauncherInstallState.Select(installRoot, manifest, package);
                return;
            }
            progress?.Report(new TransferProgress(
                $"Repairing launcher {manifest.Version} ({damage})", 0, 1));
        }
        var staging = final + $".staging-{Guid.NewGuid():N}";
        Directory.CreateDirectory(staging);
        try
        {
            await using var source = packageStream is null
                ? await OpenRemotePackageAsync(package, cancellationToken)
                : await packageStream(package, cancellationToken);
            await ExtractVerifiedAsync(source, package, staging, progress, cancellationToken);
            VerifyLauncherPayload(staging);
            BundleInventory.Verify(staging);
            InstalledVersionIdentity.Write(staging, manifest, package);
            VerifyLauncherVersion(staging, manifest, package);
            ReplaceVersionDirectory(staging, final);
            LauncherInstallState.Select(installRoot, manifest, package);
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
        }
    }

    /// <summary>
    /// Names the launcher gives its own scratch under a version root: a tree being
    /// staged, a superseded tree waiting to be dropped, and the archive being hashed.
    /// All three carry a GUID this process generated, so they are unmistakably ours.
    ///
    /// They matter because <see cref="PruneVersionRoot"/> walks the same directory. A
    /// launcher killed mid-update (Alt+F4, power loss, the installer's own host process
    /// exiting) leaves a staging tree behind, whose name passes the "looks like a
    /// version" check but which has no identity file — so the pruner threw, and kept
    /// throwing, permanently blocking every later update on that machine.
    /// </summary>
    private static readonly System.Text.RegularExpressions.Regex ScratchDirectoryName =
        new(@"\.(?:staging|replaced)-[0-9a-f]{32}$",
            System.Text.RegularExpressions.RegexOptions.CultureInvariant);

    private static readonly System.Text.RegularExpressions.Regex ScratchArchiveName =
        new(@"^\.[0-9a-f]{32}\.zip$",
            System.Text.RegularExpressions.RegexOptions.CultureInvariant);

    private static bool IsLauncherScratch(string name) => ScratchDirectoryName.IsMatch(name);

    /// <summary>
    /// Drop scratch left by an update that never finished.
    ///
    /// Failure is tolerated rather than fatal, and deliberately so: the only way a
    /// delete fails here is that a second launcher process is using that scratch right
    /// now, in which case it is not abandoned and is not ours to remove. Nothing depends
    /// on the removal succeeding — scratch is never selected, launched, or verified — so
    /// leaving it costs disk, whereas throwing would reintroduce the update-blocking bug
    /// this exists to fix.
    /// </summary>
    private static void RemoveAbandonedScratch(string versionRoot)
    {
        if (!Directory.Exists(versionRoot)) return;
        foreach (var directory in Directory.EnumerateDirectories(versionRoot))
            if (IsLauncherScratch(Path.GetFileName(directory)))
                TryRemoveScratch(directory);
        foreach (var file in Directory.EnumerateFiles(versionRoot))
            if (ScratchArchiveName.IsMatch(Path.GetFileName(file)))
                try { File.Delete(file); }
                catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }

    private static void TryRemoveScratch(string directory)
    {
        try { Directory.Delete(directory, true); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }

    /// <summary>
    /// Swap a verified staging tree into its final, immutable location.
    ///
    /// When the destination already exists it is damaged (an intact one is reused and
    /// never reaches here), so it is moved aside first and dropped only once the new
    /// tree is in place. The old tree is restored if the second move fails, so the
    /// window in which neither exists is a single rename, and a process killed inside
    /// that window leaves the version simply absent — which the next run reinstalls —
    /// rather than half-replaced.
    /// </summary>
    private static void ReplaceVersionDirectory(string staging, string final)
    {
        if (!Directory.Exists(final))
        {
            Directory.Move(staging, final);
            return;
        }
        var replaced = final + $".replaced-{Guid.NewGuid():N}";
        try
        {
            Directory.Move(final, replaced);
        }
        catch (IOException error)
        {
            throw new IOException(
                $"The damaged copy of {Path.GetFileName(final)} could not be replaced because " +
                "something is still using it. Close OpenBFME (and any running launcher) and " +
                $"try again. Details: {error.Message}", error);
        }
        try
        {
            Directory.Move(staging, final);
        }
        catch
        {
            Directory.Move(replaced, final);
            throw;
        }
        TryRemoveScratch(replaced);
    }

    /// <summary>
    /// Run a verification and return why it failed, or <c>null</c> when it passed.
    /// Only the launcher's own integrity failures are treated as damage; anything else
    /// (an unreadable disk, a permissions problem) is left to propagate, because
    /// reinstalling over it would not fix it and would hide the real cause.
    /// </summary>
    private static string? DescribeVersionDamage(Action verify)
    {
        try
        {
            verify();
            return null;
        }
        catch (InvalidDataException error)
        {
            return error.Message.TrimEnd('.');
        }
    }

    private static void VerifyLauncherPayload(string root)
    {
        var executable = Path.Combine(root, "OpenBFME.Launcher.exe");
        if (!File.Exists(executable) || new FileInfo(executable).Length < 64 * 1024)
            throw new InvalidDataException("Installed launcher executable is missing or truncated.");
        if (!File.Exists(Path.Combine(root, BundleInventory.FileName)))
            throw new InvalidDataException("Installed launcher inventory is missing.");
    }

    private static void VerifyLauncherVersion(
        string root, ReleaseManifest manifest, ReleasePackage package)
    {
        VerifyLauncherPayload(root);
        BundleInventory.Verify(root);
        InstalledVersionIdentity.Verify(root, manifest.Version, manifest.Commit, package.Sha256);
    }

    private async Task<Stream> OpenRemotePackageAsync(ReleasePackage package, CancellationToken token)
    {
        var uri = new Uri(package.Url);
        ReleaseUriPolicy.Validate(uri);

        // The token passed to GetAsync stays linked to the response body stream, so this
        // source must outlive the stream — disposing it here would risk tearing down the
        // download it is meant to protect. Ownership is handed to ResponseOwnedStream,
        // and the timer is defused as soon as headers arrive so the header deadline can
        // never fire part-way through a legitimately long download.
        var headers = CancellationTokenSource.CreateLinkedTokenSource(token);
        HttpResponseMessage? response = null;
        try
        {
            headers.CancelAfter(ResponseTimeout);
            try
            {
                response = await _http.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, headers.Token);
            }
            catch (OperationCanceledException) when (!token.IsCancellationRequested)
            {
                throw new TimeoutException(
                    $"Timed out after {ResponseTimeout.TotalSeconds:0} seconds waiting for " +
                    $"{uri.Host} to start sending {package.Name}. Check your internet connection " +
                    "and try again.");
            }
            headers.CancelAfter(Timeout.InfiniteTimeSpan);

            response.EnsureSuccessStatusCode();
            ReleaseUriPolicy.ValidateResponse(response.RequestMessage?.RequestUri
                ?? throw new InvalidDataException("Package response URL is missing."));
            if (response.Content.Headers.ContentLength is long length && length != package.Size)
                throw new InvalidDataException("Release package size does not match its manifest.");
            return new ResponseOwnedStream(
                await response.Content.ReadAsStreamAsync(token), response, headers, StallTimeout, package.Name);
        }
        catch
        {
            response?.Dispose();
            headers.Dispose();
            throw;
        }
    }

    private static async Task ExtractVerifiedAsync(
        Stream source,
        ReleasePackage package,
        string staging,
        IProgress<TransferProgress>? progress,
        CancellationToken token)
    {
        var archivePath = Path.Combine(Path.GetDirectoryName(staging)!, $".{Guid.NewGuid():N}.zip");
        try
        {
            await using (var output = new FileStream(archivePath, FileMode.CreateNew, FileAccess.Write,
                             FileShare.None, 1024 * 1024,
                             FileOptions.Asynchronous | FileOptions.SequentialScan))
            using (var sha = IncrementalHash.CreateHash(HashAlgorithmName.SHA256))
            {
                var buffer = new byte[1024 * 1024];
                long total = 0;
                while (true)
                {
                    var read = await source.ReadAsync(buffer, token);
                    if (read == 0) break;
                    total += read;
                    if (total > package.Size) throw new InvalidDataException("Package exceeds declared size.");
                    sha.AppendData(buffer.AsSpan(0, read));
                    await output.WriteAsync(buffer.AsMemory(0, read), token);
                    progress?.Report(new TransferProgress("Downloading", total, package.Size));
                }
                await output.FlushAsync(token);
                if (total != package.Size) throw new InvalidDataException("Package size mismatch.");
                var observed = Convert.ToHexString(sha.GetHashAndReset()).ToLowerInvariant();
                if (!CryptographicOperations.FixedTimeEquals(
                        Convert.FromHexString(observed), Convert.FromHexString(package.Sha256)))
                    throw new InvalidDataException("Package SHA-256 mismatch.");
            }

            using var archive = ZipFile.OpenRead(archivePath);
            long expanded = 0;
            var expandedTotal = archive.Entries.Sum(item => item.Length);
            if (expandedTotal != package.ExpandedSize)
                throw new InvalidDataException("Expanded package size does not match its manifest.");
            var canonicalRoot = Path.GetFullPath(staging) + Path.DirectorySeparatorChar;
            var destinations = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var entry in archive.Entries.OrderBy(e => e.FullName, StringComparer.Ordinal))
            {
                token.ThrowIfCancellationRequested();
                ValidateArchivePath(entry.FullName);
                var destination = Path.GetFullPath(Path.Combine(staging, entry.FullName));
                if (!destination.StartsWith(canonicalRoot, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Archive entry escapes the version directory.");
                if (!destinations.Add(destination.TrimEnd(Path.DirectorySeparatorChar)))
                    throw new InvalidDataException("Archive contains duplicate Windows paths.");
                if (entry.ExternalAttributes != 0 &&
                    ((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000)
                    throw new InvalidDataException("Symbolic links are not accepted in release packages.");
                if (entry.FullName.EndsWith('/'))
                {
                    Directory.CreateDirectory(destination);
                    continue;
                }
                expanded += entry.Length;
                if (expanded > 16L * 1024 * 1024 * 1024)
                    throw new InvalidDataException("Expanded package exceeds the supported bound.");
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                await using var input = entry.Open();
                await using var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write,
                    FileShare.None, 1024 * 1024, FileOptions.Asynchronous);
                await CopyExactAsync(input, output, entry.Length, entry.FullName, token);
                progress?.Report(new TransferProgress("Installing", expanded, expandedTotal));
            }
        }
        finally
        {
            if (File.Exists(archivePath)) File.Delete(archivePath);
        }
    }

    /// <summary>
    /// Copy exactly the number of bytes the archive's own directory declared for an
    /// entry, refusing both more and fewer.
    ///
    /// A plain CopyToAsync writes whatever the deflate stream yields, which is not
    /// bounded by the declared length at all: the whole-archive budget checked before
    /// this point is built from those same declared lengths, so a package whose entries
    /// decompress to more than they claim would sail past every size check and fill the
    /// disk. The short case matters as much — a truncated entry would otherwise be
    /// hashed into the installed identity as if it were correct, making a damaged
    /// install verify perfectly against its own record of the damage.
    /// </summary>
    private static async Task CopyExactAsync(
        Stream input, Stream output, long declaredLength, string entryName, CancellationToken token)
    {
        var buffer = new byte[128 * 1024];
        long written = 0;
        while (true)
        {
            var read = await input.ReadAsync(buffer, token);
            if (read == 0) break;
            written += read;
            if (written > declaredLength)
                throw new InvalidDataException(
                    $"Archive entry '{entryName}' expands beyond the size it declares.");
            await output.WriteAsync(buffer.AsMemory(0, read), token);
        }
        if (written != declaredLength)
            throw new InvalidDataException(
                $"Archive entry '{entryName}' is shorter than the size it declares.");
    }

    private static void ValidateArchivePath(string path)
    {
        if (path.Contains('\\') || Path.IsPathRooted(path))
            throw new InvalidDataException($"Unsafe archive path: {path}");
        var normalized = path.EndsWith('/') ? path[..^1] : path;
        var reserved = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
        };
        foreach (var segment in normalized.Split('/'))
        {
            if (segment is "" or "." or ".." ||
                segment.EndsWith(' ') || segment.EndsWith('.') ||
                segment.Any(character => character < 32 || "<>:\"|?*".Contains(character)) ||
                reserved.Contains(segment.Split('.')[0]))
                throw new InvalidDataException($"Unsafe archive path: {path}");
        }
    }

    private static void VerifyPayloadShape(string versionRoot)
    {
        var exe = Path.Combine(versionRoot, "OpenBFME.exe");
        var pck = Path.Combine(versionRoot, "OpenBFME.pck");
        if (!File.Exists(exe) || new FileInfo(exe).Length < 64 * 1024)
            throw new InvalidDataException("Installed game executable is missing or truncated.");
        if (!File.Exists(pck) || new FileInfo(pck).Length == 0)
            throw new InvalidDataException("Installed game data file is missing.");
        foreach (var item in Directory.EnumerateFileSystemEntries(versionRoot, "*", SearchOption.AllDirectories))
            if ((File.GetAttributes(item) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Installed version contains a reparse point.");
    }

    public static void VerifyInstalledVersion(
        string versionRoot,
        string? expectedVersion = null,
        string? expectedCommit = null,
        string? expectedPackageSha256 = null)
    {
        VerifyPayloadShape(versionRoot);
        InstalledVersionIdentity.Verify(
            versionRoot, expectedVersion, expectedCommit, expectedPackageSha256);
    }

    private static void SelectVersion(string root, ReleaseManifest manifest)
    {
        var old = InstallState.Load(root);
        var isHighest = old is null || CompareVersions(manifest.Version, old.HighestVersion) > 0;
        InstallState.SaveAtomic(root, new InstallState(
            InstallState.ExpectedSchema,
            manifest.Version,
            old?.CurrentVersion == manifest.Version ? old.PreviousVersion : old?.CurrentVersion,
            manifest.Commit,
            old?.CurrentVersion == manifest.Version ? old.PreviousCommit : old?.Commit,
            isHighest ? manifest.Version : old!.HighestVersion,
            isHighest ? manifest.Commit : old!.HighestCommit));
    }

    private static void PruneObsoleteVersions(string root)
    {
        var gameState = InstallState.Load(root)
            ?? throw new InvalidDataException("Selected game state is missing.");
        var launcherState = LauncherInstallState.Load(root)
            ?? throw new InvalidDataException("Selected launcher state is missing.");
        PruneVersionRoot(
            Path.Combine(root, "versions"),
            new[] { gameState.CurrentVersion, gameState.PreviousVersion });
        PruneVersionRoot(
            Path.Combine(root, "launcher-versions"),
            new[] { launcherState.CurrentVersion, launcherState.PreviousVersion });
    }

    /// <summary>
    /// Delete the version directories no longer reachable from the selection pointers.
    ///
    /// The gate on deleting anything is ownership, not intactness. Requiring a full
    /// hash-for-hash verification before removing an obsolete directory was strictly
    /// worse in both directions: a superseded tree that had picked up any damage —
    /// exactly the tree we want gone — made this throw and blocked every future update,
    /// while re-hashing gigabytes bought nothing, since the very next statement deletes
    /// what was hashed. The directory must still be ours (our identity file, naming
    /// itself), inside this root, and free of reparse points.
    /// </summary>
    private static void PruneVersionRoot(string root, IEnumerable<string?> retainedVersions)
    {
        if (!Directory.Exists(root)) return;
        var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) +
                       Path.DirectorySeparatorChar;
        var retained = retainedVersions
            .Where(value => value is not null)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var directory in Directory.EnumerateDirectories(root))
        {
            var fullDirectory = Path.GetFullPath(directory);
            if (!fullDirectory.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase) ||
                (File.GetAttributes(fullDirectory) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Version retention encountered an unsafe directory.");
            var name = Path.GetFileName(fullDirectory);
            if (retained.Contains(name)) continue;
            if (IsLauncherScratch(name))
            {
                TryRemoveScratch(fullDirectory);
                continue;
            }
            if (!System.Text.RegularExpressions.Regex.IsMatch(
                    name, "^[0-9A-Za-z][0-9A-Za-z._-]{0,63}$"))
                throw new InvalidDataException("Version retention encountered an unknown directory.");
            InstalledVersionIdentity.VerifyOwnership(fullDirectory, name);
            Directory.Delete(fullDirectory, true);
        }
    }

    public static InstallState Rollback(string root)
    {
        var current = InstallState.Load(root) ?? throw new InvalidOperationException("Nothing is installed.");
        if (string.IsNullOrWhiteSpace(current.PreviousVersion))
            throw new InvalidOperationException("No previous version is available.");
        var previousRoot = Path.Combine(root, "versions", current.PreviousVersion);
        VerifyInstalledVersion(previousRoot, current.PreviousVersion, current.PreviousCommit);
        var rolled = new InstallState(InstallState.ExpectedSchema, current.PreviousVersion,
            current.CurrentVersion,
            current.PreviousCommit ?? throw new InvalidDataException("Previous release identity is missing."),
            current.Commit,
            current.HighestVersion,
            current.HighestCommit);
        InstallState.SaveAtomic(root, rolled);
        LauncherInstallState.Rollback(root);
        return rolled;
    }

    /// <summary>
    /// Owns the HTTP response for the lifetime of the body stream, and enforces the
    /// per-read stall budget. A stalled read raises <see cref="TimeoutException"/>
    /// rather than hanging, unless the caller's own token was cancelled — in which case
    /// the cancellation is allowed to propagate so the UI can report it accurately.
    /// </summary>
    private sealed class ResponseOwnedStream(
        Stream inner,
        HttpResponseMessage response,
        CancellationTokenSource requestCancellation,
        TimeSpan stallTimeout,
        string packageName) : Stream
    {
        public override bool CanRead => inner.CanRead;
        public override bool CanSeek => inner.CanSeek;
        public override bool CanWrite => false;
        public override long Length => inner.Length;
        public override long Position { get => inner.Position; set => inner.Position = value; }
        public override void Flush() => inner.Flush();
        public override int Read(byte[] buffer, int offset, int count) => inner.Read(buffer, offset, count);
        public override long Seek(long offset, SeekOrigin origin) => inner.Seek(offset, origin);
        public override void SetLength(long value) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        public override async ValueTask<int> ReadAsync(
            Memory<byte> buffer, CancellationToken cancellationToken = default)
        {
            using var stall = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            stall.CancelAfter(stallTimeout);
            try
            {
                return await inner.ReadAsync(buffer, stall.Token);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                throw new TimeoutException(
                    $"The download of {packageName} stalled: no data arrived for " +
                    $"{stallTimeout.TotalSeconds:0} seconds. The partial download was discarded; " +
                    "check your internet connection and try again.");
            }
        }
        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                inner.Dispose();
                response.Dispose();
                requestCancellation.Dispose();
            }
            base.Dispose(disposing);
        }
        public override async ValueTask DisposeAsync()
        {
            await inner.DisposeAsync();
            response.Dispose();
            requestCancellation.Dispose();
            GC.SuppressFinalize(this);
        }
    }
}
