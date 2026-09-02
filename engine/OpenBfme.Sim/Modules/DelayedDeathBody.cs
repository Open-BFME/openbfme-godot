namespace OpenBfme.Sim;

/// <summary>Active body that holds the dying object, then resumes the normal death chain.</summary>
[SageModule("DelayedDeathBody", ModuleTier.Structural)]
public sealed class DelayedDeathBodyModule : ActiveBodyModule
{
    public new const string TypeName = "DelayedDeathBody";

    private readonly long _delayMilliseconds;
    private bool _dying;
    private int _ticksRemaining;

    public DelayedDeathBodyModule(ModuleSpec spec) : base(spec)
    {
        _delayMilliseconds = Math.Max(0,
            spec.GetLong("DelayTime", spec.GetLong("DelayedDeathTime", 0)));
    }

    public bool IsDying => _dying;
    public int TicksRemaining => _ticksRemaining;

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (_dying) return true;
        _dying = true;
        _ticksRemaining = checked((int)Math.Max(
            1, (_delayMilliseconds + world.TickMilliseconds - 1) / world.TickMilliseconds));
        self.MarkDying();
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_dying) return;
        _ticksRemaining--;
        if (_ticksRemaining <= 0) world.CompleteClaimedDeath(self, this);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        base.WriteState(writer);
        writer.WriteBool(_dying);
        writer.WriteInt(_ticksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        base.ReadState(reader);
        _dying = reader.ReadBool();
        _ticksRemaining = reader.ReadInt();
    }
}
