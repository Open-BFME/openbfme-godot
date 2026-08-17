using System.Diagnostics;

namespace OpenBFME.Launcher;

public sealed class LauncherService
{
    public LauncherOptions Options { get; private set; }
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

    public void SetChannel(string channel)
    {
        channel = channel.Trim().ToLowerInvariant();
        if (!LauncherPreferences.IsChannel(channel))
            throw new ArgumentException("Channel must be stable, playtest, or nightly.", nameof(channel));
        Options = Options with { Channel = channel };
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
        string? contentRootOverride = null,
        bool diagnostics = false)
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
        // packs remain under workspace/content-packs.
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
        // The sim records nothing unless it is asked to; the toggle is how a
        // playtester asks without being told about environment variables. It is
        // passed to the CHILD process only - the launcher never mutates its own
        // environment, so an export made mid-session still describes this run.
        if (diagnostics) start.Environment["OPENBFME_DIAGNOSTICS"] = "1";
        // Recorded from the selection that was just re-validated at the process
        // boundary, so the identity in the bug report is the one Play actually
        // mounted rather than the one the UI believed a minute earlier.
        DiagnosticsLog.CaptureIdentity(selection, Options.InstallRoot, Options.Channel);
        DiagnosticsLog.Event("info", "game.launch", new Dictionary<string, object?>
        {
            ["exe"] = exe,
            ["contentRoot"] = selection.ContentRoot,
            ["contentSource"] = inventory.Source,
            ["activePack"] = selection.ActivePackKey,
            ["supplements"] = selection.SupplementalPackKeys,
            ["diagnostics"] = diagnostics
        });
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
