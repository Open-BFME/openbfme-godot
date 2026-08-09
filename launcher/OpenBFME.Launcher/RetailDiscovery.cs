namespace OpenBFME.Launcher;

public enum RetailInstallAssessment
{
    Missing,
    NotADirectory,
    NoMarker,
    WrongEdition,
    Incomplete,
    ValidBfme2,
    ValidRotwk,
}

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

    // These are the same bounded prerequisites used by the importer's
    // doctor_install(): the edition executable plus its first three required
    // CORE_ARCHIVES. Presence is enough here; payload digests belong to the
    // separate workshop retail-payload-manifest contract.
    private static readonly string[] CoreArchives = { "ini.big", "w3d.big", "textures0.big" };

    private const string Bfme2Executable = "lotrbfme2.exe";
    private const string RotwkExecutable = "lotrbfme2ep1.exe";

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

        // Prefer an install the OpenBFME launcher itself provisioned via the
        // All-in-One workshop path, then fall back to classic retail locations.
        if (environment is null)
        {
            try
            {
                var openRoot = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "OpenBFME");
                var provisioned = AllInOneRetailProvisioner.DefaultInstallPath(openRoot, game);
                if (IsRetailInstall(game, provisioned))
                    return Path.GetFullPath(provisioned);
            }
            catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException or IOException or UnauthorizedAccessException)
            {
                // Fall through to candidate scan.
            }

            var fromRegistry = TryReadAllInOneRegistry(game);
            if (fromRegistry is not null) return fromRegistry;
        }

        foreach (var candidate in Candidates(game, environment))
            if (IsRetailInstall(game, candidate))
            {
                try { return Path.GetFullPath(candidate); }
                catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException or IOException)
                {
                    continue;
                }
            }
        return null;
    }

    /// <summary>
    /// Read the install path the All-in-One Launcher writes into HKLM, when present.
    /// Soft-fails: missing registry or access denied just means "not found here".
    /// </summary>
    public static string? TryReadAllInOneRegistry(string game)
    {
        // Keys mirror BfmeFoundationProject.BfmeKit.Data.BfmeDefaults.
        var relative = game switch
        {
            "bfme2" => @"SOFTWARE\WOW6432Node\Electronic Arts\Electronic Arts\The Battle for Middle-earth II",
            "rotwk" => @"SOFTWARE\WOW6432Node\Electronic Arts\Electronic Arts\The Lord of the Rings, The Rise of the Witch-king",
            _ => null
        };
        if (relative is null) return null;
        try
        {
            using var key = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(relative, writable: false);
            var value = key?.GetValue("InstallPath") as string
                        ?? key?.GetValue("Install Dir") as string;
            if (string.IsNullOrWhiteSpace(value)) return null;
            var full = Path.GetFullPath(value.Trim());
            return IsRetailInstall(game, full) ? full : null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or System.Security.SecurityException or ArgumentException)
        {
            return null;
        }
    }

    /// <summary>Assess a retail folder against one specific game edition.</summary>
    public static RetailInstallAssessment Assess(string game, string? directory)
    {
        var (expectedExecutable, otherExecutable, valid) = Edition(game);
        if (string.IsNullOrWhiteSpace(directory)) return RetailInstallAssessment.Missing;
        try
        {
            if (!Directory.Exists(directory)) return RetailInstallAssessment.NotADirectory;
            if (!File.Exists(Path.Combine(directory, InstallMarker)))
                return RetailInstallAssessment.NoMarker;

            var expectedPresent = File.Exists(Path.Combine(directory, expectedExecutable));
            var otherPresent = File.Exists(Path.Combine(directory, otherExecutable));
            if (!expectedPresent && otherPresent) return RetailInstallAssessment.WrongEdition;
            if (!expectedPresent || CoreArchives.Any(name => !File.Exists(Path.Combine(directory, name))))
                return RetailInstallAssessment.Incomplete;
            return valid;
        }
        catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException or IOException or UnauthorizedAccessException)
        {
            return RetailInstallAssessment.NotADirectory;
        }
    }

    /// <summary>True only when the folder is valid for <paramref name="game"/>.</summary>
    public static bool IsRetailInstall(string game, string? directory)
    {
        var assessment = Assess(game, directory);
        return assessment == (game == "bfme2"
            ? RetailInstallAssessment.ValidBfme2
            : RetailInstallAssessment.ValidRotwk);
    }

    // Compatibility for non-edition-sensitive launch environment plumbing.
    // Readiness, provisioning and conversion must use the game-aware overload.
    public static bool IsRetailInstall(string? directory) =>
        IsRetailInstall("bfme2", directory) || IsRetailInstall("rotwk", directory);

    /// <summary>
    /// A human-readable explanation of why a path was rejected, or <c>null</c> if it is
    /// acceptable. Written to be pasted straight into the launcher's status line.
    /// </summary>
    public static string? ExplainRejection(string game, string? directory)
    {
        var (expectedExecutable, _, _) = Edition(game);
        var requestedName = game == "bfme2"
            ? "The Battle for Middle-earth II"
            : "Rise of the Witch-king";
        var otherName = game == "bfme2"
            ? "Rise of the Witch-king"
            : "The Battle for Middle-earth II";
        var assessment = Assess(game, directory);
        if (assessment == RetailInstallAssessment.Missing)
            return $"No {requestedName} folder is selected. Choose the folder that game is installed in.";
        string full;
        try { full = Path.GetFullPath(directory!); }
        catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return $"'{directory}' is not a valid folder path.";
        }
        if (assessment == RetailInstallAssessment.NotADirectory)
            return $"There is no folder at {full}.";
        if (assessment == RetailInstallAssessment.NoMarker)
            return $"{full} does not contain {InstallMarker}, so it is not a game installation. " +
                   "Choose the folder that contains the game's own files.";
        if (assessment == RetailInstallAssessment.WrongEdition)
            return $"This looks like {otherName}, not {requestedName}. Choose the {requestedName} folder.";
        if (assessment == RetailInstallAssessment.Incomplete)
        {
            var required = new[] { InstallMarker, expectedExecutable }.Concat(CoreArchives);
            var missing = required.Where(name => !File.Exists(Path.Combine(full, name))).ToArray();
            return $"This {requestedName} install is incomplete: missing {string.Join(", ", missing)}.";
        }
        return assessment is RetailInstallAssessment.ValidBfme2 or RetailInstallAssessment.ValidRotwk
            ? null
            : $"The selected folder is not a valid {requestedName} installation.";
    }

    /// <summary>
    /// Compatibility explanation for callers that only need to recognise either
    /// supported edition. Edition-aware setup/import paths use the overload above.
    /// </summary>
    public static string? ExplainRejection(string? directory)
    {
        if (IsRetailInstall("bfme2", directory) || IsRetailInstall("rotwk", directory)) return null;
        var bfme2 = Assess("bfme2", directory);
        var game = bfme2 == RetailInstallAssessment.WrongEdition ? "rotwk" : "bfme2";
        return ExplainRejection(game, directory);
    }

    private static (string ExpectedExecutable, string OtherExecutable, RetailInstallAssessment Valid)
        Edition(string game) => game switch
        {
            "bfme2" => (Bfme2Executable, RotwkExecutable, RetailInstallAssessment.ValidBfme2),
            "rotwk" => (RotwkExecutable, Bfme2Executable, RetailInstallAssessment.ValidRotwk),
            _ => throw new ArgumentOutOfRangeException(nameof(game), game, "Expected bfme2 or rotwk."),
        };

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
