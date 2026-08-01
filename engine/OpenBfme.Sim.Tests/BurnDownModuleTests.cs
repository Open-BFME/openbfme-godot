using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

/// <summary>
/// Burn-down batch tests: production economy (cost debit/reject/refund), retail
/// spawn phase (command_tick + build_ticks, exact), exit->rally walk, and the
/// three new gap modules — HordeContain (member-slot health delegation),
/// BezierProjectile-lite (straight-line flight, typed damage on arrival), and
/// AttributeModifierAura-lite (radius armor aura, additive stacking clamped to
/// 10000 bp, table rebuilt at end of tick).
/// </summary>
public class BurnDownModuleTests
{
    private const long SoldierCost = 300;
    private const long SoldierBuildTicks = 20;

    private static SimConfig Config(ulong seed = 11) => new(
        new[]
        {
            new ObjectTemplate("keep", new[]
            {
                new ModuleSpec(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 900 }),
                new ModuleSpec(ProductionModule.TypeName, new Dictionary<string, long>
                {
                    ["Build:soldier"] = SoldierBuildTicks,
                    ["Cost:soldier"] = SoldierCost,
                    ["RallyXRaw"] = Fixed64.FromInt(4).Raw,
                    ["RallyYRaw"] = Fixed64.FromInt(3).Raw,
                }),
            }),
            new ObjectTemplate("soldier", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 120 }),
                new ModuleSpec(LinearMoverModule.TypeName, new Dictionary<string, long>
                {
                    ["SpeedPerTickRaw"] = Fixed64.FromFraction(1, 2).Raw,
                }),
            }),
            new ObjectTemplate("horde", new[]
            {
                new ModuleSpec(HordeContainModule.TypeName, new Dictionary<string, long>
                {
                    ["MemberCount"] = 3,
                    ["MemberHealth"] = 100,
                }),
            }),
            new ObjectTemplate("arrow", new[]
            {
                new ModuleSpec(BezierProjectileModule.TypeName, new Dictionary<string, long>
                {
                    ["FlightTicks"] = 8,
                    ["Damage"] = 40,
                }, new Dictionary<string, string>
                {
                    ["DamageType"] = DamageTypes.Siege,
                }),
            }),
            new ObjectTemplate("target", new[]
            {
                new ModuleSpec(ArmorModule.TypeName, new Dictionary<string, long> { ["Armor:siege"] = 5_000 }),
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 200 }),
            }),
            new ObjectTemplate("grunt", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 200 }),
                new ModuleSpec(LinearMoverModule.TypeName, new Dictionary<string, long>
                {
                    ["SpeedPerTickRaw"] = Fixed64.FromFraction(1, 2).Raw,
                }),
            }),
            new ObjectTemplate("banner", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 100 }),
                new ModuleSpec(AttributeModifierAuraModule.TypeName, new Dictionary<string, long>
                {
                    ["RadiusRaw"] = Fixed64.FromInt(5).Raw,
                    ["RecomputeTicks"] = 4,
                    ["ArmorBonusBp"] = 2_500,
                }),
            }),
        },
        seed,
        teamCount: 2);

    private static SimWorld NewWorld(ulong seed = 11) => new(Config(seed), ModuleRegistry.CreateDefault());

    private static FixedVector2 At(int x, int y) => new(Fixed64.FromInt(x), Fixed64.FromInt(y));

    // ---- production economy -------------------------------------------------

    [Fact]
    public void ProductionCostDebitsRejectsUnaffordableAndRefundsOnCancel()
    {
        var world = NewWorld();
        world.AddTeamResources(0, 500);
        var keep = world.SpawnObject("keep", 0, FixedVector2.Zero);
        var production = keep.FindModule<ProductionModule>()!;

        Assert.True(production.TryQueue(world, keep, "soldier"));
        Assert.Equal(500 - SoldierCost, world.TeamResources(0));

        // 200 < 300: unaffordable requests are rejected WITHOUT queueing.
        Assert.False(production.TryQueue(world, keep, "soldier"));
        Assert.Equal(1, production.QueueLength);
        Assert.Equal(500 - SoldierCost, world.TeamResources(0));

        // Cancellation refunds the full cost.
        Assert.True(production.TryCancel(world, keep, 0));
        Assert.Equal(0, production.QueueLength);
        Assert.Equal(500, world.TeamResources(0));
        Assert.False(production.TryCancel(world, keep, 0));
        Assert.Equal(500, world.TeamResources(0));
    }

    [Fact]
    public void CancelProductionCommandRefundsThroughTeamValidation()
    {
        var world = NewWorld();
        world.AddTeamResources(0, 1000);
        var keep = world.SpawnObject("keep", 0, FixedVector2.Zero);
        world.SubmitCommand(TestWorlds.Command(1, 0, 0, "queue_production",
            ("id", CommandValue.OfLong(keep.Id)), ("template", CommandValue.OfString("soldier"))));
        // Wrong team may not cancel (no refund, entry stays).
        world.SubmitCommand(TestWorlds.Command(2, 1, 0, "cancel_production",
            ("id", CommandValue.OfLong(keep.Id)), ("index", CommandValue.OfLong(0))));
        world.Advance(2);
        Assert.Equal(1, keep.FindModule<ProductionModule>()!.QueueLength);
        Assert.Equal(1000 - SoldierCost, world.TeamResources(0));
        // Right team cancels and is refunded.
        world.SubmitCommand(TestWorlds.Command(3, 0, 1, "cancel_production",
            ("id", CommandValue.OfLong(keep.Id)), ("index", CommandValue.OfLong(0))));
        world.Advance(1);
        Assert.Equal(0, keep.FindModule<ProductionModule>()!.QueueLength);
        Assert.Equal(1000, world.TeamResources(0));
    }

    [Fact]
    public void SpawnLandsExactlyAtCommandTickPlusBuildTicks()
    {
        var world = NewWorld();
        world.AddTeamResources(0, 500);
        var keep = world.SpawnObject("keep", 0, FixedVector2.Zero);
        const int commandTick = 5;
        world.SubmitCommand(TestWorlds.Command(commandTick, 0, 0, "queue_production",
            ("id", CommandValue.OfLong(keep.Id)), ("template", CommandValue.OfString("soldier"))));

        // Cost is debited when the command applies, at tick start of commandTick.
        world.Advance(commandTick);
        Assert.Equal(500 - SoldierCost, world.TeamResources(0));

        // Retail spawn phase: the soldier exists after tick commandTick + build_ticks,
        // and not one tick sooner.
        world.Advance((int)SoldierBuildTicks - 1); // now at commandTick + build_ticks - 1
        Assert.Single(world.Objects);
        world.Advance(1); // commandTick + build_ticks
        Assert.Equal(2, world.Objects.Count);
    }

    [Fact]
    public void SpawnedUnitWalksFromExitToRallyPoint()
    {
        var world = NewWorld();
        world.AddTeamResources(0, 500);
        var keep = world.SpawnObject("keep", 0, At(10, 10));
        Assert.True(keep.FindModule<ProductionModule>()!.TryQueue(world, keep, "soldier"));

        // Queued pre-tick: build ticks 1..20, spawn during tick 21 at the exit
        // point (default ExitOffset +2,0), rally walk begins next tick.
        world.Advance(21);
        var soldier = world.Objects.Values.Single(o => o.TemplateName == "soldier");
        Assert.Equal(At(12, 10), soldier.Position);

        // RallyX/YRaw are producer-relative: (10,10) + (4,3) = (14,13). The
        // LinearMover snaps exactly onto the rally point.
        world.Advance(40);
        Assert.Equal(At(14, 13), soldier.Position);
    }

    // ---- HordeContain --------------------------------------------------------

    [Fact]
    public void HordeDamageFillsMemberSlotsDeterministicallyAndKillsThrough()
    {
        var world = NewWorld();
        var horde = world.SpawnObject("horde", 1, FixedVector2.Zero);
        var contain = horde.FindModule<HordeContainModule>()!;
        Assert.Equal(3, contain.MemberCount);
        Assert.Equal(300, contain.TotalHealth);

        // 150 damage: slot 0 dies (100), overflow 50 into slot 1, slot 2 untouched.
        world.DealDamage(horde, 150);
        Assert.Equal(0, contain.MemberHealthAt(0));
        Assert.Equal(50, contain.MemberHealthAt(1));
        Assert.Equal(100, contain.MemberHealthAt(2));
        Assert.Equal(2, contain.AliveMemberCount);

        // Down to the last hit point of the last member.
        world.DealDamage(horde, 149);
        Assert.Equal(1, contain.AliveMemberCount);
        Assert.Equal(1, contain.TotalHealth);
        Assert.Single(world.Objects);

        // Final point: the horde dies through the normal death pipeline.
        world.DealDamage(horde, 1);
        world.Advance(1);
        Assert.Empty(world.Objects);
    }

    // ---- BezierProjectile-lite -----------------------------------------------

    [Fact]
    public void ProjectileFliesForFlightTicksThenDealsTypedDamageAndExpires()
    {
        var world = NewWorld();
        var target = world.SpawnObject("target", 1, At(8, 0));
        var arrow = world.SpawnObject("arrow", 0, FixedVector2.Zero);
        arrow.FindModule<BezierProjectileModule>()!.Launch(world, arrow, target.Id);

        // Flight ticks 1..7: no damage yet, arrow closing on the target.
        world.Advance(7);
        Assert.Equal(200, target.FindModule<ActiveBodyModule>()!.Health);
        Assert.Equal(2, world.Objects.Count);

        // Arrival on the 8th update: 40 siege into Armor:siege 5000bp = 20; the
        // spent projectile leaves the world the same tick.
        world.Advance(1);
        Assert.Equal(180, target.FindModule<ActiveBodyModule>()!.Health);
        Assert.Single(world.Objects);
    }

    [Fact]
    public void ProjectileExpiresHarmlesslyWhenTargetDiesMidFlight()
    {
        var world = NewWorld();
        var target = world.SpawnObject("target", 1, At(8, 0));
        var arrow = world.SpawnObject("arrow", 0, FixedVector2.Zero);
        arrow.FindModule<BezierProjectileModule>()!.Launch(world, arrow, target.Id);
        world.Advance(3);
        world.DealDamage(target, 10_000); // dies; removed next tick
        world.Advance(5);
        Assert.Empty(world.Objects); // no crash, arrow expired without damage
    }

    // ---- AttributeModifierAura-lite -------------------------------------------

    [Fact]
    public void AuraReducesDamageForAlliesInRangeOnlyAndStacksAdditively()
    {
        var world = NewWorld();
        var banner = world.SpawnObject("banner", 0, FixedVector2.Zero);
        var allyNear = world.SpawnObject("grunt", 0, At(3, 0));
        var allyFar = world.SpawnObject("grunt", 0, At(30, 0));
        var enemyNear = world.SpawnObject("grunt", 1, At(2, 0));
        world.Advance(1); // aura scans on its first update; table rebuilt at end of tick

        Assert.Equal(2_500, world.AuraArmorBonusBp(allyNear.Id));
        Assert.Equal(0, world.AuraArmorBonusBp(allyFar.Id));
        Assert.Equal(0, world.AuraArmorBonusBp(enemyNear.Id));
        Assert.Equal(0, world.AuraArmorBonusBp(banner.Id)); // carriers do not buff themselves

        world.DealDamage(allyNear, 100);
        world.DealDamage(allyFar, 100);
        world.DealDamage(enemyNear, 100);
        Assert.Equal(200 - 75, allyNear.FindModule<ActiveBodyModule>()!.Health);
        Assert.Equal(200 - 100, allyFar.FindModule<ActiveBodyModule>()!.Health);
        Assert.Equal(200 - 100, enemyNear.FindModule<ActiveBodyModule>()!.Health);

        // STACKING RULE: contributions from multiple carriers ADD (clamped to
        // 10000 bp at application). Two 2500bp banners = 5000bp = half damage.
        world.SpawnObject("banner", 0, At(1, 0));
        world.Advance(1);
        Assert.Equal(5_000, world.AuraArmorBonusBp(allyNear.Id));
        world.DealDamage(allyNear, 100);
        Assert.Equal(200 - 75 - 50, allyNear.FindModule<ActiveBodyModule>()!.Health);
    }

    [Fact]
    public void AuraMembershipFollowsMovementOnTheRecomputeCadence()
    {
        var world = NewWorld();
        world.SpawnObject("banner", 0, FixedVector2.Zero);
        var grunt = world.SpawnObject("grunt", 0, At(3, 0));
        world.Advance(1);
        Assert.Equal(2_500, world.AuraArmorBonusBp(grunt.Id));

        grunt.FindModule<LinearMoverModule>()!.SetDestination(At(30, 0));
        // Walking out of the 5-unit radius: dropped on a later rescan
        // (RecomputeTicks = 4), and the dead banner case never leaks because
        // the table is rebuilt from live carriers every tick.
        world.Advance(80);
        Assert.Equal(At(30, 0), grunt.Position);
        Assert.Equal(0, world.AuraArmorBonusBp(grunt.Id));
    }

    // ---- serialization + determinism ------------------------------------------

    private static SimWorld BuildCombinedScenario(ulong seed = 21)
    {
        var world = NewWorld(seed);
        world.AddTeamResources(0, 2000);
        world.AddTeamResources(1, 2000);
        var keep = world.SpawnObject("keep", 0, FixedVector2.Zero);          // id 1
        world.SpawnObject("banner", 0, At(5, 0));                            // id 2
        var grunt = world.SpawnObject("grunt", 0, At(4, 0));                 // id 3
        world.SpawnObject("grunt", 0, At(6, 0));                             // id 4
        var horde = world.SpawnObject("horde", 1, At(20, 0));                // id 5
        var target = world.SpawnObject("target", 1, At(8, 0));               // id 6
        var arrow = world.SpawnObject("arrow", 1, At(20, 5));                // id 7
        arrow.FindModule<BezierProjectileModule>()!.Launch(world, arrow, grunt.Id);

        world.SubmitCommand(TestWorlds.Command(10, 0, 0, "queue_production",
            ("id", CommandValue.OfLong(keep.Id)), ("template", CommandValue.OfString("soldier"))));
        world.SubmitCommand(TestWorlds.Command(10, 0, 1, "queue_production",
            ("id", CommandValue.OfLong(keep.Id)), ("template", CommandValue.OfString("soldier"))));
        world.SubmitCommand(TestWorlds.Command(12, 0, 2, "cancel_production",
            ("id", CommandValue.OfLong(keep.Id)), ("index", CommandValue.OfLong(1))));
        world.SubmitCommand(TestWorlds.Command(40, 0, 3, "move",
            ("id", CommandValue.OfLong(grunt.Id)), ("x", CommandValue.OfFixed(Fixed64.FromInt(25))), ("y", CommandValue.OfFixed(Fixed64.Zero))));
        world.SubmitCommand(TestWorlds.Command(50, 1, 0, "damage",
            ("id", CommandValue.OfLong(horde.Id)), ("amount", CommandValue.OfLong(250))));
        world.SubmitCommand(TestWorlds.Command(60, 0, 4, "damage",
            ("id", CommandValue.OfLong(target.Id)), ("amount", CommandValue.OfLong(120))));
        world.SubmitCommand(TestWorlds.Command(400, 1, 1, "damage",
            ("id", CommandValue.OfLong(horde.Id)), ("amount", CommandValue.OfLong(100))));
        return world;
    }

    [Fact]
    public void SnapshotRoundTripsMidFlightMidAuraAndMidQueue()
    {
        var original = BuildCombinedScenario();
        original.Advance(15); // queue mid-build (5 of 20 ticks), aura active, cancel already refunded
        var restored = SimWorld.Restore(original.Snapshot(), Config(21), ModuleRegistry.CreateDefault());
        Assert.Equal(original.StateHash(), restored.StateHash());

        // The aura table is derived state (not serialized): prove the restored
        // world rebuilt it by pushing identical damage through both worlds.
        Assert.Equal(original.AuraArmorBonusBp(3), restored.AuraArmorBonusBp(3));
        foreach (var world in new[] { original, restored })
        {
            world.SubmitCommand(TestWorlds.Command(16, 1, 9, "damage",
                ("id", CommandValue.OfLong(3)), ("amount", CommandValue.OfLong(100))));
        }
        for (var tick = 16; tick <= 100; tick++)
        {
            original.Tick();
            restored.Tick();
            Assert.Equal(original.StateHash(), restored.StateHash());
        }
    }

    [Fact]
    public void SnapshotRoundTripsWithProjectileLiterallyMidFlight()
    {
        var original = NewWorld(33);
        var target = original.SpawnObject("target", 1, At(8, 0));
        var arrow = original.SpawnObject("arrow", 0, FixedVector2.Zero);
        arrow.FindModule<BezierProjectileModule>()!.Launch(original, arrow, target.Id);
        original.Advance(3); // 3 of 8 flight ticks flown

        var restored = SimWorld.Restore(original.Snapshot(), Config(33), ModuleRegistry.CreateDefault());
        Assert.Equal(original.StateHash(), restored.StateHash());
        for (var tick = 0; tick < 10; tick++)
        {
            original.Tick();
            restored.Tick();
            Assert.Equal(original.StateHash(), restored.StateHash());
        }
        Assert.Equal(180, target.FindModule<ActiveBodyModule>()!.Health);
        Assert.Single(restored.Objects);
    }

    [Fact]
    public void CombinedScenarioTwinRunsStayHashIdenticalFor1000Ticks()
    {
        var a = BuildCombinedScenario();
        var b = BuildCombinedScenario();
        for (var tick = 1; tick <= 1000; tick++)
        {
            a.Tick();
            b.Tick();
            Assert.Equal(a.StateHash(), b.StateHash());
        }
        // Sanity: production ran (one soldier built, one cancelled+refunded),
        // the horde died at tick 400, the aura outlived the run.
        Assert.Contains(a.Objects.Values, o => o.TemplateName == "soldier");
        Assert.DoesNotContain(a.Objects.Values, o => o.TemplateName == "horde");
        Assert.Contains(a.Objects.Values, o => o.TemplateName == "banner");
    }

    [Fact]
    public void NewModuleTypesAreRegistered()
    {
        var registry = ModuleRegistry.CreateDefault();
        foreach (var typeName in new[]
        {
            HordeContainModule.TypeName, BezierProjectileModule.TypeName, AttributeModifierAuraModule.TypeName,
        })
        {
            Assert.True(registry.TryCreate(new ModuleSpec(typeName, null), out _), $"{typeName} not registered");
        }
    }
}
