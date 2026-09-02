using OpenBfme.Sim;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

/// <summary>
/// Corpus-facing C# sweep entry. The present bundle has no cooked map table, so
/// this runner parameterizes the seven loaded playable factions against the
/// next faction on a synthetic open grid and reports (rather than conceals)
/// non-finishes. The queue remains open until every cooked map/pair/difficulty
/// can be enumerated here.
/// </summary>
[Collection(TickBudgetCollection.Name)]
public sealed class AiSweepTests
{
    private readonly ITestOutputHelper _output;

    public AiSweepTests(ITestOutputHelper output) => _output = output;

    public static IEnumerable<object[]> LoadedFactionCases()
    {
        var path = MatchLaunchTests.RepoPath(
            "workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");
        if (!File.Exists(path))
        {
            yield return new object[] { "", "", "" };
            yield break;
        }
        var document = BundleDocument.Load(path);
        var wanted = new[] { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar" };
        var loaded = wanted.Where(side => document.Templates.Any(value => value.Side == side)).ToArray();
        for (var index = 0; index < loaded.Length; index++)
            yield return new object[] { loaded[index], loaded[(index + 1) % loaded.Length], "hard" };
    }

    [Theory]
    [MemberData(nameof(LoadedFactionCases))]
    public void LoadedFactionSyntheticGridSweep(string left, string right, string difficulty)
    {
        if (left.Length == 0)
        {
            _output.WriteLine("SKIP: corpus bundle absent; loaded-faction AI sweep unavailable");
            return;
        }
        var path = MatchLaunchTests.RepoPath(
            "workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");
        var document = BundleDocument.Load(path);
        var setup = AiRetailProofTests.SelectSetup(document, left, right);
        var world = AiRetailProofTests.Build(document, setup, difficulty, "medium");
        var winner = "none";
        var finishedTick = 0;
        for (var tick = 1; tick <= 3_000; tick++)
        {
            world.Tick();
            var leftAlive = HasStructure(world, 0);
            var rightAlive = HasStructure(world, 1);
            if (leftAlive && rightAlive) continue;
            finishedTick = tick;
            winner = leftAlive == rightAlive ? "draw" : leftAlive ? left : right;
            break;
        }
        _output.WriteLine($"ai_sweep left={left} right={right} difficulty={difficulty} " +
            $"winner={winner} ticks={(finishedTick == 0 ? 3_000 : finishedTick)} " +
            $"finished={finishedTick != 0} diagnostics={world.Diagnostics.Count} " +
            $"commands={world.AiCommandCounts(0).Values.Sum()}:{world.AiCommandCounts(1).Values.Sum()}");
        Assert.Contains(world.AiDiagnostics, value => value.Player == 0);
        Assert.Contains(world.AiDiagnostics, value => value.Player == 1);
    }

    private static bool HasStructure(SimWorld world, int team) => world.Objects.Values.Any(value =>
        value.Team == team && (AiTemplateRoles.Classify(value.Template) & AiUnitRole.Structure) != 0);
}
