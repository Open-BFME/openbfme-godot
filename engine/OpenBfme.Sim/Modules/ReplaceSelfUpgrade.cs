namespace OpenBfme.Sim;

/// <summary>Triggered replacement preserving owner, transform, and current health fraction.</summary>
[SageModule("ReplaceSelfUpgrade", ModuleTier.Structural)]
public sealed class ReplaceSelfUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "ReplaceSelfUpgrade";
    public ReplaceSelfUpgradeModule(ModuleSpec spec) : base(spec) { }

    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        var template = FirstToken(Spec, "ReplaceWith", "AndThenAddA");
        if (template.Length > 0 && world.TryGetTemplate(template, out _)) world.ReplaceObject(self, template);
    }
}
