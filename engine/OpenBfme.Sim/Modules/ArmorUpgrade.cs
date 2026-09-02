namespace OpenBfme.Sim;

[SageModule("ArmorUpgrade", ModuleTier.Structural)]
public sealed class ArmorUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "ArmorUpgrade";
    public ArmorUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        var token = FirstToken(Spec, "ArmorSetFlag", "ArmorSetFlags", "Condition");
        if (token.Length > 0) self.SetConditionToken(token);
    }
}
