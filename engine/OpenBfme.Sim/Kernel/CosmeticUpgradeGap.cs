namespace OpenBfme.Sim;

/// <summary>Runtime placeholder for an authored upgrade effect not implemented yet.</summary>
[SageModule("CosmeticUpgradeGap", ModuleTier.Cosmetic, kernel: true)]
public sealed class CosmeticUpgradeGapModule : UpgradeTriggeredModuleBase
{
    public CosmeticUpgradeGapModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self) => world.RecordTechGap(Spec.TypeName);
}
