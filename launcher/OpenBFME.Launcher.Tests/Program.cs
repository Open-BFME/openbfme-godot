using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using OpenBFME.Launcher;

var tests = new (string Name, Func<Task> Run)[]
{
    ("options", TestOptions),
    ("release source", TestReleaseSource),
    ("retail discovery", TestRetailDiscovery),
    ("language file selection", TestLanguageFileSelection),
    ("provision path safety", TestProvisionPathSafety),
    ("pinned payload manifest", TestPinnedPayloadManifest),
    ("empty pinned manifest refusal", TestEmptyPinnedManifestRefusal),
    ("pinned digest mismatch abort", TestPinnedDigestMismatchAbort),
    ("pinned digest match installs", TestPinnedDigestMatchInstalls),
    ("workshop host allowlist rejection", TestWorkshopHostAllowlistRejection),
    ("third-party download disclosure", TestDownloadDisclosureShownOnce),
    ("install cancellation", TestInstallCancellation),
    ("url policy", TestUrlPolicy),
    ("manifest validation", TestManifest),
    ("manifest signature", TestManifestSignature),
    ("install update rollback", TestInstallAndRollback),
    ("version retention", TestVersionRetention),
    ("channel and downgrade policy", TestChannelAndDowngrade),
    ("abandoned update scratch", TestAbandonedScratch),
    ("damaged obsolete version pruning", TestDamagedObsoletePruning),
    ("self update degradation", TestSelfUpdateDegradation),
    ("orderable release version", TestOrderableVersion),
    ("bounded release metadata", TestBoundedMetadata),
    ("declared entry size", TestDeclaredEntrySize),
    ("zip traversal", TestTraversal),
    ("Windows zip aliases", TestWindowsArchiveAliases),
    ("hash mismatch", TestHashMismatch),
    ("scalar importer progress", TestScalarImporterProgress),
    ("diagnostic redaction", TestRedaction),
    ("bundle inventory", TestBundleInventory),
    ("content pack catalog", TestContentPackCatalog)
};

var failed = 0;
foreach (var test in tests)
{
    try { await test.Run(); Console.WriteLine($"PASS {test.Name}"); }
    catch (Exception error) { failed++; Console.Error.WriteLine($"FAIL {test.Name}: {error}"); }
}
if (failed != 0) return 1;
Console.WriteLine("LAUNCHER_TESTS_PASS");
return 0;

static Task TestOptions()
{
    var root = Path.Combine(Path.GetTempPath(), "openbfme-options");
    var options = LauncherOptions.Parse(new[]
    {
        "--channel", "playtest", "--install-root", root,
        "--import-bfme2", "--bfme2-path", Path.Combine(Path.GetTempPath(), "retail-fixture")
    });
    Check(options.ImportGame == "bfme2", "flags not parsed");
    Check(options.Channel == "playtest", "channel not parsed");
    // Every channel gets a feed. Leaving playtest without one meant "Check for update"
    // silently did nothing for exactly the people who are on the playtest channel.
    Check(options.ManifestUri?.AbsoluteUri.Contains("/releases/latest/", StringComparison.Ordinal) == true,
        "playtest channel did not receive the default update feed");
    Throws<ArgumentException>(() => LauncherOptions.Parse(new[] { "--import-bfme2", "--import-rotwk" }));
    var stable = LauncherOptions.Parse(Array.Empty<string>());
    Check(stable.ManifestUri?.AbsoluteUri.Contains("/releases/latest/", StringComparison.Ordinal) == true,
        "stable channel did not receive the default update feed");

    // A typo must never fall through to the defaults: that silently moved the player
    // to a different channel than the one they asked for.
    Throws<ArgumentException>(() => LauncherOptions.Parse(new[] { "--chanel", "playtest" }));
    Throws<ArgumentException>(() => LauncherOptions.Parse(new[] { "--channel" }));
    Throws<ArgumentException>(() => LauncherOptions.Parse(new[] { "--channel", "beta" }));
    Throws<ArgumentException>(() => LauncherOptions.Parse(new[] { "playtest" }));
    Throws<ArgumentException>(() => LauncherOptions.Parse(new[] { "--channel", "stable", "--channel", "nightly" }));

    var provision = LauncherOptions.Parse(new[]
    {
        "--provision-bfme2", "--provision-rotwk", "--discover-release",
        "--retail-install-root", root, "--no-update", "--headless"
    });
    Check(provision.ProvisionBfme2 && provision.ProvisionRotwk, "provision flags not parsed");
    Check(provision.DiscoverRelease, "discover-release not parsed");
    Check(provision.NoUpdate && provision.Headless, "headless/no-update not parsed");
    Check(provision.RetailInstallRoot == Path.GetFullPath(root), "retail-install-root not parsed");
    Check(!provision.ManifestUriExplicit, "default manifest must not count as explicit");
    var explicitManifest = LauncherOptions.Parse(new[]
    {
        "--manifest-url", ReleaseSource.LatestManifestUri.AbsoluteUri
    });
    Check(explicitManifest.ManifestUriExplicit, "explicit --manifest-url was not flagged");
    return Task.CompletedTask;
}

static Task TestLanguageFileSelection()
{
    var files = new List<AllInOneRetailProvisioner.WorkshopFile>
    {
        new("game.dat", "ALL", "aaa", 10, "https://example/a"),
        new("game.dat", "EN", "bbb", 11, "https://example/b"),
        new("lang\\french.csf", "FR", "ccc", 2, "https://example/c"),
        new("audio.big", "ALL", "ddd", 100, "https://example/d"),
        new("..\\escape.dat", "ALL", "eee", 1, "https://example/e"),
    };
    var selected = AllInOneRetailProvisioner.SelectLanguageFiles(files, "EN");
    Check(selected.Count == 2, $"expected 2 English/ALL files, got {selected.Count}");
    var gameDat = selected.Single(f => f.Name.Equals("game.dat", StringComparison.OrdinalIgnoreCase));
    Check(gameDat.Language == "EN", "language-specific game.dat should beat ALL");
    Check(selected.All(f => !f.Name.Contains("..", StringComparison.Ordinal)), "path traversal slipped through");
    return Task.CompletedTask;
}

static Task TestProvisionPathSafety()
{
    // Default path is under the install root, never a drive root.
    var home = Path.Combine(Path.GetTempPath(), "openbfme-home-" + Guid.NewGuid().ToString("N"));
    var path = AllInOneRetailProvisioner.DefaultInstallPath(home, "bfme2");
    Check(path.Contains("retail-installs", StringComparison.OrdinalIgnoreCase), "unexpected provision path");
    Check(path.StartsWith(Path.GetFullPath(home), StringComparison.OrdinalIgnoreCase),
        "provision path escaped the install root");
    Check(AllInOneRetailProvisioner.WorkshopGuid("bfme2") == "original-BFME2", "bfme2 guid");
    Check(AllInOneRetailProvisioner.WorkshopGuid("rotwk") == "original-RotWK", "rotwk guid");
    Throws<ArgumentOutOfRangeException>(() => AllInOneRetailProvisioner.WorkshopGuid("halo"));

    AllInOneRetailProvisioner.ValidateWorkshopUri(
        new Uri("https://workshop-files.bfmeladder.com/x/y"), "ok.big");
    Throws<InvalidDataException>(() => AllInOneRetailProvisioner.ValidateWorkshopUri(
        new Uri("https://evil.example/payload.big"), "bad.big"));
    Throws<InvalidDataException>(() => AllInOneRetailProvisioner.ValidateWorkshopUri(
        new Uri("http://bfmeladder.com/insecure"), "http.big"));
    return Task.CompletedTask;
}

// ---------------------------------------------------------------------------
// Optional third-party retail download: pinned-digest verification + disclosure.
//
// The old code verified multi-hundred-MB .big files by SIZE only, which proves
// nothing about content — a substituted payload of the right length installed
// clean. These tests hold the line that replaced it: nothing is written unless it
// matches a digest pinned in a repo-tracked manifest, and an empty manifest means
// the path refuses to run rather than falling back to "close enough".
// ---------------------------------------------------------------------------

static Task TestPinnedPayloadManifest()
{
    // The shipped manifest is pinned from a maintainer's lawfully owned BFME II and
    // RotWK installations (digests only — a hash is not content). It must stay
    // structurally sound: the provisioner trusts nothing else.
    var shipped = RetailPayloadManifest.Embedded;
    Check(!shipped.IsEmpty, "the shipped pinned manifest is empty; the download path cannot run");
    foreach (var game in new[] { "bfme2", "rotwk" })
    {
        var payload = shipped.RequirePayload(game, "EN");
        Check(payload.Files.Count > 200,
            $"{game} pins only {payload.Files.Count} files; a real install has hundreds");
        Check(payload.Files.All(f => RetailPayloadManifest.IsSha256Hex(f.Sha256)),
            $"{game} has an entry without a well-formed SHA-256");
        Check(payload.Files.All(f => f.Size > 0), $"{game} has a zero-size entry");
        Check(payload.Files.All(f => RetailPayloadManifest.NormalizeRelativePath(f.Path) is not null),
            $"{game} has an entry whose path escapes the install root");
        // The install marker has to be among them or a "successful" provision would
        // produce a tree the rest of the launcher refuses to recognise.
        Check(payload.Files.Any(f =>
                f.Path.Equals(RetailDiscovery.InstallMarker, StringComparison.OrdinalIgnoreCase)),
            $"{game} does not pin {RetailDiscovery.InstallMarker}");
    }
    // Digests must be distinct per game: pinning one install twice would be a copy/paste
    // error that no other assertion here would catch.
    Check(shipped.RequirePayload("bfme2", "EN").Files.First(f => f.Path == RetailDiscovery.InstallMarker).Sha256 !=
          shipped.RequirePayload("rotwk", "EN").Files.First(f => f.Path == RetailDiscovery.InstallMarker).Sha256,
        "bfme2 and rotwk pin the same game.dat digest");

    var good = RetailPayloadManifest.Parse(PinnedManifestJson("game.dat", 4, Sha256Hex(new byte[] { 1, 2, 3, 4 })));
    Check(!good.IsEmpty, "a populated manifest reported itself empty");
    Check(good.RequirePayload("bfme2", "EN").Files.Count == 1, "pinned entry did not round-trip");

    // Every field is load-bearing, so every field is checked.
    Throws<InvalidDataException>(() => RetailPayloadManifest.Parse(
        PinnedManifestJson("game.dat", 4, "not-a-digest")));
    Throws<InvalidDataException>(() => RetailPayloadManifest.Parse(
        PinnedManifestJson("game.dat", 0, Sha256Hex(new byte[] { 1 }))));
    Throws<InvalidDataException>(() => RetailPayloadManifest.Parse(
        PinnedManifestJson("..\\escape.dat", 4, Sha256Hex(new byte[] { 1, 2, 3, 4 }))));
    Throws<InvalidDataException>(() => RetailPayloadManifest.Parse(
        """{"schema":"something.else","schemaVersion":1,"payloads":[]}"""));
    Throws<InvalidDataException>(() => RetailPayloadManifest.Parse(
        """{"schema":"openbfme.retail-payload-manifest","schemaVersion":99,"payloads":[]}"""));

    // A pinned path must never resolve outside the install tree.
    Check(RetailPayloadManifest.NormalizeRelativePath("lang/english.big") is not null, "safe path rejected");
    foreach (var unsafePath in new[] { "..", "../x", "..\\x", "a/../../b", "C:\\windows\\x", "" })
        Check(RetailPayloadManifest.NormalizeRelativePath(unsafePath) is null,
            $"unsafe path '{unsafePath}' was accepted");
    return Task.CompletedTask;
}

static async Task TestEmptyPinnedManifestRefusal()
{
    // The shipped manifest is populated, so this uses a fixture-empty one. The
    // behaviour still has to be covered: an unpinned build must refuse rather than
    // install bytes it cannot verify, and that is the state every fork starts in.
    var target = NewTempDirectory("openbfme-pin-empty");
    var handler = new RecordingWorkshopHandler(new Dictionary<string, byte[]>(), "{}");
    var provisioner = new AllInOneRetailProvisioner(new HttpClient(handler))
    {
        PayloadManifest = RetailPayloadManifest.Empty
    };
    Check(RetailPayloadManifest.Empty.IsEmpty, "the empty fixture is not empty");
    Check(RetailPayloadManifest.Parse(
            """{"schema":"openbfme.retail-payload-manifest","schemaVersion":1,"payloads":[]}""")
        .IsEmpty, "a manifest with no payloads did not report itself empty");

    await ThrowsAsync<InvalidOperationException>(() =>
        provisioner.ProvisionAsync("bfme2", target, null, CancellationToken.None));

    // Fail closed means fail EARLY: no metadata call, no bytes, no directory churn.
    Check(handler.Requests.Count == 0,
        $"an empty manifest still hit the network ({handler.Requests.Count} request(s))");
    Check(!Directory.EnumerateFileSystemEntries(target).Any(), "empty manifest still wrote to disk");

    // A manifest that pins the other game is just as empty for this one.
    var otherGame = new AllInOneRetailProvisioner(new HttpClient(handler))
    {
        PayloadManifest = RetailPayloadManifest.Parse(
            PinnedManifestJson("game.dat", 4, Sha256Hex(new byte[] { 1, 2, 3, 4 })))
    };
    await ThrowsAsync<InvalidOperationException>(() =>
        otherGame.ProvisionAsync("rotwk", target, null, CancellationToken.None));
    Check(handler.Requests.Count == 0, "an unpinned game still hit the network");
    Directory.Delete(target, true);
}

static async Task TestPinnedDigestMismatchAbort()
{
    var honest = System.Text.Encoding.ASCII.GetBytes("REAL-GAME-DATA-0123456789");
    // Same length, different content: exactly what the old size-only check missed.
    var substituted = System.Text.Encoding.ASCII.GetBytes("EVIL-GAME-DATA-0123456789");
    Check(honest.Length == substituted.Length, "test fixture must keep the size identical");

    var target = NewTempDirectory("openbfme-pin-mismatch");
    var manifest = RetailPayloadManifest.Parse(
        PinnedManifestJson("game.dat", honest.Length, Sha256Hex(honest)));
    var handler = new RecordingWorkshopHandler(
        new Dictionary<string, byte[]> { ["/files/game.dat"] = substituted },
        WorkshopPackageJson(("game.dat", honest.Length, "/files/game.dat")));
    var provisioner = new AllInOneRetailProvisioner(new HttpClient(handler))
    {
        PayloadManifest = manifest
    };

    InvalidDataException? failure = null;
    try
    {
        await provisioner.ProvisionAsync("bfme2", target, null, CancellationToken.None);
    }
    catch (InvalidDataException error) { failure = error; }
    Check(failure is not null, "a substituted payload of the right size was installed");

    // The message has to be diagnosable, not just alarming: both digests present.
    var message = failure!.Message;
    Check(message.Contains(Sha256Hex(honest), StringComparison.OrdinalIgnoreCase),
        "abort message omitted the expected digest");
    Check(message.Contains(Sha256Hex(substituted), StringComparison.OrdinalIgnoreCase),
        "abort message omitted the actual digest");

    // Nothing survives: not the file, not the partial.
    Check(!File.Exists(Path.Combine(target, "game.dat")), "mismatched file was installed anyway");
    var leftovers = Directory.EnumerateFiles(target, "*", SearchOption.AllDirectories).ToList();
    Check(leftovers.Count == 0,
        "abort left files behind: " + string.Join(", ", leftovers.Select(Path.GetFileName)));
    Directory.Delete(target, true);
}

static async Task TestPinnedDigestMatchInstalls()
{
    // The mismatch test only proves the door shuts. This proves it opens — otherwise
    // "always throws" would pass every integrity test in this file.
    var payloads = new Dictionary<string, byte[]>
    {
        ["game.dat"] = Encoding.ASCII.GetBytes("REAL-GAME-DATA-0123456789"),
        ["lotrbfme2.exe"] = Encoding.ASCII.GetBytes("BFME2-EXECUTABLE"),
        ["ini.big"] = Encoding.ASCII.GetBytes("INI-ARCHIVE"),
        ["w3d.big"] = Encoding.ASCII.GetBytes("W3D-ARCHIVE"),
        ["textures0.big"] = Encoding.ASCII.GetBytes("TEXTURE-ARCHIVE"),
    };
    var target = NewTempDirectory("openbfme-pin-match");
    var handler = new RecordingWorkshopHandler(
        payloads.ToDictionary(item => "/files/" + item.Key, item => item.Value),
        WorkshopPackageJson(payloads.Select(item =>
            (item.Key, (long)item.Value.Length, "/files/" + item.Key)).ToArray()));
    var provisioner = new AllInOneRetailProvisioner(new HttpClient(handler))
    {
        PayloadManifest = RetailPayloadManifest.Parse(PinnedManifestFilesJson(payloads))
    };

    var result = await provisioner.ProvisionAsync("bfme2", target, null, CancellationToken.None);
    Check(result.FilesInstalled == payloads.Count,
        $"expected {payloads.Count} installed files, got {result.FilesInstalled}");
    Check(File.ReadAllBytes(Path.Combine(target, "game.dat")).SequenceEqual(payloads["game.dat"]),
        "installed bytes differ from the verified payload");
    Check(!Directory.EnumerateFiles(target, "*.part", SearchOption.AllDirectories).Any(),
        "a .part file survived a successful install");

    // Re-running verifies by digest and skips; it must not re-download.
    var downloadsBefore = handler.Requests.Count(r => r.Contains("/files/", StringComparison.Ordinal));
    var again = await provisioner.ProvisionAsync("bfme2", target, null, CancellationToken.None);
    Check(again.FilesSkipped == payloads.Count && again.FilesInstalled == 0,
        "resume did not skip verified files");
    Check(handler.Requests.Count(r => r.Contains("/files/", StringComparison.Ordinal)) == downloadsBefore,
        "resume re-downloaded a file that already matched its digest");

    // But a file that no longer matches its digest is not trusted just because it exists.
    File.WriteAllBytes(Path.Combine(target, "game.dat"),
        System.Text.Encoding.ASCII.GetBytes("TAMPERED-DATA-0123456789!"));
    var repaired = await provisioner.ProvisionAsync("bfme2", target, null, CancellationToken.None);
    Check(repaired.FilesInstalled == 1, "a tampered on-disk file was skipped instead of replaced");
    Directory.Delete(target, true);
}

static async Task TestWorkshopHostAllowlistRejection()
{
    // Unit level: the allowlist itself.
    AllInOneRetailProvisioner.ValidateWorkshopUri(
        new Uri("https://bfmeladder.com/api/workshop/download?guid=original-BFME2"), "metadata");
    AllInOneRetailProvisioner.ValidateWorkshopUri(
        new Uri("https://workshop-files.bfmeladder.com/x/y.big"), "ok.big");
    foreach (var rejected in new[]
             {
                 "https://evil.example/payload.big",
                 "http://bfmeladder.com/insecure.big",
                 "https://user:pass@bfmeladder.com/creds.big",
                 "https://bfmeladder.com.evil.example/lookalike.big",
                 "https://bfmeladder.com/x.big#fragment"
             })
        Throws<InvalidDataException>(() => AllInOneRetailProvisioner.ValidateWorkshopUri(
            new Uri(rejected), "rejected.big"));

    // End to end: a poisoned package list pointing off-allowlist installs nothing.
    var honest = System.Text.Encoding.ASCII.GetBytes("REAL-GAME-DATA-0123456789");
    var target = NewTempDirectory("openbfme-pin-host");
    var handler = new RecordingWorkshopHandler(
        new Dictionary<string, byte[]> { ["/files/game.dat"] = honest },
        WorkshopPackageJson(("game.dat", honest.Length, "https://evil.example/files/game.dat")));
    var provisioner = new AllInOneRetailProvisioner(new HttpClient(handler))
    {
        PayloadManifest = RetailPayloadManifest.Parse(
            PinnedManifestJson("game.dat", honest.Length, Sha256Hex(honest)))
    };

    await ThrowsAsync<InvalidDataException>(() =>
        provisioner.ProvisionAsync("bfme2", target, null, CancellationToken.None));
    Check(!handler.Requests.Any(r => r.Contains("evil.example", StringComparison.OrdinalIgnoreCase)),
        "the launcher contacted an off-allowlist host before rejecting it");
    Check(!Directory.EnumerateFiles(target, "*", SearchOption.AllDirectories).Any(),
        "an off-allowlist download wrote to disk");
    Directory.Delete(target, true);
}

static Task TestDownloadDisclosureShownOnce()
{
    var root = NewTempDirectory("openbfme-disclosure");
    var disclosure = ThirdPartyDownloadDisclosure.ForInstallRoot(root);
    Check(!disclosure.AlreadyShown(), "disclosure claimed to be shown before anything happened");

    var shown = new List<string>();
    Check(disclosure.ShowOnce(shown.Add), "first ShowOnce did not present the notice");
    Check(shown.Count == 1, $"expected 1 presentation, got {shown.Count}");

    // The substance of the disclosure is the host and the legal-right statement.
    // Wording may be edited; these two facts may not quietly disappear.
    var text = shown[0];
    Check(text.Contains(ThirdPartyDownloadDisclosure.SourceHost, StringComparison.OrdinalIgnoreCase),
        "the notice does not name the source host");
    Check(text.Contains("legal right", StringComparison.OrdinalIgnoreCase),
        "the notice does not state the legal-right requirement");

    // Shown once, and recorded so it stays once across runs.
    Check(disclosure.AlreadyShown(), "the acknowledgement was not recorded");
    Check(File.Exists(ThirdPartyDownloadDisclosure.DefaultRecordPath(root)), "no record file was written");
    Check(!disclosure.ShowOnce(shown.Add), "the notice was presented a second time");
    Check(shown.Count == 1, "a second presentation slipped through");
    Check(!ThirdPartyDownloadDisclosure.ForInstallRoot(root).ShowOnce(shown.Add),
        "a fresh instance re-presented an already-recorded notice");

    // A corrupt or foreign record is not an acknowledgement: show it again rather
    // than assume consent that was never recorded.
    File.WriteAllText(ThirdPartyDownloadDisclosure.DefaultRecordPath(root), "{ not json");
    Check(!disclosure.AlreadyShown(), "a corrupt record counted as acknowledged");
    File.WriteAllText(ThirdPartyDownloadDisclosure.DefaultRecordPath(root),
        """{"schema":"something.else","disclosureVersion":99}""");
    Check(!disclosure.AlreadyShown(), "a foreign record counted as acknowledged");

    Directory.Delete(root, true);
    return Task.CompletedTask;
}

static string NewTempDirectory(string prefix)
{
    var path = Path.Combine(Path.GetTempPath(), $"{prefix}-{Guid.NewGuid():N}");
    Directory.CreateDirectory(path);
    return path;
}

static string Sha256Hex(byte[] bytes) =>
    Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

static string PinnedManifestJson(string path, long size, string sha256) => $$"""
{
  "schema": "openbfme.retail-payload-manifest",
  "schemaVersion": 1,
  "payloads": [
    {
      "game": "bfme2",
      "language": "EN",
      "packageVersion": "test",
      "files": [ { "path": {{System.Text.Json.JsonSerializer.Serialize(path)}}, "size": {{size}}, "sha256": "{{sha256}}" } ]
    }
  ]
}
""";

static string PinnedManifestFilesJson(IReadOnlyDictionary<string, byte[]> files)
{
    var entries = files.Select(item => $$"""
        { "path": {{System.Text.Json.JsonSerializer.Serialize(item.Key)}}, "size": {{item.Value.LongLength}}, "sha256": "{{Sha256Hex(item.Value)}}" }
        """);
    return $$"""
    {
      "schema": "openbfme.retail-payload-manifest",
      "schemaVersion": 1,
      "payloads": [
        {
          "game": "bfme2",
          "language": "EN",
          "packageVersion": "test",
          "files": [ {{string.Join(",", entries)}} ]
        }
      ]
    }
    """;
}

static string WorkshopPackageJson(params (string Name, long Size, string Url)[] files)
{
    var entries = files.Select(f =>
    {
        var url = f.Url.StartsWith("http", StringComparison.OrdinalIgnoreCase)
            ? f.Url
            : "https://workshop-files.bfmeladder.com" + f.Url;
        return $$"""
        {"Name":{{System.Text.Json.JsonSerializer.Serialize(f.Name)}},"Language":"EN",
         "Md5":"00000000000000000000000000000000","Size":{{f.Size}},
         "Url":{{System.Text.Json.JsonSerializer.Serialize(url)}}}
        """;
    });
    return $$"""
    {"Guid":"original-BFME2","Name":"Test BFME2","Version":"1.0","Game":1,
     "Files":[{{string.Join(",", entries)}}]}
    """;
}

// The publish/update target is baked in from config/release-source.json. No test may
// reintroduce an owner/name literal: two retired targets used to be reachable from here,
// and both are now listed (and refused) in that config instead.
static Task TestReleaseSource()
{
    Check(ReleaseSource.Repository.Contains('/'), "release repository must be owner/name");
    // The retired names live only in config/release-source.json — the one file allowed to
    // name a release target — so this asserts against that list instead of repeating them.
    Check(ReleaseSource.RetiredRepositories.Count >= 2, "the retired release target list is missing");
    Check(ReleaseSource.RetiredRepositories.All(dead =>
            !dead.Equals(ReleaseSource.Repository, StringComparison.OrdinalIgnoreCase)),
        "a retired release repository was reintroduced as the active target");
    Check(ReleaseSource.Host == "github.com", "unexpected release host");
    Check(ReleaseSource.ReleasePathPrefix == $"/{ReleaseSource.Repository}/releases/", "bad release path prefix");
    Check(ReleaseSource.LatestManifestUri.AbsoluteUri.EndsWith(
        "/releases/latest/download/release-manifest.json", StringComparison.Ordinal), "bad manifest URL");

    var configured = File.ReadAllText(Path.GetFullPath(Path.Combine(
        AppContext.BaseDirectory, "..", "..", "..", "..", "..", "config", "release-source.json")));
    Check(configured.Contains($"\"{ReleaseSource.Repository}\"", StringComparison.Ordinal),
        "the embedded release target drifted from config/release-source.json");
    return Task.CompletedTask;
}

// First run is "point at the retail install you own". The old UI defaulted to one
// developer's literal drive letters, which were wrong for every playtester.
static Task TestRetailDiscovery()
{
    var root = Path.Combine(Path.GetTempPath(), "openbfme-retail-" + Guid.NewGuid().ToString("N"));
    var empty = Path.Combine(root, "empty");
    var markerOnly = Path.Combine(root, "marker-only");
    var bfme2 = Path.Combine(root, "bfme2");
    var rotwk = Path.Combine(root, "rotwk");
    Directory.CreateDirectory(empty);
    Directory.CreateDirectory(markerOnly);
    Directory.CreateDirectory(bfme2);
    Directory.CreateDirectory(rotwk);
    try
    {
        Check(RetailDiscovery.Assess("bfme2", empty) == RetailInstallAssessment.NoMarker,
            "an empty directory must not be valid");
        File.WriteAllBytes(Path.Combine(markerOnly, RetailDiscovery.InstallMarker), new byte[] { 1 });
        Check(RetailDiscovery.Assess("bfme2", markerOnly) == RetailInstallAssessment.Incomplete,
            "marker-only policy must be incomplete");

        WriteRetailFixture(bfme2, "lotrbfme2.exe");
        WriteRetailFixture(rotwk, "lotrbfme2ep1.exe");
        Check(RetailDiscovery.Assess("bfme2", bfme2) == RetailInstallAssessment.ValidBfme2,
            "BFME II fixture was not valid for BFME II");
        Check(RetailDiscovery.Assess("rotwk", bfme2) == RetailInstallAssessment.WrongEdition,
            "BFME II fixture was not rejected as wrong-edition RotWK");
        Check(RetailDiscovery.Assess("rotwk", rotwk) == RetailInstallAssessment.ValidRotwk,
            "RotWK fixture was not valid for RotWK");
        Check(RetailDiscovery.Assess("bfme2", rotwk) == RetailInstallAssessment.WrongEdition,
            "RotWK fixture was not rejected as wrong-edition BFME II");
        Check(!RetailDiscovery.IsRetailInstall("bfme2", markerOnly),
            "marker-only tree passed game-aware validation");
        Check(RetailDiscovery.ExplainRejection("rotwk", bfme2)?.Contains(
                "Battle for Middle-earth II", StringComparison.OrdinalIgnoreCase) == true,
            "wrong-edition RotWK explanation did not name BFME II");
        Check(RetailDiscovery.ExplainRejection("bfme2", rotwk)?.Contains(
                "Rise of the Witch-king", StringComparison.OrdinalIgnoreCase) == true,
            "wrong-edition BFME II explanation did not name RotWK");
        Check(RetailDiscovery.ExplainRejection("bfme2", markerOnly)?.Contains(
                "incomplete", StringComparison.OrdinalIgnoreCase) == true,
            "marker-only rejection did not explain incompleteness");
        Check(RetailDiscovery.ExplainRejection("bfme2", "") is not null,
            "an empty path must be rejected");
        Check(RetailDiscovery.ExplainRejection("bfme2", Path.Combine(root, "nope")) is not null,
            "a missing folder must be rejected");

        // The override always wins so a player can point anywhere.
        var environment = new Dictionary<string, string> { [RetailDiscovery.Bfme2OverrideVariable] = bfme2 };
        Check(RetailDiscovery.Discover("bfme2", environment) == Path.GetFullPath(bfme2),
            "the BFME2_INSTALL override was ignored");

        // Candidates must come from the environment, never a developer's disk.
        var candidates = RetailDiscovery.Candidates("bfme2", new Dictionary<string, string>());
        Check(candidates.Count > 0, "no retail candidates were produced");
        // With no environment at all, every candidate must still come from a real drive
        // on this machine rather than a literal baked into the source.
        Check(candidates.All(item => Path.IsPathRooted(item) && !item.Contains("BFME2\\", StringComparison.Ordinal)),
            "a hardcoded developer path leaked into the candidate list");
        Throws<ArgumentOutOfRangeException>(() => RetailDiscovery.Candidates("halo", null));
    }
    finally { Directory.Delete(root, true); }
    return Task.CompletedTask;
}

static void WriteRetailFixture(string directory, string executable)
{
    foreach (var name in new[]
             {
                 RetailDiscovery.InstallMarker, executable,
                 "ini.big", "w3d.big", "textures0.big"
             })
        File.WriteAllBytes(Path.Combine(directory, name), new byte[] { 1 });
}

static Task TestUrlPolicy()
{
    var repository = ReleaseSource.Repository;
    ReleaseUriPolicy.Validate(new Uri($"https://github.com/{repository}/releases/download/v1/game.zip"));
    ReleaseUriPolicy.ValidateResponse(new Uri("https://release-assets.githubusercontent.com/github-production-release-asset/game.zip"));
    Throws<InvalidOperationException>(() => ReleaseUriPolicy.Validate(new Uri($"http://github.com/{repository}/a")));
    Throws<InvalidOperationException>(() => ReleaseUriPolicy.Validate(new Uri("https://evil.example/game.zip")));
    Throws<InvalidOperationException>(() => ReleaseUriPolicy.Validate(new Uri("https://github.com/other/repo/game.zip")));
    // Retired targets must be rejected now that they are no longer configured.
    foreach (var dead in ReleaseSource.RetiredRepositories)
        Throws<InvalidOperationException>(() => ReleaseUriPolicy.Validate(
            new Uri($"https://github.com/{dead}/releases/download/v1/game.zip")));
    Throws<InvalidOperationException>(() => ReleaseUriPolicy.Validate(
        new Uri($"https://github.com/evil/repo/{repository}/payload")));
    Throws<InvalidOperationException>(() => ReleaseUriPolicy.Validate(
        new Uri("https://release-assets.githubusercontent.com/attacker/game.zip")));
    return Task.CompletedTask;
}

static Task TestManifest()
{
    var repository = ReleaseSource.Repository;
    var json = $$"""
    {
      "schema":"openbfme.release-manifest",
      "schemaVersion":1,
      "repository":"{{repository}}",
      "version":"0.1.0-alpha.1",
      "channel":"playtest",
      "commit":"0123456789abcdef0123456789abcdef01234567",
      "packages":[{
        "name":"game.zip",
        "url":"https://github.com/{{repository}}/releases/download/v0.1.0-alpha.1/game.zip",
        "sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "size":123,
        "expandedSize":456,
        "kind":"game-windows-x64"
      },{
        "name":"launcher.zip",
        "url":"https://github.com/{{repository}}/releases/download/v0.1.0-alpha.1/launcher.zip",
        "sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "size":456,
        "expandedSize":789,
        "kind":"launcher-windows-x64"
      }]
    }
    """;
    var manifest = ReleaseManifest.Parse(Encoding.UTF8.GetBytes(json));
    Check(manifest.Version == "0.1.0-alpha.1", "version mismatch");

    // A manifest for a different repository must be refused, so a launcher offered a
    // retired target's manifest fails loudly instead of installing from it.
    var foreign = json.Replace($"\"repository\":\"{repository}\"",
        $"\"repository\":\"{ReleaseSource.RetiredRepositories[0]}\"", StringComparison.Ordinal);
    Throws<InvalidDataException>(() => ReleaseManifest.Parse(Encoding.UTF8.GetBytes(foreign)));
    return Task.CompletedTask;
}

static Task TestManifestSignature()
{
    var payload = Encoding.UTF8.GetBytes("{\"release\":1}\n");
    using var key = RSA.Create(2048);
    var signature = key.SignData(
        payload, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
    var text = Encoding.ASCII.GetBytes(Convert.ToBase64String(signature));
    var publicPem = key.ExportSubjectPublicKeyInfoPem();
    // ReadOnlySpan cannot be invoked through reflection; verify the production
    // overload's cryptographic behavior with a temporary public-key swap helper.
    var helper = typeof(ManifestSignature).GetMethods(
        System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)
        .Single(item => item.GetParameters().Length == 3);
    try
    {
        helper.CreateDelegate<ManifestVerifier>()(payload, text, publicPem);
    }
    catch (InvalidDataException)
    {
        throw;
    }
    text[0] = text[0] == (byte)'A' ? (byte)'B' : (byte)'A';
    Throws<InvalidDataException>(() => helper.CreateDelegate<ManifestVerifier>()(payload, text, publicPem));
    var productionField = typeof(ManifestSignature).GetField(
        "ProductionPublicKey",
        System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!;
    var productionKey = ((string)productionField.GetRawConstantValue()!).Replace("\r\n", "\n").Trim();
    var repositoryKey = File.ReadAllText(Path.GetFullPath(Path.Combine(
        AppContext.BaseDirectory, "..", "..", "..", "..", "..",
        "tools", "release", "release-manifest-public.pem"))).Replace("\r\n", "\n").Trim();
    Check(productionKey == repositoryKey, "release public key file drifted from the launcher");
    return Task.CompletedTask;
}

// A stalled download used to wedge the launcher forever: the UI created a
// CancellationTokenSource and never called Cancel(), and there was no timeout to fall
// back on. Cancellation must abort the install and leave nothing partial behind.
static async Task TestInstallCancellation()
{
    var root = NewRoot("cancel");
    try
    {
        var package = Package("1.0.0", "0123456789abcdef0123456789abcdef01234567", 70_000);
        using var cancellation = new CancellationTokenSource();

        await ThrowsAsync<OperationCanceledException>(() => new ReleaseInstaller().InstallAsync(
            package.Manifest, root, null, cancellation.Token,
            (item, _) =>
            {
                // Cancel exactly as a player pressing the button mid-download would.
                cancellation.Cancel();
                return Task.FromResult<Stream>(new MemoryStream(package.BytesFor(item)));
            }));

        Check(InstallState.Load(root) is null, "a cancelled install must not select a version");
        var versions = Path.Combine(root, "versions");
        Check(!Directory.Exists(versions) || Directory.GetFileSystemEntries(versions).Length == 0,
            "a cancelled install left staging or partial files behind");

        // After cancelling, the same install must still succeed — cancellation may not
        // poison the install root.
        await new ReleaseInstaller().InstallAsync(package.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(package.BytesFor(item))));
        Check(InstallState.Load(root)?.CurrentVersion == "1.0.0", "install after cancel did not recover");

        // The timeouts that replace the missing ones are real, positive, and separate:
        // an overall deadline would abort legitimate multi-gigabyte downloads.
        var installer = new ReleaseInstaller();
        Check(installer.MetadataTimeout > TimeSpan.Zero, "metadata timeout is not set");
        Check(installer.ResponseTimeout > TimeSpan.Zero, "response timeout is not set");
        Check(installer.StallTimeout > TimeSpan.Zero, "stall timeout is not set");
    }
    finally { Directory.Delete(root, true); }
}

static async Task TestInstallAndRollback()
{
    var root = NewRoot("install");
    try
    {
        var first = Package("1.0.0", "0123456789abcdef0123456789abcdef01234567", 70_000);
        await new ReleaseInstaller().InstallAsync(first.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(first.BytesFor(item))));
        Check(InstallState.Load(root)?.CurrentVersion == "1.0.0", "first version not selected");

        var second = Package("1.1.0", "1123456789abcdef0123456789abcdef01234567", 80_000);
        await new ReleaseInstaller().InstallAsync(second.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(second.BytesFor(item))));
        var updated = InstallState.Load(root)!;
        Check(updated.CurrentVersion == "1.1.0" && updated.PreviousVersion == "1.0.0", "update pointer wrong");
        Check(File.Exists(Path.Combine(root, "launcher-current.json")), "launcher update pointer missing");
        Check(Directory.Exists(Path.Combine(root, "launcher-versions", "1.1.0")),
            "launcher version was not installed side-by-side");
        // Damage to an installed version must be detected and then *repaired* by
        // reinstalling over it. Refusing the update instead — which is what this did —
        // left the player with no way out at all: updating was the only thing that could
        // rewrite a damaged version directory, and it declined to run while the damage
        // was there. The assertion is deliberately stronger than "it threw": the tampered
        // bytes have to be gone afterwards, which can only happen if the damage was seen.
        var gameExe = Path.Combine(root, "versions", "1.1.0", "OpenBFME.exe");
        var launcherExe = Path.Combine(root, "launcher-versions", "1.1.0", "OpenBFME.Launcher.exe");
        File.WriteAllBytes(gameExe, new byte[80_001]);
        File.WriteAllBytes(launcherExe, new byte[70_001]);
        var phases = new List<string>();
        await new ReleaseInstaller().InstallAsync(
            second.Manifest, root, new Progress<TransferProgress>(item => { lock (phases) phases.Add(item.Phase); }),
            default, (item, _) => Task.FromResult<Stream>(new MemoryStream(second.BytesFor(item))));
        Check(new FileInfo(gameExe).Length == 80_000, "a damaged game version was not repaired");
        Check(new FileInfo(launcherExe).Length == 70_000, "a damaged launcher version was not repaired");
        // Repair is never quiet: the player is told their install was rebuilt.
        Check(phases.Any(phase => phase.StartsWith("Repairing", StringComparison.Ordinal)),
            "a repair was performed without reporting it");
        ReleaseInstaller.VerifyInstalledVersion(
            Path.Combine(root, "versions", "1.1.0"), "1.1.0", second.Manifest.Commit);
        Check(Directory.GetDirectories(Path.Combine(root, "versions"))
                .All(item => !Path.GetFileName(item).Contains(".replaced-", StringComparison.Ordinal)),
            "the replaced copy of a repaired version was left behind");

        var rolled = ReleaseInstaller.Rollback(root);
        Check(rolled.CurrentVersion == "1.0.0" && rolled.PreviousVersion == "1.1.0", "rollback pointer wrong");
        Check(rolled.Commit == first.Manifest.Commit && rolled.PreviousCommit == second.Manifest.Commit,
            "rollback commit identity wrong");
        Check(rolled.HighestVersion == "1.1.0" && rolled.HighestCommit == second.Manifest.Commit,
            "rollback lost the highest accepted release identity");
        var intermediate = Package("1.0.5", "2123456789abcdef0123456789abcdef01234567", 70_000);
        await ThrowsAsync<InvalidDataException>(() => new ReleaseInstaller().InstallAsync(
            intermediate.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(intermediate.BytesFor(item)))));
        using var launcherState = System.Text.Json.JsonDocument.Parse(
            File.ReadAllBytes(Path.Combine(root, "launcher-current.json")));
        Check(launcherState.RootElement.GetProperty("currentVersion").GetString() == "1.0.0",
            "launcher rollback pointer wrong");
    }
    finally { Directory.Delete(root, true); }
}

static async Task TestChannelAndDowngrade()
{
    var root = NewRoot("channel");
    try
    {
        var current = Package("2.0.0", "a123456789abcdef0123456789abcdef01234567", 70_000);
        await new ReleaseInstaller().InstallAsync(
            current.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(current.BytesFor(item))),
            expectedChannel: "playtest");
        var old = Package("1.9.0", "b123456789abcdef0123456789abcdef01234567", 70_000);
        await ThrowsAsync<InvalidDataException>(() => new ReleaseInstaller().InstallAsync(
            old.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(old.BytesFor(item))),
            expectedChannel: "playtest"));
        await ThrowsAsync<InvalidDataException>(() => new ReleaseInstaller().InstallAsync(
            current.Manifest with { Channel = "nightly" }, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(current.BytesFor(item))),
            expectedChannel: "playtest"));
    }
    finally { Directory.Delete(root, true); }
}

static async Task TestVersionRetention()
{
    var root = NewRoot("retention");
    try
    {
        foreach (var (version, commit) in new[]
        {
            ("1.0.0", "0123456789abcdef0123456789abcdef01234567"),
            ("1.1.0", "1123456789abcdef0123456789abcdef01234567"),
            ("1.2.0", "2123456789abcdef0123456789abcdef01234567")
        })
        {
            var package = Package(version, commit, 70_000);
            await new ReleaseInstaller().InstallAsync(
                package.Manifest, root, null, default,
                (item, _) => Task.FromResult<Stream>(new MemoryStream(package.BytesFor(item))));
        }
        Check(!Directory.Exists(Path.Combine(root, "versions", "1.0.0")),
            "obsolete game version was retained");
        Check(!Directory.Exists(Path.Combine(root, "launcher-versions", "1.0.0")),
            "obsolete launcher version was retained");
        Check(Directory.Exists(Path.Combine(root, "versions", "1.1.0")) &&
              Directory.Exists(Path.Combine(root, "versions", "1.2.0")),
            "current rollback pair was pruned");
    }
    finally { Directory.Delete(root, true); }
}

// A launcher killed mid-update (Alt+F4, power loss, the host process exiting) leaves a
// staging tree and a part-written archive behind. Those used to be fatal to every later
// update on that machine: the pruner saw a directory whose name looked like a version,
// found no identity file in it, and threw — for good, since nothing ever removed it.
static async Task TestAbandonedScratch()
{
    var root = NewRoot("scratch");
    try
    {
        var first = Package("1.0.0", "0123456789abcdef0123456789abcdef01234567", 70_000);
        await new ReleaseInstaller().InstallAsync(first.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(first.BytesFor(item))));

        // Exactly what a killed update leaves: a half-extracted staging tree, a
        // superseded tree that was moved aside, and the archive being hashed.
        var versions = Path.Combine(root, "versions");
        var staging = Path.Combine(versions, "1.1.0.staging-" + new string('a', 32));
        var replaced = Path.Combine(versions, "1.0.0.replaced-" + new string('b', 32));
        Directory.CreateDirectory(staging);
        Directory.CreateDirectory(replaced);
        File.WriteAllBytes(Path.Combine(staging, "OpenBFME.exe"), new byte[17]);
        var strayArchive = Path.Combine(versions, "." + new string('c', 32) + ".zip");
        File.WriteAllBytes(strayArchive, new byte[4096]);
        Directory.CreateDirectory(Path.Combine(
            root, "launcher-versions", "1.0.0.staging-" + new string('d', 32)));

        var second = Package("1.1.0", "1123456789abcdef0123456789abcdef01234567", 80_000);
        await new ReleaseInstaller().InstallAsync(second.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(second.BytesFor(item))));

        Check(InstallState.Load(root)?.CurrentVersion == "1.1.0",
            "abandoned update scratch blocked a later update");
        Check(!Directory.Exists(staging) && !Directory.Exists(replaced),
            "abandoned staging directories were not reclaimed");
        Check(!File.Exists(strayArchive), "an abandoned download archive was not reclaimed");
        Check(!Directory.Exists(Path.Combine(
                root, "launcher-versions", "1.0.0.staging-" + new string('d', 32))),
            "abandoned launcher staging was not reclaimed");
        // The selection pointers must not leave write scratch next to themselves either.
        Check(Directory.GetFiles(root, "*.tmp").Length == 0, "a selection pointer left scratch behind");
    }
    finally { Directory.Delete(root, true); }
}

// The pruner deletes superseded versions. It used to hash every byte of one first and
// throw if anything had rotted — refusing to remove precisely the damaged directory it
// was about to delete, and blocking all further updates in the process.
static async Task TestDamagedObsoletePruning()
{
    var root = NewRoot("prune-damaged");
    try
    {
        foreach (var (version, commit) in new[]
        {
            ("1.0.0", "0123456789abcdef0123456789abcdef01234567"),
            ("1.1.0", "1123456789abcdef0123456789abcdef01234567")
        })
        {
            var seed = Package(version, commit, 70_000);
            await new ReleaseInstaller().InstallAsync(seed.Manifest, root, null, default,
                (item, _) => Task.FromResult<Stream>(new MemoryStream(seed.BytesFor(item))));
        }

        // Rot the version that the next update will retire.
        File.WriteAllBytes(Path.Combine(root, "versions", "1.0.0", "OpenBFME.pck"), new byte[999]);

        var newest = Package("1.2.0", "2123456789abcdef0123456789abcdef01234567", 70_000);
        await new ReleaseInstaller().InstallAsync(newest.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(newest.BytesFor(item))));
        Check(!Directory.Exists(Path.Combine(root, "versions", "1.0.0")),
            "a damaged obsolete version was not pruned");

        // Ownership is still required: a directory that is not ours is never deleted,
        // it stops the prune loudly.
        var foreign = Path.Combine(root, "versions", "0.9.0");
        Directory.CreateDirectory(foreign);
        File.WriteAllBytes(Path.Combine(foreign, "something.txt"), new byte[3]);
        var next = Package("1.3.0", "3123456789abcdef0123456789abcdef01234567", 70_000);
        await ThrowsAsync<InvalidDataException>(() => new ReleaseInstaller().InstallAsync(
            next.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(next.BytesFor(item)))));
        Check(Directory.Exists(foreign), "an unowned directory was deleted by version retention");
    }
    finally { Directory.Delete(root, true); }
}

// A selected launcher update that will not verify used to stop the launcher from opening
// at all — and the only controls that can repair it (Update, Roll back) live inside the
// window that then never opened. It must degrade to the executable the player ran, and
// say so.
static Task TestSelfUpdateDegradation()
{
    var root = NewRoot("self-update");
    try
    {
        // Points at a launcher version whose directory does not exist.
        File.WriteAllText(Path.Combine(root, "launcher-current.json"), """
        {"schema":"openbfme.launcher-install-state","currentVersion":"9.9.9",
         "previousVersion":null,
         "commit":"0123456789abcdef0123456789abcdef01234567","previousCommit":null,
         "packageSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
         "previousPackageSha256":null}
        """);
        var options = new LauncherOptions(
            "playtest", null, ManifestUriExplicit: false, root,
            NoUpdate: true, VerifyOnly: false, Headless: true,
            ImportGame: null, RetailPath: null,
            DiscoverRelease: false, ProvisionBfme2: false, ProvisionRotwk: false,
            RetailInstallRoot: null);

        var (relaunched, warning) = InvokeSelfUpdate(options);
        Check(!relaunched, "a launcher update that cannot be verified must not be started");
        Check(warning is not null, "a refused launcher update was handled silently");
        Check(warning!.Contains("9.9.9", StringComparison.Ordinal),
            "the warning does not name the launcher version that failed");

        // A corrupt pointer is the same story: report it, keep running.
        File.WriteAllText(Path.Combine(root, "launcher-current.json"), "{ not json");
        var corrupt = InvokeSelfUpdate(options);
        Check(!corrupt.Relaunched && corrupt.Warning is not null,
            "an unreadable launcher pointer must degrade with a warning, not stop startup");

        // The relaunched child is marked so a path spelled two ways cannot fork forever.
        var guard = (string)typeof(ReleaseInstaller).Assembly
            .GetType("OpenBFME.Launcher.LauncherSelfUpdate")!
            .GetField("RelaunchGuardVariable",
                System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!
            .GetRawConstantValue()!;
        Environment.SetEnvironmentVariable(guard, "1");
        try
        {
            var guarded = InvokeSelfUpdate(options);
            Check(!guarded.Relaunched && guarded.Warning is null,
                "an already-relaunched launcher must not consider relaunching again");
        }
        finally { Environment.SetEnvironmentVariable(guard, null); }
    }
    finally { Directory.Delete(root, true); }
    return Task.CompletedTask;
}

static (bool Relaunched, string? Warning) InvokeSelfUpdate(LauncherOptions options)
{
    var type = typeof(ReleaseInstaller).Assembly.GetType("OpenBFME.Launcher.LauncherSelfUpdate")!;
    var method = type.GetMethod("RelaunchSelected",
        System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!;
    var outcome = method.Invoke(null, new object[] { options, Array.Empty<string>() })!;
    var shape = outcome.GetType();
    return ((bool)shape.GetProperty("Relaunched")!.GetValue(outcome)!,
        (string?)shape.GetProperty("Warning")!.GetValue(outcome));
}

// The installer orders releases to refuse downgrades, and that ordering needs SemVer.
// A manifest whose version could not be ordered used to validate, install on a clean
// machine, and then make every subsequent update throw — permanently, because the
// unusable version was by then recorded in the install state.
static Task TestOrderableVersion()
{
    var repository = ReleaseSource.Repository;
    string Manifest(string version) => $$"""
    {
      "schema":"openbfme.release-manifest","schemaVersion":1,
      "repository":"{{repository}}","version":"{{version}}","channel":"playtest",
      "commit":"0123456789abcdef0123456789abcdef01234567",
      "packages":[{
        "name":"game.zip",
        "url":"https://github.com/{{repository}}/releases/download/v{{version}}/game.zip",
        "sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "size":123,"expandedSize":456,"kind":"game-windows-x64"
      },{
        "name":"launcher.zip",
        "url":"https://github.com/{{repository}}/releases/download/v{{version}}/launcher.zip",
        "sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "size":456,"expandedSize":789,"kind":"launcher-windows-x64"
      }]
    }
    """;
    // Accepted shapes, including a pre-release, which is what a playtest build is.
    foreach (var good in new[] { "1.0.0", "0.1.0-alpha.1", "10.20.30-rc.2" })
        Check(ReleaseManifest.Parse(Encoding.UTF8.GetBytes(Manifest(good))).Version == good,
            $"SemVer release '{good}' was refused");
    // Refused before anything is installed, which is the only recoverable moment.
    foreach (var bad in new[] { "beta1", "1.0", "1.0.0.0", "v1.0.0", "1234567890.0.0" })
        Throws<InvalidDataException>(() => ReleaseManifest.Parse(Encoding.UTF8.GetBytes(Manifest(bad))));
    return Task.CompletedTask;
}

// The manifest and its signature are small by definition. Buffering the response first
// and checking its size afterwards left the one case the Content-Length check cannot
// cover — a host that declares no length at all — able to allocate without limit.
static async Task TestBoundedMetadata()
{
    using var http = new HttpClient(new EndlessHandler());
    var installer = new ReleaseInstaller(http);
    await ThrowsAsync<InvalidDataException>(() =>
        installer.FetchManifestAsync(ReleaseSource.LatestManifestUri, default));
}

// An archive entry's declared uncompressed size bounds what it may actually expand to.
// Without that, the whole-package expansion budget is worthless: it is computed from the
// very numbers the entries declare, so an entry that lies about its size sails past
// every check and writes until the disk is full.
static async Task TestDeclaredEntrySize()
{
    var root = NewRoot("entry-size");
    try
    {
        var bytes = Zip(archive =>
        {
            Write(archive, "OpenBFME.exe", new byte[70_000]);
            Write(archive, "OpenBFME.pck", new byte[4096]);
        });
        // Understate what OpenBFME.pck expands to, exactly as a crafted package would.
        UnderstateEntrySize(bytes, "OpenBFME.pck", 16);
        var package = ManifestFor(bytes, "2.5.0", "5123456789abcdef0123456789abcdef01234567");
        await ThrowsAsync<InvalidDataException>(() => new ReleaseInstaller().InstallAsync(
            package.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(package.BytesFor(item)))));
        Check(!Directory.Exists(Path.Combine(root, "versions", "2.5.0")),
            "a package whose entry overran its declared size was installed");
    }
    finally { Directory.Delete(root, true); }
}

/// <summary>
/// Rewrite the uncompressed size a zip's central directory records for one entry,
/// leaving the stored bytes alone — the shape of a package that under-declares how much
/// it expands to.
/// </summary>
static void UnderstateEntrySize(byte[] archive, string entryName, uint declared)
{
    var name = Encoding.ASCII.GetBytes(entryName);
    for (var index = 0; index + 46 <= archive.Length; index++)
    {
        if (archive[index] != 0x50 || archive[index + 1] != 0x4B ||
            archive[index + 2] != 0x01 || archive[index + 3] != 0x02) continue;
        var nameLength = BitConverter.ToUInt16(archive, index + 28);
        if (nameLength != name.Length ||
            !archive.AsSpan(index + 46, nameLength).SequenceEqual(name)) continue;
        BitConverter.GetBytes(declared).CopyTo(archive, index + 24);
        return;
    }
    throw new InvalidOperationException($"Central directory entry '{entryName}' was not found.");
}

static async Task TestTraversal()
{
    var root = NewRoot("traversal");
    try
    {
        var bytes = Zip(archive =>
        {
            var entry = archive.CreateEntry("../escape.txt");
            using var writer = new StreamWriter(entry.Open());
            writer.Write("escape");
        });
        var package = ManifestFor(bytes, "2.0.0", "2123456789abcdef0123456789abcdef01234567");
        await ThrowsAsync<InvalidDataException>(() => new ReleaseInstaller().InstallAsync(
            package.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(package.BytesFor(item)))));
        Check(!File.Exists(Path.Combine(root, "escape.txt")), "archive escaped staging");
    }
    finally { Directory.Delete(root, true); }
}

static async Task TestWindowsArchiveAliases()
{
    foreach (var name in new[] { "OpenBFME.exe:payload", "CON.txt", "trailing. " })
    {
        var root = NewRoot("windows-path");
        try
        {
            var bytes = Zip(archive => Write(archive, name, new byte[] { 1 }));
            var package = ManifestFor(bytes, "2.0.1", "2123456789abcdef0123456789abcdef01234567");
            await ThrowsAsync<InvalidDataException>(() => new ReleaseInstaller().InstallAsync(
                package.Manifest, root, null, default,
                (item, _) => Task.FromResult<Stream>(new MemoryStream(package.BytesFor(item)))));
        }
        finally { Directory.Delete(root, true); }
    }
}

static async Task TestHashMismatch()
{
    var root = NewRoot("hash");
    try
    {
        var package = Package("3.0.0", "3123456789abcdef0123456789abcdef01234567", 70_000);
        var tampered = package.GameBytes.ToArray();
        tampered[^1] ^= 0xff;
        await ThrowsAsync<InvalidDataException>(() => new ReleaseInstaller().InstallAsync(
            package.Manifest, root, null, default,
            (item, _) => Task.FromResult<Stream>(new MemoryStream(
                item.Kind == "game-windows-x64" ? tampered : package.LauncherBytes))));
        Check(InstallState.Load(root) is null, "tampered package was selected");
    }
    finally { Directory.Delete(root, true); }
}

static Task TestRedaction()
{
    var method = typeof(ImporterRunner).GetMethod("Redact",
        System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!;
    // A neutral drive letter: redaction is generic, and the export scan must not have to
    // allow-list a real retail path just so a test can prove paths are redacted.
    var value = (string)method.Invoke(null, new object[] { @"failed at Q:\RetailGame\data\asset.big" })!;
    Check(!value.Contains("Q:\\RetailGame", StringComparison.OrdinalIgnoreCase), "private path leaked");
    Check(value.Contains("<private-path>", StringComparison.Ordinal), "redaction marker missing");
    return Task.CompletedTask;
}

static Task TestScalarImporterProgress()
{
    var method = typeof(ImporterRunner).GetMethod("ParseProgressLine",
        System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!;
    var value = (ImportProgress)method.Invoke(null, new object[] { "\"bootstrap-complete\"" })!;
    Check(value.Phase == "Importing", "scalar JSON progress phase changed");
    Check(value.Message.Contains("bootstrap-complete", StringComparison.Ordinal),
        "scalar JSON progress was discarded");
    return Task.CompletedTask;
}

static Task TestBundleInventory()
{
    var root = NewRoot("inventory");
    try
    {
        Directory.CreateDirectory(Path.Combine(root, "python"));
        File.WriteAllText(Path.Combine(root, "python", "python.exe"), "runtime");
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes("runtime"))).ToLowerInvariant();
        File.WriteAllText(Path.Combine(root, "openbfme-bundle-inventory.json"), $$"""
        {"schema":"openbfme.bundle-inventory","schemaVersion":1,"files":[
          {"path":"python/python.exe","size":7,"sha256":"{{hash}}"}
        ]}
        """);
        var inventory = typeof(ImporterRunner).Assembly.GetType("OpenBFME.Launcher.BundleInventory")!;
        var verify = inventory.GetMethod("Verify",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!;
        verify.Invoke(null, new object[] { root });
        File.WriteAllText(Path.Combine(root, "python", "sitecustomize.py"), "raise SystemExit()");
        Throws<System.Reflection.TargetInvocationException>(() => verify.Invoke(null, new object[] { root }));
    }
    finally { Directory.Delete(root, true); }
    return Task.CompletedTask;
}

static TestPackageSet Package(string version, string commit, int exeBytes)
{
    var bytes = Zip(archive =>
    {
        Write(archive, "OpenBFME.exe", new byte[exeBytes]);
        Write(archive, "OpenBFME.pck", new byte[] { 1, 2, 3 });
    });
    return ManifestFor(bytes, version, commit);
}

static TestPackageSet ManifestFor(byte[] bytes, string version, string commit)
{
    var sha = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    var launcherBytes = LauncherZip();
    var launcherSha = Convert.ToHexString(SHA256.HashData(launcherBytes)).ToLowerInvariant();
    var repository = ReleaseSource.Repository;
    var manifest = new ReleaseManifest(ReleaseManifest.ExpectedSchema, 1,
        repository, version, "playtest", commit,
        new[]
        {
            new ReleasePackage("game.zip",
                $"https://github.com/{repository}/releases/download/v{version}/game.zip",
                sha, bytes.Length, ExpandedSize(bytes), "game-windows-x64"),
            new ReleasePackage("launcher.zip",
                $"https://github.com/{repository}/releases/download/v{version}/launcher.zip",
                launcherSha, launcherBytes.Length, ExpandedSize(launcherBytes), "launcher-windows-x64")
        });
    return new TestPackageSet(manifest, bytes, launcherBytes);
}

static byte[] LauncherZip()
{
    var executable = new byte[70_000];
    var hash = Convert.ToHexString(SHA256.HashData(executable)).ToLowerInvariant();
    var inventory = Encoding.UTF8.GetBytes($$"""
    {"schema":"openbfme.bundle-inventory","schemaVersion":1,"files":[
      {"path":"OpenBFME.Launcher.exe","size":70000,"sha256":"{{hash}}"}
    ]}
    """);
    return Zip(archive =>
    {
        Write(archive, "OpenBFME.Launcher.exe", executable);
        Write(archive, "openbfme-bundle-inventory.json", inventory);
    });
}

static byte[] Zip(Action<ZipArchive> build)
{
    using var memory = new MemoryStream();
    using (var archive = new ZipArchive(memory, ZipArchiveMode.Create, true)) build(archive);
    return memory.ToArray();
}

static long ExpandedSize(byte[] bytes)
{
    using var memory = new MemoryStream(bytes);
    using var archive = new ZipArchive(memory, ZipArchiveMode.Read);
    return archive.Entries.Sum(entry => entry.Length);
}

static void Write(ZipArchive archive, string name, byte[] bytes)
{
    var entry = archive.CreateEntry(name, CompressionLevel.NoCompression);
    using var stream = entry.Open();
    stream.Write(bytes);
}

static string NewRoot(string suffix)
{
    var root = Path.Combine(Path.GetTempPath(), $"openbfme-launcher-{suffix}-{Guid.NewGuid():N}");
    Directory.CreateDirectory(root);
    return root;
}

static Task TestContentPackCatalog()
{
    var install = NewRoot("content-catalog");
    try
    {
        var content = Path.Combine(install, ".private", "content-packs");
        var hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        var bundle = Path.Combine(content, "rotwk-men-vslice", hash);
        Directory.CreateDirectory(bundle);
        File.WriteAllText(Path.Combine(bundle, "pack.json"), """
            {
              "schema": "openbfme.content-pack",
              "schemaVersion": 0,
              "id": "rotwk-men-vslice",
              "title": "ROTWK Men",
              "version": "1.0.0",
              "priority": 900,
              "local_retail_import": true,
              "dataPolicy": { "externalPathsAllowed": false }
            }
            """);

        var empty = ContentPackCatalog.Discover(install);
        // Install root exists with a pack but no selection yet.
        Check(empty.Packs.Count == 1, "expected one discovered pack");
        Check(empty.Packs[0].Id == "rotwk-men-vslice", "pack id mismatch");
        Check(empty.ActivePackKey is null, "no selection should mean no active pack");

        var key = empty.Packs[0].RelativeKey;
        ContentPackCatalog.SelectActivePack(content, key);
        var selected = ContentPackCatalog.ScanRoot(content, "install");
        Check(selected.ActivePackKey == key, "selection did not activate pack");
        Check(selected.Packs[0].Role == "Active", "active role not applied");

        var mods = ContentPackCatalog.EnsureModsDirectory(install);
        var modDir = Path.Combine(mods, "example_hard_orcs");
        Directory.CreateDirectory(modDir);
        File.WriteAllText(Path.Combine(modDir, "pack.json"), """
            { "id": "example_hard_orcs", "name": "Harder Orcs", "version": "1.0.0", "priority": 50 }
            """);
        var withMods = ContentPackCatalog.ScanRoot(content, "install");
        Check(withMods.Mods.Any(m => m.Id == "example_hard_orcs"), "loose mod not discovered");

        // Fail-closed: path traversal must not write selection outside the root.
        Throws<InvalidOperationException>(() =>
            ContentPackCatalog.SelectActivePack(content, "../../../Windows/evil/deadbeef"));
        Throws<InvalidOperationException>(() =>
            ContentPackCatalog.SelectActivePack(content, "rotwk-men-vslice/../other"));
        Check(!ContentPackCatalog.IsSafeBundleKey("a/../../b"), "unsafe key accepted");
        Check(ContentPackCatalog.IsSafeBundleKey($"rotwk-men-vslice/{hash}"), "safe key rejected");

        // Empty override must not be replaced by a richer install root (UI/Play parity).
        var emptyOverride = Path.Combine(install, "empty-override");
        Directory.CreateDirectory(emptyOverride);
        var overridden = ContentPackCatalog.Discover(install, emptyOverride);
        Check(overridden.Source == "override", "empty override did not win Discover");
        Check(overridden.Packs.Count == 0, "empty override should list zero packs");
        Check(Path.GetFullPath(overridden.ContentRoot) == Path.GetFullPath(emptyOverride),
            "override content root path mismatch");

        // Stale selection-only root: ScanRoot reports active key with zero packs.
        // Discover must prefer any other root that actually has packs (e.g. workspace
        // on a dev machine) over that weak selection.
        var staleInstall = NewRoot("content-stale");
        try
        {
            var staleContent = Path.Combine(staleInstall, ".private", "content-packs");
            Directory.CreateDirectory(staleContent);
            File.WriteAllText(Path.Combine(staleContent, "selection.json"), """
                {
                  "schema": "openbfme.pack-selection",
                  "schemaVersion": 0,
                  "activePack": "ghost-pack/deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
                }
                """);
            var staleScan = ContentPackCatalog.ScanRoot(staleContent, "install");
            Check(staleScan.Packs.Count == 0, "stale root should have no packs");
            Check(staleScan.ActivePackKey is not null, "stale selection should parse activePack");

            var discovered = ContentPackCatalog.Discover(staleInstall);
            if (discovered.Packs.Count > 0)
            {
                Check(!string.Equals(
                        Path.GetFullPath(discovered.ContentRoot),
                        Path.GetFullPath(staleContent),
                        StringComparison.OrdinalIgnoreCase),
                    "populated root must beat empty selection-only install");
            }
            else
            {
                // Isolated environment with no workspace packs: weak fallback is ok.
                Check(discovered.ActivePackKey is not null || discovered.Diagnostic is not null,
                    "expected weak selection or empty diagnostic");
            }

            // Explicit rich override always wins over stale install.
            var rich = ContentPackCatalog.Discover(staleInstall, content);
            Check(rich.Packs.Count >= 1, "rich override lost to stale install");
            Check(rich.Packs.Any(p => p.Id == "rotwk-men-vslice"), "rich packs missing");
        }
        finally
        {
            try { Directory.Delete(staleInstall, recursive: true); } catch { /* temp */ }
        }
    }
    finally
    {
        try { Directory.Delete(install, recursive: true); } catch { /* temp cleanup */ }
    }
    return Task.CompletedTask;
}

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static void Throws<T>(Action action) where T : Exception
{
    try { action(); }
    catch (T) { return; }
    throw new InvalidOperationException($"Expected {typeof(T).Name}.");
}

static async Task ThrowsAsync<T>(Func<Task> action) where T : Exception
{
    try { await action(); }
    catch (T) { return; }
    throw new InvalidOperationException($"Expected {typeof(T).Name}.");
}

/// <summary>
/// Stands in for the BFME Ladder workshop: one metadata response plus a fixed map of
/// path to bytes. Records every URL it is asked for, so a test can assert not just what
/// was installed but whether the launcher reached out at all.
/// </summary>
sealed class RecordingWorkshopHandler : HttpMessageHandler
{
    private readonly IReadOnlyDictionary<string, byte[]> _files;
    private readonly string _packageJson;

    public List<string> Requests { get; } = new();

    public RecordingWorkshopHandler(IReadOnlyDictionary<string, byte[]> files, string packageJson)
    {
        _files = files;
        _packageJson = packageJson;
    }

    protected override Task<HttpResponseMessage> SendAsync(
        HttpRequestMessage request, CancellationToken cancellationToken)
    {
        var uri = request.RequestUri!;
        Requests.Add(uri.AbsoluteUri);

        HttpResponseMessage response;
        if (uri.AbsolutePath.StartsWith("/api/workshop/", StringComparison.Ordinal))
        {
            response = new HttpResponseMessage(System.Net.HttpStatusCode.OK)
            {
                Content = new StringContent(_packageJson, Encoding.UTF8, "application/json")
            };
        }
        else if (_files.TryGetValue(uri.AbsolutePath, out var bytes))
        {
            response = new HttpResponseMessage(System.Net.HttpStatusCode.OK)
            {
                Content = new ByteArrayContent(bytes)
            };
        }
        else
        {
            response = new HttpResponseMessage(System.Net.HttpStatusCode.NotFound)
            {
                Content = new StringContent("missing")
            };
        }
        response.RequestMessage = request;
        return Task.FromResult(response);
    }
}

/// <summary>
/// Answers every request with an unbounded body and no declared length — a host that
/// never stops sending. A real one need not be hostile to behave this way.
/// </summary>
sealed class EndlessHandler : HttpMessageHandler
{
    protected override Task<HttpResponseMessage> SendAsync(
        HttpRequestMessage request, CancellationToken cancellationToken) =>
        Task.FromResult(new HttpResponseMessage(System.Net.HttpStatusCode.OK)
        {
            RequestMessage = request,
            Content = new StreamContent(new EndlessStream())
        });
}

sealed class EndlessStream : Stream
{
    public override bool CanRead => true;
    public override bool CanSeek => false;
    public override bool CanWrite => false;
    public override long Length => throw new NotSupportedException();
    public override long Position
    {
        get => throw new NotSupportedException();
        set => throw new NotSupportedException();
    }
    public override void Flush() { }
    public override int Read(byte[] buffer, int offset, int count) => count;
    public override ValueTask<int> ReadAsync(
        Memory<byte> buffer, CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(buffer.Length);
    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();
    public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
}

delegate void ManifestVerifier(
    ReadOnlySpan<byte> manifest,
    ReadOnlySpan<byte> signature,
    string publicKeyPem);

sealed record TestPackageSet(
    ReleaseManifest Manifest,
    byte[] GameBytes,
    byte[] LauncherBytes)
{
    internal byte[] BytesFor(ReleasePackage package) =>
        package.Kind == "game-windows-x64" ? GameBytes : LauncherBytes;
}
