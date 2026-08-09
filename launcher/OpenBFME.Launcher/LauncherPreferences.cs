using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

/// <summary>Small player-owned preferences stored beside the launcher install state.</summary>
public sealed record LauncherPreferences(
    [property: JsonPropertyName("schema")] string Schema,
    [property: JsonPropertyName("channel")] string Channel)
{
    public const string ExpectedSchema = "openbfme.launcher-preferences";
    public const string FileName = "launcher-preferences.json";

    public static string? LoadChannel(string installRoot)
    {
        var path = Path.Combine(installRoot, FileName);
        if (!File.Exists(path)) return null;

        try
        {
            var preferences = JsonSerializer.Deserialize<LauncherPreferences>(File.ReadAllBytes(path));
            if (preferences?.Schema != ExpectedSchema || !IsChannel(preferences.Channel))
                return null;
            return preferences.Channel.ToLowerInvariant();
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

        Directory.CreateDirectory(installRoot);
        AtomicFile.Write(Path.Combine(installRoot, FileName), JsonSerializer.SerializeToUtf8Bytes(
            new LauncherPreferences(ExpectedSchema, channel),
            new JsonSerializerOptions { WriteIndented = true }));
    }

    public static bool IsChannel(string? channel) =>
        channel is "stable" or "playtest" or "nightly";
}
