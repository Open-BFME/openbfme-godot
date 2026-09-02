namespace OpenBfme.Sim;

/// <summary>
/// Periodic team income in the shape of TerrainResourceBehavior (152 objects in
/// the corpus): every IntervalTicks, credit Amount to the owner's resources.
/// </summary>
[SageModule("ResourceGenerator", ModuleTier.Structural)]
public sealed class ResourceGeneratorModule : ModuleBase
{
    public const string TypeName = "ResourceGenerator";

    private readonly int _intervalTicks;
    private readonly long _amount;
    private int _ticksUntilPayout;

    public ResourceGeneratorModule(ModuleSpec spec) : base(spec)
    {
        _intervalTicks = (int)Math.Max(1, spec.GetLong("IntervalTicks", 30));
        _amount = spec.GetLong("Amount", 10);
        _ticksUntilPayout = _intervalTicks;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying)
        {
            return;
        }
        _ticksUntilPayout--;
        if (_ticksUntilPayout > 0)
        {
            return;
        }
        _ticksUntilPayout = _intervalTicks;
        world.AddTeamResources(self.Team, _amount);
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_ticksUntilPayout);
    public override void ReadState(CanonicalReader reader) => _ticksUntilPayout = reader.ReadInt();
}
