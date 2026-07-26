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
            var bytes = await response.Content.ReadAsByteArrayAsync(deadline.Token);
            if (bytes.Length > maximumBytes)
                throw new InvalidDataException("Release metadata is too large.");
            return bytes;
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
            VerifyInstalledVersion(final, manifest.Version, manifest.Commit, game.Sha256);
            await InstallLauncherAsync(
                manifest, launcher, fullRoot, progress, cancellationToken, packageStream);
            SelectVersion(fullRoot, manifest);
            PruneObsoleteVersions(fullRoot);
            return;
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
            Directory.Move(staging, final);
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
        var final = Path.Combine(versions, manifest.Version);
        if (Directory.Exists(final))
        {
            VerifyLauncherVersion(final, manifest, package);
            LauncherInstallState.Select(installRoot, manifest, package);
            return;
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
            Directory.Move(staging, final);
            LauncherInstallState.Select(installRoot, manifest, package);
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
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
                await input.CopyToAsync(output, token);
                progress?.Report(new TransferProgress("Installing", expanded, expandedTotal));
            }
        }
        finally
        {
            if (File.Exists(archivePath)) File.Delete(archivePath);
        }
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
            new[] { gameState.CurrentVersion, gameState.PreviousVersion },
            directory => VerifyInstalledVersion(directory, Path.GetFileName(directory)));
        PruneVersionRoot(
            Path.Combine(root, "launcher-versions"),
            new[] { launcherState.CurrentVersion, launcherState.PreviousVersion },
            directory =>
            {
                InstalledVersionIdentity.Verify(directory, Path.GetFileName(directory));
                BundleInventory.Verify(directory);
            });
    }

    private static void PruneVersionRoot(
        string root,
        IEnumerable<string?> retainedVersions,
        Action<string> verifyOwnedVersion)
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
            if (!System.Text.RegularExpressions.Regex.IsMatch(
                    name, "^[0-9A-Za-z][0-9A-Za-z._-]{0,63}$"))
                throw new InvalidDataException("Version retention encountered an unknown directory.");
            verifyOwnedVersion(fullDirectory);
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
