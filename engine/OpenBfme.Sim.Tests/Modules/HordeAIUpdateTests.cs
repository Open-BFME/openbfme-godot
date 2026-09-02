using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class HordeAIUpdateTests
{
    [Fact]
    public void AuthoredCatchUpRadiusPullsStragglerTowardCarrier()
    {
        var carrierTemplate = new ObjectTemplate("horde", new[]
        {
            new ModuleSpec(HordeAIUpdateModule.TypeName,
                new Dictionary<string, long>
                {
                    ["AutoAcquireEnemiesWhenIdle"] = 1,
                    ["MemberCatchUpRadius"] = 4,
                },
                new Dictionary<string, string> { ["ComboLocomotorSet"] = "SET_COMBO" }),
        });
        var memberTemplate = new ObjectTemplate("member", new[]
        {
            new ModuleSpec(LinearMoverModule.TypeName,
                new Dictionary<string, long> { ["SpeedPerTickRaw"] = Fixed64.FromInt(2).Raw }),
        });
        var world = new SimWorld(
            new SimConfig(new[] { carrierTemplate, memberTemplate }, 13, 2),
            ModuleRegistry.CreateDefault());
        var carrier = world.SpawnObject("horde", 0, FixedVector2.Zero);
        var member = world.SpawnObject("member", 0,
            new FixedVector2(Fixed64.FromInt(20), Fixed64.Zero));
        world.AddHorde(new SnapshotHorde(carrier.Id, 0, 0, new[] { member.Id }, 0));

        world.Advance(2);

        Assert.True(member.Position.X < Fixed64.FromInt(20));
        Assert.Equal("SET_COMBO", carrier.FindModule<HordeAIUpdateModule>()!.ComboLocomotorSet);
    }
}
