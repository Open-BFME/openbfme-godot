namespace OpenBfme.Sim;

/// <summary>Death presentation notification carrying the authored DeathFX list name.</summary>
[SageModule("FXListDie", ModuleTier.Cosmetic)]
public sealed class FXListDieModule : ModuleBase
{
    public const string TypeName = "FXListDie";
    private readonly string _deathFx;
    private bool _raised;

    public FXListDieModule(ModuleSpec spec) : base(spec) => _deathFx = spec.GetString("DeathFX", "");

    public override void OnDeathStarted(SimWorld world, GameObject self)
    {
        if (_raised || _deathFx.Length == 0) return;
        world.RaiseEvent(new SimEvent("death", self.Id, Name: _deathFx));
        _raised = true;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteBool(_raised);
    public override void ReadState(CanonicalReader reader) => _raised = reader.ReadBool();
}
