using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

/// <summary>Small player-owned preferences stored beside the launcher install state.</summary>
public sealed record LauncherPreferences(
    [property: JsonPropertyName("schema")] string Schema,
    [property: JsonPropertyName("channel")] string Channel,
    [property: JsonPropertyName("diagnostics")] bool Diagnostics = false)
{
    public const string ExpectedSchema = "openbfme.launcher-preferences";
    public const string FileName = "launcher-preferences.json";

    public static string? LoadChannel(string installRoot) => Load(installRoot) is { } p && IsChannel(p.Channel)
        ? p.Channel.ToLowerInvariant()
        : null;

    /// <summary>
    /// Whether the player asked for the extensive diagnostic run record. Off is
    /// the answer for a missing or unreadable preference file: diagnostics that
    /// turn themselves on are a behaviour change nobody consented to.
    /// </summary>
    public static bool LoadDiagnostics(string installRoot) => Load(installRoot)?.Diagnostics ?? false;

    private static LauncherPreferences? Load(string installRoot)
    {
        var path = Path.Combine(installRoot, FileName);
        if (!File.Exists(path)) return null;

        try
        {
            var preferences = JsonSerializer.Deserialize<LauncherPreferences>(File.ReadAllBytes(path));
            return preferences?.Schema == ExpectedSchema ? preferences : null;
        }
        catch (JsonException)
        {
            // A hand-edited or interrupted preference must not prevent the repair UI
            // from opening. Fall back to the embedded default and overwrite on save.
            return null;
        }
    }

    public static void SaveChannel(string installRoot, string channel)
    {
        channel = channel.Trim().ToLowerInvariant();
        if (!IsChannel(channel))
            throw new ArgumentException("Channel must be stable, playtest, or nightly.", nameof(channel));
        // READ-MODIFY-WRITE. The previous version rebuilt the document from the one
        // field it was changing, which is exactly how the second preference in a
        // file gets silently reset by an unrelated save.
        Save(installRoot, channel, Load(installRoot)?.Diagnostics ?? false);
    }

    public static void SaveDiagnostics(string installRoot, bool diagnostics)
    {
        var existing = Load(installRoot);
        var channel = existing is not null && IsChannel(existing.Channel) ? existing.Channel : "stable";
        Save(installRoot, channel, diagnostics);
    }

    private static void Save(string installRoot, string channel, bool diagnostics)
    {
        Directory.CreateDirectory(installRoot);
        AtomicFile.Write(Path.Combine(installRoot, FileName), JsonSerializer.SerializeToUtf8Bytes(
            new LauncherPreferences(ExpectedSchema, channel, diagnostics),
            new JsonSerializerOptions { WriteIndented = true }));
    }

    public static bool IsChannel(string? channel) =>
        channel is "stable" or "playtest" or "nightly";
}
