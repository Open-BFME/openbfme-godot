using System.IO;

namespace OpenBFME.Launcher;

/// <summary>
/// Always-on breadcrumb log so "it died" reports have a trail even when WinExe
/// has no console and WER is empty.
/// </summary>
internal static class LifecycleLog
{
    private static readonly object Gate = new();

    /// <summary>
    /// The legacy always-on breadcrumb file. STILL WRITTEN, deliberately: support
    /// instructions, previous bug reports and the operator's own habits all point
    /// at this path, and a diagnostics change that moves it would invalidate them
    /// for no gain. Every breadcrumb is now ALSO mirrored into the shared run-
    /// directory contract (see <see cref="DiagnosticsRun"/>), which is what makes
    /// it exportable next to the sim's and the importer's records.
    /// </summary>
    internal static string LegacyPath { get; } = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "OpenBFME",
        "launcher-lifecycle.log");

    internal static void Write(string stage, string detail = "")
    {
        // Mirrored first, so a breadcrumb still reaches the run record if the
        // legacy append throws (a full disk, a locked file, a roaming profile
        // that vanished mid-session).
        DiagnosticsLog.Event("info", $"lifecycle.{stage}", new Dictionary<string, object?>
        {
            ["stage"] = stage,
            ["detail"] = detail
        });
        try
        {
            var line = $"{DateTime.UtcNow:o}\t{Environment.ProcessId}\t{stage}\t{detail}";
            lock (Gate)
            {
                var dir = System.IO.Path.GetDirectoryName(LegacyPath);
                if (dir is not null) Directory.CreateDirectory(dir);
                File.AppendAllText(LegacyPath, line + Environment.NewLine);
            }
        }
        catch
        {
            // Logging must never kill the launcher.
        }
    }
}
