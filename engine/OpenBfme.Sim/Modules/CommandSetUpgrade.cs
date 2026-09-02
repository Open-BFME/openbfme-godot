namespace OpenBfme.Sim;

[SageModule("CommandSetUpgrade", ModuleTier.Structural)]
public sealed class CommandSetUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "CommandSetUpgrade";
    public CommandSetUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        var name = FirstToken(Spec, "CommandSet");
        if (name.Length > 0) self.SetCurrentCommandSet(name);
    }
}
