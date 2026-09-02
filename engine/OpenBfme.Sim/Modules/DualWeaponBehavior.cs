namespace OpenBfme.Sim;

/// <summary>Selects the secondary weapon inside SwitchWeaponOnCloseRangeDistance; otherwise retains the weapon-set primary.</summary>
[SageModule("DualWeaponBehavior", ModuleTier.Structural)]
public sealed class DualWeaponBehaviorModule : ModuleBase
{
    public const string TypeName = "DualWeaponBehavior";
    private readonly Fixed64 _closeRange;

    public DualWeaponBehaviorModule(ModuleSpec spec) : base(spec) =>
        _closeRange = ModuleRuntime.ReadFixed(spec, "SwitchWeaponOnCloseRangeDistance", Fixed64.Zero);

    public bool UseSecondary(GameObject self, GameObject target) =>
        _closeRange > Fixed64.Zero
        && self.Position.DistanceSquaredTo(target.Position) <= _closeRange * _closeRange;
}
