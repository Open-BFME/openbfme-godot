using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class AIUpdateInterfaceTests
{
    [Fact]
    public void AuthoredIdleAcquireFindsEnemyAndGuardReturnsToAnchor()
    {
        var weapon = new WeaponTemplate(
            "sword", Fixed64.FromInt(5), Fixed64.Zero, 1, 0, PreAttackType.PER_SHOT,
            0, 0, 0,
            new[] { new DamageNugget(Fixed64.FromInt(5), Fixed64.Zero, 0, DamageType.SLASH, "", "NORMAL") });
        var guard = new ObjectTemplate(
            "guard",
            new ModuleSpec[]
            {
                new(AIUpdateInterfaceModule.TypeName,
                    new Dictionary<string, long> { ["AutoAcquireEnemiesWhenIdle"] = 1, ["MoodAttackCheckRate"] = 66,
                        ["GuardRadius"] = 1 }),
                new(LocomotorModule.TypeName, new Dictionary<string, long>
                {
                    ["Speed"] = 30, ["Acceleration"] = 900, ["Braking"] = 900, ["TurnRate"] = 360,
                }),
            },
            weaponSets: new[] { new WeaponSet(null, new Dictionary<WeaponSlot, string> { [WeaponSlot.PRIMARY] = "sword" }) },
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(20)));
        var enemy = new ObjectTemplate("enemy", Array.Empty<ModuleSpec>(),
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(20)));
        var world = new SimWorld(
            new SimConfig(new[] { guard, enemy }, 11, 2, weaponTemplates: new[] { weapon }),
            ModuleRegistry.CreateDefault());
        var sentry = world.SpawnObject("guard", 0, FixedVector2.Zero);
        var target = world.SpawnObject("enemy", 1, new FixedVector2(Fixed64.FromInt(2), Fixed64.Zero));

        world.Tick();
        Assert.True(target.Health < target.MaxHealth);

        sentry.Combat!.ClearOrder();
        sentry.SetPosition(new FixedVector2(Fixed64.FromInt(10), Fixed64.Zero));
        world.Tick();
        Assert.True(sentry.FindModule<LocomotorModule>()!.HasOrder);
        Assert.Equal(FixedVector2.Zero, sentry.FindModule<LocomotorModule>()!.Destination);
    }
}
