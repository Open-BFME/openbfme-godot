using Xunit;

namespace OpenBfme.Sim.Tests.Modules;

public sealed class CastleMemberBehaviorTests
{
    [Fact]
    public void SpawnedMemberRecordsCastleAndDiesWithIt()
    {
        var castleTemplate = ModuleBatchBTestSupport.Template("castle",
            new[] { ModuleBatchBTestSupport.Spec(CastleBehaviorModule.TypeName) });
        var breachMemberTemplate = ModuleBatchBTestSupport.Template("wall",
            new[]
            {
                ModuleBatchBTestSupport.Spec(CastleMemberBehaviorModule.TypeName,
                    new Dictionary<string, long>
                    {
                        ["CountsForEvaCastleBreached"] = 1,
                        ["StoreUpgradePrice"] = 1,
                    },
                    new Dictionary<string, string>
                    {
                        ["BeingBuiltSound"] = "BuildingBigConstructionLoop",
                    })
            });
        var eventMemberTemplate = ModuleBatchBTestSupport.Template("farm-interface",
            new[]
            {
                ModuleBatchBTestSupport.Spec(CastleMemberBehaviorModule.TypeName,
                    strings: new Dictionary<string, string>
                    {
                        ["CampDestroyedOwnerEvaEvent"] = "EvaFortressOwner",
                        ["CampDestroyedAllyEvaEvent"] = "EvaFortressAlly",
                        ["CampDestroyedAttackerEvaEvent"] = "EvaFortressAttacker",
                    })
            });
        var world = ModuleBatchBTestSupport.World(new[] { castleTemplate, breachMemberTemplate, eventMemberTemplate });
        var castle = world.SpawnObject("castle", 0, ModuleBatchBTestSupport.At(0));
        var member = world.SpawnObjectFrom("wall", 0, ModuleBatchBTestSupport.At(1), castle);
        var eventMember = world.SpawnObjectFrom("farm-interface", 0, ModuleBatchBTestSupport.At(2), castle);

        var module = member.FindModule<CastleMemberBehaviorModule>()!;
        Assert.Equal(castle.Id, module.CastleId);
        Assert.True(module.StoresUpgradePrice);
        Assert.Contains("CASTLE_MEMBER_COUNTS_FOR_EVA_BREACH", member.ConditionTokens);
        Assert.Contains("CASTLE_MEMBER_STORE_UPGRADE_PRICE", member.ConditionTokens);
        Assert.Contains(world.EventsThisTick,
            value => value.Kind == "sound" && value.Name == "BuildingBigConstructionLoop");
        castle.MarkDead();
        world.Tick();

        Assert.Equal(new[] { "EvaFortressOwner", "EvaFortressAlly", "EvaFortressAttacker" },
            world.EventsThisTick.Where(value => value.Kind == "sound").Select(value => value.Name));
        Assert.DoesNotContain(member.Id, world.Objects.Keys);
        Assert.DoesNotContain(eventMember.Id, world.Objects.Keys);
    }
}
