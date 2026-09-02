using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class PorcupineFormationBodyModuleTests
{
    [Fact]
    public void NonDefaultHordeFormationAppliesAuthoredDamagePercent()
    {
        var template = new ObjectTemplate("pike", new[]
        {
            new ModuleSpec(PorcupineFormationBodyModule.TypeName,
                new Dictionary<string, long> { ["MaxHealth"] = 100, ["DamageMultiplierPercent"] = 50 }),
        });
        var world = new SimWorld(new SimConfig(new[] { template }, 4, 2), ModuleRegistry.CreateDefault());
        var pike = world.SpawnObject("pike", 0, FixedVector2.Zero);
        world.AddHorde(new SnapshotHorde(100, 0, 0, new[] { pike.Id }, Formation: 1));
        world.Tick();

        world.DealDamage(pike, 10);

        Assert.True(pike.FindModule<PorcupineFormationBodyModule>()!.FormationActive);
        Assert.Equal(95, pike.FindModule<PorcupineFormationBodyModule>()!.Health);
    }
}
