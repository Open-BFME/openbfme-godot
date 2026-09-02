using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class CastleMemberBehaviorTests
{
    [Fact]
    public void SpawnedMemberRecordsCastleAndDiesWithIt()
    {
        var castleTemplate = ModuleBatchBTestSupport.Template("castle",
            new[] { ModuleBatchBTestSupport.Spec(CastleBehaviorModule.TypeName) });
        var memberTemplate = ModuleBatchBTestSupport.Template("wall",
            new[] { ModuleBatchBTestSupport.Spec(CastleMemberBehaviorModule.TypeName) });
        var world = ModuleBatchBTestSupport.World(new[] { castleTemplate, memberTemplate });
        var castle = world.SpawnObject("castle", 0, ModuleBatchBTestSupport.At(0));
        var member = world.SpawnObjectFrom("wall", 0, ModuleBatchBTestSupport.At(1), castle);

        Assert.Equal(castle.Id, member.FindModule<CastleMemberBehaviorModule>()!.CastleId);
        castle.MarkDead();
        world.Tick();

        Assert.DoesNotContain(member.Id, world.Objects.Keys);
    }
}
