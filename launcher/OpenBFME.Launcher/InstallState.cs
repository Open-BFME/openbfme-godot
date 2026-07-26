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
        var state = JsonSerializer.Deserialize<InstallState>(File.ReadAllBytes(path))
            ?? throw new InvalidDataException("Install state is empty.");
        state.Validate();
        return state;
    }

    public static void SaveAtomic(string root, InstallState state)
    {
        state.Validate();
        Directory.CreateDirectory(root);
        var path = Path.Combine(root, "current.json");
        var temporary = path + $".{Environment.ProcessId}.tmp";
        var bytes = JsonSerializer.SerializeToUtf8Bytes(state,
            new JsonSerializerOptions { WriteIndented = true });
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
