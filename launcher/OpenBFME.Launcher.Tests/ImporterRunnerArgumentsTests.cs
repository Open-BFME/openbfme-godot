using OpenBFME.Launcher;

internal static class ImporterRunnerArgumentsTests
{
    internal static Task Run()
    {
        var root = Path.Combine(Path.GetTempPath(), "openbfme-native-args");
        var retail = Path.Combine(root, "retail");
        var state = Path.Combine(root, "state");
        var content = Path.Combine(root, "content-packs");
        var expected = new[]
        {
            "-m", "openbfme_importer.native_content",
            "--install", Path.GetFullPath(retail),
            "--state-root", Path.GetFullPath(state),
            "--content-root", Path.GetFullPath(content),
            "--maps", "all"
        };
        var actual = ImporterRunner.BuildNativeContentArguments(retail, state, content);
        Require(actual.SequenceEqual(expected),
            "native content arguments changed: " + string.Join(" ", actual));
        var preparation = ImporterRunner.BuildNativePreparationArguments(retail, state, content);
        Require(preparation.SequenceEqual(expected.Append("--prepare-only")),
            "native preparation arguments changed: " + string.Join(" ", preparation));

        var batch = ImporterRunner.BuildRotwkBatchArguments(
            Path.Combine(root, "tools", "rotwk_faction_convert_batch.py"), retail, state);
        Require(batch.Contains("rotwk") && batch.Contains(Path.GetFullPath(state)),
            "batch converter arguments do not target RotWK state");

        var publish = ImporterRunner.BuildRotwkPublishArguments(
            Path.Combine(root, "tools", "openbfme_import.py"), retail, state, content,
            Path.Combine(root, "importer", "profiles", "men-fords-v1.json"), true);
        var factions = publish
            .Select((value, index) => (value, index))
            .Where(item => item.value == "--faction")
            .Select(item => publish[item.index + 1])
            .ToArray();
        Require(factions.SequenceEqual(new[]
            { "men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar" }),
            "multi-faction publish arguments do not contain all seven factions");

        var progress = ImporterRunner.ParseProgressLine(
            "{\"phase\":\"selection\",\"message\":\"Native content ready\",\"percent\":95}");
        Require(progress is { Phase: "selection", Message: "Native content ready", Percent: 95 },
            "native progress record was not parsed");
        return Task.CompletedTask;
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
