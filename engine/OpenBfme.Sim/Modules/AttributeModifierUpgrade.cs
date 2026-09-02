namespace OpenBfme.Sim;

/// <summary>
/// Applies the authored AttributeModifier name as an authoritative condition
/// token when TriggeredBy is satisfied. Numeric modifier contents are not in
/// bundle-v1, so their stat arithmetic is deferred until that table is cooked;
/// the activation itself is deterministic, queryable, and snapshot-backed by
/// CombatState. CustomAnimAndDuration is presentation-only.
/// </summary>
[SageModule("AttributeModifierUpgrade", ModuleTier.Structural)]
public sealed class AttributeModifierUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "AttributeModifierUpgrade";
    public AttributeModifierUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        var modifier = FirstToken(Spec, "AttributeModifier");
        if (modifier.Length > 0) self.SetConditionToken(modifier);
    }
}
