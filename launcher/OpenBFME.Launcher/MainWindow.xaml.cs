using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace OpenBFME.Launcher;

public sealed record NewsItem(string Tag, string Title, string Body);

/// <summary>One row in the main-panel asset pack / mod list.</summary>
public sealed record PackListItem(
    string Role,
    string Title,
    string Subtitle,
    string KindLabel,
    string RelativeKey,
    Visibility SetActiveVisible);

public partial class MainWindow : Window
{
    private readonly LauncherService _service;
    private CancellationTokenSource? _operation;
    private GitHubReleaseFeed.ReleaseCandidate? _releaseCandidate;
    private readonly ObservableCollection<NewsItem> _news = new();
    private readonly ObservableCollection<PackListItem> _packs = new();
    private ContentPackInventory? _contentInventory;

    private static readonly SolidColorBrush OkBrush = Freeze(
        new SolidColorBrush(Color.FromRgb(0x8F, 0xB5, 0x7A)));
    private static readonly SolidColorBrush WarnBrush = Freeze(
        new SolidColorBrush(Color.FromRgb(0x9A, 0x9A, 0xA3)));

    /// <summary>
    /// Thrown when the launcher cannot start at all. <see cref="App"/> reports it and
    /// exits rather than showing a window that would misrepresent what is configured.
    /// </summary>
    public sealed class StartupException(string message) : Exception(message);

    public MainWindow()
    {
        InitializeComponent();
        // Manual + primary work area: CenterScreen follows the cursor/last-focus
        // monitor and often opens on a side display. Re-apply after HWND creation
        // because Windows may still place the first Show() on a non-primary screen.
        WindowStartupLocation = WindowStartupLocation.Manual;
        CenterOnPrimaryMonitor();
        // Re-assert after HWND and first frame — Windows can still place the
        // initial Show() on the monitor where the process was started / focused.
        SourceInitialized += (_, _) => CenterOnPrimaryMonitor();
        ContentRendered += (_, _) => CenterOnPrimaryMonitor();

        LauncherOptions options;
        try
        {
            options = LauncherOptions.Parse(Environment.GetCommandLineArgs().Skip(1));
        }
        catch (Exception error)
        {
            throw new StartupException(
                $"{error.Message}\n\nOpenBFME did not start because its command line could " +
                "not be understood. Correct the arguments (or remove them all to use the " +
                "defaults) and try again.");
        }

        _service = new LauncherService(options);
        NewsFeed.ItemsSource = _news;
        PacksFeed.ItemsSource = _packs;
        ChannelText.Text = PlatformCompat.IsWine
            ? $"{options.Channel} · {ReleaseSource.Repository} · Wine"
            : $"{options.Channel} · {ReleaseSource.Repository}";
        LifecycleLog.Write("window",
            $"ctor paint begin host={PlatformCompat.HostDescription} wine={PlatformCompat.IsWine}");
        RefreshState();
        PrefillRetailPaths();
        RefreshAioStatus();
        RefreshContentPacks();
        RefreshChecklist();
        RefreshNewsFeed();
        if (PlatformCompat.IsWine)
        {
            StatusText.Text =
                "Wine mode — use a self-contained win-x64 build; paste paths if Browse fails.";
        }
        LifecycleLog.Write("window", "ctor paint done");
        Closed += (_, _) => LifecycleLog.Write("window", "closed");
        Loaded += OnLoaded;
    }

    private void Settings_Click(object sender, RoutedEventArgs e)
    {
        SettingsOverlay.Visibility = SettingsOverlay.Visibility == Visibility.Visible
            ? Visibility.Collapsed
            : Visibility.Visible;
    }

    /// <summary>
    /// Place the window in the center of the primary monitor's usable area
    /// (excludes taskbar). Used instead of CenterScreen so multi-monitor desktops
    /// do not open the launcher on a side display.
    /// </summary>
    private void CenterOnPrimaryMonitor()
    {
        try
        {
            WindowStartupLocation = WindowStartupLocation.Manual;

            var width = ActualWidth > 0 ? ActualWidth : Width;
            var height = ActualHeight > 0 ? ActualHeight : Height;
            if (double.IsNaN(width) || width <= 0) width = 1100;
            if (double.IsNaN(height) || height <= 0) height = 680;

            // SystemParameters.WorkArea is already the primary monitor in WPF DIPs.
            // Avoid Win32 pixel→DIP conversion: under PerMonitorV2 a first HWND on a
            // secondary display can scale the primary rect with the wrong DPI.
            var work = SystemParameters.WorkArea;
            Left = work.Left + Math.Max(0, (work.Width - width) / 2);
            Top = work.Top + Math.Max(0, (work.Height - height) / 2);

            if (Left + width > work.Right) Left = Math.Max(work.Left, work.Right - width);
            if (Top + height > work.Bottom) Top = Math.Max(work.Top, work.Bottom - height);
            if (Left < work.Left) Left = work.Left;
            if (Top < work.Top) Top = work.Top;

            LifecycleLog.Write("window",
                $"centered primary left={Left:0} top={Top:0} " +
                $"work=({work.Left:0},{work.Top:0},{work.Width:0}x{work.Height:0})");
        }
        catch (Exception error)
        {
            LifecycleLog.Write("window", $"center primary failed: {error.Message}");
            // Last resort — still better than leaving Manual at (0,0) on a side panel.
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
        }
    }

    private void RefreshNewsFeed()
    {
        _news.Clear();
        if (_releaseCandidate is not null)
        {
            _news.Add(new NewsItem(
                _releaseCandidate.PreRelease ? "PRE-RELEASE" : "PATCH",
                _releaseCandidate.Tag,
                string.IsNullOrWhiteSpace(_releaseCandidate.Name)
                    ? $"Available on {ReleaseSource.Repository} ({_releaseCandidate.Source}). Use Update to install."
                    : $"{_releaseCandidate.Name} — tap Update in the top bar to install."));
        }
        else
        {
            _news.Add(new NewsItem(
                "STATUS",
                "No engine build published yet",
                $"When {ReleaseSource.Repository} publishes a signed release, Update will pick it up automatically."));
        }

        var bfme = ResolvePath("bfme2", BfmePath.Text);
        var rotwk = ResolvePath("rotwk", RotwkPath.Text);
        if (bfme is null || rotwk is null)
        {
            _news.Add(new NewsItem(
                "SETUP",
                "Game files needed",
                "Open Settings to download BFME II and RotWK (same workshop path as All-in-One), or point at installs you already own."));
        }
        else
        {
            _news.Add(new NewsItem(
                "READY",
                "Game files found",
                "BFME II and RotWK installs are available. Convert from Settings if you need a fresh content pack."));
        }

        if (_contentInventory is { Packs.Count: > 0 } inv)
        {
            var active = inv.ActivePackKey is null
                ? "No active selection yet — use Set active on a pack."
                : $"Active: {inv.ActivePackKey.Split('/')[0]} (+ {inv.SupplementalPackKeys.Count} supplements).";
            _news.Add(new NewsItem(
                "PACKS",
                $"{inv.Packs.Count} asset pack(s) · {inv.Mods.Count} mod(s)",
                $"{active} Source: {inv.Source} → {inv.ContentRoot}"));
        }
        else
        {
            _news.Add(new NewsItem(
                "PACKS",
                "No converted asset packs yet",
                "Convert BFME II / RotWK from Settings, or set Content root to a folder that already has selection.json."));
        }

        _news.Add(new NewsItem(
            "NOTE",
            "Code-only engine",
            "OpenBFME never ships EA game data. Converted packs stay private on this machine."));
    }

    private string? ContentRootOverrideOrNull()
    {
        var text = ContentRootPath.Text?.Trim() ?? "";
        return text.Length == 0 ? null : text;
    }

    private void RefreshContentPacks()
    {
        try
        {
            var inventory = _service.DiscoverContent(ContentRootOverrideOrNull());
            _contentInventory = inventory;

            if (string.IsNullOrWhiteSpace(ContentRootPath.Text) ||
                !Directory.Exists(ContentRootPath.Text.Trim()))
            {
                ContentRootPath.Text = inventory.ContentRoot;
            }

            ContentRootNote.Text =
                $"Detected via {inventory.Source}. Play sets OPENBFME_CONTENT to this folder.";
            PacksSourceText.Text = inventory.Diagnostic is not null
                ? inventory.Diagnostic
                : $"{inventory.Packs.Count} pack(s), {inventory.Mods.Count} mod(s) · {inventory.Source}";

            PacksSettingsSummary.Text =
                inventory.ActivePackKey is null
                    ? $"No active selection in {inventory.ContentRoot}."
                    : $"Active {inventory.ActivePackKey}" +
                      (inventory.SupplementalPackKeys.Count > 0
                          ? $" with {inventory.SupplementalPackKeys.Count} supplement(s)."
                          : ".");

            _packs.Clear();
            foreach (var pack in inventory.Packs)
            {
                var kind = pack.IsRetailDerived ? "retail pack" : "pack";
                var canActivate = pack.Role != "Active" &&
                                  pack.RelativeKey.Contains('/', StringComparison.Ordinal);
                _packs.Add(new PackListItem(
                    pack.Role.ToUpperInvariant(),
                    pack.Title,
                    $"{pack.Id} · v{pack.Version}" +
                    (pack.Role == "Supplement" ? " · loaded with active" : ""),
                    kind,
                    pack.RelativeKey,
                    canActivate ? Visibility.Visible : Visibility.Collapsed));
            }

            foreach (var mod in inventory.Mods)
            {
                _packs.Add(new PackListItem(
                    "MOD",
                    mod.Title,
                    $"{mod.Id} · v{mod.Version} · priority {mod.Priority}",
                    "loose mod",
                    mod.RelativeKey,
                    Visibility.Collapsed));
            }

            if (_packs.Count == 0)
            {
                _packs.Add(new PackListItem(
                    "EMPTY",
                    "No packs installed",
                    "Convert from Settings, or drop a pack with pack.json under the content root.",
                    "—",
                    "",
                    Visibility.Collapsed));
            }
        }
        catch (Exception error)
        {
            LifecycleLog.Write("window", $"content pack scan failed: {error.Message}");
            PacksSourceText.Text = error.Message;
            PacksSettingsSummary.Text = error.Message;
            _packs.Clear();
            _packs.Add(new PackListItem(
                "ERROR",
                "Could not scan packs",
                error.Message,
                "—",
                "",
                Visibility.Collapsed));
        }
    }


    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        LifecycleLog.Write("window", "loaded");
        // Defer network until the window has painted and the message loop is idle.
        // Doing GitHub work inside Loaded (even async) has made the UI look frozen
        // and, with some host shells, contributed to "it died" reports.
        Dispatcher.BeginInvoke(async () =>
        {
            try
            {
                LifecycleLog.Write("window", "idle network begin");
                await RefreshReleaseCandidateAsync();
                await RunStartupFlagsAsync();
                RefreshChecklist();
                LifecycleLog.Write("window", "idle network done");
            }
            catch (Exception error)
            {
                LifecycleLog.Write("window", $"idle network fault: {error}");
                ShowError(error, modal: false);
            }
        }, System.Windows.Threading.DispatcherPriority.ApplicationIdle);
    }

    /// <summary>
    /// Install-state load is strict (good for install/rollback). The UI must never let
    /// that throw out of paint/checklist paths — Sol review: that was a primary
    /// dialog-then-exit and click-then-die root cause.
    /// </summary>
    private InstallState? TryCurrent(out Exception? error)
    {
        try
        {
            error = null;
            return _service.Current;
        }
        catch (Exception ex)
        {
            error = ex;
            return null;
        }
    }

    private void RefreshState()
    {
        var state = TryCurrent(out var error);
        if (error is not null)
        {
            VersionText.Text = "Install state unreadable";
            PlayButton.IsEnabled = false;
            StatusText.Text = error.Message;
            DetailText.Text = error.Message;
            DetailPanel.Visibility = Visibility.Visible;
            RefreshChecklist();
            return;
        }
        VersionText.Text = state is null ? "Not installed" : $"Version {state.CurrentVersion}";
        PlayButton.IsEnabled = state is not null;
        RefreshChecklist();
    }

    private void RefreshChecklist()
    {
        try
        {
            var state = TryCurrent(out var stateError);
            var engine = state is not null;
            CheckEngineText.Text = engine
                ? $"Engine · {state!.CurrentVersion}"
                : stateError is null ? "Engine · missing" : "Engine · error";
            CheckEngineText.Foreground = engine ? OkBrush : WarnBrush;

            if (_releaseCandidate is not null)
            {
                CheckReleaseText.Text = $"Release · {_releaseCandidate.Tag}";
                CheckReleaseText.Foreground = OkBrush;
            }
            else
            {
                CheckReleaseText.Text = "Release · none";
                CheckReleaseText.Foreground = WarnBrush;
            }

            SetRetailChecklist("bfme2", BfmePath.Text, "BFME II", CheckBfmeText);
            SetRetailChecklist("rotwk", RotwkPath.Text, "RotWK", CheckRotwkText);

            var packCount = (_contentInventory?.Packs.Count ?? 0) + (_contentInventory?.Mods.Count ?? 0);
            var active = _contentInventory?.ActivePackKey;
            if (packCount == 0)
            {
                CheckPacksText.Text = "Packs · none";
                CheckPacksText.Foreground = WarnBrush;
            }
            else if (active is not null)
            {
                var shortId = active.Split('/')[0];
                CheckPacksText.Text = $"Packs · {packCount} · {shortId}";
                CheckPacksText.Foreground = OkBrush;
            }
            else
            {
                CheckPacksText.Text = $"Packs · {packCount}";
                CheckPacksText.Foreground = WarnBrush;
            }

            try { RefreshNewsFeed(); } catch { /* never break checklist */ }
        }
        catch (Exception error)
        {
            // Checklist must never kill the process. Surface and continue.
            try
            {
                StatusText.Text = $"Status panel error: {error.Message}";
            }
            catch { /* ignore */ }
        }
    }

    private static SolidColorBrush Freeze(SolidColorBrush brush)
    {
        if (brush.CanFreeze) brush.Freeze();
        return brush;
    }

    private string? ResolvePath(string game, string boxText)
    {
        if (RetailDiscovery.IsRetailInstall(game, boxText))
            return Path.GetFullPath(boxText);
        return AllInOneRetailProvisioner.ResolveExisting(game, _service.Options.InstallRoot);
    }

    private void SetRetailChecklist(string game, string path, string label,
        System.Windows.Controls.TextBlock text)
    {
        var assessment = RetailDiscovery.Assess(game, path);
        var valid = assessment is RetailInstallAssessment.ValidBfme2 or RetailInstallAssessment.ValidRotwk;
        var state = assessment switch
        {
            RetailInstallAssessment.WrongEdition => "wrong edition",
            RetailInstallAssessment.Incomplete => "incomplete",
            _ when valid => "ready",
            _ => "needed",
        };
        text.Text = $"{label} · {state}";
        text.Foreground = valid ? OkBrush : WarnBrush;
    }

    private void PrefillRetailPaths()
    {
        Prefill("bfme2", BfmePath, BfmeDetectText, "BFME II");
        Prefill("rotwk", RotwkPath, RotwkDetectText, "Rise of the Witch-king");

        void Prefill(string game, System.Windows.Controls.TextBox box,
            System.Windows.Controls.TextBlock note, string label)
        {
            string? found;
            try
            {
                found = AllInOneRetailProvisioner.ResolveExisting(game, _service.Options.InstallRoot)
                        ?? RetailDiscovery.Discover(game);
            }
            catch (Exception error)
            {
                note.Text = error.Message;
                return;
            }
            if (found is not null)
            {
                box.Text = found;
                note.Text = $"Found your {label} installation here.";
                return;
            }
            box.Text = _service.RetailTargetPath(game);
            note.Text = $"No {label} installation found. " +
                        $"Click Download to fetch game files via the All-in-One workshop " +
                        $"(or Browse to a folder that contains {RetailDiscovery.InstallMarker}).";
        }
    }

    private void RefreshAioStatus()
    {
        var exe = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "BFME All In One Launcher", "AllInOneLauncher.exe");
        if (File.Exists(exe))
        {
            AioStatusText.Text =
                "All-in-One Launcher is installed. OpenBFME can download BFME II / RotWK " +
                "silently using the same workshop host, or you can manage installs there.";
            OpenAioButton.IsEnabled = true;
        }
        else
        {
            AioStatusText.Text =
                "All-in-One Launcher was not found under %APPDATA%. You can still download " +
                "BFME II / RotWK from here (same workshop API), or install the community " +
                "launcher from bfmeladder.com.";
            OpenAioButton.IsEnabled = false;
        }
    }

    private async Task RefreshReleaseCandidateAsync()
    {
        ReleaseCandidateText.Text = "Checking GitHub…";
        try
        {
            var (uri, candidate) = await _service.ResolveManifestAsync(CancellationToken.None);
            _releaseCandidate = candidate;
            if (candidate is not null)
            {
                ReleaseCandidateText.Text =
                    $"{candidate.Tag}" +
                    (candidate.PreRelease ? " · pre-release" : "") +
                    $" · ready to install";
            }
            else
            {
                ReleaseCandidateText.Text = "";
            }
        }
        catch (Exception error)
        {
            _releaseCandidate = null;
            ReleaseCandidateText.Text = "";
            StatusText.Text = error.Message;
        }
        RefreshChecklist();
    }

    private async Task RunStartupFlagsAsync()
    {
        // Only run work the player explicitly asked for on the command line.
        // GUI open no longer auto-updates (that path was a stability hazard).
        if (_service.Options.ProvisionBfme2)
            await ProvisionAsync("bfme2");
        if (_service.Options.ProvisionRotwk)
            await ProvisionAsync("rotwk");

        if (_service.Options.ImportGame is not null && _service.Options.RetailPath is not null)
            await ImportAsync(_service.Options.ImportGame, _service.Options.RetailPath);

        // Explicit headless/CLI update only. GUI players use "Check for update".
        if (!_service.Options.NoUpdate &&
            (_service.Options.Headless || _service.Options.DiscoverRelease) &&
            _service.Options.ImportGame is null)
            await UpdateAsync(quietIfMissing: true);

        if (!_service.Options.VerifyOnly) return;

        try
        {
            if (TryCurrent(out _) is null)
            {
                StatusText.Text = "Nothing is installed yet, so there is no version to verify.";
                return;
            }
            ReleaseInstaller.VerifyInstalledVersion(Path.GetDirectoryName(_service.CurrentGamePath())!);
            StatusText.Text = "Installed version verified.";
            ClearDetail();
        }
        catch (Exception error)
        {
            ShowError(error, "The installed version failed verification. It may be damaged or " +
                             "modified. Use \"Roll back\", or check for an update to reinstall it.");
        }
    }

    private async void Update_Click(object sender, RoutedEventArgs e) => await UpdateAsync();

    private async void SetupAll_Click(object sender, RoutedEventArgs e)
    {
        await RunOperationAsync(async token =>
        {
            StatusText.Text = "Checking GitHub for a release candidate…";
            try
            {
                await UpdateCoreAsync(token, quietIfMissing: false);
            }
            catch (Exception error) when (IsMissingRelease(error))
            {
                StatusText.Text =
                    "No signed OpenBFME release is published yet — continuing with game files. " +
                    error.Message;
            }

            if (!RetailDiscovery.IsRetailInstall("bfme2", BfmePath.Text))
                await ProvisionCoreAsync("bfme2", token);
            else
                StatusText.Text = "BFME II already present.";

            if (!RetailDiscovery.IsRetailInstall("rotwk", RotwkPath.Text))
                await ProvisionCoreAsync("rotwk", token);
            else
                StatusText.Text = "RotWK already present.";

            PrefillRetailPaths();
            RefreshChecklist();
            StatusText.Text =
                "Setup finished. Convert assets when ready, then Play once an engine build is installed.";
            Progress.Value = 100;
        });
    }

    private async Task UpdateAsync(bool quietIfMissing = false)
    {
        await RunOperationAsync(token => UpdateCoreAsync(token, quietIfMissing));
    }

    private async Task UpdateCoreAsync(CancellationToken token, bool quietIfMissing)
    {
        StatusText.Text = "Checking GitHub for a release candidate…";
        Uri manifestUri;
        GitHubReleaseFeed.ReleaseCandidate? candidate;
        try
        {
            (manifestUri, candidate) = await _service.ResolveManifestAsync(token);
            _releaseCandidate = candidate;
            if (candidate is not null)
            {
                ReleaseCandidateText.Text =
                    $"{candidate.Tag}" +
                    (candidate.PreRelease ? " · pre-release" : "") +
                    " · ready to install";
            }
        }
        catch (Exception error) when (quietIfMissing && IsMissingRelease(error))
        {
            ReleaseCandidateText.Text = "";
            StatusText.Text = error.Message;
            RefreshChecklist();
            return;
        }

        StatusText.Text = "Fetching signed release manifest…";
        var manifest = await _service.Installer.FetchManifestAsync(manifestUri, token);
        var progress = new Progress<TransferProgress>(item =>
        {
            Progress.Value = item.Percent;
            StatusText.Text = $"{item.Phase} — {item.Percent:0}%";
        });
        await _service.Installer.InstallAsync(
            manifest, _service.Options.InstallRoot, progress, token,
            expectedChannel: _service.Options.Channel);
        StatusText.Text = $"OpenBFME {manifest.Version} is ready.";
        RefreshState();
    }

    private static bool IsMissingRelease(Exception error)
    {
        if (error is TimeoutException) return false;
        var message = error.Message;
        return message.Contains("No GitHub Releases exist yet", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("No stable release candidate", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("No playtest release candidate", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("No nightly release candidate", StringComparison.OrdinalIgnoreCase) ||
               (message.Contains("No ", StringComparison.Ordinal) &&
                message.Contains("release candidate with a release-manifest.json", StringComparison.OrdinalIgnoreCase));
    }

    private async void DownloadBfme_Click(object sender, RoutedEventArgs e) =>
        await ProvisionAsync("bfme2");

    private async void DownloadRotwk_Click(object sender, RoutedEventArgs e) =>
        await ProvisionAsync("rotwk");

    private async Task ProvisionAsync(string game)
    {
        await RunOperationAsync(token => ProvisionCoreAsync(game, token));
    }

    private async Task ProvisionCoreAsync(string game, CancellationToken token)
    {
        var label = game == "bfme2" ? "BFME II" : "RotWK";

        // Where the bytes come from, and that a licence is the player's responsibility,
        // is said once — before the first download on this machine, not buried in a
        // settings page nobody opens.
        _service.DownloadDisclosure.ShowOnce(text => MessageBox.Show(
            this, text, "Third-party game files",
            MessageBoxButton.OK, MessageBoxImage.Information));

        StatusText.Text = $"Downloading {label} via All-in-One workshop…";
        var progress = new Progress<AllInOneRetailProvisioner.ProvisionProgress>(item =>
        {
            Progress.Value = item.Percent;
            StatusText.Text = $"{item.Phase}: {item.Message}";
        });
        var result = await _service.ProvisionRetailAsync(game, progress, token);
        if (game == "bfme2")
        {
            BfmePath.Text = result.InstallPath;
            BfmeDetectText.Text =
                $"Downloaded {result.PackageName} ({result.FilesInstalled} files, " +
                $"{result.FilesSkipped} already present).";
        }
        else
        {
            RotwkPath.Text = result.InstallPath;
            RotwkDetectText.Text =
                $"Downloaded {result.PackageName} ({result.FilesInstalled} files, " +
                $"{result.FilesSkipped} already present).";
        }
        Progress.Value = 100;
        StatusText.Text = $"{label} is ready at {result.InstallPath}.";
        ClearDetail();
        RefreshChecklist();
    }

    private async void ImportBfme_Click(object sender, RoutedEventArgs e) =>
        await ImportAsync("bfme2", BfmePath.Text);

    private async void ImportRotwk_Click(object sender, RoutedEventArgs e) =>
        await ImportAsync("rotwk", RotwkPath.Text);

    private void BrowseBfme_Click(object sender, RoutedEventArgs e) =>
        Browse("bfme2", BfmePath, BfmeDetectText, "Select your Battle for Middle-earth II folder");

    private void BrowseRotwk_Click(object sender, RoutedEventArgs e) =>
        Browse("rotwk", RotwkPath, RotwkDetectText, "Select your Rise of the Witch-king folder");

    private void OpenAio_Click(object sender, RoutedEventArgs e)
    {
        var exe = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "BFME All In One Launcher", "AllInOneLauncher.exe");
        if (!File.Exists(exe))
        {
            ShowError(new FileNotFoundException(
                "All-in-One Launcher was not found.", exe));
            return;
        }
        try
        {
            Process.Start(new ProcessStartInfo(exe) { UseShellExecute = true });
            StatusText.Text = PlatformCompat.IsWine
                ? "Launched All-in-One under Wine. UAC elevation is often unavailable — install games via Download here if it fails."
                : "Opened the All-in-One Launcher. Approve UAC if Windows asks.";
        }
        catch (Exception error)
        {
            ShowError(error,
                PlatformCompat.IsWine
                    ? "Could not start All-in-One under this Wine prefix. Prefer the Download buttons in this launcher for BFME II / RotWK."
                    : "Windows could not start the All-in-One Launcher. It usually needs administrator approval (UAC).");
        }
    }

    private void Browse(string game, System.Windows.Controls.TextBox box,
        System.Windows.Controls.TextBlock note, string title)
    {
        try
        {
            var initial = box.Text.Length > 0 ? box.Text : null;
            var folder = PlatformCompat.PickFolder(title, initial);
            if (folder is null) return;
            box.Text = folder;
            note.Text = RetailDiscovery.ExplainRejection(game, folder)
                        ?? "This folder looks like a valid game installation.";
            RefreshChecklist();
        }
        catch (Exception error)
        {
            ShowError(error,
                "Folder picker failed. Paste the full path to the game install " +
                "(the folder that contains game.dat) into the path box.");
        }
    }

    private async Task ImportAsync(string game, string path)
    {
        var rejection = RetailDiscovery.ExplainRejection(game, path);
        if (rejection is not null)
        {
            ShowError(new InvalidOperationException(rejection));
            return;
        }

        var previousGamePackKeys = (_contentInventory?.Packs ?? Array.Empty<ContentPackEntry>())
            .Where(pack => pack.Id.StartsWith(game + "-", StringComparison.OrdinalIgnoreCase))
            .Select(pack => pack.RelativeKey)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        await RunOperationAsync(async token =>
        {
            StatusText.Text = "Preparing conversion…";
            var progress = new Progress<ImportProgress>(item =>
            {
                if (item.Percent is double value) Progress.Value = value;
                StatusText.Text = $"{item.Phase}: {item.Message}";
            });
            var stateRoot = Path.Combine(_service.Options.InstallRoot, ".private", "retail-work");
            var code = await _service.Importer.RunAsync(AppContext.BaseDirectory, game, path,
                stateRoot, progress, token);
            if (code != 0)
                throw new InvalidOperationException(
                    $"The conversion failed (exit code {code}). The messages above show which " +
                    "step failed. Your game installation was not modified — the importer only " +
                    "reads from it.");
            Progress.Value = 100;
            ClearDetail();
            RefreshContentPacks();
            RefreshChecklist();
            var gamePacks = _contentInventory?.Packs
                .Where(pack => pack.Id.StartsWith(game + "-", StringComparison.OrdinalIgnoreCase))
                .ToArray() ?? Array.Empty<ContentPackEntry>();
            var newlyPublished = gamePacks
                .Where(pack => !previousGamePackKeys.Contains(pack.RelativeKey))
                .ToArray();
            var activeKey = _contentInventory?.ActivePackKey;
            var activeForGame = activeKey is not null &&
                                (newlyPublished.Length > 0
                                    ? newlyPublished.Any(pack => pack.RelativeKey.Equals(
                                        activeKey, StringComparison.OrdinalIgnoreCase))
                                    : gamePacks.Length == 1 && gamePacks[0].RelativeKey.Equals(
                                        activeKey, StringComparison.OrdinalIgnoreCase));
            var label = game == "bfme2" ? "BFME II" : "RotWK Angmar";
            StatusText.Text = activeForGame
                ? $"{label} assets published; the active selection was unchanged."
                : $"{label} assets published but not active. Use Set active before Play.";
        });
    }

    private void RefreshPacks_Click(object sender, RoutedEventArgs e)
    {
        RefreshContentPacks();
        RefreshChecklist();
        StatusText.Text = _contentInventory is null
            ? "Pack scan finished."
            : $"Found {_contentInventory.Packs.Count} pack(s) and {_contentInventory.Mods.Count} mod(s).";
    }

    private void SetActivePack_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (sender is not Button { Tag: string key } || string.IsNullOrWhiteSpace(key))
                return;
            var root = ContentRootOverrideOrNull()
                       ?? _contentInventory?.ContentRoot
                       ?? ContentPackCatalog.DefaultContentRoot(_service.Options.InstallRoot);
            ContentPackCatalog.SelectActivePack(root, key);
            ContentRootPath.Text = root;
            RefreshContentPacks();
            RefreshChecklist();
            StatusText.Text = $"Active pack set to {key.Split('/')[0]}.";
            ClearDetail();
        }
        catch (Exception error)
        {
            ShowError(error, "Could not update selection.json for that pack.");
        }
    }

    private void BrowseContentRoot_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var folder = PlatformCompat.PickFolder(
                "Select content-packs folder (contains selection.json or pack bundles)",
                ContentRootPath.Text);
            if (folder is null) return;
            ContentRootPath.Text = folder;
            RefreshContentPacks();
            RefreshChecklist();
        }
        catch (Exception error)
        {
            ShowError(error, "Folder picker failed. Paste the content-packs path into the box.");
        }
    }

    private void OpenContentRoot_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var root = ContentRootOverrideOrNull()
                       ?? _contentInventory?.ContentRoot
                       ?? ContentPackCatalog.DefaultContentRoot(_service.Options.InstallRoot);
            Directory.CreateDirectory(root);
            OpenFolder(root);
            StatusText.Text = $"Opened {root}";
        }
        catch (Exception error) { ShowError(error); }
    }

    private void OpenMods_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var mods = ContentPackCatalog.EnsureModsDirectory(_service.Options.InstallRoot);
            OpenFolder(mods);
            StatusText.Text =
                $"Mods folder: {mods}. Drop a folder with pack.json here, then Rescan.";
            RefreshContentPacks();
        }
        catch (Exception error) { ShowError(error); }
    }

    private void OpenInstall_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Directory.CreateDirectory(_service.Options.InstallRoot);
            OpenFolder(_service.Options.InstallRoot);
        }
        catch (Exception error) { ShowError(error); }
    }

    private void OpenGameFolder_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var exe = _service.CurrentGamePath();
            var folder = Path.GetDirectoryName(exe)
                         ?? throw new InvalidOperationException("Installed game folder is unknown.");
            OpenFolder(folder);
        }
        catch (Exception error)
        {
            ShowError(error, "Install an engine build first (Update / Get ready), then open the game folder.");
        }
    }

    private void VerifyEngine_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (TryCurrent(out _) is null)
            {
                StatusText.Text = "Nothing is installed yet, so there is no version to verify.";
                return;
            }
            ReleaseInstaller.VerifyInstalledVersion(Path.GetDirectoryName(_service.CurrentGamePath())!);
            StatusText.Text = "Installed version verified.";
            ClearDetail();
        }
        catch (Exception error)
        {
            ShowError(error, "The installed version failed verification. It may be damaged or " +
                             "modified. Use \"Roll back\", or check for an update to reinstall it.");
        }
    }

    private static void OpenFolder(string path)
    {
        Process.Start(new ProcessStartInfo
        {
            FileName = path,
            UseShellExecute = true
        });
    }

    private void Play_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var content = ContentRootOverrideOrNull();
            _service.LaunchGame(BfmePath.Text, RotwkPath.Text, content);
            var mounted = _service.ResolveContentRoot(content);
            StatusText.Text = $"OpenBFME started · content {mounted}";
            ClearDetail();
        }
        catch (Exception error) { ShowError(error); }
    }

    private void Rollback_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var state = ReleaseInstaller.Rollback(_service.Options.InstallRoot);
            StatusText.Text = $"Rolled back to {state.CurrentVersion}.";
            ClearDetail();
            RefreshState();
        }
        catch (Exception error) { ShowError(error); }
    }

    private void Cancel_Click(object sender, RoutedEventArgs e)
    {
        var operation = _operation;
        if (operation is null || operation.IsCancellationRequested) return;
        StatusText.Text = "Cancelling…";
        CancelButton.IsEnabled = false;
        try { operation.Cancel(); }
        catch (ObjectDisposedException) { /* the operation finished as we clicked */ }
    }

    private async Task RunOperationAsync(Func<CancellationToken, Task> operation)
    {
        if (_operation is not null) return;
        _operation = new CancellationTokenSource();
        SetOperationRunning(true);
        try { await operation(_operation.Token); }
        catch (OperationCanceledException)
        {
            StatusText.Text = "Cancelled. Finished files were kept so you can resume; partial parts were discarded.";
            Progress.Value = 0;
            ClearDetail();
        }
        catch (Exception error) { ShowError(error); }
        finally
        {
            var finished = _operation;
            _operation = null;
            finished?.Dispose();
            try
            {
                SetOperationRunning(false);
                RefreshChecklist();
            }
            catch (Exception error)
            {
                try { ShowError(error, modal: false); } catch { /* never throw from finally */ }
            }
        }
    }

    private void SetOperationRunning(bool running)
    {
        UpdateButton.IsEnabled = !running;
        SettingsButton.IsEnabled = !running || SettingsOverlay.Visibility == Visibility.Visible;
        SetupAllButton.IsEnabled = !running;
        RollbackButton.IsEnabled = !running;
        ImportBfmeButton.IsEnabled = !running;
        ImportRotwkButton.IsEnabled = !running;
        DownloadBfmeButton.IsEnabled = !running;
        DownloadRotwkButton.IsEnabled = !running;
        BrowseBfmeButton.IsEnabled = !running;
        BrowseRotwkButton.IsEnabled = !running;
        OpenAioButton.IsEnabled = !running && File.Exists(Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "BFME All In One Launcher", "AllInOneLauncher.exe"));
        BfmePath.IsEnabled = !running;
        RotwkPath.IsEnabled = !running;
        ContentRootPath.IsEnabled = !running;
        BrowseContentRootButton.IsEnabled = !running;
        OpenContentRootButton.IsEnabled = !running;
        RefreshPacksSettingsButton.IsEnabled = !running;
        OpenModsButton.IsEnabled = !running;
        OpenInstallButton.IsEnabled = !running;
        PlayFromSettingsButton.IsEnabled = !running;
        VerifyEngineButton.IsEnabled = !running;
        OpenGameFolderButton.IsEnabled = !running;
        CancelButton.IsEnabled = running;
        if (running) PlayButton.IsEnabled = false;
        else RefreshState();
    }

    private void ClearDetail()
    {
        DetailText.Text = string.Empty;
        DetailPanel.Visibility = Visibility.Collapsed;
    }

    private void ShowError(Exception error, string? guidance = null, bool modal = true)
    {
        StatusText.Text = guidance ?? error.Message;
        DetailText.Text = guidance is null ? error.Message : $"{guidance}\n\n{error.Message}";
        DetailPanel.Visibility = Visibility.Visible;
        if (modal)
            MessageBox.Show(DetailText.Text, "OpenBFME launcher", MessageBoxButton.OK, MessageBoxImage.Error);
    }
}
