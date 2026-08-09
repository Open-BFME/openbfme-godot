using System.Collections.Concurrent;
using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace OpenBFME.Launcher;

/// <summary>
/// Redaction for anything that leaves this machine inside a bug report.
///
/// <para>The launcher already had one redactor — <see cref="ImporterRunner.Redact"/> —
/// and it is kept exactly as it is, because it does a different job: it sanitises
/// an untrusted line of subprocess output, where the safe answer is to obliterate
/// the whole path. That is the wrong answer here. "Which content root did the
/// launcher mount" IS the evidence a bug report exists to carry, and an artifact
/// that answers it with <c>&lt;private-path&gt;</c> is worthless.</para>
///
/// <para>So this scrubs the identifying part and keeps the shape: the user's home
/// directory becomes <c>%USERPROFILE%</c>, another machine's becomes
/// <c>&lt;user&gt;</c>, and anything that names a credential loses its value
/// entirely. Both redactors are exercised by the launcher test suite.</para>
/// </summary>
internal static class Redaction
{
    /// <summary>
    /// Names whose value never leaves the machine. An ALLOW LIST governs which
    /// environment variables are captured at all; this is the second belt.
    /// </summary>
    private static readonly string[] SecretNameFragments =
    {
        "token", "secret", "password", "passwd", "apikey", "api_key",
        "privatekey", "private_key", "signingkey", "signing_key",
        "credential", "bearer", "authorization", "session", "cookie"
    };

    /// <summary>
    /// <c>&lt;name-containing-a-credential-word&gt; &lt;separator&gt; &lt;value&gt;</c>.
    /// The name parts around the fragment are ZERO-or-more, which is not an
    /// accident: the first spelling required a leading character, so it matched
    /// <c>github_token=</c> and missed the two spellings that actually turn up —
    /// a bare <c>token=…</c> and a JSON key whose whole name IS the fragment,
    /// like <c>"signing_key": …</c>. Both leaked through the launcher test suite
    /// before this was corrected.
    /// </summary>
    private static readonly Regex SecretAssignment = new(
        @"(?<![A-Za-z0-9_\-])(?<name>[A-Za-z0-9_\-]*(token|secret|password|passwd|apikey|api_key|privatekey|private_key|signingkey|signing_key|credential|bearer|authorization|cookie)[A-Za-z0-9_\-]*)(?<sep>""?\s*[:=]\s*""?)(?<value>[^""\r\n,;}]+)",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    private static readonly Regex ForeignUserDirectory = new(
        @"(?<lead>[\\/](?:Users|home)[\\/])(?<name>[^\\/""'\s]+)",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    internal static bool IsSecretName(string name)
    {
        foreach (var fragment in SecretNameFragments)
            if (name.Contains(fragment, StringComparison.OrdinalIgnoreCase))
                return true;
        return false;
    }

    internal static string Scrub(string? text)
    {
        if (string.IsNullOrEmpty(text)) return text ?? string.Empty;
        var result = text;

        // LONGEST PREFIX FIRST. LocalApplicationData lives under the profile, so
        // redacting the home directory first would leave every AppData path
        // reading "%USERPROFILE%\AppData\Local\..." and the specific token would
        // never match anything.
        foreach (var (folder, token) in new[]
                 {
                     (Environment.SpecialFolder.LocalApplicationData, "%LOCALAPPDATA%"),
                     (Environment.SpecialFolder.ApplicationData, "%APPDATA%"),
                     (Environment.SpecialFolder.UserProfile, "%USERPROFILE%")
                 })
        {
            var value = SafeFolder(folder);
            if (value.Length < 4) continue;
            result = ReplacePathPrefix(result, value, token);
        }

        result = ForeignUserDirectory.Replace(result, match => match.Groups["lead"].Value + "<user>");
        result = SecretAssignment.Replace(
            result, match => match.Groups["name"].Value + match.Groups["sep"].Value + "<redacted>");
        return result;
    }

    private static string SafeFolder(Environment.SpecialFolder folder)
    {
        try { return Environment.GetFolderPath(folder); }
        catch { return string.Empty; }
    }

    private static string ReplacePathPrefix(string text, string value, string token)
    {
        var result = text.Replace(value, token, StringComparison.OrdinalIgnoreCase);
        var forward = value.Replace('\\', '/');
        if (!string.Equals(forward, value, StringComparison.Ordinal))
            result = result.Replace(forward, token, StringComparison.OrdinalIgnoreCase);
        return result;
    }
}

/// <summary>
/// One process's diagnostic run directory, in the shared Open-BFME log contract.
///
/// <para>THE CONTRACT, identical for the importer, converter, sim and launcher:</para>
/// <list type="bullet">
///   <item>root <c>%LOCALAPPDATA%\OpenBFME\logs</c>, override <c>OPENBFME_LOG_DIR</c></item>
///   <item>run dir <c>&lt;UTC yyyyMMddTHHmmssZ&gt;-&lt;component&gt;-&lt;pid&gt;</c></item>
///   <item><c>run.log</c> human-readable, <c>run.jsonl</c> one JSON object per
///   event (ts, level, component, event, fields), <c>env.json</c>,
///   <c>identity.json</c>, and on failure <c>error.txt</c></item>
///   <item>retention: newest 20 kept, pruned on start; a run carrying
///   <c>error.txt</c> survives until it is beyond 100 runs old</item>
/// </list>
///
/// <para>WHY THE LAUNCHER RECORDS BY DEFAULT while the sim does not. The sim's
/// current behaviour is to write nothing, and the contract forbids a regression,
/// so it stays opt-in. The launcher's current behaviour is ALREADY an always-on
/// breadcrumb file (<see cref="LifecycleLog"/>), written for exactly this reason:
/// "it died" reports from a WinExe with no console and an empty WER. Moving those
/// breadcrumbs into the contract layout is what makes them exportable. The legacy
/// file keeps being written too, so nothing that reads it breaks.
/// <c>OPENBFME_DIAGNOSTICS=1</c> adds the verbose events on top.</para>
/// </summary>
internal sealed class DiagnosticsRun : IDisposable
{
    internal const int RetentionKeep = 20;
    internal const int RetentionHardLimit = 100;
    internal const int TailLines = 120;

    internal static readonly string[] Components = { "importer", "converter", "sim", "launcher" };

    /// <summary>
    /// Environment variables a bug report may carry. An ALLOW LIST, because a
    /// deny list ships the next secret somebody invents.
    /// </summary>
    private static readonly string[] EnvironmentAllowList =
    {
        "OPENBFME_CONTENT", "OPENBFME_DIAGNOSTICS", "OPENBFME_LOG_DIR",
        "OPENBFME_PROFILE_BOOT", "OPENBFME_STRICT_PARITY_PROFILE",
        "BFME2_INSTALL", "ROTWK_INSTALL",
        "OS", "PROCESSOR_ARCHITECTURE", "NUMBER_OF_PROCESSORS", "DOTNET_ROOT"
    };

    private readonly object _gate = new();
    private readonly List<string> _tail = new();
    private readonly List<string> _failures = new();
    private readonly StreamWriter? _log;
    private readonly StreamWriter? _jsonl;
    private int _events;
    private bool _closed;

    internal string Directory { get; }
    internal string Component { get; }
    internal bool Verbose { get; }

    private DiagnosticsRun(string directory, string component, bool verbose)
    {
        Directory = directory;
        Component = component;
        Verbose = verbose;
        _log = new StreamWriter(
            new FileStream(Path.Combine(directory, "run.log"), FileMode.Create, FileAccess.Write, FileShare.ReadWrite))
        { AutoFlush = true };
        _jsonl = new StreamWriter(
            new FileStream(Path.Combine(directory, "run.jsonl"), FileMode.Create, FileAccess.Write, FileShare.ReadWrite))
        { AutoFlush = true };
    }

    /// <summary>
    /// Prune, create this process's run directory, and start recording. Returns
    /// null and changes nothing when the directory cannot be made: diagnostics
    /// that can take the launcher down are worse than no diagnostics.
    /// </summary>
    internal static DiagnosticsRun? Start(string component, string? rootOverride = null, bool? verbose = null)
    {
        try
        {
            var root = rootOverride ?? DefaultRoot();
            System.IO.Directory.CreateDirectory(root);
            // Pruned BEFORE this run's directory exists, so "keep the newest 20"
            // means twenty finished runs plus this one.
            Prune(root);
            var directory = Path.Combine(root, RunDirectoryName(component, DateTime.UtcNow, Environment.ProcessId));
            System.IO.Directory.CreateDirectory(directory);
            var run = new DiagnosticsRun(directory, component, verbose ?? IsVerboseRequested());
            run.WriteEnvironmentDocument();
            run.Event("info", "run.begin", new Dictionary<string, object?>
            {
                ["runDirectory"] = directory,
                ["pid"] = Environment.ProcessId,
                ["verbose"] = run.Verbose
            });
            return run;
        }
        catch
        {
            return null;
        }
    }

    internal static bool IsVerboseRequested() =>
        string.Equals(Environment.GetEnvironmentVariable("OPENBFME_DIAGNOSTICS"), "1", StringComparison.Ordinal);

    internal static string DefaultRoot()
    {
        var over = Environment.GetEnvironmentVariable("OPENBFME_LOG_DIR");
        if (!string.IsNullOrWhiteSpace(over)) return over.Trim();
        return Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenBFME", "logs");
    }

    internal static string BugReportRoot() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenBFME", "bug-reports");

    /// <summary>
    /// <c>&lt;UTC yyyyMMddTHHmmssZ&gt;-&lt;component&gt;-&lt;pid&gt;</c>. The compact
    /// basic-ISO stamp is deliberate: ':' is illegal in a Windows directory name,
    /// and this form sorts lexicographically in the same order it sorts
    /// chronologically — which is what makes retention a sort rather than a stat
    /// of every directory's timestamps.
    /// </summary>
    internal static string RunDirectoryName(string component, DateTime utc, int pid) =>
        string.Create(CultureInfo.InvariantCulture, $"{utc:yyyyMMdd'T'HHmmss}Z-{component}-{pid}");

    internal static bool IsRunDirectoryName(string name)
    {
        var parts = name.Split('-');
        if (parts.Length != 3) return false;
        if (!DateTime.TryParseExact(parts[0], "yyyyMMdd'T'HHmmss'Z'", CultureInfo.InvariantCulture,
                DateTimeStyles.AdjustToUniversal | DateTimeStyles.AssumeUniversal, out _)) return false;
        if (Array.IndexOf(Components, parts[1]) < 0) return false;
        return int.TryParse(parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out _);
    }

    /// <summary>
    /// Keep the newest <see cref="RetentionKeep"/> run directories. A directory
    /// carrying <c>error.txt</c> is EXEMPT until it is beyond
    /// <see cref="RetentionHardLimit"/> runs old: the failing run is the one the
    /// bug report is about, and evicting it to make room for twenty uneventful
    /// successes is exactly backwards. Returns what it removed so a test can
    /// assert the decision rather than the side effect.
    /// </summary>
    internal static IReadOnlyList<string> Prune(string root)
    {
        var removed = new List<string>();
        if (!System.IO.Directory.Exists(root)) return removed;
        var names = System.IO.Directory.GetDirectories(root)
            .Select(Path.GetFileName)
            .Where(name => name is not null && IsRunDirectoryName(name))
            .Select(name => name!)
            .OrderByDescending(name => name, StringComparer.Ordinal)
            .ToList();
        for (var index = RetentionKeep; index < names.Count; index++)
        {
            var path = Path.Combine(root, names[index]);
            if (index < RetentionHardLimit && File.Exists(Path.Combine(path, "error.txt"))) continue;
            try
            {
                System.IO.Directory.Delete(path, recursive: true);
                removed.Add(names[index]);
            }
            catch
            {
                // A locked directory is not a reason to fail a launch.
            }
        }
        return removed;
    }

    internal void Event(string level, string name, IReadOnlyDictionary<string, object?>? fields = null)
    {
        if (_closed) return;
        var scrubbed = ScrubFields(fields);
        var stamp = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
        var payload = JsonSerializer.Serialize(scrubbed);
        var line = $"{stamp} {level.ToUpperInvariant(),-5} {name} {payload}";
        lock (_gate)
        {
            _tail.Add(line);
            if (_tail.Count > TailLines) _tail.RemoveAt(0);
            _events++;
            try
            {
                _log?.WriteLine(line);
                _jsonl?.WriteLine(JsonSerializer.Serialize(new Dictionary<string, object?>
                {
                    ["ts"] = stamp,
                    ["level"] = level,
                    ["component"] = Component,
                    ["event"] = name,
                    ["fields"] = scrubbed
                }));
            }
            catch
            {
                // Logging must never kill the launcher.
            }
        }
    }

    /// <summary>An event that also earns this run an <c>error.txt</c>.</summary>
    internal void Failure(string name, string message, string? detail = null)
    {
        lock (_gate) _failures.Add(Redaction.Scrub($"{name}: {message}{(detail is null ? "" : "\n" + detail)}"));
        Event("error", name, new Dictionary<string, object?> { ["message"] = message, ["detail"] = detail });
    }

    /// <summary>
    /// Record which content the launcher is about to mount, from the selection it
    /// actually validated — never from a path it merely intends to use.
    /// </summary>
    internal void CaptureIdentity(ContentSelectionValidation? selection, string? installRoot, string? channel)
    {
        var selectionPath = selection is null ? null : Path.Combine(selection.ContentRoot, "selection.json");
        var document = new Dictionary<string, object?>
        {
            ["schema"] = "openbfme.diagnostics.identity",
            ["schemaVersion"] = 1,
            ["component"] = Component,
            ["capturedFrom"] = "ContentPackCatalog.ValidateSelection",
            ["capturedAt"] = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
            ["contentRoot"] = Redaction.Scrub(selection?.ContentRoot),
            ["selectionOk"] = selection?.Ok,
            ["selectionReason"] = Redaction.Scrub(selection?.Reason),
            ["activePack"] = selection?.ActivePackKey,
            ["activePackDigest"] = DigestOf(selection?.ActivePackKey),
            ["supplementalPacks"] = selection?.SupplementalPackKeys ?? (IReadOnlyList<string>)Array.Empty<string>(),
            ["selectionPath"] = Redaction.Scrub(selectionPath),
            ["selectionSha256"] = Sha256OfFile(selectionPath),
            ["installRoot"] = Redaction.Scrub(installRoot),
            ["channel"] = channel
        };
        WriteJson("identity.json", document);
        Event("info", "content.identity", new Dictionary<string, object?>
        {
            ["activePack"] = selection?.ActivePackKey,
            ["selectionSha256"] = document["selectionSha256"],
            ["ok"] = selection?.Ok
        });
    }

    /// <summary>"" when unreadable. Never a fabricated digest.</summary>
    internal static string Sha256OfFile(string? path)
    {
        if (string.IsNullOrEmpty(path) || !File.Exists(path)) return string.Empty;
        try
        {
            using var stream = File.OpenRead(path);
            return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
        }
        catch
        {
            return string.Empty;
        }
    }

    private static string DigestOf(string? packKey)
    {
        if (string.IsNullOrEmpty(packKey)) return string.Empty;
        var slash = packKey.LastIndexOf('/');
        return slash < 0 ? string.Empty : packKey[(slash + 1)..];
    }

    private void WriteEnvironmentDocument()
    {
        var variables = new Dictionary<string, object?>();
        foreach (var name in EnvironmentAllowList)
        {
            var value = Environment.GetEnvironmentVariable(name);
            if (string.IsNullOrEmpty(value)) continue;
            variables[name] = Redaction.IsSecretName(name) ? "<redacted>" : Redaction.Scrub(value);
        }
        var assembly = typeof(DiagnosticsRun).Assembly;
        WriteJson("env.json", new Dictionary<string, object?>
        {
            ["schema"] = "openbfme.diagnostics.env",
            ["schemaVersion"] = 1,
            ["component"] = Component,
            ["capturedAt"] = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
            ["pid"] = Environment.ProcessId,
            ["os"] = new Dictionary<string, object?>
            {
                ["description"] = System.Runtime.InteropServices.RuntimeInformation.OSDescription,
                ["architecture"] = System.Runtime.InteropServices.RuntimeInformation.OSArchitecture.ToString(),
                ["processArchitecture"] = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
                ["processorCount"] = Environment.ProcessorCount,
                ["is64Bit"] = Environment.Is64BitOperatingSystem,
                ["wine"] = PlatformCompat.IsWine
            },
            ["runtime"] = new Dictionary<string, object?>
            {
                ["framework"] = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
                ["clr"] = Environment.Version.ToString()
            },
            ["launcher"] = new Dictionary<string, object?>
            {
                ["version"] = assembly.GetName().Version?.ToString(),
                ["informationalVersion"] =
                    assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion,
                ["location"] = Redaction.Scrub(AppContext.BaseDirectory)
            },
            ["environment"] = variables,
            ["git"] = new Dictionary<string, object?> { ["commit"] = ResolveGitCommit() }
        });
    }

    /// <summary>
    /// Best effort, and honestly empty when it cannot be resolved. A shipped
    /// launcher has no <c>.git</c>; this only answers for a repository run.
    /// </summary>
    private static string ResolveGitCommit()
    {
        try
        {
            var informational = typeof(DiagnosticsRun).Assembly
                .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
            if (informational is not null && informational.Contains('+'))
                return informational[(informational.IndexOf('+') + 1)..];
            var directory = new DirectoryInfo(AppContext.BaseDirectory);
            while (directory is not null)
            {
                var head = Path.Combine(directory.FullName, ".git", "HEAD");
                if (File.Exists(head))
                {
                    var text = File.ReadAllText(head).Trim();
                    if (!text.StartsWith("ref:", StringComparison.Ordinal)) return text;
                    var reference = text[4..].Trim();
                    var referencePath = Path.Combine(directory.FullName, ".git", reference.Replace('/', Path.DirectorySeparatorChar));
                    return File.Exists(referencePath) ? File.ReadAllText(referencePath).Trim() : string.Empty;
                }
                directory = directory.Parent;
            }
        }
        catch
        {
            // Never fail a launch over provenance sugar.
        }
        return string.Empty;
    }

    private void WriteErrorDocument()
    {
        var builder = new StringBuilder();
        builder.AppendLine($"Open BFME {Component} failure report");
        builder.AppendLine($"captured {DateTime.UtcNow:o}");
        builder.AppendLine($"run directory: {Redaction.Scrub(Directory)}");
        builder.AppendLine();
        builder.AppendLine($"FAILURES ({_failures.Count})");
        foreach (var failure in _failures) builder.AppendLine($"  - {failure}");
        builder.AppendLine();
        builder.AppendLine($"LAST {_tail.Count} EVENTS");
        foreach (var line in _tail) builder.AppendLine($"  {line}");
        try { File.WriteAllText(Path.Combine(Directory, "error.txt"), builder.ToString()); }
        catch { /* the failure is already recorded in run.jsonl */ }
    }

    private void WriteJson(string name, IReadOnlyDictionary<string, object?> document)
    {
        try
        {
            File.WriteAllText(
                Path.Combine(Directory, name),
                JsonSerializer.Serialize(document, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch
        {
            // Logging must never kill the launcher.
        }
    }

    private static Dictionary<string, object?> ScrubFields(IReadOnlyDictionary<string, object?>? fields)
    {
        var scrubbed = new Dictionary<string, object?>();
        if (fields is null) return scrubbed;
        foreach (var (key, value) in fields)
            scrubbed[key] = Redaction.IsSecretName(key) ? "<redacted>" : ScrubValue(value);
        return scrubbed;
    }

    private static object? ScrubValue(object? value) => value switch
    {
        string text => Redaction.Scrub(text),
        IReadOnlyDictionary<string, object?> map => ScrubFields(map),
        IEnumerable<string> list => list.Select(Redaction.Scrub).ToArray(),
        _ => value
    };

    public void Dispose() => Close();

    internal void Close()
    {
        if (_closed) return;
        Event("info", "run.end", new Dictionary<string, object?>
        {
            ["events"] = _events,
            ["failures"] = _failures.Count
        });
        _closed = true;
        if (_failures.Count > 0) WriteErrorDocument();
        try { _log?.Dispose(); } catch { /* ignore */ }
        try { _jsonl?.Dispose(); } catch { /* ignore */ }
    }
}

/// <summary>
/// The launcher process's own run, plus the seam every call site uses. A no-op
/// when the run could not be created, so no caller has to null-check.
/// </summary>
internal static class DiagnosticsLog
{
    internal const string ComponentName = "launcher";

    private static readonly ConcurrentDictionary<string, byte> Started = new();
    private static DiagnosticsRun? _run;

    internal static DiagnosticsRun? Current => _run;
    internal static string? RunDirectory => _run?.Directory;

    internal static void Start()
    {
        if (!Started.TryAdd("run", 0)) return;
        _run = DiagnosticsRun.Start(ComponentName);
    }

    internal static void Event(string level, string name, IReadOnlyDictionary<string, object?>? fields = null) =>
        _run?.Event(level, name, fields);

    internal static void Failure(string name, string message, string? detail = null) =>
        _run?.Failure(name, message, detail);

    internal static void CaptureIdentity(ContentSelectionValidation? selection, string? installRoot, string? channel) =>
        _run?.CaptureIdentity(selection, installRoot, channel);

    internal static void Close()
    {
        _run?.Close();
        _run = null;
    }
}
