namespace OpenBfme.Sim;

[SageModule("WeaponSetUpgrade", ModuleTier.Structural)]
public sealed class WeaponSetUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "WeaponSetUpgrade";
    public WeaponSetUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        var token = FirstToken(Spec, "WeaponSetFlags", "WeaponSetFlag", "Condition");
        if (token.Length > 0) self.SetConditionToken(token);
    }
}
