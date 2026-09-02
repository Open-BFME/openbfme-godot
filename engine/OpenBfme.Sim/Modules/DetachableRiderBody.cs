namespace OpenBfme.Sim;

/// <summary>Active body that emits its authored detached-rider template once on death.</summary>
[SageModule("DetachableRiderBody", ModuleTier.Structural)]
public sealed class DetachableRiderBodyModule : ActiveBodyModule
{
    public new const string TypeName = "DetachableRiderBody";
    private bool _detached;

    public DetachableRiderBodyModule(ModuleSpec spec) : base(spec) { }

    public bool HasDetached => _detached;

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (!_detached)
        {
            var rider = Spec.GetString("RiderTemplate",
                Spec.GetString("DetachedRider", Spec.GetString("DetachedRiderTemplate", "")));
            if (rider.Length > 0 && world.TryGetTemplate(rider, out _))
            {
                world.SpawnObject(rider, self.Team, self.Position, self.Elevation, self.HeadingRadians);
                _detached = true;
            }
        }
        return false;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        base.WriteState(writer);
        writer.WriteBool(_detached);
    }

    public override void ReadState(CanonicalReader reader)
    {
        base.ReadState(reader);
        _detached = reader.ReadBool();
    }
}
