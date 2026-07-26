using System.Windows;

namespace OpenBFME.Launcher;

public partial class MainWindow : Window
{
    private readonly LauncherService _service;
    private CancellationTokenSource? _operation;

    /// <summary>
    /// Thrown when the launcher cannot start at all. <see cref="App"/> reports it and
    /// exits rather than showing a window that would misrepresent what is configured.
    /// </summary>
    public sealed class StartupException(string message) : Exception(message);

    public MainWindow()
    {
        InitializeComponent();

        LauncherOptions options;
        try
        {
            options = LauncherOptions.Parse(Environment.GetCommandLineArgs().Skip(1));
        }
        catch (Exception error)
        {
            // Previously this fell back to LauncherOptions.Parse(empty), so a typo like
            // "--chanel playtest" silently substituted the defaults and could downgrade
            // the player to a different channel than they asked for. Bad arguments are
            // now fatal: it is the only outcome that cannot mislead.
            throw new StartupException(
                $"{error.Message}\n\nOpenBFME did not start because its command line could " +
                "not be understood. Correct the arguments (or remove them all to use the " +
                "defaults) and try again.");
        }

        _service = new LauncherService(options);
        ChannelText.Text = $"Channel: {options.Channel} • {ReleaseSource.Repository}";
        RefreshState();
        PrefillRetailPaths();
        Loaded += OnLoaded;
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        // async void: nothing above us can observe a fault, so nothing may escape.
        try
        {
            await RunStartupFlagsAsync();
        }
        catch (Exception error)
        {
            ShowError(error);
        }
    }

    private void RefreshState()
    {
        InstallState? state;
        try
        {
            state = _service.Current;
        }
        catch (Exception error)
        {
            // A corrupt or tampered current.json must not blank the UI silently.
            VersionText.Text = "Install state unreadable";
            PlayButton.IsEnabled = false;
            ShowError(error);
            return;
        }
        VersionText.Text = state is null ? "Not installed" : $"Version {state.CurrentVersion}";
        PlayButton.IsEnabled = state is not null;
    }

    /// <summary>
    /// Fill the retail path boxes from what is actually on this machine.
    /// The old defaults were one developer's literal drive letters, which were wrong
    /// for every playtester and turned first run into a guessing game.
    /// </summary>
    private void PrefillRetailPaths()
    {
        Prefill("bfme2", BfmePath, BfmeDetectText, "BFME II");
        Prefill("rotwk", RotwkPath, RotwkDetectText, "Rise of the Witch-king");

        static void Prefill(string game, System.Windows.Controls.TextBox box,
            System.Windows.Controls.TextBlock note, string label)
        {
            string? found;
            try
            {
                found = RetailDiscovery.Discover(game);
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
            box.Text = string.Empty;
            note.Text = $"No {label} installation found automatically. " +
                        $"Use Browse to pick the folder that contains {RetailDiscovery.InstallMarker}.";
        }
    }

    private async Task RunStartupFlagsAsync()
    {
        if (_service.Options.ImportGame is not null && _service.Options.RetailPath is not null)
            await ImportAsync(_service.Options.ImportGame, _service.Options.RetailPath);
        else if (!_service.Options.NoUpdate && _service.Options.ManifestUri is not null)
            await UpdateAsync();

        if (!_service.Options.VerifyOnly) return;

        // Verification is the one thing a player runs *because* they suspect damage,
        // so a failure here must be reported, never thrown out of an async void handler
        // into an unhandled-exception crash.
        try
        {
            if (_service.Current is null)
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

    private async Task UpdateAsync()
    {
        if (_service.Options.ManifestUri is null)
        {
            StatusText.Text = "No release feed is configured. Use --manifest-url with the GitHub release manifest.";
            return;
        }
        await RunOperationAsync(async token =>
        {
            StatusText.Text = "Checking release manifest…";
            var manifest = await _service.Installer.FetchManifestAsync(_service.Options.ManifestUri, token);
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
        });
    }

    private async void ImportBfme_Click(object sender, RoutedEventArgs e) =>
        await ImportAsync("bfme2", BfmePath.Text);

    private async void ImportRotwk_Click(object sender, RoutedEventArgs e) =>
        await ImportAsync("rotwk", RotwkPath.Text);

    private void BrowseBfme_Click(object sender, RoutedEventArgs e) =>
        Browse(BfmePath, BfmeDetectText, "Select your Battle for Middle-earth II folder");

    private void BrowseRotwk_Click(object sender, RoutedEventArgs e) =>
        Browse(RotwkPath, RotwkDetectText, "Select your Rise of the Witch-king folder");

    private static void Browse(System.Windows.Controls.TextBox box,
        System.Windows.Controls.TextBlock note, string title)
    {
        var dialog = new Microsoft.Win32.OpenFolderDialog { Title = title, Multiselect = false };
        if (box.Text.Length > 0)
        {
            try { dialog.InitialDirectory = Path.GetFullPath(box.Text); } catch { /* best effort */ }
        }
        if (dialog.ShowDialog() != true) return;
        box.Text = dialog.FolderName;
        note.Text = RetailDiscovery.ExplainRejection(dialog.FolderName)
                    ?? "This folder looks like a valid game installation.";
    }

    private async Task ImportAsync(string game, string path)
    {
        // Check the folder before starting: a conversion that runs for minutes and then
        // fails on a path typo wastes the playtester's time and teaches them nothing.
        var rejection = RetailDiscovery.ExplainRejection(path);
        if (rejection is not null)
        {
            ShowError(new InvalidOperationException(rejection));
            return;
        }

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
            StatusText.Text = game == "bfme2" ? "BFME II assets imported." : "RotWK Angmar assets imported.";
            ClearDetail();
        });
    }

    private void Play_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _service.LaunchGame();
            StatusText.Text = "OpenBFME started.";
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
            // Genuine user cancellation. Downloads and installs stage into a temporary
            // directory that is deleted on the way out, so nothing partial survives.
            StatusText.Text = "Cancelled. Nothing was changed.";
            Progress.Value = 0;
            ClearDetail();
        }
        catch (Exception error) { ShowError(error); }
        finally
        {
            var finished = _operation;
            _operation = null;
            finished?.Dispose();
            SetOperationRunning(false);
        }
    }

    /// <summary>
    /// Disable the action buttons individually. The window as a whole must stay enabled:
    /// setting <c>IsEnabled=false</c> on the Window also disabled Cancel, which is
    /// precisely the control a player needs while an operation is misbehaving.
    /// </summary>
    private void SetOperationRunning(bool running)
    {
        UpdateButton.IsEnabled = !running;
        RollbackButton.IsEnabled = !running;
        ImportBfmeButton.IsEnabled = !running;
        ImportRotwkButton.IsEnabled = !running;
        BrowseBfmeButton.IsEnabled = !running;
        BrowseRotwkButton.IsEnabled = !running;
        BfmePath.IsEnabled = !running;
        RotwkPath.IsEnabled = !running;
        CancelButton.IsEnabled = running;
        if (running) PlayButton.IsEnabled = false;
        else RefreshState();
    }

    private void ClearDetail()
    {
        DetailText.Text = string.Empty;
        DetailPanel.Visibility = Visibility.Collapsed;
    }

    private void ShowError(Exception error, string? guidance = null)
    {
        StatusText.Text = guidance ?? error.Message;
        DetailText.Text = guidance is null ? error.Message : $"{guidance}\n\n{error.Message}";
        DetailPanel.Visibility = Visibility.Visible;
        MessageBox.Show(DetailText.Text, "OpenBFME launcher", MessageBoxButton.OK, MessageBoxImage.Error);
    }
}
