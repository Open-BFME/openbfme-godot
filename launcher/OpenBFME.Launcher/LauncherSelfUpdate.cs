using System.Diagnostics;

namespace OpenBFME.Launcher;

internal static class LauncherSelfUpdate
{
    internal static bool RelaunchSelected(LauncherOptions options, IReadOnlyList<string> arguments)
    {
        var state = LauncherInstallState.Load(options.InstallRoot);
        if (state is null) return false;
        var selectedRoot = Path.Combine(
            options.InstallRoot, "launcher-versions", state.CurrentVersion);
        var selected = Path.Combine(selectedRoot, "OpenBFME.Launcher.exe");
        var current = Path.GetFullPath(
            Environment.ProcessPath ?? throw new InvalidOperationException("Launcher process path is unavailable."));
        if (Path.GetFullPath(selected).Equals(current, StringComparison.OrdinalIgnoreCase))
            return false;
        InstalledVersionIdentity.Verify(
            selectedRoot, state.CurrentVersion, state.Commit, state.PackageSha256);
        BundleInventory.Verify(selectedRoot);
        var start = new ProcessStartInfo(selected)
        {
            UseShellExecute = false,
            WorkingDirectory = selectedRoot
        };
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        Process.Start(start);
        return true;
    }
}
