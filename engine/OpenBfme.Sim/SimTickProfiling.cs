using System.Runtime.CompilerServices;

[assembly: InternalsVisibleTo("OpenBfme.Sim.Tests")]

namespace OpenBfme.Sim;

internal enum SimTickPhase
{
    Commands,
    Movement,
    MovementTargets,
    MovementObjects,
    MovementFinish,
    Combat,
    Economy,
    Modules,
    StoreSync,
    Hash,
}

internal interface ISimTickPhaseObserver
{
    void Begin(SimTickPhase phase);
    void End(SimTickPhase phase);
}
