namespace OpenBFME.Launcher;

/// <summary>
/// Finds the player's own retail installation on this machine.
///
/// OpenBFME ships no copyrighted game data, so first run is always: point at a
/// retail install you already own, then convert. Making the player type a path
/// from memory is the single most likely place a playtester gives up, so the
/// launcher looks for it first and only falls back to asking.
///
/// Kept deliberately in step with <c>retail_install_candidates()</c> /
/// <c>discover_retail_install()</c> in <c>importer/openbfme_importer/paths.py</c>
/// and <c>resolveGameRoot()</c> in <c>tools/bfme-launcher-mcp/src/core.mjs</c>.
/// Every candidate is derived from this machine's environment — never from a
/// developer's disk. The launcher previously defaulted these fields to one
/// developer's literal drive letters, which were wrong for every playtester.
/// </summary>
public static class RetailDiscovery
{
    /// <summary>A directory is only a retail install if this file is in it.</summary>
    public const string InstallMarker = "game.dat";

    /// <summary>Environment override; always wins, so a player can point anywhere.</summary>
    public const string Bfme2OverrideVariable = "BFME2_INSTALL";

    /// <summary>Environment override for the Rise of the Witch-king install.</summary>
    public const string RotwkOverrideVariable = "ROTWK_INSTALL";

    private static readonly string[] Bfme2RelativeDirectories =
    {
        @"Electronic Arts\The Battle for Middle-earth II",
        @"EA Games\The Battle for Middle-earth II",
        @"Steam\steamapps\common\The Battle for Middle-earth II",
        @"GOG Galaxy\Games\The Battle for Middle-earth II",
    };

    private static readonly string[] RotwkRelativeDirectories =
    {
        @"Electronic Arts\The Lord of the Rings, The Rise of the Witch-king",
        @"EA Games\The Lord of the Rings, The Rise of the Witch-king",
        @"Steam\steamapps\common\The Lord of the Rings, The Rise of the Witch-king",
        @"GOG Galaxy\Games\The Lord of the Rings, The Rise of the Witch-king",
    };

    /// <summary>
    /// Candidate directories for <paramref name="game"/>, most likely first.
    /// </summary>
    public static IReadOnlyList<string> Candidates(
        string game, IDictionary<string, string>? environment = null)
    {
        var relatives = game switch
        {
            "bfme2" => Bfme2RelativeDirectories,
            "rotwk" => RotwkRelativeDirectories,
            _ => throw new ArgumentOutOfRangeException(nameof(game), game, "Expected bfme2 or rotwk.")
        };

        var roots = new List<string>();
        foreach (var variable in new[] { "ProgramFiles(x86)", "ProgramFiles", "ProgramW6432" })
        {
            var value = Read(environment, variable);
            if (!string.IsNullOrWhiteSpace(value)) roots.Add(value.Trim());
        }
        // Games are commonly installed to a secondary data drive rather than C:.
        foreach (var letter in "CDEFGH")
        {
            var drive = $"{letter}:\\";
            if (!SafeDirectoryExists(drive)) continue;
            roots.Add(drive);
            roots.Add(Path.Combine(drive, "Games"));
        }

        var candidates = new List<string>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var root in roots)
            foreach (var relative in relatives)
            {
                string candidate;
                try { candidate = Path.Combine(root, relative); }
                catch (ArgumentException) { continue; }
                if (seen.Add(candidate)) candidates.Add(candidate);
            }
        return candidates;
    }

    /// <summary>
    /// The first plausible install for <paramref name="game"/>, or <c>null</c> if none
    /// is found. Returning null rather than a guess is deliberate: the caller can then
    /// say "we could not find it, here is where we looked", which is actionable, instead
    /// of failing later against a path that never existed.
    /// </summary>
    public static string? Discover(string game, IDictionary<string, string>? environment = null)
    {
        var overrideVariable = game == "rotwk" ? RotwkOverrideVariable : Bfme2OverrideVariable;
        var configured = Read(environment, overrideVariable);
        if (!string.IsNullOrWhiteSpace(configured))
        {
            try { return Path.GetFullPath(configured.Trim()); }
            catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException)
            {
                throw new InvalidOperationException(
                    $"{overrideVariable} is set to '{configured}', which is not a usable path.");
            }
        }

        foreach (var candidate in Candidates(game, environment))
            if (IsRetailInstall(candidate))
            {
                try { return Path.GetFullPath(candidate); }
                catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException or IOException)
                {
                    continue;
                }
            }
        return null;
    }

    /// <summary>True when <paramref name="directory"/> looks like a real retail install.</summary>
    public static bool IsRetailInstall(string? directory)
    {
        if (string.IsNullOrWhiteSpace(directory)) return false;
        try { return File.Exists(Path.Combine(directory, InstallMarker)); }
        catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException or IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    /// <summary>
    /// A human-readable explanation of why a path was rejected, or <c>null</c> if it is
    /// acceptable. Written to be pasted straight into the launcher's status line.
    /// </summary>
    public static string? ExplainRejection(string? directory)
    {
        if (string.IsNullOrWhiteSpace(directory))
            return "No game folder is selected. Choose the folder your retail game is installed in.";
        string full;
        try { full = Path.GetFullPath(directory); }
        catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return $"'{directory}' is not a valid folder path.";
        }
        if (!SafeDirectoryExists(full))
            return $"There is no folder at {full}.";
        if (!IsRetailInstall(full))
            return $"{full} does not contain {InstallMarker}, so it is not a game installation. " +
                   "Choose the folder that contains the game's own files.";
        return null;
    }

    private static string? Read(IDictionary<string, string>? environment, string name) =>
        environment is null
            ? Environment.GetEnvironmentVariable(name)
            : environment.TryGetValue(name, out var value) ? value : null;

    private static bool SafeDirectoryExists(string path)
    {
        try { return Directory.Exists(path); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }
}
