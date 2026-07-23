using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class CombatTests
{
    private static ModuleSpec Body(long maxHealth) =>
        new(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = maxHealth });

    private static ModuleSpec Mover() =>
        new(LinearMoverModule.TypeName, new Dictionary<string, long>
        {
            ["SpeedPerTickRaw"] = Fixed64.FromFraction(1, 5).Raw,
        });

    private static ModuleSpec Weapon(long damage, string damageType = DamageTypes.Slash, int reload = 8, int range = 2) =>
        new(WeaponModule.TypeName,
            new Dictionary<string, long>
            {
                ["Damage"] = damage,
                ["ReloadTicks"] = reload,
                ["RangeRaw"] = Fixed64.FromInt(range).Raw,
            },
            new Dictionary<string, string> { ["DamageType"] = damageType });

    private static ModuleSpec Ai() =>
        new(AiCombatModule.TypeName, new Dictionary<string, long>
        {
            ["VisionRangeRaw"] = Fixed64.FromInt(30).Raw,
        });

    private static SimConfig Config() => new(
        new[]
        {
            new ObjectTemplate("swordsman", new[] { Body(100), Mover(), Weapon(12), Ai() }),
            new ObjectTemplate("veteran", new[]
            {
                Body(100),
                new ModuleSpec(ArmorModule.TypeName, new Dictionary<string, long>
                {
                    ["Armor:slash"] = 5_000,
                    ["ArmorDefault"] = 10_000,
                }),
                Mover(), Weapon(12), Ai(),
            }),
            new ObjectTemplate("peasant_crushable", new[]
            {
                Body(60),
                new ModuleSpec(SquishCollideModule.TypeName, null),
            }),
            new ObjectTemplate("stone_wall", new[] { Body(60) }),
            new ObjectTemplate("knight", new[] { Body(200), Mover(), Weapon(30, DamageTypes.Crush, 6, 1), Ai() }),
        },
        randomSeed: 11,
        teamCount: 2);

    private static SimWorld NewWorld() => new(Config(), ModuleRegistry.CreateDefault());

    [Fact]
    public void OpposingSoldiersFightToDeterministicResolution()
    {
        SimWorld Build()
        {
            var world = NewWorld();
            world.SpawnObject("swordsman", 0, new FixedVector2(Fixed64.FromInt(-6), Fixed64.Zero));
            world.SpawnObject("swordsman", 1, new FixedVector2(Fixed64.FromInt(6), Fixed64.Zero));
            return world;
        }

        var a = Build();
        var b = Build();
        var resolved = -1;
        for (var tick = 1; tick <= 600; tick++)
        {
            a.Tick();
            b.Tick();
            Assert.Equal(a.StateHash(), b.StateHash());
            if (resolved < 0 && a.Objects.Count == 1)
            {
                resolved = tick;
            }
        }
        Assert.True(resolved > 0, "combat never resolved");
        // Symmetric duel: team 0's unit has the lower id, scans first, so the
        // survivor must be deterministic — assert it IS object 1.
        Assert.Equal(1, a.Objects.Values.Single().Id);
    }

    [Fact]
    public void ArmorHalvesSlashDamage()
    {
        var world = NewWorld();
        var veteran = world.SpawnObject("veteran", 0, FixedVector2.Zero);
        var plain = world.SpawnObject("swordsman", 0, FixedVector2.Zero);
        world.DealDamage(veteran, 12, DamageTypes.Slash);
        world.DealDamage(plain, 12, DamageTypes.Slash);
        Assert.Equal(94, veteran.FindModule<ActiveBodyModule>()!.Health);
        Assert.Equal(88, plain.FindModule<ActiveBodyModule>()!.Health);
        // Non-slash uses ArmorDefault (100%).
        world.DealDamage(veteran, 10, DamageTypes.Siege);
        Assert.Equal(84, veteran.FindModule<ActiveBodyModule>()!.Health);
    }

    [Fact]
    public void CrushOnlyLandsOnCrushableTargets()
    {
        var world = NewWorld();
        var peasant = world.SpawnObject("peasant_crushable", 1, FixedVector2.Zero);
        var wall = world.SpawnObject("stone_wall", 1, FixedVector2.Zero);
        world.DealDamage(peasant, 30, DamageTypes.Crush);
        world.DealDamage(wall, 30, DamageTypes.Crush);
        Assert.Equal(30, peasant.FindModule<ActiveBodyModule>()!.Health);
        Assert.Equal(60, wall.FindModule<ActiveBodyModule>()!.Health);
    }

    [Fact]
    public void KnightRidesDownCrushablePeasant()
    {
        var world = NewWorld();
        world.SpawnObject("knight", 0, new FixedVector2(Fixed64.FromInt(-8), Fixed64.Zero));
        var peasant = world.SpawnObject("peasant_crushable", 1, new FixedVector2(Fixed64.FromInt(4), Fixed64.Zero));
        world.Advance(400);
        Assert.True(peasant.IsDead || !world.Objects.ContainsKey(peasant.Id), "peasant should be crushed");
        Assert.Single(world.Objects);
    }

    [Fact]
    public void MidBattleSnapshotRestoresToIdenticalOutcome()
    {
        var world = NewWorld();
        world.SpawnObject("veteran", 0, new FixedVector2(Fixed64.FromInt(-5), Fixed64.Zero));
        world.SpawnObject("swordsman", 1, new FixedVector2(Fixed64.FromInt(5), Fixed64.Zero));
        world.Advance(60);

        var restored = SimWorld.Restore(world.Snapshot(), Config(), ModuleRegistry.CreateDefault());
        for (var tick = 61; tick <= 500; tick++)
        {
            world.Tick();
            restored.Tick();
            Assert.Equal(world.StateHash(), restored.StateHash());
        }
        Assert.Equal(world.Objects.Count, restored.Objects.Count);
    }

    [Fact]
    public void DeadTargetsAreDroppedAndRetargetingIsDeterministic()
    {
        SimWorld Build()
        {
            var world = NewWorld();
            world.SpawnObject("swordsman", 0, new FixedVector2(Fixed64.FromInt(-4), Fixed64.Zero));
            world.SpawnObject("swordsman", 1, new FixedVector2(Fixed64.FromInt(4), Fixed64.Zero));
            world.SpawnObject("swordsman", 1, new FixedVector2(Fixed64.FromInt(5), Fixed64.FromInt(1)));
            return world;
        }

        var a = Build();
        var b = Build();
        for (var tick = 1; tick <= 900; tick++)
        {
            a.Tick();
            b.Tick();
            Assert.Equal(a.StateHash(), b.StateHash());
        }
        // 2v1: team 1 must win and both survivors belong to team 1.
        Assert.All(a.Objects.Values, o => Assert.Equal(1, o.Team));
        Assert.True(a.Objects.Count >= 1);
    }
}
