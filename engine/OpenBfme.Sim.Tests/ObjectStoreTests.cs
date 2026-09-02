using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class ObjectStoreTests
{
    [Fact]
    public void SlotZeroIsReservedAndFreedSlotsReuseInLifoOrder()
    {
        var store = new ObjectStore(2);
        var first = store.Allocate(10);
        var second = store.Allocate(11);
        var third = store.Allocate(12);
        Assert.Equal(new[] { 1, 2, 3 }, new[] { first, second, third });

        store.Free(first);
        store.Free(third);
        Assert.Equal(third, store.Allocate(13));
        Assert.Equal(first, store.Allocate(14));
        Assert.Equal(0, store.Id[0]);
        Assert.DoesNotContain(0, store.LiveSlots());
    }

    [Fact]
    public void DoublingGrowthPreservesEveryParallelArray()
    {
        var store = new ObjectStore(1);
        var slot = store.Allocate(7);
        store.TemplateIndex[slot] = 3;
        store.Owner[slot] = 2;
        store.X[slot] = Fixed64.FromInt(11);
        store.Y[slot] = Fixed64.FromInt(12);
        store.Z[slot] = Fixed64.FromInt(13);
        store.Yaw[slot] = Fixed64.FromFraction(1, 2);
        store.Health[slot] = Fixed64.FromInt(99);
        store.MaxHealth[slot] = Fixed64.FromInt(100);
        store.State[slot] = 4;
        store.Anim[slot] = 5;
        store.AnimFrame[slot] = Fixed64.FromFraction(3, 2);
        store.Flags[slot] = 9;
        for (var id = 8; id < 40; id++)
        {
            store.Allocate(id);
        }

        Assert.True(store.Capacity >= 40);
        Assert.Equal(7, store.Id[slot]);
        Assert.Equal(3, store.TemplateIndex[slot]);
        Assert.Equal(2, store.Owner[slot]);
        Assert.Equal(Fixed64.FromInt(11), store.X[slot]);
        Assert.Equal(Fixed64.FromInt(12), store.Y[slot]);
        Assert.Equal(Fixed64.FromInt(13), store.Z[slot]);
        Assert.Equal(Fixed64.FromFraction(1, 2), store.Yaw[slot]);
        Assert.Equal(Fixed64.FromInt(99), store.Health[slot]);
        Assert.Equal(Fixed64.FromInt(100), store.MaxHealth[slot]);
        Assert.Equal(4, store.State[slot]);
        Assert.Equal(5, store.Anim[slot]);
        Assert.Equal(Fixed64.FromFraction(3, 2), store.AnimFrame[slot]);
        Assert.Equal(9, store.Flags[slot]);
    }
}
