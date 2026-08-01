using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

public sealed record InstallState(
    [property: JsonPropertyName("schema")] string Schema,
    [property: JsonPropertyName("currentVersion")] string CurrentVersion,
    [property: JsonPropertyName("previousVersion")] string? PreviousVersion,
    [property: JsonPropertyName("commit")] string Commit,
    [property: JsonPropertyName("previousCommit")] string? PreviousCommit,
    [property: JsonPropertyName("highestVersion")] string HighestVersion,
    [property: JsonPropertyName("highestCommit")] string HighestCommit)
{
    public const string ExpectedSchema = "openbfme.install-state";

    public static InstallState? Load(string root)
    {
        var path = Path.Combine(root, "current.json");
        if (!File.Exists(path)) return null;
        InstallState state;
        try
        {
            state = JsonSerializer.Deserialize<InstallState>(File.ReadAllBytes(path))
                ?? throw new InvalidDataException("Install state is empty.");
        }
        catch (JsonException error)
        {
            throw new InvalidDataException($"Install state is not valid JSON: {error.Message}", error);
        }
        state.Validate();
        return state;
    }

    public static void SaveAtomic(string root, InstallState state)
    {
        state.Validate();
        Directory.CreateDirectory(root);
        AtomicFile.Write(Path.Combine(root, "current.json"), JsonSerializer.SerializeToUtf8Bytes(
            state, new JsonSerializerOptions { WriteIndented = true }));
    }

    private void Validate()
    {
        var highestMatchesCurrent = HighestVersion == CurrentVersion && HighestCommit == Commit;
        var highestMatchesPrevious = HighestVersion == PreviousVersion && HighestCommit == PreviousCommit;
        if (Schema != ExpectedSchema ||
            !SafeVersion(CurrentVersion) ||
            !FullSha1(Commit) ||
            !SafeVersion(HighestVersion) ||
            !FullSha1(HighestCommit) ||
            !(highestMatchesCurrent || highestMatchesPrevious) ||
            (PreviousVersion is null) != (PreviousCommit is null) ||
            PreviousVersion is not null && (!SafeVersion(PreviousVersion) || !FullSha1(PreviousCommit!)) ||
            PreviousVersion == CurrentVersion)
            throw new InvalidDataException("Install state is invalid.");
    }

    private static bool SafeVersion(string value) =>
        System.Text.RegularExpressions.Regex.IsMatch(value, "^[0-9A-Za-z][0-9A-Za-z._-]{0,63}$");
    private static bool FullSha1(string value) =>
        System.Text.RegularExpressions.Regex.IsMatch(value, "^[0-9a-f]{40}$");
}

/// <summary>
/// Replace a small file's contents in one step, and make the new contents survive a
/// power loss.
///
/// Both selection pointers go through here so they cannot drift apart. They previously
/// had not: the game pointer was written through to disk, while the launcher pointer
/// used a buffered WriteAllBytes, so a power cut could leave a zero-length
/// launcher-current.json — which the launcher reads before it does anything else and
/// which, being unparseable, stopped it from starting at all.
///
/// The scratch name carries a GUID rather than the process id. A process id is reused
/// by Windows, so a crash mid-write left a stale file that a later run with the same id
/// collided with on FileMode.CreateNew, permanently failing every save from then on.
/// </summary>
internal static class AtomicFile
{
    internal static void Write(string path, ReadOnlySpan<byte> bytes)
    {
        var temporary = path + $".{Guid.NewGuid():N}.tmp";
        try
        {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write,
                       FileShare.None, 4096, FileOptions.WriteThrough))
            {
                stream.Write(bytes);
                stream.WriteByte((byte)'\n');
                stream.Flush(true);
            }
            if (File.Exists(path)) File.Replace(temporary, path, null);
            else File.Move(temporary, path);
        }
        finally
        {
            // The rename consumes the scratch file on success; anything else leaves it,
            // and leaving it next to a selection pointer would be litter the pruner
            // never looks at.
            if (File.Exists(temporary))
                try { File.Delete(temporary); }
                catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        }
    }
}
