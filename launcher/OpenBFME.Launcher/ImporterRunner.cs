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

        var start = NewProcess(launcherDirectory);
        var firstCommand = game == "bfme2"
            ? new[]
            {
                script, "--state-root", state, "--json", "build",
                "--install", retail, "--game", game, "--profile", "men-fords-v0",
                "--godot-content-root", contentRoot
            }
            : new[]
            {
                script, "--state-root", state, "--json", "import-faction",
                "--install", retail, "--game", game, "--faction", "angmar", "--convert"
            };
        foreach (var argument in firstCommand)
            start.ArgumentList.Add(argument);

        var exitCode = await RunPythonAsync(start, progress, cancellationToken);
        if (exitCode != 0) return exitCode;
        if (game == "bfme2") return 0;

        progress?.Report(new ImportProgress("Packaging", "Building and selecting the local content pack.", null));
        var publish = NewProcess(launcherDirectory);
        foreach (var argument in new[]
        {
            script, "--state-root", state, "--json", "publish-faction-to-slice",
            "--install", retail, "--game", game, "--faction", game == "rotwk" ? "angmar" : "men",
            "--base-profile", baseProfile, "--godot-content-root", contentRoot
        })
            publish.ArgumentList.Add(argument);
        return await RunPythonAsync(publish, progress, cancellationToken);
    }

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

    private static ProcessStartInfo NewProcess(string launcherDirectory)
    {
        var start = new ProcessStartInfo
        {
            FileName = ResolvePython(launcherDirectory),
            WorkingDirectory = launcherDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        }.WithImporterEnvironment();
        start.ArgumentList.Add("-X");
        start.ArgumentList.Add("utf8");
        start.ArgumentList.Add("-B");
        start.ArgumentList.Add("-I");
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
            var phase = root.TryGetProperty("stage", out var p)
                ? p.GetString() ?? "Importing"
                : "Importing";
            var message = root.TryGetProperty("detail", out var m)
                ? m.GetString() ?? phase
                : phase;
            double? percent = root.TryGetProperty("fraction", out var n)
                && n.TryGetDouble(out var value)
                ? Math.Clamp(value * 100, 0, 100)
                : null;
            return new ImportProgress(phase, Redact(message), percent);
        }
        catch (JsonException)
        {
            return new ImportProgress("Importing", Redact(line), null);
        }
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
