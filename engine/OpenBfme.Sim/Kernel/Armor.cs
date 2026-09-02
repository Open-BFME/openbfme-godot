namespace OpenBfme.Sim;

/// <summary>
/// ArmorSet-shaped scaling (SAGE armor.ini semantics): per-damage-type basis
/// points, 10000 = 100% damage taken. Design data keys: "Armor:&lt;type&gt;";
/// unlisted types take "ArmorDefault" (default 10000).
/// </summary>
[SageModule("Armor", ModuleTier.Structural, kernel: true)]
public sealed class ArmorModule : ModuleBase
{
    public const string TypeName = "Armor";

    public ArmorModule(ModuleSpec spec) : base(spec)
    {
    }

    public override long ModifyIncomingDamage(GameObject self, string damageType, long amount)
    {
        var basisPoints = Spec.GetLong("Armor:" + damageType, Spec.GetLong("ArmorDefault", 10_000));
        basisPoints = Math.Clamp(basisPoints, 0, 100_000);
        return amount * basisPoints / 10_000;
    }
}
