using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class SymbioticStructuresBodyTests
{
    [Fact]
    public void ProxyForwardsDamageToNamedLivingSymbiote()
    {
        var keep = new ObjectTemplate("KeepLeft", new[]
        {
            new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 100 }),
        });
        var proxy = new ObjectTemplate("proxy", new[]
        {
            new ModuleSpec(SymbioticStructuresBodyModule.TypeName, null,
                new Dictionary<string, string> { ["Symbiote"] = "KeepLeft" }),
        });
        var world = new SimWorld(new SimConfig(new[] { keep, proxy }, 5, 2), ModuleRegistry.CreateDefault());
        var linked = world.SpawnObject("KeepLeft", 0, FixedVector2.Zero);
        var proxyObject = world.SpawnObject("proxy", 0, FixedVector2.Zero);

        world.DealDamage(proxyObject, 30);

        Assert.Equal(70, linked.FindModule<ActiveBodyModule>()!.Health);
        Assert.False(proxyObject.IsDead);
    }
}
