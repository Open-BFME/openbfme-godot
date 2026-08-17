using System.IO;

namespace OpenBFME.Launcher;

public partial class App : System.Windows.Application
{
    public App()
    {
        // Process-level nets: the WPF shell had none, so any uncaught UI/async fault
        // killed the WinExe with no console and no repair surface. Handled exceptions
        // keep the window alive so the player can still Browse / Check for update.
        DispatcherUnhandledException += (_, e) =>
        {
            ReportKeepAlive(e.Exception, "ui");
            e.Handled = true;
        };
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            if (e.ExceptionObject is Exception ex)
                ReportKeepAlive(ex, "domain");
        };
        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            ReportKeepAlive(e.Exception, "task");
            e.SetObserved();
        };
    }

    protected override async void OnStartup(System.Windows.StartupEventArgs e)
    {
        base.OnStartup(e);
        // FIRST, before anything that can fail. The run directory has to exist
        // before the first breadcrumb, or the earliest and most interesting part
        // of a "it died on startup" report is the part that is missing.
        DiagnosticsLog.Start();
        Exit += (_, _) => DiagnosticsLog.Close();
        LifecycleLog.Write("app", $"startup args=[{string.Join(' ', e.Args)}]");

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

        if (options.ExportBugReport)
        {
            // Handled before the window and before any update work: the point of
            // the headless form is to still produce a bundle when the UI is the
            // thing that is broken.
            EnsureHeadlessConsole();
            try
            {
                var report = BugReportExporter.Export(summary: new Dictionary<string, object?>
                {
                    ["installRoot"] = options.InstallRoot,
                    ["channel"] = options.Channel,
                    ["invocation"] = "headless --export-bug-report"
                });
                Console.WriteLine(report.ZipPath);
                Console.Error.WriteLine(
                    $"Bug report written: {report.ZipPath} ({report.RunCount} runs, {report.FileCount} files).");
                Shutdown(0);
            }
            catch (Exception error)
            {
                Console.Error.WriteLine($"Bug report export failed: {error.Message}");
                Shutdown(1);
            }
            return;
        }

        if (!options.Headless)
        {
            try
            {
                // Explicit main window + lifetime. Relying on "first Show() becomes main"
                // has failed players when a transient child/dialog raced shutdown.
                ShutdownMode = System.Windows.ShutdownMode.OnMainWindowClose;
                var window = new MainWindow();
                MainWindow = window;
                window.Show();
                // Activate without repositioning — MainWindow already centered on primary.
                window.Activate();
                LifecycleLog.Write("app",
                    $"main window shown left={window.Left:0} top={window.Top:0} " +
                    $"primaryWork={System.Windows.SystemParameters.WorkArea}");
            }
            catch (Exception error)
            {
                LifecycleLog.Write("app", $"main window failed: {error}");
                Fail(error.Message, 2, headless: false);
            }
            return;
        }

        // WinExe has no console by default; attach or allocate one so CI and the
        // clean-folder acceptance path can actually see progress and errors.
        EnsureHeadlessConsole();

        ShutdownMode = System.Windows.ShutdownMode.OnExplicitShutdown;

        using var cancellation = new CancellationTokenSource();
        Console.CancelKeyPress += (_, args) =>
        {
            args.Cancel = true;
            Console.Error.WriteLine("Cancellation requested; stopping…");
            try { cancellation.Cancel(); } catch (ObjectDisposedException) { }
        };

        try
        {
            var service = new LauncherService(options);

            if (options.DiscoverRelease || options.Channel != "stable")
            {
                try
                {
                    var (uri, candidate) = await service.ResolveManifestAsync(cancellation.Token);
                    if (candidate is not null)
                        Console.Error.WriteLine(
                            $"Release candidate: {candidate.Tag} " +
                            $"(prerelease={candidate.PreRelease}, source={candidate.Source})");
                    else
                        Console.Error.WriteLine($"Manifest: {uri}");
                }
                catch (Exception error) when (options.NoUpdate)
                {
                    Console.Error.WriteLine(error.Message);
                }
            }

            if (options.ProvisionBfme2 || options.ProvisionRotwk)
            {
                // Headless has no dialog, but the notice still has to be said once.
                service.DownloadDisclosure.ShowOnce(Console.Error.WriteLine);
            }

            if (options.ProvisionBfme2)
            {
                var result = await service.ProvisionRetailAsync(
                    "bfme2", null, cancellation.Token);
                Console.Error.WriteLine(
                    $"Provisioned BFME II at {result.InstallPath} " +
                    $"({result.FilesInstalled} new, {result.FilesSkipped} skipped).");
            }
            if (options.ProvisionRotwk)
            {
                var result = await service.ProvisionRetailAsync(
                    "rotwk", null, cancellation.Token);
                Console.Error.WriteLine(
                    $"Provisioned RotWK at {result.InstallPath} " +
                    $"({result.FilesInstalled} new, {result.FilesSkipped} skipped).");
            }

            if (options.ImportGame is not null && options.RetailPath is not null)
            {
                var rejection = RetailDiscovery.ExplainRejection(options.RetailPath);
                if (rejection is not null) throw new InvalidOperationException(rejection);

                var state = Path.Combine(options.InstallRoot, "workspace", "retail-work");
                var exit = await service.Importer.RunAsync(
                    AppContext.BaseDirectory,
                    options.ImportGame,
                    options.RetailPath,
                    state,
                    null,
                    cancellation.Token);
                if (exit != 0) throw new InvalidOperationException($"Importer exited with code {exit}.");
            }
            else if (!options.NoUpdate)
            {
                var (manifestUri, _) = await service.ResolveManifestAsync(cancellation.Token);
                var manifest = await service.Installer.FetchManifestAsync(
                    manifestUri, cancellation.Token);
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
            Shutdown(130);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.Message);
            Shutdown(1);
        }
    }

    private static void ReportKeepAlive(Exception error, string origin)
    {
        try
        {
            LifecycleLog.Write(origin, error.ToString());
            // A caught-and-kept-alive fault is still the thing the bug report is
            // about, so it earns this run an error.txt rather than only a
            // breadcrumb line the exporter cannot distinguish from noise.
            DiagnosticsLog.Failure($"unhandled.{origin}", error.Message, error.ToString());
            Console.Error.WriteLine($"[{origin}] {error}");

            // Only modal if a dispatcher still exists and the main window is up.
            // Never Show dialogs during hard domain teardown — that can re-crash.
            if (Current?.MainWindow is { IsLoaded: true } &&
                Current.Dispatcher is { HasShutdownStarted: false } dispatcher)
            {
                dispatcher.BeginInvoke(() =>
                {
                    try
                    {
                        System.Windows.MessageBox.Show(
                            error.Message,
                            "OpenBFME launcher",
                            System.Windows.MessageBoxButton.OK,
                            System.Windows.MessageBoxImage.Error);
                    }
                    catch { /* ignore */ }
                });
            }
        }
        catch { /* never throw from the handler */ }
    }

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

    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AttachConsole(int dwProcessId);

    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AllocConsole();

    private const int AttachParentProcess = -1;

    private static void EnsureHeadlessConsole()
    {
        if (!AttachConsole(AttachParentProcess))
            AllocConsole();
    }
}
