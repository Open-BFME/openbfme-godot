using Xunit;

namespace OpenBfme.Sim.Tests;

public sealed class ModuleBatchCCombatUtilityTests
{
    [Fact]
    public void FireWeaponUpdateExecutesAuthoredNuggetThroughCombatDamage()
    {
        var block = new BundleBlock("FireWeaponNugget", "",
            new Dictionary<string, BundleValue>
            {
                ["FireDelay"] = BundleValue.Whole(33),
                ["OneShot"] = BundleValue.Flag(true),
                ["WeaponName"] = BundleValue.Text("PulseWeapon"),
            }, Array.Empty<BundleBlock>());
        // Empty is the exact 33-row top-level corpus shape.
        var fire = new ModuleSpec(FireWeaponUpdateModule.TypeName, blocks: new[] { block });
        var attacker = new ObjectTemplate("attacker", new[] { fire }, bodyHealth: Body(100));
        var target = new ObjectTemplate("target", Array.Empty<ModuleSpec>(), bodyHealth: Body(100));
        var weapon = new WeaponTemplate("PulseWeapon", Fixed64.FromInt(20), Fixed64.Zero,
            1, 0, PreAttackType.PER_SHOT, 0, 0, 0,
            new[] { new DamageNugget(Fixed64.FromInt(25), Fixed64.Zero, 0,
                DamageType.DEFAULT, "", "NORMAL") });
        var world = World(new[] { attacker, target }, new[] { weapon });
        var source = world.SpawnObject("attacker", 0, At(0));
        var victim = world.SpawnObject("target", 1, At(5));

        world.Tick();

        Assert.Equal(Fixed64.FromInt(75), victim.Health);
        Assert.Equal(1, source.FindModule<FireWeaponUpdateModule>()!.FiredCount);
    }

    [Fact]
    public void NotifyCrushingMarksTheCurrentCombatTarget()
    {
        var notify = new ModuleSpec(NotifyTargetsOfImminentProbableCrushingUpdateModule.TypeName);
        var attackerTemplate = new ObjectTemplate("crusher", new[] { notify }, bodyHealth: Body(100));
        var targetTemplate = new ObjectTemplate("target", Array.Empty<ModuleSpec>(), bodyHealth: Body(100));
        var world = World(new[] { attackerTemplate, targetTemplate });
        var attacker = world.SpawnObject("crusher", 0, At(0));
        var target = world.SpawnObject("target", 1, At(2));
        attacker.Combat!.EngagedTargetId = target.Id;

        world.Tick();

        Assert.True(target.HasConditionToken("IMMINENT_CRUSH"));
        Assert.Equal(target.Id,
            attacker.FindModule<NotifyTargetsOfImminentProbableCrushingUpdateModule>()!.NotifiedTargetId);
    }

    [Fact]
    public void DeletionUpdateUsesExactAuthoredLifetimeAndTwinRunState()
    {
        var module = new ModuleSpec(DeletionUpdateModule.TypeName,
            new Dictionary<string, long> { ["MinLifetime"] = 66, ["MaxLifetime"] = 66 });
        var template = new ObjectTemplate("temporary", new[] { module });
        var first = World(new[] { template });
        var second = World(new[] { template });
        first.SpawnObject("temporary", 0, At(0));
        second.SpawnObject("temporary", 0, At(0));

        first.Tick();
        second.Tick();
        Assert.Equal(first.StateHash(), second.StateHash());
        Assert.Single(first.Objects);
        first.Tick();
        second.Tick();
        Assert.Equal(first.StateHash(), second.StateHash());
        Assert.Empty(first.Objects);
    }

    [Fact]
    public void SiegeDockingBehaviorRoundTripsDockedObjectId()
    {
        var module = new ModuleSpec(SiegeDockingBehaviorModule.TypeName);
        var template = new ObjectTemplate("siege", new[] { module }, bodyHealth: Body(100));
        var config = new SimConfig(new[] { template }, 5, 2);
        var world = new SimWorld(config, ModuleRegistry.CreateDefault(), 33);
        var siege = world.SpawnObject("siege", 0, At(0));

        Assert.True(siege.FindModule<SiegeDockingBehaviorModule>()!.Dock(siege, 77));
        var restored = SimWorld.Restore(world.Snapshot(), config, ModuleRegistry.CreateDefault());

        var restoredSiege = Assert.Single(restored.Objects.Values);
        Assert.Equal(77, restoredSiege.FindModule<SiegeDockingBehaviorModule>()!.DockedObjectId);
        Assert.True(restoredSiege.HasConditionToken("DOCKED"));
    }

    [Fact]
    public void GateOpenAndCloseHonorsDefaultPathingThresholdAndResetTime()
    {
        var module = new ModuleSpec(GateOpenAndCloseBehaviorModule.TypeName,
            new Dictionary<string, long> { ["OpenByDefault"] = 0, ["PercentOpenForPathing"] = 50,
                ["RepelCollidingUnits"] = 1, ["ResetTimeInMilliseconds"] = 66,
                ["TimeBeforePlayingClosedSound"] = 0, ["TimeBeforePlayingOpenSound"] = 0 },
            new Dictionary<string, string>
            {
                ["SoundClosingGateLoop"] = "SyntheticCloseLoop",
                ["SoundFinishedClosingGate"] = "SyntheticClosed",
                ["SoundFinishedOpeningGate"] = "SyntheticOpened",
                ["SoundOpeningGateLoop"] = "SyntheticOpenLoop",
            });
        var template = new ObjectTemplate("gate", new[] { module }, bodyHealth: Body(100));
        var world = World(new[] { template });
        var gate = world.SpawnObject("gate", 0, At(0));
        var behavior = gate.FindModule<GateOpenAndCloseBehaviorModule>()!;

        behavior.RequestOpen(world, gate);
        behavior.SetAnimationOpenPercent(gate, 49);
        Assert.False(behavior.IsPathable);
        behavior.SetAnimationOpenPercent(gate, 50);
        Assert.True(behavior.IsPathable);
        Assert.True(behavior.RepelsCollidingUnits);
        world.Advance(2);

        Assert.False(behavior.IsOpen);
        Assert.True(gate.HasConditionToken("GATE_CLOSED"));
    }

    private static SimWorld World(
        IEnumerable<ObjectTemplate> templates,
        IEnumerable<WeaponTemplate>? weapons = null) =>
        new(new SimConfig(templates, 41, 2, weaponTemplates: weapons), ModuleRegistry.CreateDefault(), 33);
    private static BodyHealthTemplate Body(int health) => new(Fixed64.FromInt(health));
    private static FixedVector2 At(int x, int y = 0) => new(Fixed64.FromInt(x), Fixed64.FromInt(y));
}
