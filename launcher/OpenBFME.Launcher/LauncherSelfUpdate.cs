using System.Diagnostics;

namespace OpenBFME.Launcher;

/// <summary>
/// The result of considering a self-update at startup. <paramref name="Warning"/> is
/// non-null when a newer launcher was selected but could not be handed control; the
/// caller must show it, because continuing silently on the older executable would hide
/// the fact that an update is installed and unusable.
/// </summary>
internal readonly record struct SelfUpdateOutcome(bool Relaunched, string? Warning);

internal static class LauncherSelfUpdate
{
    /// <summary>
    /// Set on the relaunched child so it never relaunches again.
    ///
    /// Handing control onwards is a one-hop operation, but the check that stops the hop
    /// is a path comparison, and paths have more than one spelling: reach the install
    /// root through a junction, a substituted drive, or an 8.3 short name and the child
    /// does not recognise itself as the selected launcher, relaunches, and forks
    /// endlessly. An explicit marker does not depend on two paths being spelled alike.
    /// </summary>
    internal const string RelaunchGuardVariable = "OPENBFME_LAUNCHER_RELAUNCHED";

    internal static SelfUpdateOutcome RelaunchSelected(
        LauncherOptions options, IReadOnlyList<string> arguments)
    {
        if (Environment.GetEnvironmentVariable(RelaunchGuardVariable) == "1")
            return new SelfUpdateOutcome(false, null);

        LauncherInstallState? state;
        try
        {
            state = LauncherInstallState.Load(options.InstallRoot);
        }
        catch (Exception error)
        {
            return Degrade("The record of which launcher version to run is unreadable", error);
        }
        if (state is null) return new SelfUpdateOutcome(false, null);

        var selectedRoot = Path.Combine(
            options.InstallRoot, "launcher-versions", state.CurrentVersion);
        var selected = Path.Combine(selectedRoot, "OpenBFME.Launcher.exe");
        var current = Path.GetFullPath(
            Environment.ProcessPath ?? throw new InvalidOperationException("Launcher process path is unavailable."));
        if (Path.GetFullPath(selected).Equals(current, StringComparison.OrdinalIgnoreCase))
            return new SelfUpdateOutcome(false, null);

        try
        {
            InstalledVersionIdentity.Verify(
                selectedRoot, state.CurrentVersion, state.Commit, state.PackageSha256);
            BundleInventory.Verify(selectedRoot);
        }
        catch (Exception error)
        {
            return Degrade($"The installed launcher update {state.CurrentVersion} failed verification", error);
        }

        var start = new ProcessStartInfo(selected)
        {
            UseShellExecute = false,
            WorkingDirectory = selectedRoot
        };
        start.Environment[RelaunchGuardVariable] = "1";
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        Process child;
        try
        {
            child = Process.Start(start)
                ?? throw new InvalidOperationException("No process was created.");
        }
        catch (Exception error)
        {
            return Degrade($"The installed launcher update {state.CurrentVersion} could not be started", error);
        }

        // Parent used to Shutdown(0) the moment Process.Start returned. If the child
        // died instantly (missing runtime, AV quarantine, broken single-file host) the
        // player saw a flash and no repair UI. Require the child to stay alive briefly.
        try
        {
            if (child.WaitForExit(2000))
            {
                return Degrade(
                    $"The installed launcher update {state.CurrentVersion} exited immediately " +
                    $"(code {child.ExitCode})",
                    new InvalidOperationException(
                        "Updated launcher process terminated during handoff."));
            }
        }
        catch (Exception error)
        {
            return Degrade(
                $"The installed launcher update {state.CurrentVersion} could not be supervised",
                error);
        }

        return new SelfUpdateOutcome(true, null);
    }

    /// <summary>
    /// Report a self-update that cannot be honoured, and keep running.
    ///
    /// Refusing to start was the previous behaviour and it was the wrong trade. The
    /// selected launcher is damaged often enough to matter — antivirus quarantines a
    /// file out of a bundled .NET runtime, a disk goes bad, an update is interrupted —
    /// and exiting left no way back in, because the one control that repairs it (Update,
    /// or Roll back) lives in a window that never opened. The executable the player just
    /// ran is by construction intact, verifies every manifest against the same pinned
    /// key, and can reinstall the broken one, so continuing on it is both safe and the
    /// only path that recovers. It is never quiet about it.
    /// </summary>
    private static SelfUpdateOutcome Degrade(string summary, Exception error) =>
        new(false,
            $"{summary}, so OpenBFME is running the launcher you started instead.\n\n" +
            "Use \"Check for update\" to reinstall it, or \"Roll back\" to return to the " +
            $"previous launcher.\n\nDetails: {error.Message}");
}
