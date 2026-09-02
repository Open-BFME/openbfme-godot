namespace OpenBfme.Sim;

/// <summary>
/// LifetimeUpdate (198 objects): the object expires after LifetimeTicks,
/// routed through the normal death pipeline so SlowDeath-class modules can
/// still claim the removal.
/// </summary>
[SageModule("Lifetime", ModuleTier.Structural)]
public sealed class LifetimeModule : ModuleBase
{
    public const string TypeName = "Lifetime";

    private int _ticksRemaining;
    private bool _expired;

    public LifetimeModule(ModuleSpec spec) : base(spec)
    {
        _ticksRemaining = (int)Math.Max(1, spec.GetLong("LifetimeTicks", 300));
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_expired || self.IsDying)
        {
            return;
        }
        _ticksRemaining--;
        if (_ticksRemaining <= 0)
        {
            _expired = true;
            world.HandleDeath(self);
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksRemaining);
        writer.WriteBool(_expired);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksRemaining = reader.ReadInt();
        _expired = reader.ReadBool();
    }
}
