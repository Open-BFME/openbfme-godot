namespace OpenBfme.Sim;

/// <summary>Registers a foundation as build plot zero so the normal economy build command can construct on it.</summary>
[SageModule("FoundationAIUpdate", ModuleTier.Structural)]
public sealed class FoundationAIUpdateModule : ModuleBase
{
    public const string TypeName = "FoundationAIUpdate";
    private readonly string _variation;
    private bool _registered;

    public FoundationAIUpdateModule(ModuleSpec spec) : base(spec) =>
        _variation = spec.GetString("BuildVariation", "");

    public override void OnCreated(SimWorld world, GameObject self, GameObject? creator)
    {
        world.RegisterFoundation(self, _variation);
        _registered = true;
    }

    public bool IsRegistered => _registered;

    internal void CompleteFoundation(SimWorld world, GameObject self)
    {
        world.UnregisterFoundation(self.Id);
        self.MarkDead();
        _registered = false;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteBool(_registered);
    public override void ReadState(CanonicalReader reader) => _registered = reader.ReadBool();
}
