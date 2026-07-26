namespace OpenBFME.Launcher;

public partial class App : System.Windows.Application
{
    protected override async void OnStartup(System.Windows.StartupEventArgs e)
    {
        base.OnStartup(e);

        LauncherOptions options;
        try { options = LauncherOptions.Parse(e.Args); }
        catch (Exception error)
        {
            Fail(error.Message, 2, headless: e.Args.Contains("--headless"));
            return;
        }

        // A self-update that cannot be honoured degrades to running this executable and
        // says so; it must never stop the launcher from opening, because the controls
        // that repair a broken update are inside it. Only an unexpected fault here is
        // fatal.
        SelfUpdateOutcome selfUpdate;
        try
        {
            selfUpdate = LauncherSelfUpdate.RelaunchSelected(options, e.Args);
        }
        catch (Exception error)
        {
            Fail($"Selected launcher update is invalid: {error.Message}", 1, options.Headless);
            return;
        }
        if (selfUpdate.Relaunched)
        {
            Shutdown(0);
            return;
        }
        if (selfUpdate.Warning is not null) Warn(selfUpdate.Warning, options.Headless);

        if (!options.Headless)
        {
            try
            {
                new MainWindow().Show();
            }
            catch (Exception error)
            {
                // The window could not even be constructed (bad arguments, unreadable
                // embedded release target). Report it instead of dying on an unhandled
                // exception with a stack trace no playtester can act on.
                Fail(error.Message, 2, headless: false);
            }
            return;
        }

        ShutdownMode = System.Windows.ShutdownMode.OnExplicitShutdown;

        // Headless runs are used by CI and by the clean-VM acceptance job, where a
        // wedged job costs an hour of runner time. Ctrl+C must actually stop the work,
        // including any importer child process.
        using var cancellation = new CancellationTokenSource();
        Console.CancelKeyPress += (_, args) =>
        {
            args.Cancel = true;
            Console.Error.WriteLine("Cancellation requested; stopping…");
            // ReSharper disable once AccessToDisposedClosure — the handler cannot outlive
            // the await below, which completes before the using scope ends.
            try { cancellation.Cancel(); } catch (ObjectDisposedException) { }
        };

        try
        {
            var service = new LauncherService(options);
            if (options.ImportGame is not null && options.RetailPath is not null)
            {
                var rejection = RetailDiscovery.ExplainRejection(options.RetailPath);
                if (rejection is not null) throw new InvalidOperationException(rejection);

                var state = Path.Combine(options.InstallRoot, ".private", "retail-work");
                var exit = await service.Importer.RunAsync(
                    AppContext.BaseDirectory,
                    options.ImportGame,
                    options.RetailPath,
                    state,
                    null,
                    cancellation.Token);
                if (exit != 0) throw new InvalidOperationException($"Importer exited with code {exit}.");
            }
            else if (!options.NoUpdate && options.ManifestUri is not null)
            {
                var manifest = await service.Installer.FetchManifestAsync(
                    options.ManifestUri, cancellation.Token);
                await service.Installer.InstallAsync(
                    manifest, options.InstallRoot, null, cancellation.Token,
                    expectedChannel: options.Channel);
            }
            if (options.VerifyOnly)
            {
                var current = service.Current
                    ?? throw new InvalidOperationException("No installed release is selected.");
                ReleaseInstaller.VerifyInstalledVersion(Path.Combine(
                    options.InstallRoot, "versions", current.CurrentVersion),
                    current.CurrentVersion, current.Commit);
            }
            Shutdown(0);
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
            Console.Error.WriteLine("Cancelled.");
            Shutdown(130); // conventional exit code for SIGINT
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.Message);
            Shutdown(1);
        }
    }

    /// <summary>
    /// Report a startup failure everywhere the user might be looking, then exit non-zero.
    /// A GUI launch has no console attached, so stderr alone would be invisible; a
    /// headless run has no desktop, so a modal dialog would hang the job forever.
    /// </summary>
    /// <summary>
    /// Report something the player needs to know about but that does not stop the run.
    /// Same reasoning as <see cref="Fail"/> about where the message has to appear, minus
    /// the exit — a headless job must not be blocked by a modal nobody can dismiss.
    /// </summary>
    private static void Warn(string message, bool headless)
    {
        Console.Error.WriteLine(message);
        if (!headless)
            System.Windows.MessageBox.Show(
                message, "OpenBFME launcher",
                System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Warning);
    }

    private void Fail(string message, int exitCode, bool headless)
    {
        Console.Error.WriteLine(message);
        if (!headless)
            System.Windows.MessageBox.Show(
                message, "OpenBFME launcher",
                System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Error);
        Shutdown(exitCode);
    }
}
