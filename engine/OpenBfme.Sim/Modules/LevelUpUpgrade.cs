namespace OpenBfme.Sim;

[SageModule("LevelUpUpgrade", ModuleTier.Structural)]
public sealed class LevelUpUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "LevelUpUpgrade";
    public LevelUpUpgradeModule(ModuleSpec spec) : base(spec) { }
    protected override void ApplyEffect(SimWorld world, GameObject self) =>
        self.FindModule<ExperienceLevelModule>()?.GrantLevels(
            checked((int)Math.Max(1, Spec.GetLong("LevelsToGain", 1))));
}
