using System.Diagnostics;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

internal sealed class TickPhaseProbe : ISimTickPhaseObserver
{
    private readonly long[] _started = new long[Enum.GetValues<SimTickPhase>().Length];
    private readonly long[] _elapsed = new long[Enum.GetValues<SimTickPhase>().Length];

    public void Begin(SimTickPhase phase) => _started[(int)phase] = Stopwatch.GetTimestamp();

    public void End(SimTickPhase phase) =>
        _elapsed[(int)phase] += Stopwatch.GetTimestamp() - _started[(int)phase];

    public double Milliseconds(SimTickPhase phase) =>
        _elapsed[(int)phase] * 1_000d / Stopwatch.Frequency;

    public void WriteTable(ITestOutputHelper output, string scenario, int ticks, long totalMilliseconds)
    {
        output.WriteLine($"tick profile: {scenario}");
        output.WriteLine("phase         total_ms    ms_per_tick");
        foreach (var phase in Enum.GetValues<SimTickPhase>())
        {
            var milliseconds = Milliseconds(phase);
            output.WriteLine($"{PhaseName(phase),-12} {milliseconds,10:F3} {milliseconds / ticks,14:F3}");
        }
        output.WriteLine($"wall         {totalMilliseconds,10} {(double)totalMilliseconds / ticks,14:F3}");
    }

    private static string PhaseName(SimTickPhase phase) => phase switch
    {
        SimTickPhase.StoreSync => "store sync",
        _ => phase.ToString().ToLowerInvariant(),
    };
}
