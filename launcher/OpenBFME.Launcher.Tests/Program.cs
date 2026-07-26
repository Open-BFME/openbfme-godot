using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using OpenBFME.Launcher;

var tests = new (string Name, Func<Task> Run)[]
{
    ("options", TestOptions),
    ("release source", TestReleaseSource),
    ("retail discovery", TestRetailDiscovery),
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
    ("bundle inventory", TestBundleInventory)
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
    return Task.CompletedTask;
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
    Directory.CreateDirectory(root);
    try
    {
        Check(RetailDiscovery.ExplainRejection(root) is not null, "a folder without game.dat must be rejected");
        Check(!RetailDiscovery.IsRetailInstall(root), "a folder without game.dat is not an install");
        File.WriteAllBytes(Path.Combine(root, RetailDiscovery.InstallMarker), new byte[] { 1 });
        Check(RetailDiscovery.IsRetailInstall(root), "a folder with game.dat is an install");
        Check(RetailDiscovery.ExplainRejection(root) is null, "a valid install must not be rejected");
        Check(RetailDiscovery.ExplainRejection("") is not null, "an empty path must be rejected");
        Check(RetailDiscovery.ExplainRejection(Path.Combine(root, "nope")) is not null,
            "a missing folder must be rejected");

        // The override always wins so a player can point anywhere.
        var environment = new Dictionary<string, string> { [RetailDiscovery.Bfme2OverrideVariable] = root };
        Check(RetailDiscovery.Discover("bfme2", environment) == Path.GetFullPath(root),
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
        var options = new LauncherOptions("playtest", null, root, true, false, true, null, null);

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
