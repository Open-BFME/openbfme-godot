namespace OpenBfme.Sim;

[SageModule("ModelConditionUpgrade", ModuleTier.Cosmetic)]
public sealed class ModelConditionUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "ModelConditionUpgrade";
    public ModelConditionUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self) => world.RecordTechGap(TypeName);
}
