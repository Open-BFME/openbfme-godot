namespace OpenBfme.Sim;

[SageModule("AttributeModifierUpgrade", ModuleTier.Cosmetic)]
public sealed class AttributeModifierUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "AttributeModifierUpgrade";
    public AttributeModifierUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self) => world.RecordTechGap(TypeName);
}
