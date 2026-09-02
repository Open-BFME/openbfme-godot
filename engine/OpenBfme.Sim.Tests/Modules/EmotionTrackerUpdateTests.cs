using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class EmotionTrackerUpdateTests
{
    [Fact]
    public void FearScanDistanceMapsTerrorToMovementAndDamageModifiers()
    {
        var actorTemplate = ModuleBatchBTestSupport.Template("actor", new[]
        {
            ModuleBatchBTestSupport.Spec(EmotionTrackerUpdateModule.TypeName,
                new Dictionary<string, long> { ["FearScanDistance"] = 10 },
                new Dictionary<string, string>
                {
                    ["AddEmotion"] = "Terror_Base",
                    ["AfraidOf"] = "NONE +MordorCaveTroll",
                    ["AlwaysAfraidOf"] = "NONE +MordorBalrog",
                }),
            ModuleBatchBTestSupport.Spec(LinearMoverModule.TypeName,
                new Dictionary<string, long> { ["SpeedPerTickRaw"] = Fixed64.One.Raw }),
        });
        var terrorTemplate = ModuleBatchBTestSupport.Template("MordorBalrog", Array.Empty<ModuleSpec>(),
            kindOf: new[] { "MONSTER" });
        var world = ModuleBatchBTestSupport.World(new[] { actorTemplate, terrorTemplate });
        var actor = world.SpawnObject("actor", 0, ModuleBatchBTestSupport.At(0));
        var balrog = world.SpawnObject("MordorBalrog", 1, ModuleBatchBTestSupport.At(2));
        actor.FindModule<LinearMoverModule>()!.SetDestination(ModuleBatchBTestSupport.At(10));

        world.Tick();

        var tracker = actor.FindModule<EmotionTrackerUpdateModule>()!;
        Assert.Equal("TERROR", tracker.Emotion);
        Assert.Equal(Fixed64.FromFraction(1, 2), tracker.MovementSpeedMultiplier);
        Assert.True(actor.Position.X > Fixed64.Zero && actor.Position.X < Fixed64.One);
        world.DealDamage(balrog, 100);
        world.Tick();
        Assert.Equal("", tracker.Emotion);

        var fearTarget = ModuleBatchBTestSupport.Template("MordorCaveTroll", Array.Empty<ModuleSpec>(),
            kindOf: new[] { "MONSTER" });
        var fearWorld = ModuleBatchBTestSupport.World(new[] { actorTemplate, fearTarget });
        var afraidActor = fearWorld.SpawnObject("actor", 0, ModuleBatchBTestSupport.At(0));
        fearWorld.SpawnObject("MordorCaveTroll", 1, ModuleBatchBTestSupport.At(2));
        fearWorld.Tick();

        Assert.Equal("FEAR", afraidActor.FindModule<EmotionTrackerUpdateModule>()!.Emotion);

        var alertSpec = new ModuleSpec(EmotionTrackerUpdateModule.TypeName,
            data: new Dictionary<string, long>
            {
                ["TauntAndPointDistance"] = 300,
                ["TauntAndPointUpdateDelay"] = 1000,
            },
            blocks: new[]
            {
                new BundleBlock("AddEmotion", "OVERRIDE Alert_Base",
                    new Dictionary<string, BundleValue> { ["Duration"] = BundleValue.Whole(9_999_999) },
                    Array.Empty<BundleBlock>()),
            });
        var alertOnly = ModuleBatchBTestSupport.Template("alert-actor", new[]
        {
            alertSpec,
        });
        var alertWorld = ModuleBatchBTestSupport.World(new[] { alertOnly, terrorTemplate });
        var alertActor = alertWorld.SpawnObject("alert-actor", 0, ModuleBatchBTestSupport.At(0));
        var alertTarget = alertWorld.SpawnObject("MordorBalrog", 1, ModuleBatchBTestSupport.At(2));
        alertWorld.Tick();

        var alertTracker = alertActor.FindModule<EmotionTrackerUpdateModule>()!;
        Assert.Equal("ALERT", alertTracker.Emotion);
        alertWorld.DealDamage(alertTarget, 100);
        alertWorld.Tick();
        Assert.Equal("ALERT", alertTracker.Emotion);
    }
}
