using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class HighlanderBodyTests
{
    [Fact]
    public void OrdinaryDamageFloorsAtOneAndUnresistableDamageKills()
    {
        var body = new ModuleSpec(HighlanderBodyModule.TypeName,
            new Dictionary<string, long>
            {
                ["MaxHealth"] = 50,
                ["InitialHealth"] = 40,
            });
        var template = new ObjectTemplate(
            "highlander",
            new[] { body, new ModuleSpec(DestroyDieModule.TypeName) });
        var world = new SimWorld(
            new SimConfig(new[] { template }, 7, 2),
            ModuleRegistry.CreateDefault());
        var target = world.SpawnObject("highlander", 1, FixedVector2.Zero);

        world.DealDamage(target, 100, DamageTypes.Slash);
        Assert.Equal(1, target.FindModule<HighlanderBodyModule>()!.Health);
        Assert.False(target.IsDead);

        world.DealDamage(target, 1, DamageTypes.Unresistable);
        Assert.Equal(0, target.FindModule<HighlanderBodyModule>()!.Health);
        Assert.True(target.IsDead);
    }

    [Fact]
    public void KernelWeaponDamageUsesTheSameFloorAndUnresistableException()
    {
        var slash = Weapon("slash", 100, DamageType.SLASH);
        var unresistable = Weapon("unresistable", 1, DamageType.UNRESISTABLE);
        var targetTemplate = new ObjectTemplate(
            "highlander",
            new[] { new ModuleSpec(HighlanderBodyModule.TypeName), new ModuleSpec(DestroyDieModule.TypeName) },
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(40)));
        var ordinaryTemplate = Attacker("ordinary", slash.Name);
        var killerTemplate = Attacker("killer", unresistable.Name);
        var world = new SimWorld(
            new SimConfig(
                new[] { targetTemplate, ordinaryTemplate, killerTemplate },
                11,
                2,
                weaponTemplates: new[] { slash, unresistable }),
            ModuleRegistry.CreateDefault());
        var ordinary = world.SpawnObject("ordinary", 0, FixedVector2.Zero);
        var target = world.SpawnObject("highlander", 1,
            new FixedVector2(Fixed64.One, Fixed64.Zero));

        Assert.True(world.SubmitCommand(Attack(world.TickIndex + 1, ordinary.Id, target.Id)));
        world.Tick();
        Assert.Equal(Fixed64.One, target.Health);
        Assert.False(target.IsDead);

        world.HandleDeath(ordinary);
        var killer = world.SpawnObject("killer", 0, FixedVector2.Zero);
        Assert.True(world.SubmitCommand(Attack(world.TickIndex + 1, killer.Id, target.Id)));
        world.Tick();
        Assert.Equal(Fixed64.Zero, target.Health);
        Assert.True(target.IsDead);
    }

    private static ObjectTemplate Attacker(string name, string weapon) => new(
        name,
        new[] { new ModuleSpec(DestroyDieModule.TypeName) },
        weaponSets: new[]
        {
            new WeaponSet(null,
                new Dictionary<WeaponSlot, string> { [WeaponSlot.PRIMARY] = weapon }),
        },
        bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(10)));

    private static WeaponTemplate Weapon(string name, int damage, DamageType damageType) => new(
        name,
        Fixed64.FromInt(2),
        Fixed64.Zero,
        1,
        0,
        PreAttackType.PER_SHOT,
        0,
        0,
        0,
        new[]
        {
            new DamageNugget(Fixed64.FromInt(damage), Fixed64.Zero, 0, damageType, "", "NORMAL"),
        });

    private static SimCommand Attack(int tick, int attacker, int target) =>
        TestWorlds.Command(tick, 0, 0, "attack",
            ("objects", CommandValue.OfLongList(new long[] { attacker })),
            ("target", CommandValue.OfLong(target)));
}
