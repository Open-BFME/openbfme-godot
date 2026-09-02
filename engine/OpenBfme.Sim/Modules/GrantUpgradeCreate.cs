namespace OpenBfme.Sim;

/// <summary>Grants UpgradeToGrant at creation or at authored build completion.</summary>
[SageModule("GrantUpgradeCreate", ModuleTier.Structural)]
public sealed class GrantUpgradeCreateModule : ModuleBase
{
    public const string TypeName = "GrantUpgradeCreate";
    private readonly string _upgrade;
    private readonly bool _onBuildComplete;
    private bool _granted;

    public GrantUpgradeCreateModule(ModuleSpec spec) : base(spec)
    {
        _upgrade = spec.GetString("UpgradeToGrant", "").Split((char[]?)null,
            StringSplitOptions.RemoveEmptyEntries).FirstOrDefault() ?? "";
        _onBuildComplete = ModuleRuntime.ReadBool(spec, "GiveOnBuildComplete");
    }

    public override void OnCreated(SimWorld world, GameObject self, GameObject? creator)
    {
        if (!_onBuildComplete) Grant(world, self);
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_onBuildComplete && !self.IsUnderConstruction) Grant(world, self);
    }

    private void Grant(SimWorld world, GameObject self)
    {
        if (_granted || _upgrade.Length == 0) return;
        _granted = world.GrantUpgrade(self, _upgrade);
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteBool(_granted);
    public override void ReadState(CanonicalReader reader) => _granted = reader.ReadBool();
}
