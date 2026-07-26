using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

internal sealed record LauncherInstallState(
    [property: JsonPropertyName("schema")] string Schema,
    [property: JsonPropertyName("currentVersion")] string CurrentVersion,
    [property: JsonPropertyName("previousVersion")] string? PreviousVersion,
    [property: JsonPropertyName("commit")] string Commit,
    [property: JsonPropertyName("previousCommit")] string? PreviousCommit,
    [property: JsonPropertyName("packageSha256")] string PackageSha256,
    [property: JsonPropertyName("previousPackageSha256")] string? PreviousPackageSha256)
{
    private const string ExpectedSchema = "openbfme.launcher-install-state";
    private const string StateName = "launcher-current.json";

    internal static LauncherInstallState? Load(string root)
    {
        var path = Path.Combine(root, StateName);
        if (!File.Exists(path)) return null;
        var state = JsonSerializer.Deserialize<LauncherInstallState>(File.ReadAllBytes(path))
            ?? throw new InvalidDataException("Launcher install state is empty.");
        state.Validate();
        return state;
    }

    internal static void Select(string root, ReleaseManifest manifest, ReleasePackage package)
    {
        var old = Load(root);
        var same = old?.CurrentVersion == manifest.Version;
        SaveAtomic(root, new LauncherInstallState(
            ExpectedSchema,
            manifest.Version,
            same ? old?.PreviousVersion : old?.CurrentVersion,
            manifest.Commit,
            same ? old?.PreviousCommit : old?.Commit,
            package.Sha256,
            same ? old?.PreviousPackageSha256 : old?.PackageSha256));
    }

    internal static void Rollback(string root)
    {
        var current = Load(root) ?? throw new InvalidOperationException("No launcher update is installed.");
        if (current.PreviousVersion is null || current.PreviousCommit is null ||
            current.PreviousPackageSha256 is null)
            return;
        var priorRoot = Path.Combine(root, "launcher-versions", current.PreviousVersion);
        InstalledVersionIdentity.Verify(
            priorRoot, current.PreviousVersion, current.PreviousCommit, current.PreviousPackageSha256);
        BundleInventory.Verify(priorRoot);
        SaveAtomic(root, new LauncherInstallState(
            ExpectedSchema,
            current.PreviousVersion,
            current.CurrentVersion,
            current.PreviousCommit,
            current.Commit,
            current.PreviousPackageSha256,
            current.PackageSha256));
    }

    private static void SaveAtomic(string root, LauncherInstallState state)
    {
        state.Validate();
        Directory.CreateDirectory(root);
        var path = Path.Combine(root, StateName);
        var temporary = path + $".{Environment.ProcessId}.tmp";
        File.WriteAllBytes(temporary, JsonSerializer.SerializeToUtf8Bytes(
            state, new JsonSerializerOptions { WriteIndented = true }));
        if (File.Exists(path)) File.Replace(temporary, path, null);
        else File.Move(temporary, path);
    }

    private void Validate()
    {
        var previousAllNull = PreviousVersion is null && PreviousCommit is null &&
                              PreviousPackageSha256 is null;
        var previousAllPresent = PreviousVersion is not null && PreviousCommit is not null &&
                                 PreviousPackageSha256 is not null;
        if (Schema != ExpectedSchema || !SafeVersion(CurrentVersion) || !FullSha1(Commit) ||
            !Sha256(PackageSha256) || !(previousAllNull || previousAllPresent) ||
            previousAllPresent && (!SafeVersion(PreviousVersion!) || !FullSha1(PreviousCommit!) ||
                                   !Sha256(PreviousPackageSha256!) || PreviousVersion == CurrentVersion))
            throw new InvalidDataException("Launcher install state is invalid.");
    }

    private static bool SafeVersion(string value) =>
        System.Text.RegularExpressions.Regex.IsMatch(value, "^[0-9A-Za-z][0-9A-Za-z._-]{0,63}$");
    private static bool FullSha1(string value) =>
        System.Text.RegularExpressions.Regex.IsMatch(value, "^[0-9a-f]{40}$");
    private static bool Sha256(string value) =>
        System.Text.RegularExpressions.Regex.IsMatch(value, "^[0-9a-f]{64}$");
}
