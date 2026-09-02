using System.Text.Json;
using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class PresentationSnapshotTests
{
    [Fact]
    public void MovementAndDyingTransitionsPublishSlotsWithoutChangingCanonicalHash()
    {
        var template = new ObjectTemplate("soldier", new[]
        {
            new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long>
            {
                ["MaxHealth"] = 100,
            }),
            new ModuleSpec(LinearMoverModule.TypeName, new Dictionary<string, long>
            {
                ["SpeedPerTickRaw"] = Fixed64.FromInt(1).Raw,
            }),
            new ModuleSpec(SlowDeathModule.TypeName, new Dictionary<string, long>
            {
                ["DeathTicks"] = 60,
            }),
        });
        var first = World(template);
        var second = World(template);
        var unit = first.SpawnObject("soldier", 0, At(0));
        second.SpawnObject("soldier", 0, At(0));
        var initialHash = first.StateHash();
        _ = Snapshot(first);
        Assert.Equal(initialHash, first.StateHash());

        unit.FindModule<LinearMoverModule>()!.SetDestination(At(20));
        second.Objects[1].FindModule<LinearMoverModule>()!.SetDestination(At(20));
        first.Tick();
        second.Tick();
        var moving = Snapshot(first);
        Assert.Equal(SimWorld.PresentationMoving, State(moving) & SimWorld.PresentationMoving);
        Assert.Equal(SimWorld.AnimationMove, Anim(moving));
        Assert.True(Frame(moving) >= 0m);
        Assert.Equal(first.StateHash(), second.StateHash());

        first.DealDamage(unit, 100);
        second.DealDamage(second.Objects[1], 100);
        first.Tick();
        second.Tick();
        var dying = Snapshot(first);
        Assert.Equal(SimWorld.PresentationDying, State(dying) & SimWorld.PresentationDying);
        Assert.Equal(SimWorld.AnimationDie, Anim(dying));
        Assert.Equal(first.StateHash(), second.StateHash());
    }

    [Fact]
    public void IdleWorldHashIsIdenticalBeforeAndAfterSnapshotProjection()
    {
        var template = new ObjectTemplate("idle", Array.Empty<ModuleSpec>());
        var world = World(template);
        world.SpawnObject("idle", 0, At(3));
        world.Advance(20);
        var before = world.StateHash();
        var document = Snapshot(world);
        Assert.Equal(SimWorld.AnimationIdle, Anim(document));
        Assert.Equal(before, world.StateHash());
    }

    [Fact]
    public void FireWindowStartsOneShotThenReturnsToIdle()
    {
        var set = new WeaponSet(null,
            new Dictionary<WeaponSlot, string> { [WeaponSlot.PRIMARY] = "TestSword" });
        var template = new ObjectTemplate(
            "attacker",
            Array.Empty<ModuleSpec>(),
            new[] { set },
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(100)));
        var targetTemplate = new ObjectTemplate(
            "target",
            Array.Empty<ModuleSpec>(),
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(100)));
        var weapon = new WeaponTemplate(
            "TestSword",
            Fixed64.FromInt(3),
            Fixed64.Zero,
            100,
            0,
            PreAttackType.PER_SHOT,
            0,
            0,
            0,
            new[]
            {
                new DamageNugget(Fixed64.One, Fixed64.Zero, 0,
                    DamageType.SLASH, "", "NORMAL"),
            });
        var world = new SimWorld(
            new SimConfig(new[] { template, targetTemplate }, 7, 2,
                weaponTemplates: new[] { weapon }),
            ModuleRegistry.CreateDefault());
        var unit = world.SpawnObject("attacker", 0, At(0));
        world.SpawnObject("target", 1, At(1));
        world.Tick();
        Assert.Contains(world.EventsThisTick,
            value => value.Kind == "fire" && value.Object == unit.Id);

        var attacking = Snapshot(world);
        Assert.Equal(SimWorld.PresentationAttacking,
            State(attacking) & SimWorld.PresentationAttacking);
        Assert.Equal(SimWorld.AnimationAttack, Anim(attacking));
        Assert.Equal(0m, Frame(attacking));

        world.Advance(32);
        var settled = Snapshot(world);
        Assert.Equal(0, State(settled) & SimWorld.PresentationAttacking);
        Assert.Equal(SimWorld.AnimationIdle, Anim(settled));
    }

    private static SimWorld World(ObjectTemplate template) =>
        new(new SimConfig(new[] { template }, 7, 2), ModuleRegistry.CreateDefault());

    private static FixedVector2 At(int x) =>
        new(Fixed64.FromInt(x), Fixed64.Zero);

    private static JsonElement Snapshot(SimWorld world) =>
        JsonDocument.Parse(SnapshotWriter.Write(world)).RootElement.Clone();

    private static JsonElement Objects(JsonElement root) => root.GetProperty("objects");
    private static int State(JsonElement root) => Objects(root).GetProperty("state")[0].GetInt32();
    private static int Anim(JsonElement root) => Objects(root).GetProperty("anim")[0].GetInt32();
    private static decimal Frame(JsonElement root) => Objects(root).GetProperty("anim_frame")[0].GetDecimal();
}
