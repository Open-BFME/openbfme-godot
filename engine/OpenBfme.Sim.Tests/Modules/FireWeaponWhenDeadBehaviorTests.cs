using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class FireWeaponWhenDeadBehaviorTests
{
    [Fact]
    public void DeathWeaponDamagesEnemyAtDeathPosition()
    {
        var weapon = ModuleBatchBTestSupport.Weapon("Explosion", 10, radius: 5, type: DamageType.FLAME);
        var sourceTemplate = ModuleBatchBTestSupport.Template("mine", new[]
        {
            ModuleBatchBTestSupport.Spec(FireWeaponWhenDeadBehaviorModule.TypeName,
                new Dictionary<string, long> { ["StartsActive"] = 1 },
                new Dictionary<string, string> { ["DeathWeapon"] = weapon.Name }),
        });
        var targetTemplate = ModuleBatchBTestSupport.Template("target", Array.Empty<ModuleSpec>());
        var world = ModuleBatchBTestSupport.World(new[] { sourceTemplate, targetTemplate }, new[] { weapon });
        var mine = world.SpawnObject("mine", 0, ModuleBatchBTestSupport.At(0));
        var target = world.SpawnObject("target", 1, ModuleBatchBTestSupport.At(2));

        world.DealDamage(mine, 100);

        Assert.Equal(Fixed64.FromInt(90), target.Health);
        Assert.True(mine.FindModule<FireWeaponWhenDeadBehaviorModule>()!.HasFired);
    }
}
