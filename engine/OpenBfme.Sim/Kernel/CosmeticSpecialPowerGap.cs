namespace OpenBfme.Sim;

/// <summary>Runtime placeholder for an authored special-power effect not implemented yet.</summary>
[SageModule("CosmeticSpecialPowerGap", ModuleTier.Cosmetic, kernel: true)]
public sealed class CosmeticSpecialPowerGapModule : SpecialPowerEffectModuleBase
{
    public CosmeticSpecialPowerGapModule(ModuleSpec spec) : base(spec) { }
    internal override void Cast(
        SimWorld world,
        GameObject caster,
        int targetId,
        FixedVector2 targetPosition) => world.RecordTechGap(Spec.TypeName);
}
