using System.Diagnostics;

namespace OpenBFME.Launcher;

public sealed class LauncherService
{
    public LauncherOptions Options { get; }
    public ReleaseInstaller Installer { get; } = new();
    public ImporterRunner Importer { get; } = new();

    public LauncherService(LauncherOptions options) => Options = options;

    public InstallState? Current => InstallState.Load(Options.InstallRoot);

    public string CurrentGamePath()
    {
        var state = Current ?? throw new InvalidOperationException("OpenBFME is not installed.");
        var root = Path.Combine(Options.InstallRoot, "versions", state.CurrentVersion);
        ReleaseInstaller.VerifyInstalledVersion(root, state.CurrentVersion, state.Commit);
        return Path.Combine(root, "OpenBFME.exe");
    }

    public void LaunchGame()
    {
        var exe = CurrentGamePath();
        var start = new ProcessStartInfo(exe)
        {
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(exe)!
        };
        start.Environment["OPENBFME_CONTENT"] = Path.Combine(
            Options.InstallRoot, ".private", "content-packs");
        Process.Start(start);
    }
}
