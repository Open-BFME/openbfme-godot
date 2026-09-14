using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

/// <summary>
/// Corpus-scale tests — every native map, every ordered AI faction matchup, the
/// full-tick retail AI proof — each take minutes with the workspace corpus
/// present and pushed the Debug sim suite past ten minutes. They stay off the
/// default run behind OPENBFME_SLOW_TESTS=1; the nightly workflow sets it.
/// xunit 2.9 has no dynamic skip, so gating is an explicit early return that
/// prints the reason, matching the corpus-absent SKIP convention.
/// </summary>
internal static class SlowTests
{
    public const string VariableName = "OPENBFME_SLOW_TESTS";

    public static bool Enabled =>
        IsEnabled(Environment.GetEnvironmentVariable(VariableName));

    internal static bool IsEnabled(string? value) =>
        string.Equals(value, "1", StringComparison.Ordinal)
        || string.Equals(value, "true", StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// True when the slow gate is open; otherwise prints the skip reason and
    /// returns false so the caller returns before touching the corpus.
    /// </summary>
    public static bool Require(ITestOutputHelper output, string testName)
    {
        if (Enabled)
        {
            return true;
        }

        output.WriteLine(
            $"SKIP: {testName} is corpus-scale and exceeds the default suite budget; " +
            $"set {VariableName}=1 to run it (the nightly workflow does)");
        return false;
    }
}
