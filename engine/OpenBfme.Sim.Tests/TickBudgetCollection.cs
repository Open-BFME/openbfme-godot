using Xunit;

namespace OpenBfme.Sim.Tests;

[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class TickBudgetCollection
{
    public const string Name = "Tick budget";
}
