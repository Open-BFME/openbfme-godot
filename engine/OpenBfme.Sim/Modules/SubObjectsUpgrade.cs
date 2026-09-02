namespace OpenBfme.Sim;

[SageModule("SubObjectsUpgrade", ModuleTier.Cosmetic)]
public sealed class SubObjectsUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "SubObjectsUpgrade";
    public SubObjectsUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self) => world.RecordTechGap(TypeName);
}
