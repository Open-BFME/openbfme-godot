using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class PhysicsBehaviorTests
{
    [Fact]
    public void AuthoredMassImpulseIntegratesGravityAndLands()
    {
        var template = new ObjectTemplate("rock", new[]
        {
            new ModuleSpec(PhysicsBehaviorModule.TypeName,
                new Dictionary<string, long>
                {
                    ["Mass"] = 2,
                    ["GravityMultRaw"] = Fixed64.One.Raw,
                }),
        });
        var world = new SimWorld(new SimConfig(new[] { template }, 10, 2), ModuleRegistry.CreateDefault(), 100);
        var rock = world.SpawnObject("rock", 0, FixedVector2.Zero);
        var physics = rock.FindModule<PhysicsBehaviorModule>()!;
        physics.ApplyImpulse(new FixedVector2(Fixed64.FromInt(2), Fixed64.Zero), Fixed64.FromInt(4));

        world.Tick();
        Assert.Equal(Fixed64.FromFraction(1, 10), rock.Position.X);
        Assert.Equal(Fixed64.FromInt(2) * Fixed64.FromFraction(1, 10), rock.Elevation);

        world.Advance(8);
        Assert.Equal(Fixed64.Zero, rock.Elevation);
        Assert.Equal(FixedVector2.Zero, physics.HorizontalVelocity);
    }

    [Fact]
    public void WeaponMetaImpactFeedsPhysicsKnockback()
    {
        var weapon = new WeaponTemplate(
            "hammer", Fixed64.FromInt(3), Fixed64.Zero, 1, 0, PreAttackType.PER_SHOT,
            0, 0, 0,
            new[] { new DamageNugget(Fixed64.One, Fixed64.Zero, 0, DamageType.CRUSH, "", "NORMAL") },
            new MetaImpactNugget(Fixed64.FromInt(4), Fixed64.Zero, Fixed64.Zero));
        var attacker = new ObjectTemplate(
            "attacker",
            Array.Empty<ModuleSpec>(),
            weaponSets: new[] { new WeaponSet(null,
                new Dictionary<WeaponSlot, string> { [WeaponSlot.PRIMARY] = "hammer" }) },
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(10)));
        var target = new ObjectTemplate(
            "target",
            new[] { new ModuleSpec(PhysicsBehaviorModule.TypeName,
                new Dictionary<string, long> { ["Mass"] = 2 }) },
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(10)));
        var world = new SimWorld(
            new SimConfig(new[] { attacker, target }, 18, 2, weaponTemplates: new[] { weapon }),
            ModuleRegistry.CreateDefault(), 100);
        var source = world.SpawnObject("attacker", 0, FixedVector2.Zero);
        var victim = world.SpawnObject("target", 1,
            new FixedVector2(Fixed64.One, Fixed64.Zero));
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "attack",
            ("objects", CommandValue.OfLongList(new long[] { source.Id })),
            ("target", CommandValue.OfLong(victim.Id)))));

        world.Tick();

        Assert.True(victim.Position.X > Fixed64.One);
        Assert.True(victim.Elevation > Fixed64.Zero);
    }
}
