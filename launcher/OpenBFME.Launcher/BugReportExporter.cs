using System.Diagnostics;
using System.Globalization;
using System.IO.Compression;
using System.Text.Json;

namespace OpenBFME.Launcher;

/// <summary>
/// The artifact a playtester sends back.
///
/// <para>WHAT THIS IS FOR. Wave-one playtesting is private and remote: the person
/// who saw the bug is not the person who can read a log, and "check
/// %LOCALAPPDATA%" is a request most testers will get wrong or ignore. One button
/// has to produce one file that answers, without a follow-up round trip, which
/// build ran, which pack it mounted, why that pack and not another, and what the
/// last thing to happen was — across all four components, because "the sim looked
/// wrong" is very often an importer or selection fact.</para>
///
/// <para>WHAT IT MUST NOT CONTAIN, and why the copy is an ALLOW LIST rather than a
/// directory zip. Everything in this repository's threat model points one way:
/// retail bytes never leave <c>.private</c> (AGENTS.md rule 6) and the CI
/// publication boundary scans for developer-machine paths. A run directory is a
/// directory somebody could drop anything into — a crash dump, a copied pack, a
/// screenshot. So the exporter copies exactly the five contract documents by
/// name, refuses anything oversized, and every text file goes through
/// <see cref="Redaction"/> on the way in. The zip is therefore small, readable,
/// and provably free of both retail payload and home paths.</para>
/// </summary>
internal static class BugReportExporter
{
    /// <summary>The only files ever copied out of a run directory.</summary>
    internal static readonly string[] AllowedFiles =
    {
        "run.log", "run.jsonl", "env.json", "identity.json", "error.txt"
    };

    /// <summary>
    /// Per-file ceiling. A run.jsonl from a long soak can reach tens of MB, and a
    /// bug report nobody can email is a bug report nobody sends. Oversized files
    /// are TRUNCATED FROM THE END (the tail is the interesting part) and say so
    /// in the copy, rather than being silently dropped.
    /// </summary>
    internal const long MaxFileBytes = 4 * 1024 * 1024;

    internal const int DefaultRunsPerComponent = 5;

    internal sealed record Result(string ZipPath, int RunCount, int FileCount, IReadOnlyList<string> Runs);

    /// <summary>
    /// Zip the newest <paramref name="runsPerComponent"/> run directories of every
    /// component into <c>%LOCALAPPDATA%\OpenBFME\bug-reports\&lt;timestamp&gt;.zip</c>.
    /// </summary>
    internal static Result Export(
        string? logRoot = null,
        string? destinationRoot = null,
        int runsPerComponent = DefaultRunsPerComponent,
        IReadOnlyDictionary<string, object?>? summary = null)
    {
        var root = logRoot ?? DiagnosticsRun.DefaultRoot();
        var outputRoot = destinationRoot ?? DiagnosticsRun.BugReportRoot();
        Directory.CreateDirectory(outputRoot);
        var stamp = DateTime.UtcNow.ToString("yyyyMMdd'T'HHmmss'Z'", CultureInfo.InvariantCulture);
        var zipPath = Path.Combine(outputRoot, $"openbfme-bug-report-{stamp}.zip");

        var runs = SelectRuns(root, runsPerComponent);
        var files = 0;
        using (var stream = new FileStream(zipPath, FileMode.Create, FileAccess.Write, FileShare.None))
        using (var archive = new ZipArchive(stream, ZipArchiveMode.Create))
        {
            foreach (var run in runs)
            {
                var name = Path.GetFileName(run);
                foreach (var allowed in AllowedFiles)
                {
                    var source = Path.Combine(run, allowed);
                    if (!File.Exists(source)) continue;
                    if (!CopyText(archive, $"runs/{name}/{allowed}", source)) continue;
                    files++;
                }
            }
            WriteManifest(archive, root, runs, summary);
            files++;
        }

        return new Result(zipPath, runs.Count, files, runs.Select(Path.GetFileName).Select(n => n!).ToList());
    }

    /// <summary>
    /// Newest N per component, newest first. Per COMPONENT rather than newest N
    /// overall: a tester who launched the game forty times would otherwise send a
    /// bundle with no importer run in it at all, and the importer is where half
    /// of "the content looks wrong" actually lives.
    /// </summary>
    internal static IReadOnlyList<string> SelectRuns(string logRoot, int runsPerComponent)
    {
        var selected = new List<string>();
        if (!Directory.Exists(logRoot)) return selected;
        var byComponent = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        foreach (var directory in Directory.GetDirectories(logRoot))
        {
            var name = Path.GetFileName(directory);
            if (name is null || !DiagnosticsRun.IsRunDirectoryName(name)) continue;
            var component = name.Split('-')[1];
            if (!byComponent.TryGetValue(component, out var list))
                byComponent[component] = list = new List<string>();
            list.Add(directory);
        }
        foreach (var component in DiagnosticsRun.Components)
        {
            if (!byComponent.TryGetValue(component, out var list)) continue;
            selected.AddRange(list
                .OrderByDescending(Path.GetFileName, StringComparer.Ordinal)
                .Take(Math.Max(1, runsPerComponent)));
        }
        selected.Sort(StringComparer.Ordinal);
        return selected;
    }

    private static bool CopyText(ZipArchive archive, string entryName, string source)
    {
        string text;
        try
        {
            // SHARED READ, and this is load-bearing rather than defensive. The most
            // valuable run in the bundle is usually the launcher's OWN, which is
            // open for writing right now; File.ReadAllText requests FileShare.Read
            // and therefore throws on it, which would have quietly shipped every
            // bug report with the current session missing.
            using var stream = new FileStream(
                source, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            // Oversized files are truncated FROM THE END - the tail is the part a
            // failure is in - and say so in the copy, rather than being silently
            // dropped from the bundle.
            var truncated = stream.Length > MaxFileBytes;
            if (truncated) stream.Seek(-MaxFileBytes, SeekOrigin.End);
            using var reader = new StreamReader(stream);
            text = reader.ReadToEnd();
            if (truncated)
                text = $"[truncated: only the last {MaxFileBytes} bytes of this file are included]\n" + text;
        }
        catch
        {
            return false;
        }

        // Redacted ON THE WAY IN, so the zip on disk never holds the unredacted
        // bytes even for a moment.
        var entry = archive.CreateEntry(entryName, CompressionLevel.Optimal);
        using var writer = new StreamWriter(entry.Open());
        writer.Write(Redaction.Scrub(text));
        return true;
    }

    private static void WriteManifest(
        ZipArchive archive,
        string logRoot,
        IReadOnlyList<string> runs,
        IReadOnlyDictionary<string, object?>? summary)
    {
        var document = new Dictionary<string, object?>
        {
            ["schema"] = "openbfme.bug-report",
            ["schemaVersion"] = 1,
            ["createdAt"] = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
            ["logRoot"] = Redaction.Scrub(logRoot),
            ["runs"] = runs.Select(Path.GetFileName).ToArray(),
            ["allowedFiles"] = AllowedFiles,
            ["contains"] =
                "Only the diagnostic documents listed in allowedFiles. No converted retail bytes, "
                + "no content packs, no credentials; home directories are replaced with tokens.",
            ["summary"] = summary is null ? null : Scrub(summary)
        };
        var entry = archive.CreateEntry("bug-report.json", CompressionLevel.Optimal);
        using var writer = new StreamWriter(entry.Open());
        writer.Write(JsonSerializer.Serialize(document, new JsonSerializerOptions { WriteIndented = true }));
    }

    private static Dictionary<string, object?> Scrub(IReadOnlyDictionary<string, object?> fields)
    {
        var result = new Dictionary<string, object?>();
        foreach (var (key, value) in fields)
            result[key] = Redaction.IsSecretName(key)
                ? "<redacted>"
                : value is string text ? Redaction.Scrub(text) : value;
        return result;
    }

    /// <summary>
    /// Open Explorer with the zip already selected. Selecting the file, not just
    /// opening the folder: the next thing the tester has to do is drag it into a
    /// chat window, and a folder of forty files does not help with that.
    /// </summary>
    internal static void Reveal(string zipPath)
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "explorer.exe",
                Arguments = $"/select,\"{zipPath}\"",
                UseShellExecute = true
            });
        }
        catch
        {
            // Wine and locked-down shells have no explorer.exe. The path is
            // already on screen, so this is a convenience, never a failure.
        }
    }
}
