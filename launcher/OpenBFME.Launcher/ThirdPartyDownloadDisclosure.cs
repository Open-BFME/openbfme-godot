using System.Text.Json;

namespace OpenBFME.Launcher;

/// <summary>
/// One-time disclosure in front of the optional third-party game-file download.
///
/// The download path ships available by default — it is not gated behind a build flag
/// or a config switch. What it is not allowed to be is silent: before the first
/// download on a machine, the player is told which host the bytes come from and that
/// they must have the legal right to obtain those files. The acknowledgement is
/// recorded on disk so the notice appears once rather than every run.
///
/// This is informational. It does not block the flow, and it is not a substitute for
/// the payload digest verification in <see cref="RetailPayloadManifest"/> — that is a
/// separate, mandatory, fail-closed control protecting the player from a poisoned or
/// substituted mirror payload.
/// </summary>
public sealed class ThirdPartyDownloadDisclosure
{
    /// <summary>Record file, relative to the launcher install root.</summary>
    public const string RecordRelativePath = "third-party-download-disclosure.json";

    public const string SchemaId = "openbfme.third-party-download-disclosure";

    /// <summary>
    /// Bump when the wording materially changes, so players see the new text once.
    /// </summary>
    public const int DisclosureVersion = 1;

    /// <summary>The host the download path reaches, named in player-facing text.</summary>
    public const string SourceHost = "bfmeladder.com";

    public static string SourceHostDisplay =>
        $"{SourceHost} (the BFME Ladder / All-in-One workshop)";

    /// <summary>
    /// The words the player must be shown once. Naming the host and stating the
    /// legal-right requirement are the substance of the disclosure, so the text lives
    /// here and the launcher tests assert both are present.
    /// </summary>
    public static string DisclosureText =>
        "Where these game files come from\n\n" +
        "OpenBFME can download Battle for Middle-earth II / Rise of the Witch-king game " +
        $"data from {SourceHostDisplay} — a community-run host that is not operated by, " +
        "affiliated with, or endorsed by OpenBFME or Electronic Arts. OpenBFME itself " +
        "never redistributes game data.\n\n" +
        "You must have the legal right to obtain and use these files — that normally " +
        "means owning a copy of the game. OpenBFME cannot grant you that right.\n\n" +
        "Every downloaded file is checked against a pinned SHA-256 digest before it is " +
        "installed; anything that does not match is deleted rather than used.\n\n" +
        "If you already have the game installed, you can skip this entirely and point " +
        "OpenBFME at your own installation with Browse….\n\n" +
        "This notice is shown once.";

    public string RecordPath { get; }

    public ThirdPartyDownloadDisclosure(string recordPath) => RecordPath = recordPath;

    public static string DefaultRecordPath(string installRoot) =>
        Path.Combine(installRoot, RecordRelativePath);

    public static ThirdPartyDownloadDisclosure ForInstallRoot(string installRoot) =>
        new(DefaultRecordPath(installRoot));

    /// <summary>
    /// True when the current disclosure version has already been acknowledged on this
    /// machine. An unreadable or older record counts as "not yet shown": showing the
    /// notice a second time is harmless, skipping it wrongly is not.
    /// </summary>
    public bool AlreadyShown()
    {
        try
        {
            if (!File.Exists(RecordPath)) return false;
            using var document = JsonDocument.Parse(File.ReadAllText(RecordPath));
            var root = document.RootElement;
            if (!root.TryGetProperty("schema", out var schema) ||
                schema.GetString() != SchemaId)
                return false;
            return root.TryGetProperty("disclosureVersion", out var version) &&
                   version.ValueKind == JsonValueKind.Number &&
                   version.GetInt32() >= DisclosureVersion;
        }
        catch (Exception error) when (
            error is JsonException or IOException or UnauthorizedAccessException or FormatException)
        {
            return false;
        }
    }

    /// <summary>Persist the acknowledgement so the notice is shown once.</summary>
    public void RecordShown()
    {
        var directory = Path.GetDirectoryName(Path.GetFullPath(RecordPath));
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
        var record = new
        {
            schema = SchemaId,
            disclosureVersion = DisclosureVersion,
            sourceHost = SourceHost,
            acknowledgedAtUtc = DateTime.UtcNow.ToString("o")
        };
        File.WriteAllText(RecordPath, JsonSerializer.Serialize(record, new JsonSerializerOptions
        {
            WriteIndented = true
        }) + "\n");
    }

    /// <summary>
    /// Show the notice through <paramref name="show"/> if it has not been shown yet, then
    /// record it. Returns true when the notice was presented on this call. A failure to
    /// write the record must not stop the player, so it is swallowed — the worst case is
    /// the notice appearing again.
    /// </summary>
    public bool ShowOnce(Action<string> show)
    {
        if (AlreadyShown()) return false;
        show(DisclosureText);
        try { RecordShown(); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        return true;
    }
}
