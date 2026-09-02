namespace OpenBfme.Sim;

/// <summary>
/// Applies every authored StatusToSet token through the normal upgrade
/// evaluator. Most retail rows intentionally author no StatusToSet; those rows
/// still consume exactly once and carry no invented status. Custom animation is
/// presentation-only.
/// </summary>
[SageModule("StatusBitsUpgrade", ModuleTier.Structural)]
public sealed class StatusBitsUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "StatusBitsUpgrade";
    public StatusBitsUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        foreach (var status in Tokens(Spec.GetString("StatusToSet", "")))
            self.SetConditionToken(status);
    }
}
