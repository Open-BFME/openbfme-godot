namespace OpenBfme.Sim;

/// <summary>
/// Minimal straight-line locomotion: moves toward Destination at Speed units per
/// tick. Stands in for the locomotor lane until P2 brings pathing; exists in P0
/// so the determinism gates exercise continuous fixed-point state every tick.
/// </summary>
[SageModule("LinearMover", ModuleTier.Structural)]
public sealed class LinearMoverModule : ModuleBase
{
    public const string TypeName = "LinearMover";

    private readonly Fixed64 _speedPerTick;
    private FixedVector2 _destination;
    private bool _moving;

    public LinearMoverModule(ModuleSpec spec) : base(spec)
    {
        _speedPerTick = spec.GetFixed("SpeedPerTickRaw", Fixed64.FromFraction(1, 10));
    }

    public void SetDestination(FixedVector2 destination)
    {
        _destination = destination;
        _moving = true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_moving)
        {
            return;
        }
        var offset = _destination - self.Position;
        var distanceSquared = offset.LengthSquared();
        if (distanceSquared == Fixed64.Zero)
        {
            // Below Q32.32 squared-length precision the residual offset can be
            // nonzero yet unrepresentable as progress — snap instead of stalling
            // a hair short of the destination.
            self.SetPosition(_destination);
            _moving = false;
            return;
        }
        var distance = Fixed64.Sqrt(distanceSquared);
        if (distance <= _speedPerTick)
        {
            self.SetPosition(_destination);
            _moving = false;
            return;
        }
        var step = offset * (_speedPerTick / distance);
        self.SetPosition(self.Position + step);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteVector(_destination);
        writer.WriteBool(_moving);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _destination = reader.ReadVector();
        _moving = reader.ReadBool();
    }
}
