using System.IO;

namespace OpenBFME.Launcher;

/// <summary>
/// Runtime environment notes for the Windows-only WPF launcher.
///
/// Native target is Windows 10/11 x64. Linux/macOS is not a second GUI port —
/// the supported non-Windows path is the <b>self-contained win-x64</b> build
/// under Wine (or Proton) with a working D3D / Vulkan translation layer.
/// Detect Wine so we can soft-degrade APIs that commonly break there
/// (modern folder pickers, elevation prompts) without failing closed for
/// native Windows players.
/// </summary>
internal static class PlatformCompat
{
    private static readonly Lazy<bool> Wine = new(DetectWine, isThreadSafe: true);

    /// <summary>True when running under Wine / Wine-based layers (Proton, CrossOver).</summary>
    public static bool IsWine => Wine.Value;

    public static string HostDescription =>
        IsWine
            ? "Windows (Wine / compatibility layer)"
            : "Windows";

    /// <summary>
    /// Ask the user to pick a folder. Uses the modern OpenFolderDialog first
    /// (native Windows). On failure — common under older Wine builds — falls
    /// back to "pick any file in the folder" via OpenFileDialog, then takes
    /// the parent directory.
    /// </summary>
    public static string? PickFolder(string title, string? initialDirectory)
    {
        try
        {
            var dialog = new Microsoft.Win32.OpenFolderDialog
            {
                Title = title,
                Multiselect = false
            };
            if (!string.IsNullOrWhiteSpace(initialDirectory))
            {
                try { dialog.InitialDirectory = Path.GetFullPath(initialDirectory); }
                catch { /* best effort */ }
            }
            if (dialog.ShowDialog() == true && !string.IsNullOrWhiteSpace(dialog.FolderName))
                return dialog.FolderName;
            return null;
        }
        catch (Exception error)
        {
            LifecycleLog.Write("compat", $"OpenFolderDialog failed ({error.GetType().Name}): {error.Message}");
        }

        // Fallback: pick a file inside the game folder (game.dat preferred).
        try
        {
            var fileDialog = new Microsoft.Win32.OpenFileDialog
            {
                Title = title + " — select game.dat (or any file in the install folder)",
                Filter = "Game marker|game.dat|All files|*.*",
                CheckFileExists = true,
                Multiselect = false
            };
            if (!string.IsNullOrWhiteSpace(initialDirectory) && Directory.Exists(initialDirectory))
                fileDialog.InitialDirectory = initialDirectory;
            if (fileDialog.ShowDialog() == true && !string.IsNullOrWhiteSpace(fileDialog.FileName))
                return Path.GetDirectoryName(fileDialog.FileName);
        }
        catch (Exception error)
        {
            LifecycleLog.Write("compat", $"OpenFileDialog fallback failed: {error.Message}");
            throw new InvalidOperationException(
                "This environment could not open a folder picker. " +
                "Paste the full path to the game folder into the path box instead" +
                (IsWine ? " (Wine folder dialogs are often incomplete)." : "."),
                error);
        }

        return null;
    }

    private static bool DetectWine()
    {
        // Official Wine markers. Proton and CrossOver set these as well.
        if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WINEPREFIX")) ||
            !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WINELOADER")) ||
            !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WINEARCH")))
            return true;

        try
        {
            using var key = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(
                @"Software\Wine", writable: false);
            if (key is not null) return true;
        }
        catch { /* registry may be restricted */ }

        try
        {
            // ntdll exports wine_get_version only under Wine.
            return GetProcAddress(GetModuleHandle("ntdll.dll"), "wine_get_version") != IntPtr.Zero;
        }
        catch
        {
            return false;
        }
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private static extern IntPtr GetModuleHandle(string lpModuleName);

    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Ansi,
        ExactSpelling = true, SetLastError = true)]
    private static extern IntPtr GetProcAddress(IntPtr hModule, string procName);
}
