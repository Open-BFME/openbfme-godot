namespace OpenBFME.Launcher;

public sealed record LauncherOptions(
    string Channel,
    Uri? ManifestUri,
    bool ManifestUriExplicit,
    string InstallRoot,
    bool NoUpdate,
    bool VerifyOnly,
    bool Headless,
    string? ImportGame,
    string? RetailPath,
    bool DiscoverRelease,
    bool ProvisionBfme2,
    bool ProvisionRotwk,
    string? RetailInstallRoot)
{
    /// <summary>Flags that take exactly one value.</summary>
    private static readonly HashSet<string> ValueFlags = new(StringComparer.Ordinal)
    {
        "--channel", "--manifest-url", "--install-root", "--bfme2-path", "--rotwk-path",
        "--retail-install-root"
    };

    /// <summary>Flags that take no value.</summary>
    private static readonly HashSet<string> SwitchFlags = new(StringComparer.Ordinal)
    {
        "--import-bfme2", "--import-rotwk", "--no-update", "--verify-only", "--headless",
        "--discover-release", "--provision-bfme2", "--provision-rotwk"
    };

    public static LauncherOptions Parse(IEnumerable<string> arguments)
    {
        var args = arguments.ToArray();

        // Reject unknown flags outright. A mistyped "--chanel playtest" used to fall
        // through to the "stable" default, quietly moving the player to a different
        // channel than they asked for. Silent degradation is the worst outcome on a
        // stranger's machine, so an unrecognised argument stops the launcher instead.
        for (var index = 0; index < args.Length; index++)
        {
            var argument = args[index];
            if (!argument.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException(
                    $"Unexpected argument '{argument}'. Flags must start with '--', " +
                    "and a value flag takes exactly one value.");
            if (SwitchFlags.Contains(argument)) continue;
            if (ValueFlags.Contains(argument))
            {
                index++; // skip its value; Value() validates that one is present
                continue;
            }
            throw new ArgumentException(
                $"Unknown option '{argument}'. Supported options are: " +
                string.Join(", ", ValueFlags.Concat(SwitchFlags).OrderBy(x => x, StringComparer.Ordinal)) + ".");
        }

        string Value(string flag, string fallback)
        {
            var index = Array.IndexOf(args, flag);
            if (index < 0) return fallback;
            if (Array.LastIndexOf(args, flag) != index)
                throw new ArgumentException($"{flag} was supplied more than once.");
            if (index + 1 >= args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException($"{flag} requires a value.");
            return args[index + 1];
        }

        bool Has(string flag) => Array.IndexOf(args, flag) >= 0;

        // Resolve the root first because the persisted preference lives with install
        // state. An explicit CLI channel wins for this process only; parsing never
        // rewrites the preference, so operator/test invocations cannot clobber the
        // player's next double-click choice.
        var root = Path.GetFullPath(Value(
            "--install-root",
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenBFME")));
        var persistedChannel = LauncherPreferences.LoadChannel(root);
        var channel = Value("--channel", persistedChannel ?? ReleaseSource.DefaultChannel)
            .Trim().ToLowerInvariant();
        if (!LauncherPreferences.IsChannel(channel))
            throw new ArgumentException("--channel must be stable, playtest, or nightly.");

        // Every channel gets a feed. Previously only "stable" had a default manifest
        // URL, so "Check for update" silently did nothing on the playtest channel —
        // which is exactly the channel playtesters are on. The default is still the
        // "latest" asset URL; GitHubReleaseFeed upgrades that to a pre-release scan
        // when the channel is playtest/nightly or when latest 404s.
        var manifestExplicit = Array.IndexOf(args, "--manifest-url") >= 0;
        var manifestText = Value("--manifest-url", ReleaseSource.LatestManifestUri.AbsoluteUri);
        Uri? manifestUri = null;
        if (manifestText.Length > 0)
        {
            if (!Uri.TryCreate(manifestText, UriKind.Absolute, out manifestUri))
                throw new ArgumentException("--manifest-url must be an absolute URL.");
            ReleaseUriPolicy.Validate(manifestUri);
        }

        var bfme2 = Has("--import-bfme2");
        var rotwk = Has("--import-rotwk");
        if (bfme2 && rotwk) throw new ArgumentException("Select only one import game per run.");
        var game = bfme2 ? "bfme2" : rotwk ? "rotwk" : null;
        var retail = game == "bfme2" ? Value("--bfme2-path", "") :
            game == "rotwk" ? Value("--rotwk-path", "") : "";
        if (game is not null && retail.Length == 0)
            throw new ArgumentException($"--import-{game} requires its retail path flag.");

        var retailInstallRootText = Value("--retail-install-root", "");
        string? retailInstallRoot = retailInstallRootText.Length == 0
            ? null
            : Path.GetFullPath(retailInstallRootText);
        return new LauncherOptions(channel, manifestUri, manifestExplicit, root, Has("--no-update"),
            Has("--verify-only"), Has("--headless"), game,
            retail.Length == 0 ? null : Path.GetFullPath(retail),
            Has("--discover-release"),
            Has("--provision-bfme2"),
            Has("--provision-rotwk"),
            retailInstallRoot);
    }
}

public static class ReleaseUriPolicy
{
    private static readonly HashSet<string> RedirectHosts = new(StringComparer.OrdinalIgnoreCase)
    {
        "objects.githubusercontent.com",
        "github-releases.githubusercontent.com",
        "release-assets.githubusercontent.com"
    };

    public static void Validate(Uri uri)
    {
        ValidateCommon(uri);
        if (!uri.DnsSafeHost.Equals(ReleaseSource.Host, StringComparison.OrdinalIgnoreCase) ||
            !uri.AbsolutePath.StartsWith(ReleaseSource.ReleasePathPrefix, StringComparison.OrdinalIgnoreCase) ||
            !string.IsNullOrEmpty(uri.Query))
            throw new InvalidOperationException(
                $"Release URL does not belong to the approved repository ({ReleaseSource.Repository}).");
    }

    public static void ValidateResponse(Uri uri)
    {
        ValidateCommon(uri);
        if (uri.DnsSafeHost.Equals(ReleaseSource.Host, StringComparison.OrdinalIgnoreCase))
        {
            Validate(uri);
            return;
        }
        if (!RedirectHosts.Contains(uri.DnsSafeHost))
            throw new InvalidOperationException("Release redirect host is not approved.");
    }

    private static void ValidateCommon(Uri uri)
    {
        if (uri.Scheme != Uri.UriSchemeHttps || uri.UserInfo.Length != 0 ||
            !string.IsNullOrEmpty(uri.Fragment))
            throw new InvalidOperationException("Release URLs must use credential-free HTTPS.");
    }
}
