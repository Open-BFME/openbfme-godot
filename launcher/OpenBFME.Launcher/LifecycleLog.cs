using System.IO;

namespace OpenBFME.Launcher;

/// <summary>
/// Always-on breadcrumb log so "it died" reports have a trail even when WinExe
/// has no console and WER is empty.
/// </summary>
internal static class LifecycleLog
{
    private static readonly object Gate = new();
    private static readonly string Path = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "OpenBFME",
        "launcher-lifecycle.log");

    internal static void Write(string stage, string detail = "")
    {
        try
        {
            var line = $"{DateTime.UtcNow:o}\t{Environment.ProcessId}\t{stage}\t{detail}";
            lock (Gate)
            {
                var dir = System.IO.Path.GetDirectoryName(Path);
                if (dir is not null) Directory.CreateDirectory(dir);
                File.AppendAllText(Path, line + Environment.NewLine);
            }
        }
        catch
        {
            // Logging must never kill the launcher.
        }
    }
}
