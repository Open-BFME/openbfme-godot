namespace OpenBfme.Sim;

/// <summary>
/// Generic hero ability timeline. UnpackTime, PreparationTime and
/// PersistentPrepTime precede each nested effect; PackTime keeps the carrier
/// busy afterwards. StartAbilityRange is checked in exact fixed-point space.
/// </summary>
[SageModule("SpecialAbilityUpdate", ModuleTier.Structural)]
public sealed class SpecialAbilityUpdateModule : TimedSpecialAbilityModuleBase
{
    public const string TypeName = "SpecialAbilityUpdate";

    public SpecialAbilityUpdateModule(ModuleSpec spec) : base(spec) { }

    protected override void FireEffect(
        SimWorld world,
        GameObject caster,
        int targetId,
        FixedVector2 targetPosition,
        string powerName)
    {
        world.RaiseEvent(new SimEvent(
            "ability", caster.Id, targetId == 0 ? null : targetId, Name: powerName));
        world.ExecuteNestedPowerEffects(caster, this, powerName, targetId, targetPosition);
    }
}
