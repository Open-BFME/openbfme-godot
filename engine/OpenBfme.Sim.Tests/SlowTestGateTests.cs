using System.Text.RegularExpressions;
using Xunit;

namespace OpenBfme.Sim.Tests;

/// <summary>
/// Guards the OPENBFME_SLOW_TESTS gate itself: the corpus-scale tests that blew
/// the default suite past its budget must opt in through SlowTests.Require so a
/// later lane cannot silently re-add an ungated minutes-long test to the same
/// methods.
/// </summary>
public sealed class SlowTestGateTests
{
    [Theory]
    [InlineData("1", true)]
    [InlineData("true", true)]
    [InlineData("TRUE", true)]
    [InlineData("0", false)]
    [InlineData("false", false)]
    [InlineData("yes", false)]
    [InlineData("", false)]
    public void EnabledOnlyForExplicitOptInValues(string value, bool expected) =>
        Assert.Equal(expected, SlowTests.IsEnabled(value));

    [Fact]
    public void UnsetVariableDisablesSlowTests() =>
        Assert.False(SlowTests.IsEnabled(null));

    [Theory]
    [InlineData("MapDocumentCorpusTests.cs", "EveryNativeSelectionMapLoadsAndBuildsAWorld")]
    [InlineData("AiSweepTests.cs", "EveryPlayableFactionPairBothOrdersHardVsMediumIsDeterministicAndUsesCoreLoop")]
    [InlineData("AiRetailProofTests.cs", "MenHardVsMordorMediumRetailProofReportsHonestOutcomeAndTwinHash")]
    public void CorpusScaleTestsRequireTheSlowGate(string file, string method)
    {
        var source = File.ReadAllText(MatchLaunchTests.RepoPath(
            "engine", "OpenBfme.Sim.Tests", file));
        Assert.Matches(
            new Regex($"SlowTests\\.Require\\(\\s*_output,\\s*nameof\\({method}\\)\\s*\\)"),
            source);
    }
}
