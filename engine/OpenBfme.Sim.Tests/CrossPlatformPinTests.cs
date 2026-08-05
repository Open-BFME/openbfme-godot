using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

/// <summary>
/// Pins the state hash of a fixed scenario to a constant. CI runs this suite on
/// Windows AND Linux: both must reproduce the identical SHA-256, proving the
/// fixed-point kernel is bit-identical across platforms — the property the
/// 8-player lockstep target depends on. If an intentional sim change moves the
/// hash, re-pin it in the same commit and say so in the commit message.
/// </summary>
public class CrossPlatformPinTests
{
    // Re-pinned when authoritative object transforms gained elevation + heading.
    public const string PinnedHash = "413a50d76536bf951af1e7bc41bcb5a7e10dbfbce7b3bc834268dddf63a4c0f9";

    private static string ComputeScenarioHash()
    {
        var world = TestWorlds.BuildAndSubmit();
        world.Advance(2000);
        return world.StateHash();
    }

    [Fact]
    public void FixedScenarioHashMatchesPinnedConstant()
    {
        var actual = ComputeScenarioHash();
        Assert.True(PinnedHash == actual, $"pinned={PinnedHash} actual={actual}");
    }
}
