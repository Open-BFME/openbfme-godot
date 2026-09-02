using System.Diagnostics;
using System.IO;
using System.Text.Json;

namespace OpenBFME.Launcher;

public sealed record ImportProgress(string Phase, string Message, double? Percent);

public sealed class ImporterRunner
{
    public async Task<int> RunAsync(
        string launcherDirectory,
        string game,
        string retailPath,
        string stateRoot,
        IProgress<ImportProgress>? progress,
        CancellationToken cancellationToken)
    {
        if (game is not ("bfme2" or "rotwk")) throw new ArgumentOutOfRangeException(nameof(game));
        BundleInventory.Verify(launcherDirectory);
        var script = Path.GetFullPath(Path.Combine(launcherDirectory, "tools", "openbfme_import.py"));
        if (!File.Exists(script)) throw new FileNotFoundException("Bundled importer entry point is missing.", script);
        var retail = Path.GetFullPath(retailPath);
        var state = Path.GetFullPath(stateRoot);
        var privateRoot = Directory.GetParent(state)
            ?? throw new InvalidOperationException("Importer state root has no parent.");
        var contentRoot = Path.Combine(privateRoot.FullName, "content-packs");
        var baseProfile = Path.GetFullPath(Path.Combine(
            launcherDirectory, "importer", "profiles", "men-fords-v1.json"));
        if (!Directory.Exists(retail)) throw new DirectoryNotFoundException("Retail installation is missing.");
        if (!File.Exists(baseProfile)) throw new FileNotFoundException("Bundled base import profile is missing.", baseProfile);
        Directory.CreateDirectory(state);

        progress?.Report(new ImportProgress("Tools", "Verifying pinned conversion tools.", null));
        var bootstrap = NewProcess(launcherDirectory);
        foreach (var argument in new[] { script, "--state-root", state, "--json", "bootstrap-tools" })
            bootstrap.ArgumentList.Add(argument);
        var bootstrapExit = await RunPythonAsync(bootstrap, progress, cancellationToken);
        if (bootstrapExit != 0) return bootstrapExit;

        if (game == "bfme2")
        {
            var build = NewProcess(launcherDirectory);
            foreach (var argument in new[]
            {
                script, "--state-root", state, "--json", "build",
                "--install", retail, "--game", game, "--profile", "men-fords-v0",
                "--godot-content-root", contentRoot
            })
                build.ArgumentList.Add(argument);
            return await RunPythonAsync(build, progress, cancellationToken);
        }

        progress?.Report(new ImportProgress(
            "Native content", "Preparing the layered RotWK effective INI tree.", null));
        var prepare = NewProcess(
            launcherDirectory,
            isolated: false,
            workingDirectory: Path.Combine(launcherDirectory, "importer"));
        foreach (var argument in BuildNativePreparationArguments(retail, state, contentRoot))
            prepare.ArgumentList.Add(argument);
        var prepareExit = await RunPythonAsync(prepare, progress, cancellationToken);
        if (prepareExit != 0) return prepareExit;

        var batchTool = Path.GetFullPath(Path.Combine(
            launcherDirectory, "tools", "rotwk_faction_convert_batch.py"));
        var useBatch = File.Exists(batchTool);
        if (useBatch)
        {
            progress?.Report(new ImportProgress(
                "Factions", "Converting every faction discovered in the RotWK census.", null));
            var batch = NewProcess(launcherDirectory);
            foreach (var argument in BuildRotwkBatchArguments(batchTool, retail, state))
                batch.ArgumentList.Add(argument);
            var batchExit = await RunPythonAsync(batch, progress, cancellationToken);
            if (batchExit != 0) return batchExit;
        }
        else
        {
            progress?.Report(new ImportProgress(
                "Factions",
                "The all-faction batch tool is absent; keeping the Men/Fords-based Angmar fallback.",
                null));
            var fallback = NewProcess(launcherDirectory);
            foreach (var argument in new[]
            {
                script, "--state-root", state, "--json", "import-faction",
                "--install", retail, "--game", game, "--faction", "angmar", "--convert"
            })
                fallback.ArgumentList.Add(argument);
            var fallbackExit = await RunPythonAsync(fallback, progress, cancellationToken);
            if (fallbackExit != 0) return fallbackExit;
        }

        progress?.Report(new ImportProgress("Packaging", "Building and selecting the local content pack.", null));
        var publish = NewProcess(launcherDirectory);
        foreach (var argument in BuildRotwkPublishArguments(
                     script, retail, state, contentRoot, baseProfile, useBatch))
            publish.ArgumentList.Add(argument);
        var publishExit = await RunPythonAsync(publish, progress, cancellationToken);
        if (publishExit != 0) return publishExit;

        progress?.Report(new ImportProgress(
            "Native content", "Building the native-core bundle and map documents.", null));
        var native = NewProcess(
            launcherDirectory,
            isolated: false,
            workingDirectory: Path.Combine(launcherDirectory, "importer"));
        foreach (var argument in BuildNativeContentArguments(retail, state, contentRoot))
            native.ArgumentList.Add(argument);
        return await RunPythonAsync(native, progress, cancellationToken);
    }

    internal static IReadOnlyList<string> BuildRotwkBatchArguments(
        string batchTool,
        string retailPath,
        string stateRoot) =>
        new[]
        {
            Path.GetFullPath(batchTool),
            "--install", Path.GetFullPath(retailPath),
            "--game", "rotwk",
            "--state-root", Path.GetFullPath(stateRoot)
        };

    internal static IReadOnlyList<string> BuildRotwkPublishArguments(
        string importerScript,
        string retailPath,
        string stateRoot,
        string contentRoot,
        string baseProfile,
        bool allFactions)
    {
        var arguments = new List<string>
        {
            Path.GetFullPath(importerScript),
            "--state-root", Path.GetFullPath(stateRoot),
            "--json", "publish-faction-to-slice",
            "--install", Path.GetFullPath(retailPath),
            "--game", "rotwk"
        };
        foreach (var faction in allFactions
                     ? new[] { "men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar" }
                     : new[] { "angmar" })
        {
            arguments.Add("--faction");
            arguments.Add(faction);
        }
        arguments.AddRange(new[]
        {
            "--base-profile", Path.GetFullPath(baseProfile),
            "--godot-content-root", Path.GetFullPath(contentRoot)
        });
        return arguments;
    }

    internal static IReadOnlyList<string> BuildNativeContentArguments(
        string retailPath,
        string stateRoot,
        string contentRoot) =>
        new[]
        {
            "-m", "openbfme_importer.native_content",
            "--install", Path.GetFullPath(retailPath),
            "--state-root", Path.GetFullPath(stateRoot),
            "--content-root", Path.GetFullPath(contentRoot),
            "--maps", "all"
        };

    internal static IReadOnlyList<string> BuildNativePreparationArguments(
        string retailPath,
        string stateRoot,
        string contentRoot) =>
        BuildNativeContentArguments(retailPath, stateRoot, contentRoot)
            .Append("--prepare-only")
            .ToArray();

    /// <summary>
    /// Run one bundled-Python step to completion, and — critically — make cancellation
    /// actually stop it.
    ///
    /// Previously cancellation only abandoned the <c>await</c>: the Python process and
    /// everything it had spawned (Blender, ffmpeg) kept running against the player's
    /// retail installation with nothing left watching it. A "cancelled" conversion that
    /// is still converting is exactly the kind of invisible failure this project has
    /// already shipped twice, so the child tree is killed and a failure to kill it is
    /// reported loudly rather than swallowed.
    /// </summary>
    private static async Task<int> RunPythonAsync(
        ProcessStartInfo start,
        IProgress<ImportProgress>? progress,
        CancellationToken cancellationToken)
    {
        using var process = new Process { StartInfo = start, EnableRaisingEvents = true };
        process.Start();

        // The pumps deliberately do not take the caller's token: they must keep draining
        // while we terminate the child, and they end on their own when its pipes close.
        var stdout = PumpAsync(process.StandardOutput, progress, CancellationToken.None);
        var stderr = PumpErrorsAsync(process.StandardError, progress, CancellationToken.None);

        try
        {
            await process.WaitForExitAsync(cancellationToken);
        }
        catch (OperationCanceledException)
        {
            TerminateTree(process, progress);
            // Drain what the child already wrote, but never block cancellation forever.
            await WhenAllOrTimeout(new[] { stdout, stderr }, TimeSpan.FromSeconds(10));
            throw;
        }

        await Task.WhenAll(stdout, stderr);
        return process.ExitCode;
    }

    private static void TerminateTree(Process process, IProgress<ImportProgress>? progress)
    {
        try
        {
            if (process.HasExited) return;
            progress?.Report(new ImportProgress(
                "Cancelling", "Stopping the conversion and its helper tools…", null));
            process.Kill(entireProcessTree: true);
            if (process.WaitForExit(15_000)) return;

            Report(progress,
                "The conversion process did not stop when cancelled. It may still be running " +
                "and reading your game files. Close it from Task Manager (python.exe) before " +
                "starting another import.");
        }
        catch (InvalidOperationException)
        {
            // The process exited between the HasExited check and the Kill. Nothing to do.
        }
        catch (Exception error)
        {
            Report(progress,
                $"Could not stop the conversion process after cancelling: {error.Message} " +
                "It may still be running; close python.exe from Task Manager before retrying.");
        }

        static void Report(IProgress<ImportProgress>? progress, string message)
        {
            Console.Error.WriteLine(message);
            progress?.Report(new ImportProgress("Cancelling", message, null));
        }
    }

    private static async Task WhenAllOrTimeout(IEnumerable<Task> tasks, TimeSpan timeout)
    {
        var all = Task.WhenAll(tasks);
        if (await Task.WhenAny(all, Task.Delay(timeout)) != all) return;
        try { await all; } catch { /* drain faults; the cancellation is the real outcome */ }
    }

    private static string ResolvePython(string launcherDirectory)
    {
        var bundled = Path.Combine(launcherDirectory, "python", "python.exe");
        if (!File.Exists(bundled))
            throw new FileNotFoundException(
                "The conversion tools that ship with OpenBFME are missing from this install " +
                "(no bundled Python runtime was found next to the launcher). This usually means " +
                "the launcher was copied out of its folder, or the download was incomplete. " +
                "Re-extract the launcher package and run it from the extracted folder.",
                bundled);
        return bundled;
    }

    private static ProcessStartInfo NewProcess(
        string launcherDirectory,
        bool isolated = true,
        string? workingDirectory = null)
    {
        var start = new ProcessStartInfo
        {
            FileName = ResolvePython(launcherDirectory),
            WorkingDirectory = workingDirectory ?? launcherDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        }.WithImporterEnvironment();
        start.ArgumentList.Add("-X");
        start.ArgumentList.Add("utf8");
        start.ArgumentList.Add("-B");
        if (isolated) start.ArgumentList.Add("-I");
        start.ArgumentList.Add("-S");
        return start;
    }

    private static async Task PumpAsync(
        StreamReader reader,
        IProgress<ImportProgress>? progress,
        CancellationToken token)
    {
        while (await reader.ReadLineAsync(token) is { } line)
        {
            var parsed = ParseProgressLine(line);
            progress?.Report(parsed);
        }
    }

    internal static ImportProgress ParseProgressLine(string line)
    {
        if (line.Length > 64 * 1024)
            throw new InvalidDataException("Importer progress record is too large.");
        try
        {
            using var json = JsonDocument.Parse(line);
            var root = json.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                return new ImportProgress("Importing", Redact(line), null);
            var phase = TextProperty(root, "stage")
                ?? TextProperty(root, "phase")
                ?? "Importing";
            var message = TextProperty(root, "detail")
                ?? TextProperty(root, "message")
                ?? phase;
            double? percent = null;
            if (root.TryGetProperty("fraction", out var fraction)
                && fraction.TryGetDouble(out var fractionalValue))
                percent = Math.Clamp(fractionalValue * 100, 0, 100);
            else if (root.TryGetProperty("percent", out var direct)
                     && direct.TryGetDouble(out var directValue))
                percent = Math.Clamp(directValue, 0, 100);
            return new ImportProgress(phase, Redact(message), percent);
        }
        catch (JsonException)
        {
            return new ImportProgress("Importing", Redact(line), null);
        }

        static string? TextProperty(JsonElement root, string name) =>
            root.TryGetProperty(name, out var value)
            && value.ValueKind == JsonValueKind.String
                ? value.GetString()
                : null;
    }

    private static async Task PumpErrorsAsync(
        StreamReader reader,
        IProgress<ImportProgress>? progress,
        CancellationToken token)
    {
        while (await reader.ReadLineAsync(token) is { } line)
        {
            var message = Redact(line);
            if (progress is null)
                Console.Error.WriteLine(message);
            else
                progress.Report(new ImportProgress("Importer", message, null));
        }
    }

    internal static string Redact(string text)
    {
        var result = text;
        foreach (var prefix in new[] { @"[A-Za-z]:\\", @"\\\\[^\\\s]+\\[^\\\s]+\\" })
            result = System.Text.RegularExpressions.Regex.Replace(result,
                prefix + @"[^\r\n""]+", "<private-path>", System.Text.RegularExpressions.RegexOptions.IgnoreCase);
        return result.Length <= 400 ? result : result[..400] + "…";
    }
}

internal static class ProcessStartInfoExtensions
{
    internal static ProcessStartInfo WithImporterEnvironment(this ProcessStartInfo start)
    {
        start.Environment["PYTHONUTF8"] = "1";
        start.Environment["PYTHONDONTWRITEBYTECODE"] = "1";
        return start;
    }
}
