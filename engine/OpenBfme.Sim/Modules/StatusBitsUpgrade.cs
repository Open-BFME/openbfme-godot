namespace OpenBfme.Sim;

[SageModule("StatusBitsUpgrade", ModuleTier.Cosmetic)]
public sealed class StatusBitsUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "StatusBitsUpgrade";
    public StatusBitsUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self) => world.RecordTechGap(TypeName);
}
