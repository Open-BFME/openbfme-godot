namespace OpenBfme.Sim;

/// <summary>
/// Unpauses the SpecialPowerModule named by SpecialPowerTemplate when the
/// authored TriggeredBy upgrade expression is satisfied. ObeyRechageOnTrigger
/// is accepted as authored metadata; cooldown ownership remains in SimWorld's
/// single special-power readiness table.
/// </summary>
[SageModule("UnpauseSpecialPowerUpgrade", ModuleTier.Structural)]
public sealed class UnpauseSpecialPowerUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "UnpauseSpecialPowerUpgrade";
    private readonly string _powerName;

    public UnpauseSpecialPowerUpgradeModule(ModuleSpec spec) : base(spec) =>
        _powerName = spec.GetString("SpecialPowerTemplate", "");

    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        foreach (var controller in self.Modules.OfType<GenericSpecialPowerModule>())
            if (controller.Controls(_powerName)) controller.Unpause();
    }
}
