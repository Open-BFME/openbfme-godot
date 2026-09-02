namespace OpenBfme.Sim;

/// <summary>
/// SAGE HighlanderBody is an ActiveBody that ordinary damage cannot reduce
/// below one health. UNRESISTABLE damage and deliberate death handling remain
/// able to finish the object.
/// </summary>
[SageModule("HighlanderBody", ModuleTier.Structural)]
public sealed class HighlanderBodyModule : ActiveBodyModule
{
    public new const string TypeName = "HighlanderBody";

    public HighlanderBodyModule(ModuleSpec spec) : base(spec)
    {
    }

    public override long ModifyIncomingDamage(GameObject self, string damageType, long amount)
    {
        if (amount < 0)
            throw new ArgumentOutOfRangeException(nameof(amount), "Damage must be non-negative");
        if (damageType.Equals(DamageTypes.Unresistable, StringComparison.OrdinalIgnoreCase))
            return amount;
        return Health <= 1 ? 0 : Math.Min(amount, Health - 1);
    }

    internal static Fixed64 ClampIncomingDamage(
        Fixed64 health,
        Fixed64 amount,
        DamageType damageType)
    {
        if (damageType == DamageType.UNRESISTABLE) return amount;
        return health <= Fixed64.One
            ? Fixed64.Zero
            : Fixed64.Min(amount, health - Fixed64.One);
    }
}
