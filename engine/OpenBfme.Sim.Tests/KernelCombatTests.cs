using System.Diagnostics;
using System.Text.Json;
using OpenBfme.Sim;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

[Collection(TickBudgetCollection.Name)]
public sealed class KernelCombatTests
{
    private readonly ITestOutputHelper _output;

    public KernelCombatTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void IniShapedWeaponParsingRoundsMillisecondsAt33And100Milliseconds()
    {
        var spec = new ModuleSpec(
            "TestBow",
            new Dictionary<string, long>
            {
                ["AttackRange"] = 20,
                ["DelayBetweenShots"] = 1_000,
                ["PreAttackDelay"] = 50,
                ["FiringDuration"] = 16,
                ["ClipSize"] = 3,
                ["ClipReloadTime"] = 149,
                ["Damage"] = 30,
                ["Radius"] = 2,
            },
            new Dictionary<string, string>
            {
                ["DamageType"] = "SLASH",
                ["DamageFXType"] = "SWORD_SLASH",
                ["DeathType"] = "NORMAL",
                ["ProjectileNugget"] = "ArrowProjectile",
            });

        var at33 = WeaponTemplate.Parse(spec, 33);
        var at100 = WeaponTemplate.Parse(spec, 100);

        Assert.Equal(30, at33.DelayBetweenShotsTicks);
        Assert.Equal(2, at33.PreAttackDelayTicks);
        Assert.Equal(1, at33.FiringDurationTicks);
        Assert.Equal(10, at100.DelayBetweenShotsTicks);
        Assert.Equal(1, at100.PreAttackDelayTicks);
        Assert.Equal(1, at100.FiringDurationTicks);
        Assert.Equal(1, at100.ClipReloadTimeTicks);
        Assert.Equal(Fixed64.FromInt(30), Assert.Single(at33.DamageNuggets).Damage);
        Assert.Equal(DamageType.SLASH, Assert.Single(at33.DamageNuggets).DamageType);
        Assert.Equal("ArrowProjectile", at33.Projectile!.ProjectileTemplateName);
    }

    [Fact]
    public void NestedCookRowsParseRepeatedNuggetsWeaponSetsAndArmorFallback()
    {
        var weapon = WeaponTemplate.Parse("CookedSword", new Dictionary<string, object?>
        {
            ["fields"] = new Dictionary<string, object?>
            {
                ["AttackRange"] = 3,
                ["MinimumAttackRange"] = 1,
                ["DelayBetweenShots"] = 100,
                ["DamageNugget"] = new object?[]
                {
                    new Dictionary<string, object?>
                    {
                        ["fields"] = new Dictionary<string, object?>
                        {
                            ["Damage"] = 30.5m,
                            ["Radius"] = 0,
                            ["DelayTime"] = 33,
                            ["DamageType"] = "HERO_RANGED",
                            ["DamageFXType"] = "MAGIC",
                            ["DeathType"] = "NORMAL",
                        },
                    },
                    new Dictionary<string, object?>
                    {
                        ["Damage"] = 5,
                        ["Radius"] = 4,
                        ["DamageType"] = "FLAME",
                        ["FriendlyFire"] = true,
                    },
                },
                ["MetaImpactNugget"] = new Dictionary<string, object?>
                {
                    ["Amount"] = 10,
                    ["Radius"] = 5,
                    ["TaperOff"] = 1,
                },
                ["ProjectileNugget"] = new Dictionary<string, object?>
                {
                    ["ProjectileTemplateName"] = "CookedArrow",
                },
            },
        }, 33);
        var armor = ArmorTemplate.Parse("Plate", new Dictionary<string, object?>
        {
            ["Armor"] = new object?[] { "DEFAULT 100%", "SLASH 75%" },
        });
        var set = WeaponSet.Parse(new Dictionary<string, object?>
        {
            ["Conditions"] = "UPGRADED HERO_MODE",
            ["Weapon"] = new object?[] { "PRIMARY CookedSword", "SECONDARY Backup" },
            ["AutoChooseSources"] = "PRIMARY FROM_AI FROM_PLAYER",
            ["PreferredAgainst"] = "PRIMARY INFANTRY HERO",
        });
        var moduleSet = WeaponSet.Parse(new ModuleSpec(
            "WeaponSet",
            stringData: new Dictionary<string, string>
            {
                ["Conditions"] = "MOUNTED",
                ["Weapon:PRIMARY"] = "CookedSword",
                ["AutoChooseSources:PRIMARY"] = "FROM_AI FROM_PLAYER",
                ["PreferredAgainst:PRIMARY"] = "HERO",
            }));
        var moduleArmorSet = ArmorSet.Parse(new ModuleSpec(
            "ArmorSet",
            stringData: new Dictionary<string, string>
            {
                ["Conditions"] = "MOUNTED",
                ["Armor"] = "Plate",
                ["DamageFX"] = "PlateDamageFX",
            }));

        Assert.Equal(2, weapon.DamageNuggets.Count);
        Assert.Equal(Fixed64.FromFraction(61, 2), weapon.DamageNuggets[0].Damage);
        Assert.Equal(1, weapon.DamageNuggets[0].DelayTicks);
        Assert.True(weapon.DamageNuggets[1].FriendlyFire);
        Assert.NotNull(weapon.MetaImpact);
        Assert.Equal(Fixed64.FromFraction(3, 4), armor.MultiplierFor(DamageType.SLASH));
        Assert.Equal(Fixed64.One, armor.MultiplierFor(DamageType.MAGIC));
        Assert.Equal("CookedSword", set.PrimaryWeaponName);
        Assert.Equal(new[] { "FROM_AI", "FROM_PLAYER" }, set.AutoChooseSources[WeaponSlot.PRIMARY]);
        Assert.Equal("CookedSword", moduleSet.PrimaryWeaponName);
        Assert.Contains("MOUNTED", moduleSet.Conditions);
        Assert.Equal("Plate", moduleArmorSet.ArmorName);
        Assert.Equal("PlateDamageFX", moduleArmorSet.DamageFX);
    }

    [Fact]
    public void ConditionalWeaponSetSelectsPrimarySlot()
    {
        var weak = Weapon("Weak", 1, 5);
        var strong = Weapon("Strong", 1, 20);
        var attackerTemplate = CombatTemplate(
            "attacker",
            100,
            new[]
            {
                Set("Weak"),
                Set("Strong", "UPGRADED"),
            });
        var world = World(new[] { attackerTemplate, TargetTemplate("target", 100) }, new[] { weak, strong });
        var attacker = world.SpawnObject("attacker", 0, Position(0));
        var target = world.SpawnObject("target", 1, Position(1));
        attacker.SetConditionToken("UPGRADED");

        world.Tick();

        Assert.Equal(Fixed64.FromInt(80), target.Health);
    }

    [Fact]
    public void SlashDamageUsesArmorAndFireDamageEventsCarryAttackerTargetAndAmount()
    {
        var weapon = Weapon("Sword", 2, 30, delayMilliseconds: 1_000);
        var armor = new ArmorTemplate("HalfSlash", new Dictionary<DamageType, Fixed64>
        {
            [DamageType.DEFAULT] = Fixed64.One,
            [DamageType.SLASH] = Fixed64.Half,
        });
        var world = World(
            new[]
            {
                CombatTemplate("attacker", 100, new[] { Set("Sword") }),
                TargetTemplate("target", 100, new[] { new ArmorSet(null, "HalfSlash") }),
            },
            new[] { weapon },
            new[] { armor });
        var attacker = world.SpawnObject("attacker", 0, Position(0));
        var target = world.SpawnObject("target", 1, Position(1));

        world.Tick();

        Assert.Equal(Fixed64.FromInt(85), target.Health);
        var fire = Assert.Single(world.EventsThisTick, value => value.Kind == "fire");
        Assert.Equal(attacker.Id, fire.Object);
        Assert.Equal(target.Id, fire.Target);
        var damage = Assert.Single(world.EventsThisTick, value => value.Kind == "damage");
        Assert.Equal(attacker.Id, damage.Object);
        Assert.Equal(target.Id, damage.Target);
        Assert.Equal(Fixed64.FromInt(15), damage.Amount);
    }

    [Fact]
    public void ThousandMillisecondShotDelayAt33MillisecondsFiresEveryThirtyTicks()
    {
        var world = World(
            new[]
            {
                CombatTemplate("archer", 100, new[] { Set("Bow") }),
                TargetTemplate("target", 10_000),
            },
            new[] { Weapon("Bow", 3, 1, delayMilliseconds: 1_000) });
        world.SpawnObject("archer", 0, Position(0));
        world.SpawnObject("target", 1, Position(1));
        var fireTicks = new List<int>();

        for (var tick = 1; tick <= 65; tick++)
        {
            world.Tick();
            if (world.EventsThisTick.Any(value => value.Kind == "fire")) fireTicks.Add(tick);
        }

        Assert.Equal(new[] { 1, 31, 61 }, fireTicks);
    }

    [Fact]
    public void PreAttackPerClipReloadAndDelayedNuggetLandOnExactTicks()
    {
        var weapon = new WeaponTemplate(
            "ClipBow",
            Fixed64.FromInt(3),
            Fixed64.Zero,
            3,
            2,
            PreAttackType.PER_CLIP,
            0,
            2,
            5,
            new[] { new DamageNugget(Fixed64.One, Fixed64.Zero, 2, DamageType.PIERCE, "", "NORMAL") },
            projectile: new ProjectileNugget("Arrow"));
        var world = World(
            new[]
            {
                CombatTemplate("archer", 100, new[] { Set("ClipBow") }),
                TargetTemplate("target", 100),
            },
            new[] { weapon });
        world.SpawnObject("archer", 0, Position(0));
        var target = world.SpawnObject("target", 1, Position(1));
        var fireTicks = new List<int>();
        var damageTicks = new List<int>();

        for (var tick = 1; tick <= 13; tick++)
        {
            world.Tick();
            if (world.EventsThisTick.Any(value => value.Kind == "fire")) fireTicks.Add(tick);
            if (world.EventsThisTick.Any(value => value.Kind == "damage")) damageTicks.Add(tick);
        }

        Assert.Equal(new[] { 3, 6, 13 }, fireTicks);
        Assert.Equal(new[] { 5, 8 }, damageTicks);
        Assert.Equal(Fixed64.FromInt(98), target.Health);
        Assert.Contains(world.EventsThisTick,
            value => value.Kind == "fire" && value.Name == "ClipBow:projectile:Arrow");
    }

    [Fact]
    public void InitialHealthIsPublishedAndStopCancelsAttackPursuit()
    {
        var body = new BodyHealthTemplate(Fixed64.FromInt(100), Fixed64.FromInt(75));
        var attackerTemplate = new ObjectTemplate(
            "attacker",
            new ModuleSpec[]
            {
                new(LinearMoverModule.TypeName, new Dictionary<string, long>
                {
                    ["SpeedPerTickRaw"] = Fixed64.One.Raw,
                }),
                new(DestroyDieModule.TypeName),
            },
            new[] { Set("Sword") },
            bodyHealth: body);
        var world = World(
            new[] { attackerTemplate, TargetTemplate("target", 100) },
            new[] { Weapon("Sword", 1, 10) });
        var attacker = world.SpawnObject("attacker", 0, Position(0));
        var target = world.SpawnObject("target", 1, Position(20));
        world.SubmitCommand(Attack(1, attacker.Id, target.Id));
        world.Tick();
        var afterApproach = attacker.Position.X;
        world.SubmitCommand(TestWorlds.Command(2, 0, 1, "stop",
            ("objects", CommandValue.OfLongList(new long[] { attacker.Id }))));

        world.Tick();

        Assert.Equal(Fixed64.FromInt(75), attacker.Health);
        Assert.Equal(afterApproach, attacker.Position.X);
        using var snapshot = JsonDocument.Parse(SnapshotWriter.Write(world));
        Assert.Equal(75, snapshot.RootElement.GetProperty("objects").GetProperty("health")[0].GetDecimal());
        Assert.Equal(100, snapshot.RootElement.GetProperty("objects").GetProperty("max_health")[0].GetDecimal());
    }

    [Fact]
    public void ExplicitAttackMovesIntoRangeAndAggressivePursuesWhileHoldGroundDoesNot()
    {
        SimWorld Build(UnitStance stance, int targetX)
        {
            var world = World(
                new[]
                {
                    CombatTemplate("fighter", 100, new[] { Set("Sword") }, moving: true),
                    TargetTemplate("target", 1_000),
                },
                new[] { Weapon("Sword", 2, 10, delayMilliseconds: 100) });
            var attacker = world.SpawnObject("fighter", 0, Position(0));
            var target = world.SpawnObject("target", 1, Position(targetX));
            world.SubmitCommand(Attack(1, attacker.Id, target.Id));
            world.SubmitCommand(Stance(1, attacker.Id, stance));
            return world;
        }

        var approach = Build(UnitStance.Aggressive, 10);
        var approachAttacker = approach.Objects[1];
        var approachTarget = approach.Objects[2];
        var firstFire = 0;
        for (var tick = 1; tick <= 20; tick++)
        {
            approach.Tick();
            if (firstFire == 0 && approach.EventsThisTick.Any(value => value.Kind == "fire")) firstFire = tick;
        }
        Assert.True(firstFire > 1, "out-of-range attacker fired without approaching");
        Assert.True(approachAttacker.Position.X > Fixed64.Zero);

        approachTarget.SetPosition(Position(30));
        var beforePursuit = approachAttacker.Position.X;
        approach.Advance(3);
        Assert.True(approachAttacker.Position.X > beforePursuit);

        var hold = Build(UnitStance.HoldGround, 1);
        var holdAttacker = hold.Objects[1];
        var holdTarget = hold.Objects[2];
        hold.Tick();
        Assert.Contains(hold.EventsThisTick, value => value.Kind == "fire");
        holdTarget.SetPosition(Position(20));
        hold.Advance(5);
        Assert.Equal(Fixed64.Zero, holdAttacker.Position.X);
        Assert.DoesNotContain(hold.EventsThisTick, value => value.Kind == "fire");
    }

    [Fact]
    public void RadiusNuggetDamagesEveryEnemyInsideAndNoOneElse()
    {
        var radiusWeapon = new WeaponTemplate(
            "Blast",
            Fixed64.FromInt(10),
            Fixed64.Zero,
            30,
            0,
            PreAttackType.PER_SHOT,
            0,
            0,
            0,
            new[] { new DamageNugget(Fixed64.FromInt(10), Fixed64.FromInt(3), 0, DamageType.FLAME, "", "NORMAL") });
        var world = World(
            new[]
            {
                CombatTemplate("caster", 100, new[] { Set("Blast") }),
                TargetTemplate("enemy", 100),
                TargetTemplate("ally", 100),
            },
            new[] { radiusWeapon });
        world.SpawnObject("caster", 0, Position(0));
        var impact = world.SpawnObject("enemy", 1, Position(5));
        var inside = world.SpawnObject("enemy", 1, Position(7));
        var outside = world.SpawnObject("enemy", 1, Position(9));
        var ally = world.SpawnObject("ally", 0, Position(6));

        world.Tick();

        Assert.Equal(Fixed64.FromInt(90), impact.Health);
        Assert.Equal(Fixed64.FromInt(90), inside.Health);
        Assert.Equal(Fixed64.FromInt(100), outside.Health);
        Assert.Equal(Fixed64.FromInt(100), ally.Health);
    }

    [Fact]
    public void SlowDeathSetsDyingFlagThenFreesSlotAndRaisesOneDeathEvent()
    {
        var victimTemplate = new ObjectTemplate(
            "victim",
            new ModuleSpec[]
            {
                new(SlowDeathModule.TypeName, new Dictionary<string, long> { ["DeathTicks"] = 3 }),
                new(DestroyDieModule.TypeName),
            },
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(10)));
        var world = World(
            new[] { CombatTemplate("attacker", 100, new[] { Set("Kill") }), victimTemplate },
            new[] { Weapon("Kill", 2, 10) });
        world.SpawnObject("attacker", 0, Position(0));
        var victim = world.SpawnObject("victim", 1, Position(1));
        var slot = world.ObjectStore.LiveSlots().Single(value => world.ObjectStore.Id[value] == victim.Id);
        var deathEvents = 0;

        for (var tick = 1; tick <= 3; tick++)
        {
            world.Tick();
            deathEvents += world.EventsThisTick.Count(value => value.Kind == "death");
            if (tick < 3)
            {
                Assert.True(world.Objects.ContainsKey(victim.Id));
                Assert.Equal(4, world.ObjectStore.Flags[slot] & 4);
            }
        }

        Assert.False(world.Objects.ContainsKey(victim.Id));
        Assert.False(world.ObjectStore.IsLive(slot));
        Assert.Equal(1, deathEvents);
    }

    [Fact]
    public void HordeLosingEveryMemberDisappearsFromSnapshot()
    {
        var blast = new WeaponTemplate(
            "HordeBlast", Fixed64.FromInt(10), Fixed64.Zero, 30, 0,
            PreAttackType.PER_SHOT, 0, 0, 0,
            new[] { new DamageNugget(Fixed64.FromInt(10), Fixed64.FromInt(5), 0, DamageType.MAGIC, "", "NORMAL") });
        var world = World(
            new[]
            {
                CombatTemplate("attacker", 100, new[] { Set("HordeBlast") }),
                TargetTemplate("member", 10),
            },
            new[] { blast });
        world.SpawnObject("attacker", 0, Position(0));
        var members = new[]
        {
            world.SpawnObject("member", 1, Position(4)).Id,
            world.SpawnObject("member", 1, Position(5)).Id,
            world.SpawnObject("member", 1, Position(6)).Id,
        };
        world.AddHorde(new SnapshotHorde(100, 1, 0, members, 0));

        world.Tick();

        Assert.Empty(world.Hordes);
        using var snapshot = JsonDocument.Parse(SnapshotWriter.Write(world));
        Assert.Empty(snapshot.RootElement.GetProperty("hordes").EnumerateArray());
    }

    [Fact]
    public void TwinRunHordesAttackMoveHashIdenticallyFor900TicksAndOneSideIsAnnihilated()
    {
        SimWorld Build()
        {
            var world = World(
                new[] { CombatTemplate("member", 100, new[] { Set("Sword") }, locomotor: true) },
                new[] { Weapon("Sword", 3, 25, delayMilliseconds: 66) });
            var left = new List<int>();
            var right = new List<int>();
            for (var index = 0; index < 10; index++) left.Add(world.SpawnObject("member", 0, Position(20, 20 + index)).Id);
            for (var index = 0; index < 10; index++) right.Add(world.SpawnObject("member", 1, Position(80, 20 + index)).Id);
            world.AddHorde(new SnapshotHorde(100, 0, 0, left, 0));
            world.AddHorde(new SnapshotHorde(101, 1, 0, right, 0));
            world.SubmitCommand(AttackMove(1, new long[] { 100 }, Position(50, 25), team: 0));
            world.SubmitCommand(AttackMove(1, new long[] { 101 }, Position(50, 25), team: 1));
            return world;
        }

        var a = Build();
        var b = Build();
        var fireCount = 0;
        var minimumDistance = Fixed64.MaxValue;
        for (var tick = 1; tick <= 900; tick++)
        {
            a.Tick();
            b.Tick();
            fireCount += a.EventsThisTick.Count(value => value.Kind == "fire");
            foreach (var left in a.Objects.Values.Where(value => value.Team == 0))
            foreach (var right in a.Objects.Values.Where(value => value.Team == 1))
                minimumDistance = Fixed64.Min(minimumDistance,
                    Fixed64.Sqrt(left.Position.DistanceSquaredTo(right.Position)));
            Assert.Equal(a.StateHash(), b.StateHash());
        }

        var teamZero = a.Objects.Values.Count(value => value.Team == 0);
        var teamOne = a.Objects.Values.Count(value => value.Team == 1);
        var firstZero = a.Objects.Values.FirstOrDefault(value => value.Team == 0);
        var firstOne = a.Objects.Values.FirstOrDefault(value => value.Team == 1);
        _output.WriteLine($"twin_run_survivors_team0={teamZero} team1={teamOne} " +
            $"team0_x={(firstZero == null ? "none" : firstZero.Position.X.ToString())} " +
            $"team1_x={(firstOne == null ? "none" : firstOne.Position.X.ToString())} " +
            $"fires={fireCount} min_distance={minimumDistance}");
        Assert.True(teamZero == 0 || teamOne == 0, $"team0={teamZero} team1={teamOne}");
        Assert.NotEmpty(a.Objects);
    }

    [Fact]
    [Trait("Category", "Scale")]
    public void FourThousandMembersAttackMoveFor300TicksAndReportElapsedMilliseconds()
    {
        var world = World(
            new[] { CombatTemplate("member", 100, new[] { Set("ScaleSword") }, locomotor: true) },
            new[] { Weapon("ScaleSword", 3, 100, delayMilliseconds: 66) });
        var leftHordes = new List<long>();
        var rightHordes = new List<long>();
        var hordeRows = new List<SnapshotHorde>();
        var hordeId = 100_000;
        for (var side = 0; side < 2; side++)
        {
            for (var horde = 0; horde < 200; horde++)
            {
                var members = new List<int>();
                var x = side == 0 ? 50 : 450;
                for (var member = 0; member < 10; member++)
                {
                    members.Add(world.SpawnObject("member", side, Position(x, 5 + horde)).Id);
                }
                hordeRows.Add(new SnapshotHorde(hordeId, side, 0, members, 0));
                (side == 0 ? leftHordes : rightHordes).Add(hordeId);
                hordeId++;
            }
        }
        foreach (var horde in hordeRows.OrderBy(value => value.Id)) world.AddHorde(horde);
        world.SubmitCommand(AttackMove(1, leftHordes, Position(450, 105), 0));
        world.SubmitCommand(AttackMove(1, rightHordes, Position(50, 105), 1));

        var profile = new TickPhaseProbe();
        world.TickPhaseObserver = profile;
        var stopwatch = Stopwatch.StartNew();
        for (var tick = 0; tick < 300; tick++)
        {
            world.Tick();
            _ = world.StateHash();
        }
        stopwatch.Stop();

        _output.WriteLine($"scale_200x10_per_team_300_ticks_elapsed_ms={stopwatch.ElapsedMilliseconds}");
        profile.WriteTable(_output, "combat 200 hordes x 10 members per side", 300, stopwatch.ElapsedMilliseconds);
#if RELEASE
        Assert.True(stopwatch.ElapsedMilliseconds < 33L * 300,
            $"combat averaged {(double)stopwatch.ElapsedMilliseconds / 300:F3} ms/tick; budget is 33 ms/tick");
#endif
    }

    [Fact]
    public void WrongTeamAttackAndStanceCommandsUseDiagnosticRefusalPath()
    {
        var world = World(
            new[] { CombatTemplate("fighter", 100, new[] { Set("Sword") }) },
            new[] { Weapon("Sword", 2, 10) });
        var fighter = world.SpawnObject("fighter", 1, Position(0));
        var target = world.SpawnObject("fighter", 0, Position(20));
        world.SubmitCommand(Attack(1, fighter.Id, target.Id, team: 0));
        world.SubmitCommand(Stance(1, fighter.Id, UnitStance.Aggressive, team: 0));

        world.Tick();

        Assert.Equal(2, world.Diagnostics.Count(value => value.Code == "wrong_team"));
        Assert.Equal(Fixed64.FromInt(100), fighter.Health);
    }

    private static WeaponTemplate Weapon(
        string name,
        int range,
        int damage,
        int delayMilliseconds = 33) => new(
        name,
        Fixed64.FromInt(range),
        Fixed64.Zero,
        IniValueReaderForTests.Ticks(delayMilliseconds, 33),
        0,
        PreAttackType.PER_SHOT,
        0,
        0,
        0,
        new[] { new DamageNugget(Fixed64.FromInt(damage), Fixed64.Zero, 0, DamageType.SLASH, "", "NORMAL") });

    private static WeaponSet Set(string weapon, params string[] conditions) =>
        new(conditions, new Dictionary<WeaponSlot, string> { [WeaponSlot.PRIMARY] = weapon });

    private static ObjectTemplate CombatTemplate(
        string name,
        int health,
        IReadOnlyList<WeaponSet> weaponSets,
        bool moving = false,
        bool locomotor = false)
    {
        var modules = new List<ModuleSpec>();
        if (moving)
        {
            modules.Add(new ModuleSpec(LinearMoverModule.TypeName, new Dictionary<string, long>
            {
                ["SpeedPerTickRaw"] = Fixed64.One.Raw,
            }));
        }
        if (locomotor)
        {
            modules.Add(new ModuleSpec(LocomotorModule.TypeName, new Dictionary<string, long>
            {
                ["Speed"] = 30,
                ["Acceleration"] = 900,
                ["Braking"] = 900,
                ["TurnRate"] = 10_000,
            }));
        }
        modules.Add(new ModuleSpec(DestroyDieModule.TypeName));
        return new ObjectTemplate(
            name,
            modules,
            weaponSets,
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(health)));
    }

    private static ObjectTemplate TargetTemplate(
        string name,
        int health,
        IReadOnlyList<ArmorSet>? armorSets = null) => new(
        name,
        new[] { new ModuleSpec(DestroyDieModule.TypeName) },
        armorSets: armorSets,
        bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(health)));

    private static SimWorld World(
        IReadOnlyList<ObjectTemplate> templates,
        IReadOnlyList<WeaponTemplate> weapons,
        IReadOnlyList<ArmorTemplate>? armor = null) => new(
        new SimConfig(templates, 42, 2, weaponTemplates: weapons, armorTemplates: armor),
        ModuleRegistry.CreateDefault(),
        33);

    private static FixedVector2 Position(int x, int y = 0) =>
        new(Fixed64.FromInt(x), Fixed64.FromInt(y));

    private static SimCommand Attack(int tick, int attacker, int target, int team = 0) =>
        TestWorlds.Command(tick, team, 0, "attack",
            ("objects", CommandValue.OfLongList(new long[] { attacker })),
            ("target", CommandValue.OfLong(target)));

    private static SimCommand AttackMove(
        int tick,
        IEnumerable<long> objects,
        FixedVector2 goal,
        int team) =>
        TestWorlds.Command(tick, team, 0, "attack_move",
            ("objects", CommandValue.OfLongList(objects)),
            ("x", CommandValue.OfFixed(goal.X)),
            ("y", CommandValue.OfFixed(goal.Y)));

    private static SimCommand Stance(int tick, int objectId, UnitStance stance, int team = 0) =>
        TestWorlds.Command(tick, team, 1, "stance",
            ("objects", CommandValue.OfLongList(new long[] { objectId })),
            ("stance", CommandValue.OfString(stance switch
            {
                UnitStance.Aggressive => "aggressive",
                UnitStance.Battle => "battle",
                UnitStance.HoldGround => "hold_ground",
                _ => throw new ArgumentOutOfRangeException(nameof(stance)),
            })));

    private static class IniValueReaderForTests
    {
        public static int Ticks(int milliseconds, int tickMilliseconds)
        {
            if (milliseconds == 0) return 0;
            return Math.Max(1, (2 * milliseconds + tickMilliseconds) / (2 * tickMilliseconds));
        }
    }
}
