using Xunit;

namespace OpenBfme.Sim.Tests;

/// <summary>
/// Canonical-state half of the core-module oracle. The behavior tests use
/// authored corpus shapes and assert their SAGE-visible golden effects; every
/// selected type is also independently twin-run and snapshot/reload checked
/// here so a state field cannot silently escape WriteState/ReadState.
/// </summary>
public sealed class ModuleBatchCDeterminismTests
{
    [Theory]
    [InlineData(GenericSpecialPowerModule.TypeName)]
    [InlineData(UnpauseSpecialPowerUpgradeModule.TypeName)]
    [InlineData(AttributeModifierUpgradeModule.TypeName)]
    [InlineData(StatusBitsUpgradeModule.TypeName)]
    [InlineData(ModelConditionUpgradeModule.TypeName)]
    [InlineData(AutoAbilityBehaviorModule.TypeName)]
    [InlineData(OCLSpecialPowerModule.TypeName)]
    [InlineData(RespawnUpdateModule.TypeName)]
    [InlineData(InvisibilityUpdateModule.TypeName)]
    [InlineData(GiveUpgradeUpdateModule.TypeName)]
    [InlineData(FireWeaponUpdateModule.TypeName)]
    [InlineData(NotifyTargetsOfImminentProbableCrushingUpdateModule.TypeName)]
    [InlineData(DeletionUpdateModule.TypeName)]
    [InlineData(SiegeDockingBehaviorModule.TypeName)]
    [InlineData(GateOpenAndCloseBehaviorModule.TypeName)]
    [InlineData(RefundDieModule.TypeName)]
    [InlineData(SlavedUpdateModule.TypeName)]
    [InlineData(ToggleMountedSpecialAbilityUpdateModule.TypeName)]
    [InlineData(CitadelSlaughterHordeContainModule.TypeName)]
    [InlineData(CastleUpgradeModule.TypeName)]
    public void EveryBatchCTypeHasCanonicalSnapshotAndTwinRunState(string typeName)
    {
        var module = new ModuleSpec(typeName);
        var template = new ObjectTemplate("oracle", new[] { module },
            bodyHealth: new BodyHealthTemplate(Fixed64.FromInt(100)), techEnabled: true);
        var config = new SimConfig(new[] { template }, 0xC0DEC, 2);
        var first = new SimWorld(config, ModuleRegistry.CreateDefault(), 33);
        var second = new SimWorld(config, ModuleRegistry.CreateDefault(), 33);
        first.SpawnObject("oracle", 0, FixedVector2.Zero);
        second.SpawnObject("oracle", 0, FixedVector2.Zero);

        for (var tick = 0; tick < 3; tick++)
        {
            Assert.Equal(first.StateHash(), second.StateHash());
            first.Tick();
            second.Tick();
        }

        Assert.Equal(first.StateHash(), second.StateHash());
        var snapshot = first.Snapshot();
        var restored = SimWorld.Restore(snapshot, config, ModuleRegistry.CreateDefault());
        Assert.Equal(snapshot, restored.Snapshot());
        Assert.Equal(first.StateHash(), restored.StateHash());
        first.Tick();
        restored.Tick();
        Assert.Equal(first.StateHash(), restored.StateHash());
    }
}
