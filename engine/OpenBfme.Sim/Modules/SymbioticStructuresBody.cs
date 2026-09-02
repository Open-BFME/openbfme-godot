namespace OpenBfme.Sim;

/// <summary>
/// Proxy structure body. Damage is forwarded to the lowest-id living structure
/// whose template is named by Symbiote/LinkedTemplate; without that partner the
/// proxy is gated and takes no damage.
/// </summary>
[SageModule("SymbioticStructuresBody", ModuleTier.Structural)]
public sealed class SymbioticStructuresBodyModule : ModuleBase
{
    public const string TypeName = "SymbioticStructuresBody";

    public SymbioticStructuresBodyModule(ModuleSpec spec) : base(spec) { }

    public string Symbiote => Spec.GetString("LinkedTemplate", Spec.GetString("Symbiote", ""));

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (amount < 0) throw new ArgumentOutOfRangeException(nameof(amount));
        var linked = world.Objects.Values.FirstOrDefault(candidate =>
            candidate.Id != self.Id && candidate.Team == self.Team
            && !candidate.IsDead && !candidate.IsDying
            && candidate.TemplateName.Equals(Symbiote, StringComparison.Ordinal));
        if (linked != null) world.DealDamage(linked, amount);
        return true;
    }
}
