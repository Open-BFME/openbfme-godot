namespace OpenBfme.Sim;

[SageModule("SpecialPowerModule", ModuleTier.Cosmetic)]
public sealed class GenericSpecialPowerModule : SpecialPowerEffectModuleBase
{
    public const string TypeName = "SpecialPowerModule";
    public GenericSpecialPowerModule(ModuleSpec spec) : base(spec) { }

    internal override void Cast(
        SimWorld world,
        GameObject caster,
        int targetId,
        FixedVector2 targetPosition)
    {
        var handled = false;
        foreach (var heal in caster.Modules.OfType<AutoHealBehaviorModule>())
            handled |= heal.TriggerButton(world);
        if (!handled) world.RecordTechGap(TypeName);
    }
}
