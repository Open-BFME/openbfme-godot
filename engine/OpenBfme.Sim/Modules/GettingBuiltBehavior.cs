namespace OpenBfme.Sim;

/// <summary>
/// GettingBuiltBehavior-shaped construction gate (350 objects in the corpus):
/// the object spawns under construction and other economic modules idle until
/// the build completes. P1 scope: time-driven completion; builder escorting
/// arrives with the worker lane.
/// </summary>
[SageModule("GettingBuiltBehavior", ModuleTier.Structural)]
public sealed class GettingBuiltModule : ModuleBase
{
    public const string TypeName = "GettingBuiltBehavior";

    private int _ticksRemaining;
    private bool _started;

    public GettingBuiltModule(ModuleSpec spec) : base(spec)
    {
        _ticksRemaining = (int)Math.Max(1, spec.GetLong("ConstructionTicks", 60));
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_started)
        {
            _started = true;
            self.SetUnderConstruction(true);
        }
        if (!self.IsUnderConstruction)
        {
            return;
        }
        _ticksRemaining--;
        if (_ticksRemaining <= 0)
        {
            self.SetUnderConstruction(false);
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_started);
        writer.WriteInt(_ticksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _started = reader.ReadBool();
        _ticksRemaining = reader.ReadInt();
    }
}
