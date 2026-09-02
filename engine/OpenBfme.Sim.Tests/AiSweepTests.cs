using System.Diagnostics;
using System.Text.Json;
using OpenBfme.Sim;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

[Collection(TickBudgetCollection.Name)]
public sealed class AiSweepTests
{
    private const int SweepLimitTicks = 36_000;
    private static readonly string[] PlayableFactions =
    {
        "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar",
    };

    private readonly ITestOutputHelper _output;

    public AiSweepTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void EveryPlayableFactionPairBothOrdersHardVsMediumIsDeterministicAndUsesCoreLoop()
    {
        var path = MatchLaunchTests.RepoPath(
            "workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");
        if (!File.Exists(path))
        {
            _output.WriteLine($"SKIP: corpus bundle absent at {path}; all-faction AI sweep unavailable");
            return;
        }

        var document = BundleDocument.Load(path);
        var loaded = PlayableFactions
            .Where(side => document.Templates.Any(value => value.Side == side))
            .ToArray();
        Assert.Equal(PlayableFactions, loaded);
        var cases = OrderedPairCases(loaded).ToArray();
        Assert.Equal(42, cases.Length);
        var results = new List<AiSweepResult>(cases.Length);
        var commandFailures = new List<string>();
        var reportPath = MatchLaunchTests.RepoPath(
            "workspace", "logs", "lane-kernel-g", "ai-sweep.json");
        Directory.CreateDirectory(Path.GetDirectoryName(reportPath)!);

        foreach (var (left, right) in cases)
        {
            var setup = AiRetailProofTests.SelectSetup(document, left, right);
            var first = AiRetailProofTests.Build(document, setup, "hard", "medium");
            var second = AiRetailProofTests.Build(document, setup, "hard", "medium");
            var finishedTick = 0;
            var stopwatch = Stopwatch.StartNew();
            for (var tick = 1; tick <= SweepLimitTicks; tick++)
            {
                first.Tick();
                second.Tick();
                if (tick == 1 || tick % 300 == 0)
                    Assert.Equal(first.StateHash(), second.StateHash());
                if (HasStructure(first, 0) && HasStructure(first, 1)) continue;
                finishedTick = tick;
                break;
            }
            stopwatch.Stop();
            Assert.Equal(first.StateHash(), second.StateHash());
            var ticks = finishedTick == 0 ? SweepLimitTicks : finishedTick;
            var leftCounts = first.AiCommandCounts(0);
            var rightCounts = first.AiCommandCounts(1);
            _output.WriteLine($"ai_sweep_case left={left} right={right} ticks={ticks} " +
                $"left_commands={FormatCounts(leftCounts)} right_commands={FormatCounts(rightCounts)} " +
                $"diagnostics={string.Join(',', first.Diagnostics.GroupBy(value => value.Code).Select(value => $"{value.Key}:{value.Count()}"))}");
            RecordMissingCommand(commandFailures, left, right, "left", leftCounts, "build");
            RecordMissingCommand(commandFailures, left, right, "left", leftCounts, "train");
            RecordMissingCommand(commandFailures, left, right, "right", rightCounts, "build");
            RecordMissingCommand(commandFailures, left, right, "right", rightCounts, "train");
            var millisecondsPerTick = (decimal)stopwatch.ElapsedTicks * 1_000m
                / Stopwatch.Frequency / Math.Max(1, ticks * 2L);
            var winner = Winner(first, left, right);
            results.Add(new AiSweepResult(
                left,
                right,
                finishedTick != 0,
                winner,
                ticks,
                decimal.Round(millisecondsPerTick, 3, MidpointRounding.AwayFromZero),
                CopyCounts(leftCounts),
                CopyCounts(rightCounts)));
            _output.WriteLine(
                $"| {left} | {right} | {(finishedTick != 0 ? "yes" : "no")} | {winner} | " +
                $"{ticks} | {millisecondsPerTick:F3} |");
            WriteReport(reportPath, cases.Length, results);
        }

        _output.WriteLine($"ai_sweep_summary cases={results.Count} finished={results.Count(value => value.Finished)} " +
            $"non_finishing={results.Count(value => !value.Finished)} report={reportPath}");
        Assert.True(commandFailures.Count == 0, string.Join(" | ", commandFailures));
    }

    private static void WriteReport(
        string reportPath,
        int totalCases,
        IReadOnlyList<AiSweepResult> results) =>
        File.WriteAllText(reportPath, JsonSerializer.Serialize(
            new AiSweepReport(
                "hard",
                "medium",
                SweepLimitTicks,
                totalCases,
                results.Count,
                results.Count(value => value.Finished),
                results),
            new JsonSerializerOptions { WriteIndented = true }));

    private static IEnumerable<(string Left, string Right)> OrderedPairCases(IReadOnlyList<string> factions)
    {
        for (var left = 0; left < factions.Count; left++)
        for (var right = left + 1; right < factions.Count; right++)
        {
            yield return (factions[left], factions[right]);
            yield return (factions[right], factions[left]);
        }
    }

    private static void RecordMissingCommand(
        ICollection<string> failures,
        string left,
        string right,
        string seat,
        IReadOnlyDictionary<string, int> counts,
        string command)
    {
        if (!counts.TryGetValue(command, out var count) || count == 0)
            failures.Add($"{left} vs {right} {seat} AI issued no {command} command");
    }

    private static IReadOnlyDictionary<string, int> CopyCounts(
        IReadOnlyDictionary<string, int> counts)
    {
        var result = new SortedDictionary<string, int>(StringComparer.Ordinal);
        foreach (var pair in counts) result.Add(pair.Key, pair.Value);
        return result;
    }

    private static string FormatCounts(IReadOnlyDictionary<string, int> counts) =>
        string.Join(',', counts.Select(value => $"{value.Key}:{value.Value}"));

    private static bool HasStructure(SimWorld world, int team) => world.Objects.Values.Any(value =>
        value.Team == team && (AiTemplateRoles.Classify(value.Template) & AiUnitRole.Structure) != 0);

    private static string Winner(SimWorld world, string leftName, string rightName)
    {
        var left = HasStructure(world, 0);
        var right = HasStructure(world, 1);
        return left == right ? (left ? "none" : "draw") : left ? leftName : rightName;
    }

    private sealed record AiSweepResult(
        string Left,
        string Right,
        bool Finished,
        string Winner,
        int Ticks,
        decimal MillisecondsPerTick,
        IReadOnlyDictionary<string, int> LeftCommands,
        IReadOnlyDictionary<string, int> RightCommands);

    private sealed record AiSweepReport(
        string LeftDifficulty,
        string RightDifficulty,
        int LimitTicks,
        int Cases,
        int CompletedCases,
        int Finished,
        IReadOnlyList<AiSweepResult> Results);
}
