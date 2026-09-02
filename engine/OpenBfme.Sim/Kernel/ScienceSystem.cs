namespace OpenBfme.Sim;

/// <summary>Executable effect seam for a cooked SpecialPower definition.</summary>
public abstract class SpecialPowerEffectModuleBase : ModuleBase
{
    private readonly string _powerName;

    protected SpecialPowerEffectModuleBase(ModuleSpec spec) : base(spec) =>
        _powerName = spec.GetString("SpecialPowerTemplate", spec.GetString("SpecialPower", ""));

    internal bool Matches(SpecialPowerTemplate power) =>
        _powerName.Length == 0
        || _powerName.Equals(power.Name, StringComparison.Ordinal)
        || _powerName.Equals(power.Enum, StringComparison.Ordinal);

    internal virtual bool CanCast(SimWorld world, GameObject caster, SpecialPowerTemplate power) => true;

    internal abstract void Cast(
        SimWorld world,
        GameObject caster,
        int targetId,
        FixedVector2 targetPosition);
}
