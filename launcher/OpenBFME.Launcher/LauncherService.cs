using System.Diagnostics;

namespace OpenBFME.Launcher;

public sealed class LauncherService
{
    public LauncherOptions Options { get; }
    public ReleaseInstaller Installer { get; } = new();
    public ImporterRunner Importer { get; } = new();
    public GitHubReleaseFeed ReleaseFeed { get; } = new();
    public AllInOneRetailProvisioner RetailProvisioner { get; } = new();

    /// <summary>
    /// One-time notice naming the download host and the legal-right requirement.
    /// Informational: it does not gate the download, it just refuses to let it be silent.
    /// </summary>
    public ThirdPartyDownloadDisclosure DownloadDisclosure { get; }

    public LauncherService(LauncherOptions options)
    {
        Options = options;
        DownloadDisclosure = ThirdPartyDownloadDisclosure.ForInstallRoot(options.InstallRoot);
    }

    public InstallState? Current => InstallState.Load(Options.InstallRoot);

    public string CurrentGamePath()
    {
        var state = Current ?? throw new InvalidOperationException("OpenBFME is not installed.");
        var root = Path.Combine(Options.InstallRoot, "versions", state.CurrentVersion);
        ReleaseInstaller.VerifyInstalledVersion(root, state.CurrentVersion, state.Commit);
        return Path.Combine(root, "OpenBFME.exe");
    }

    /// <summary>
    /// Resolve which content-packs folder Play should mount. Matches
    /// <see cref="ContentPackCatalog.Discover"/> so the UI and the game agree.
    /// </summary>
    public ContentPackInventory DiscoverContent(string? contentRootOverride = null) =>
        ContentPackCatalog.Discover(Options.InstallRoot, contentRootOverride);

    /// <summary>
    /// Same root <see cref="DiscoverContent"/> would show. Override (when set)
    /// always wins — even if empty — so the pack list and Play cannot disagree.
    /// </summary>
    public string ResolveContentRoot(string? contentRootOverride = null)
    {
        var inventory = DiscoverContent(contentRootOverride);
        try
        {
            Directory.CreateDirectory(inventory.ContentRoot);
        }
        catch
        {
            // Override may be on a missing drive; still return the path so the
            // failure is visible at game start rather than silently remapped.
        }
        return inventory.ContentRoot;
    }

    public ContentSelectionValidation ValidateContentSelection(
        string? contentRootOverride = null)
    {
        var root = DiscoverContent(contentRootOverride).ContentRoot;
        return ContentPackCatalog.ValidateSelection(root);
    }

    public void LaunchGame(
        string? bfme2Path = null,
        string? rotwkPath = null,
        string? contentRootOverride = null)
    {
        var exe = CurrentGamePath();
        var inventory = DiscoverContent(contentRootOverride);
        var selection = ContentPackCatalog.ValidateSelection(inventory.ContentRoot);
        if (!selection.Ok)
            throw new InvalidOperationException($"Play unavailable: {selection.Reason}");
        var start = new ProcessStartInfo(exe)
        {
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(exe)!
        };
        start.Environment["OPENBFME_CONTENT"] = selection.ContentRoot;
        // Prefer the paths the player just set in the UI, then discovery. Conversion
        // packs remain under .private/content-packs.
        var bfme2 = RetailDiscovery.IsRetailInstall(bfme2Path)
            ? Path.GetFullPath(bfme2Path!)
            : AllInOneRetailProvisioner.ResolveExisting("bfme2", Options.InstallRoot);
        if (bfme2 is not null) start.Environment["BFME2_INSTALL"] = bfme2;
        var rotwk = RetailDiscovery.IsRetailInstall(rotwkPath)
            ? Path.GetFullPath(rotwkPath!)
            : AllInOneRetailProvisioner.ResolveExisting("rotwk", Options.InstallRoot);
        if (rotwk is not null) start.Environment["ROTWK_INSTALL"] = rotwk;
        // Re-check at the process boundary so a removed/replaced selection cannot
        // race an earlier UI or service-layer verdict.
        selection = ContentPackCatalog.ValidateSelection(selection.ContentRoot);
        if (!selection.Ok)
            throw new InvalidOperationException($"Play unavailable: {selection.Reason}");
        Process.Start(start);
    }

    /// <summary>
    /// Resolve the best signed release-manifest for the configured channel. An explicit
    /// <c>--manifest-url</c> always wins. Otherwise the GitHub release feed is always
    /// consulted — including for stable — so a pre-release-only alpha still surfaces
    /// instead of a permanent 404 from <c>/releases/latest</c>.
    /// </summary>
    public async Task<(Uri ManifestUri, GitHubReleaseFeed.ReleaseCandidate? Candidate)>
        ResolveManifestAsync(CancellationToken cancellationToken)
    {
        if (Options.ManifestUriExplicit && Options.ManifestUri is not null)
            return (Options.ManifestUri, null);

        var candidate = await ReleaseFeed.ResolveAsync(Options.Channel, cancellationToken);
        return (candidate.ManifestUri, candidate);
    }

    public string RetailTargetPath(string game)
    {
        if (Options.RetailInstallRoot is not null)
            return Path.GetFullPath(Path.Combine(Options.RetailInstallRoot, game));
        return AllInOneRetailProvisioner.DefaultInstallPath(Options.InstallRoot, game);
    }

    public Task<AllInOneRetailProvisioner.ProvisionResult> ProvisionRetailAsync(
        string game,
        IProgress<AllInOneRetailProvisioner.ProvisionProgress>? progress,
        CancellationToken cancellationToken) =>
        RetailProvisioner.ProvisionAsync(
            game, RetailTargetPath(game), progress, cancellationToken);
}
