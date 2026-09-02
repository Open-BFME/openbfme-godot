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
                    ["AfraidOf"] = "NONE +TERROR",
                }),
            ModuleBatchBTestSupport.Spec(LinearMoverModule.TypeName,
                new Dictionary<string, long> { ["SpeedPerTickRaw"] = Fixed64.One.Raw }),
        });
        var terrorTemplate = ModuleBatchBTestSupport.Template("terror", Array.Empty<ModuleSpec>(), kindOf: new[] { "TERROR" });
        var world = ModuleBatchBTestSupport.World(new[] { actorTemplate, terrorTemplate });
        var actor = world.SpawnObject("actor", 0, ModuleBatchBTestSupport.At(0));
        world.SpawnObject("terror", 1, ModuleBatchBTestSupport.At(2));
        actor.FindModule<LinearMoverModule>()!.SetDestination(ModuleBatchBTestSupport.At(10));

        world.Tick();

        var tracker = actor.FindModule<EmotionTrackerUpdateModule>()!;
        Assert.Equal("TERROR", tracker.Emotion);
        Assert.Equal(Fixed64.FromFraction(1, 2), tracker.MovementSpeedMultiplier);
        Assert.True(actor.Position.X > Fixed64.Zero && actor.Position.X < Fixed64.One);
    }
}
