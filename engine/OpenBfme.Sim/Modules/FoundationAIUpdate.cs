namespace OpenBfme.Sim;

/// <summary>Registers a foundation as build plot zero so the normal economy build command can construct on it.</summary>
[SageModule("FoundationAIUpdate", ModuleTier.Structural)]
public sealed class FoundationAIUpdateModule : ModuleBase
{
    public const string TypeName = "FoundationAIUpdate";
    private readonly int _variation;
    private bool _registered;

    public FoundationAIUpdateModule(ModuleSpec spec) : base(spec) =>
        _variation = checked((int)Math.Max(0, spec.GetLong("BuildVariation", 0)));

    public override void OnCreated(SimWorld world, GameObject self, GameObject? creator)
    {
        world.RegisterFoundation(self);
        _registered = true;
    }

    public bool IsRegistered => _registered;
    public int BuildVariation => _variation;

    internal void ApplyBuildVariation(GameObject structure)
    {
        var condition = _variation switch
        {
            1 => "BUILD_VARIATION_ONE",
            2 => "BUILD_VARIATION_TWO",
            _ => "",
        };
        if (condition.Length > 0) structure.TrySetConditionToken(condition);
    }

    internal void CompleteFoundation(SimWorld world, GameObject self)
    {
        world.UnregisterFoundation(self.Id);
        self.MarkDead();
        _registered = false;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteBool(_registered);
    public override void ReadState(CanonicalReader reader) => _registered = reader.ReadBool();
}
