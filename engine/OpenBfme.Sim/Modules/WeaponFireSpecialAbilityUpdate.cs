namespace OpenBfme.Sim;

/// <summary>Timed hero ability that fires authored SpecialWeapon once through CombatSystem.</summary>
[SageModule("WeaponFireSpecialAbilityUpdate", ModuleTier.Structural)]
public sealed class WeaponFireSpecialAbilityUpdateModule : TimedSpecialAbilityModuleBase
{
    public const string TypeName = "WeaponFireSpecialAbilityUpdate";
    private readonly string _weapon;

    public WeaponFireSpecialAbilityUpdateModule(ModuleSpec spec) : base(spec) =>
        _weapon = spec.GetString("SpecialWeapon", "");

    protected override void FireEffect(
        SimWorld world,
        GameObject caster,
        int targetId,
        FixedVector2 targetPosition,
        string powerName)
    {
        world.RaiseEvent(new SimEvent(
            "ability", caster.Id, targetId == 0 ? null : targetId, Name: powerName));
        if (targetId != 0 && world.Objects.TryGetValue(targetId, out var target))
            world.Combat.FireWeaponOnce(world, caster, target, _weapon);
        else
            world.Combat.FireWeaponAtPosition(world, caster, _weapon, targetPosition);
    }
}
