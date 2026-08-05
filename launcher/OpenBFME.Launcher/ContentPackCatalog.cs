using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenBFME.Launcher;

/// <summary>
/// One installed content pack or loose mod discovered under a content root.
/// </summary>
public sealed record ContentPackEntry(
    string Id,
    string Title,
    string Version,
    string Kind,
    string Role,
    string RelativeKey,
    string RootPath,
    string PackJsonPath,
    int Priority,
    bool IsRetailDerived,
    long ApproximateBytes);

/// <summary>
/// Snapshot of packs/mods under one resolved content root, plus selection state.
/// </summary>
public sealed record ContentPackInventory(
    string ContentRoot,
    string Source,
    string? ActivePackKey,
    IReadOnlyList<string> SupplementalPackKeys,
    IReadOnlyList<ContentPackEntry> Packs,
    IReadOnlyList<ContentPackEntry> Mods,
    string? SelectionPath,
    string? Diagnostic);

internal sealed record PackSelectionDocument(
    [property: JsonPropertyName("schema")] string Schema,
    [property: JsonPropertyName("schemaVersion")] int SchemaVersion,
    [property: JsonPropertyName("activePack")] string ActivePack,
    [property: JsonPropertyName("supplementalPacks")] IReadOnlyList<string>? SupplementalPacks);

/// <summary>
/// Finds converted asset packs and loose mods the way the Godot client does:
/// <c>selection.json</c> under a content root, immutable
/// <c>&lt;pack-id&gt;/&lt;bundle-hash&gt;/pack.json</c> trees, plus loose mod folders.
/// </summary>
public static class ContentPackCatalog
{
    public const string SelectionSchema = "openbfme.pack-selection";
    public const int SelectionSchemaVersion = 0;
    public const string ContentPackSchema = "openbfme.content-pack";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
        WriteIndented = true
    };

    /// <summary>
    /// Resolve the content root the UI and Play must both use.
    /// Order when no override: OPENBFME_CONTENT → install
    /// <c>.private/content-packs</c> → nearby repo workspace → Godot user cache.
    /// An explicit override always wins and is never replaced by a richer root
    /// (so the pack list and OPENBFME_CONTENT cannot disagree).
    /// Prefer roots that actually contain packs/mods; a bare/stale
    /// <c>selection.json</c> alone does not shadow a populated root.
    /// </summary>
    public static ContentPackInventory Discover(string installRoot, string? contentRootOverride = null)
    {
        // Explicit override: scan only that folder (even if empty). Never fall through.
        if (!string.IsNullOrWhiteSpace(contentRootOverride))
        {
            string full;
            try { full = Path.GetFullPath(contentRootOverride.Trim()); }
            catch (Exception error)
            {
                return EmptyInventory(
                    contentRootOverride.Trim(),
                    "override",
                    $"Content root path is invalid: {error.Message}");
            }

            if (!Directory.Exists(full))
            {
                return EmptyInventory(
                    full,
                    "override",
                    "Content root does not exist yet. Convert assets or pick a folder " +
                    "that already has pack bundles / selection.json.");
            }

            return ScanRoot(full, "override");
        }

        ContentPackInventory? weak = null;
        foreach (var candidate in EnumerateContentRoots(installRoot, contentRootOverride: null))
        {
            var inventory = ScanRoot(candidate.Path, candidate.Source);
            if (inventory.Packs.Count > 0 || inventory.Mods.Count > 0)
                return inventory;

            // Selection-only / empty tree: remember the first as a weak fallback,
            // but keep looking for a root that actually has packs.
            if (weak is null &&
                (inventory.ActivePackKey is not null || inventory.SelectionPath is not null))
            {
                weak = inventory with
                {
                    Diagnostic = inventory.Diagnostic ??
                        "This content root has a selection.json but no pack bundles on disk."
                };
            }
        }

        if (weak is not null)
            return weak;

        var fallback = DefaultContentRoot(installRoot);
        return EmptyInventory(
            fallback,
            "install",
            "No content packs found yet. Convert BFME II / RotWK from Settings, " +
            "or point Content root at a folder that contains selection.json / pack bundles.");
    }

    private static ContentPackInventory EmptyInventory(string root, string source, string diagnostic) =>
        new(
            root,
            source,
            null,
            Array.Empty<string>(),
            Array.Empty<ContentPackEntry>(),
            Array.Empty<ContentPackEntry>(),
            File.Exists(Path.Combine(root, "selection.json"))
                ? Path.Combine(root, "selection.json")
                : null,
            diagnostic);

    public static IEnumerable<(string Path, string Source)> EnumerateContentRoots(
        string installRoot, string? contentRootOverride = null)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var list = new List<(string Path, string Source)>();

        void Add(string? path, string source)
        {
            if (string.IsNullOrWhiteSpace(path)) return;
            string full;
            try { full = Path.GetFullPath(path); }
            catch { return; }
            if (!Directory.Exists(full)) return;
            if (!seen.Add(full)) return;
            list.Add((full, source));
        }

        if (!string.IsNullOrWhiteSpace(contentRootOverride))
            Add(contentRootOverride, "override");

        var env = Environment.GetEnvironmentVariable("OPENBFME_CONTENT");
        if (!string.IsNullOrWhiteSpace(env))
            Add(env, "environment");

        Add(Path.Combine(Path.GetFullPath(installRoot), ".private", "content-packs"), "install");

        foreach (var seed in new[]
                 {
                     Environment.CurrentDirectory,
                     AppContext.BaseDirectory,
                     Path.GetDirectoryName(Environment.ProcessPath) ?? ""
                 })
        {
            if (string.IsNullOrWhiteSpace(seed)) continue;
            foreach (var workspace in WalkForWorkspace(seed))
                Add(workspace, "workspace");
        }

        // Godot durable user cache (user://content-packs) when present.
        var godotUser = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "Godot", "app_userdata");
        if (Directory.Exists(godotUser))
        {
            try
            {
                foreach (var project in Directory.EnumerateDirectories(godotUser))
                {
                    var packs = Path.Combine(project, "content-packs");
                    if (Directory.Exists(packs))
                        Add(packs, "godot-user");
                }
            }
            catch
            {
                // Best-effort scan only.
            }
        }

        return list;
    }

    private static IEnumerable<string> WalkForWorkspace(string start)
    {
        string? current;
        try { current = Path.GetFullPath(start); }
        catch { yield break; }

        for (var depth = 0; depth < 8 && !string.IsNullOrEmpty(current); depth++)
        {
            var candidate = Path.Combine(current, ".private", "content-packs");
            if (Directory.Exists(candidate))
                yield return candidate;

            // Repo layout: open-bfme/.private/content-packs while exe lives under launcher/...
            var parent = Directory.GetParent(current);
            if (parent is null) yield break;
            current = parent.FullName;
        }
    }

    public static ContentPackInventory ScanRoot(string contentRoot, string source)
    {
        var root = Path.GetFullPath(contentRoot);
        var selectionPath = Path.Combine(root, "selection.json");
        string? activeKey = null;
        IReadOnlyList<string> supplements = Array.Empty<string>();
        string? diagnostic = null;

        if (File.Exists(selectionPath))
        {
            try
            {
                var doc = JsonSerializer.Deserialize<PackSelectionDocument>(
                    File.ReadAllBytes(selectionPath), JsonOptions);
                if (doc is null ||
                    !string.Equals(doc.Schema, SelectionSchema, StringComparison.Ordinal) ||
                    doc.SchemaVersion != SelectionSchemaVersion)
                {
                    diagnostic = "selection.json has an unsupported schema.";
                }
                else
                {
                    activeKey = NormalizeKey(doc.ActivePack);
                    supplements = (doc.SupplementalPacks ?? Array.Empty<string>())
                        .Select(NormalizeKey)
                        .Where(static k => k.Length > 0)
                        .Distinct(StringComparer.OrdinalIgnoreCase)
                        .ToArray();
                }
            }
            catch (Exception error)
            {
                diagnostic = $"selection.json could not be read: {error.Message}";
            }
        }

        var packs = new List<ContentPackEntry>();
        var mods = new List<ContentPackEntry>();
        var seenKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        // Immutable published layout: <pack-id>/<bundle-hash>/pack.json
        try
        {
            foreach (var packIdDir in Directory.EnumerateDirectories(root))
            {
                var packIdName = Path.GetFileName(packIdDir);
                if (packIdName.StartsWith(".", StringComparison.Ordinal)) continue;

                // Direct pack root (loose): pack.json immediately under content root child.
                var directPackJson = Path.Combine(packIdDir, "pack.json");
                if (File.Exists(directPackJson))
                {
                    TryAddEntry(directPackJson, packIdName, packIdDir, activeKey, supplements,
                        packs, mods, seenKeys);
                    continue;
                }

                foreach (var bundleDir in Directory.EnumerateDirectories(packIdDir))
                {
                    var packJson = Path.Combine(bundleDir, "pack.json");
                    if (!File.Exists(packJson)) continue;
                    var relative = NormalizeKey(packIdName + "/" + Path.GetFileName(bundleDir));
                    TryAddEntry(packJson, relative, bundleDir, activeKey, supplements,
                        packs, mods, seenKeys);
                }
            }
        }
        catch (Exception error)
        {
            diagnostic = diagnostic is null
                ? $"Could not scan packs: {error.Message}"
                : diagnostic + " " + error.Message;
        }

        // Loose mods living next to the content root or under install-root/mods.
        foreach (var modsDir in EnumerateModDirectories(root))
        {
            try
            {
                foreach (var modDir in Directory.EnumerateDirectories(modsDir))
                {
                    var packJson = Path.Combine(modDir, "pack.json");
                    if (!File.Exists(packJson)) continue;
                    var relative = "mods/" + Path.GetFileName(modDir);
                    if (!seenKeys.Add(relative)) continue;
                    var meta = ReadPackMeta(packJson);
                    if (meta is null) continue;
                    mods.Add(new ContentPackEntry(
                        meta.Id,
                        meta.Title,
                        meta.Version,
                        "mod",
                        "Installed",
                        relative,
                        modDir,
                        packJson,
                        meta.Priority,
                        IsRetailDerived: false,
                        ApproximateDirectoryBytes(modDir)));
                }
            }
            catch
            {
                // Skip unreadable mod trees.
            }
        }

        packs.Sort(static (a, b) =>
        {
            var role = RoleRank(a.Role).CompareTo(RoleRank(b.Role));
            if (role != 0) return role;
            var pri = b.Priority.CompareTo(a.Priority);
            if (pri != 0) return pri;
            return string.Compare(a.Title, b.Title, StringComparison.OrdinalIgnoreCase);
        });
        mods.Sort(static (a, b) =>
            string.Compare(a.Title, b.Title, StringComparison.OrdinalIgnoreCase));

        return new ContentPackInventory(
            root,
            source,
            activeKey,
            supplements,
            packs,
            mods,
            File.Exists(selectionPath) ? selectionPath : null,
            diagnostic);
    }

    /// <summary>
    /// Point selection.json at <paramref name="relativeKey"/> (<c>id/hash</c>),
    /// keeping existing supplemental entries that still exist on disk.
    /// Keys are fail-closed: no <c>..</c>, no absolute paths, must stay under
    /// <paramref name="contentRoot"/>.
    /// </summary>
    public static void SelectActivePack(string contentRoot, string relativeKey)
    {
        var root = Path.GetFullPath(contentRoot);
        var key = NormalizeKey(relativeKey);
        if (!IsSafeBundleKey(key))
            throw new InvalidOperationException(
                "Active pack must be a safe relative key of the form pack-id/bundle-hash " +
                "(no '..', no absolute paths).");

        var packRoot = ResolveContainedPath(root, key)
            ?? throw new InvalidOperationException(
                "Active pack path escapes the content root.");
        var packJson = Path.Combine(packRoot, "pack.json");
        if (!File.Exists(packJson))
            throw new FileNotFoundException("That pack bundle is not installed.", packJson);

        var selectionPath = Path.Combine(root, "selection.json");
        var supplements = new List<string>();
        if (File.Exists(selectionPath))
        {
            try
            {
                var existing = JsonSerializer.Deserialize<PackSelectionDocument>(
                    File.ReadAllBytes(selectionPath), JsonOptions);
                if (existing?.SupplementalPacks is not null)
                {
                    foreach (var entry in existing.SupplementalPacks)
                    {
                        var normalized = NormalizeKey(entry);
                        if (!IsSafeBundleKey(normalized) ||
                            normalized.Equals(key, StringComparison.OrdinalIgnoreCase))
                            continue;
                        if (ResolveContainedPath(root, normalized) is null)
                            continue;
                        var path = Path.Combine(root,
                            normalized.Replace('/', Path.DirectorySeparatorChar), "pack.json");
                        if (File.Exists(path))
                            supplements.Add(normalized);
                    }
                }
            }
            catch
            {
                // Start clean if prior selection was corrupt.
            }
        }

        Directory.CreateDirectory(root);
        var document = new PackSelectionDocument(
            SelectionSchema,
            SelectionSchemaVersion,
            key,
            supplements.Count == 0 ? null : supplements);
        var bytes = JsonSerializer.SerializeToUtf8Bytes(document, JsonOptions);
        var temporary = selectionPath + "." + Guid.NewGuid().ToString("N") + ".tmp";
        File.WriteAllBytes(temporary, bytes);
        File.Move(temporary, selectionPath, overwrite: true);
    }

    /// <summary>
    /// Bundle keys are exactly <c>pack-id/bundle-hash</c> with safe path segments.
    /// </summary>
    public static bool IsSafeBundleKey(string? key)
    {
        if (string.IsNullOrWhiteSpace(key)) return false;
        var normalized = NormalizeKey(key);
        if (normalized.Length == 0 || normalized.Contains(':', StringComparison.Ordinal))
            return false;
        var parts = normalized.Split('/');
        if (parts.Length != 2) return false;
        foreach (var part in parts)
        {
            if (part.Length == 0 || part is "." or ".." ||
                part.Contains('\\', StringComparison.Ordinal))
                return false;
        }
        return true;
    }

    /// <summary>
    /// Combine <paramref name="relativeKey"/> under <paramref name="root"/> and
    /// require the full path to stay inside the root (no link-following needed for
    /// selection writes; lexical containment after GetFullPath).
    /// </summary>
    public static string? ResolveContainedPath(string root, string relativeKey)
    {
        if (!IsSafeBundleKey(relativeKey) && !IsSafeLooseRelative(relativeKey))
            return null;
        string fullRoot;
        string candidate;
        try
        {
            fullRoot = Path.GetFullPath(root);
            candidate = Path.GetFullPath(Path.Combine(
                fullRoot, relativeKey.Replace('/', Path.DirectorySeparatorChar)));
        }
        catch
        {
            return null;
        }

        var prefix = fullRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                     + Path.DirectorySeparatorChar;
        if (!candidate.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) &&
            !candidate.Equals(fullRoot, StringComparison.OrdinalIgnoreCase))
            return null;
        return candidate;
    }

    private static bool IsSafeLooseRelative(string key)
    {
        var normalized = NormalizeKey(key);
        if (normalized.Length == 0 || normalized.Contains(':', StringComparison.Ordinal))
            return false;
        foreach (var part in normalized.Split('/'))
        {
            if (part.Length == 0 || part is "." or ".." ||
                part.Contains('\\', StringComparison.Ordinal))
                return false;
        }
        return true;
    }

    public static string EnsureModsDirectory(string installRoot)
    {
        var mods = Path.Combine(Path.GetFullPath(installRoot), "mods");
        Directory.CreateDirectory(mods);
        return mods;
    }

    public static string DefaultContentRoot(string installRoot) =>
        Path.Combine(Path.GetFullPath(installRoot), ".private", "content-packs");

    private static void TryAddEntry(
        string packJson,
        string relativeKey,
        string packRoot,
        string? activeKey,
        IReadOnlyList<string> supplements,
        List<ContentPackEntry> packs,
        List<ContentPackEntry> mods,
        HashSet<string> seenKeys)
    {
        if (!seenKeys.Add(relativeKey)) return;
        var meta = ReadPackMeta(packJson);
        if (meta is null) return;

        var isContentPack = meta.HasContentPackSchema;
        var role = "Installed";
        if (activeKey is not null &&
            relativeKey.Equals(activeKey, StringComparison.OrdinalIgnoreCase))
            role = "Active";
        else if (supplements.Any(s => s.Equals(relativeKey, StringComparison.OrdinalIgnoreCase)))
            role = "Supplement";

        var entry = new ContentPackEntry(
            meta.Id,
            meta.Title,
            meta.Version,
            isContentPack ? "pack" : "mod",
            role,
            relativeKey,
            packRoot,
            packJson,
            meta.Priority,
            meta.IsRetailDerived,
            ApproximateDirectoryBytes(packRoot));

        if (isContentPack) packs.Add(entry);
        else mods.Add(entry);
    }

    private static IEnumerable<string> EnumerateModDirectories(string contentRoot)
    {
        yield return Path.Combine(contentRoot, "mods");
        var privateParent = Directory.GetParent(contentRoot);
        if (privateParent is not null)
        {
            yield return Path.Combine(privateParent.FullName, "mods");
            var install = privateParent.Parent;
            if (install is not null)
                yield return Path.Combine(install.FullName, "mods");
        }

        // Repo game/mods next to .private/content-packs → ../../game/mods
        string? repoGuess = null;
        try
        {
            repoGuess = Path.GetFullPath(Path.Combine(contentRoot, "..", "..", "game", "mods"));
        }
        catch
        {
            // ignore
        }
        if (repoGuess is not null && Directory.Exists(repoGuess))
            yield return repoGuess;
    }

    private sealed record PackMeta(
        string Id, string Title, string Version, int Priority,
        bool HasContentPackSchema, bool IsRetailDerived);

    private static PackMeta? ReadPackMeta(string packJsonPath)
    {
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllBytes(packJsonPath));
            var root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object) return null;

            var id = ReadString(root, "id");
            if (string.IsNullOrWhiteSpace(id)) return null;

            var schema = ReadString(root, "schema");
            var hasSchema = string.Equals(schema, ContentPackSchema, StringComparison.Ordinal);
            if (hasSchema)
            {
                if (root.TryGetProperty("schemaVersion", out var ver) &&
                    ver.ValueKind == JsonValueKind.Number &&
                    ver.GetInt32() != 0)
                    return null;
            }

            var title = FirstNonEmpty(
                ReadString(root, "displayName"),
                ReadString(root, "title"),
                ReadString(root, "name"),
                id);
            var version = FirstNonEmpty(ReadString(root, "version"), "—");
            var priority = 0;
            if (root.TryGetProperty("priority", out var pri) &&
                pri.ValueKind == JsonValueKind.Number)
                priority = pri.GetInt32();

            var retail = root.TryGetProperty("local_retail_import", out var lri) &&
                         lri.ValueKind == JsonValueKind.True;
            if (!retail && root.TryGetProperty("redistributable", out var redist) &&
                redist.ValueKind == JsonValueKind.False)
                retail = hasSchema; // private converted packs typically mark non-redistributable

            return new PackMeta(id!, title!, version!, priority, hasSchema, retail);
        }
        catch
        {
            return null;
        }
    }

    private static string? ReadString(JsonElement root, string name) =>
        root.TryGetProperty(name, out var el) && el.ValueKind == JsonValueKind.String
            ? el.GetString()
            : null;

    private static string FirstNonEmpty(params string?[] values)
    {
        foreach (var value in values)
        {
            if (!string.IsNullOrWhiteSpace(value))
                return value.Trim();
        }
        return "";
    }

    private static string NormalizeKey(string? key)
    {
        if (string.IsNullOrWhiteSpace(key)) return "";
        return key.Trim().Replace('\\', '/').Trim('/');
    }

    private static int RoleRank(string role) => role switch
    {
        "Active" => 0,
        "Supplement" => 1,
        _ => 2
    };

    /// <summary>
    /// Cheap size hint only — never walk the whole tree (that froze the UI for
    /// multi-second Discover on large retail pack sets). Prefer pack.json size
    /// as a stand-in; UI shows "size n/a" when zero.
    /// </summary>
    private static long ApproximateDirectoryBytes(string directory)
    {
        try
        {
            var packJson = Path.Combine(directory, "pack.json");
            return File.Exists(packJson) ? new FileInfo(packJson).Length : 0;
        }
        catch
        {
            return 0;
        }
    }
}
