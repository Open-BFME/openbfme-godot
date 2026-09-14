using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class LargeGroupAudioUpdateTests
{
    private static ModuleSpec Spec(string key, long weight = 1) => new(
        LargeGroupAudioUpdateModule.TypeName,
        new Dictionary<string, long> { ["UnitWeight"] = weight },
        new Dictionary<string, string> { ["Key"] = key });

    [Fact]
    public void RegisteredAsCosmeticAndParsesListJoinedKeyAndUnitWeight()
    {
        var registry = ModuleRegistry.CreateDefault();
        Assert.True(registry.TryGetTier(LargeGroupAudioUpdateModule.TypeName, out var tier));
        Assert.Equal(ModuleTier.Cosmetic, tier);
        Assert.True(registry.TryCreate(Spec("HUMAN\nUNIT\nINFANTRY", 3), out var module));
        var audio = Assert.IsType<LargeGroupAudioUpdateModule>(module);
        Assert.Equal("HUMAN UNIT INFANTRY", audio.CategoryKey);
        Assert.Equal(3, audio.UnitWeight);
    }

    [Fact]
    public void SingleTokenKeyParsesAndUnitWeightDefaultsToOne()
    {
        var spec = new ModuleSpec(
            LargeGroupAudioUpdateModule.TypeName,
            stringData: new Dictionary<string, string> { ["Key"] = "ORC MONSTER" });
        var audio = new LargeGroupAudioUpdateModule(spec);
        Assert.Equal("ORC MONSTER", audio.CategoryKey);
        Assert.Equal(1, audio.UnitWeight);
        Assert.Equal(1, new LargeGroupAudioUpdateModule(Spec("x", 0)).UnitWeight);
    }

    private static SimWorld WorldWith() => new(new SimConfig(new[]
    {
        new ObjectTemplate("voice-a", new[] { Spec("HUMAN UNIT INFANTRY", 3) }),
        new ObjectTemplate("voice-b", new[] { Spec("HUMAN UNIT INFANTRY", 1) }),
        new ObjectTemplate("voice-orc", new[] { Spec("ORC MONSTER", 9) }),
        new ObjectTemplate("silent", new ModuleSpec[] { new(InactiveBodyModule.TypeName) }),
    }, 17, 2), ModuleRegistry.CreateDefault());

    [Fact]
    public void DominantCategoryIsHighestSummedWeightAndWeightedPickSelectsMember()
    {
        var world = WorldWith();
        var first = world.SpawnObject("voice-a", 0, FixedVector2.Zero);
        var second = world.SpawnObject("voice-b", 0, FixedVector2.Zero);
        var third = world.SpawnObject("voice-orc", 0, FixedVector2.Zero);

        var pick = LargeGroupAudioUpdateModule.SelectWeightedSpeaker(
            world, new[] { first.Id, second.Id, third.Id }, 0);

        Assert.NotNull(pick);
        Assert.Equal("ORC MONSTER", pick!.Value.CategoryKey);
        Assert.Equal(9, pick.Value.GroupWeight);
        Assert.Equal(third.Id, pick.Value.ObjectId);
    }

    [Fact]
    public void EqualGroupWeightsBreakToOrdinalFirstKey()
    {
        var world = new SimWorld(new SimConfig(new[]
        {
            new ObjectTemplate("beta", new[] { Spec("BETA", 1) }),
            new ObjectTemplate("alpha", new[] { Spec("ALPHA", 1) }),
        }, 17, 2), ModuleRegistry.CreateDefault());
        var beta = world.SpawnObject("beta", 0, FixedVector2.Zero);
        var alpha = world.SpawnObject("alpha", 0, FixedVector2.Zero);

        var pick = LargeGroupAudioUpdateModule.SelectWeightedSpeaker(
            world, new[] { beta.Id, alpha.Id }, 0);

        Assert.Equal("ALPHA", pick!.Value.CategoryKey);
        Assert.Equal(alpha.Id, pick.Value.ObjectId);
    }

    [Fact]
    public void WeightedPickWalksMembersInAscendingIdOrderAndWraps()
    {
        var world = new SimWorld(new SimConfig(new[]
        {
            new ObjectTemplate("w3", new[] { Spec("G", 3) }),
            new ObjectTemplate("w2", new[] { Spec("G", 2) }),
            new ObjectTemplate("w1", new[] { Spec("G", 1) }),
        }, 17, 2), ModuleRegistry.CreateDefault());
        var heavy = world.SpawnObject("w3", 0, FixedVector2.Zero);
        var mid = world.SpawnObject("w2", 0, FixedVector2.Zero);
        var light = world.SpawnObject("w1", 0, FixedVector2.Zero);
        var ids = new[] { light.Id, heavy.Id, mid.Id };

        Assert.Equal(heavy.Id, LargeGroupAudioUpdateModule
            .SelectWeightedSpeaker(world, ids, 0)!.Value.ObjectId);
        Assert.Equal(heavy.Id, LargeGroupAudioUpdateModule
            .SelectWeightedSpeaker(world, ids, 2)!.Value.ObjectId);
        Assert.Equal(mid.Id, LargeGroupAudioUpdateModule
            .SelectWeightedSpeaker(world, ids, 3)!.Value.ObjectId);
        Assert.Equal(light.Id, LargeGroupAudioUpdateModule
            .SelectWeightedSpeaker(world, ids, 5)!.Value.ObjectId);
        Assert.Equal(heavy.Id, LargeGroupAudioUpdateModule
            .SelectWeightedSpeaker(world, ids, 6)!.Value.ObjectId);
    }

    [Fact]
    public void DeadSilentAndMissingObjectsAreSkippedAndEmptyReturnsNull()
    {
        var world = WorldWith();
        var dying = world.SpawnObject("voice-orc", 0, FixedVector2.Zero);
        var silent = world.SpawnObject("silent", 0, FixedVector2.Zero);
        var live = world.SpawnObject("voice-b", 0, FixedVector2.Zero);
        world.HandleDeath(dying);

        var pick = LargeGroupAudioUpdateModule.SelectWeightedSpeaker(
            world, new[] { dying.Id, silent.Id, live.Id, 9999 }, 0);

        Assert.Equal(live.Id, pick!.Value.ObjectId);
        Assert.Null(LargeGroupAudioUpdateModule.SelectWeightedSpeaker(
            world, new[] { silent.Id, dying.Id }, 0));
        Assert.Null(LargeGroupAudioUpdateModule.SelectWeightedSpeaker(
            world, Array.Empty<int>(), 0));
    }

    [Fact]
    public void SelectionIsAReadOnlyProjectionThatCannotMoveTheStateHash()
    {
        SimWorld Build()
        {
            var world = WorldWith();
            world.SpawnObject("voice-a", 0, FixedVector2.Zero);
            world.SpawnObject("voice-orc", 0, FixedVector2.Zero);
            world.Tick();
            return world;
        }

        var first = Build();
        var second = Build();
        var before = first.StateHash();
        var a = LargeGroupAudioUpdateModule.SelectWeightedSpeaker(
            first, first.Objects.Keys, 7);
        var b = LargeGroupAudioUpdateModule.SelectWeightedSpeaker(
            second, second.Objects.Keys, 7);

        Assert.Equal(a, b);
        Assert.Equal(before, first.StateHash());
        Assert.Equal(first.StateHash(), second.StateHash());
    }
}
