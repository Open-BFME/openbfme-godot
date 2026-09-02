namespace OpenBfme.Sim;

/// <summary>
/// Active body that replaces its dying object with a fresh, full-health instance
/// at the original anchor after the authored respawn/recovery time.
/// </summary>
[SageModule("RespawnBody", ModuleTier.Structural)]
public sealed class RespawnBodyModule : ActiveBodyModule
{
    public new const string TypeName = "RespawnBody";

    private readonly long _respawnMilliseconds;
    private bool _waiting;
    private int _ticksRemaining;
    private FixedVector2 _anchor;
    private Fixed64 _anchorElevation;
    private Fixed64 _anchorHeading;

    public RespawnBodyModule(ModuleSpec spec) : base(spec)
    {
        _respawnMilliseconds = Math.Max(0,
            spec.GetLong("RespawnTime", spec.GetLong("RecoveryTime", 5_000)));
    }

    public bool IsWaitingToRespawn => _waiting;
    public int TicksRemaining => _ticksRemaining;

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (_waiting) return true;
        if (Spec.Data.ContainsKey("CanRespawn") && Spec.GetLong("CanRespawn", 0) == 0) return false;
        _waiting = true;
        _ticksRemaining = MillisecondsToTicks(_respawnMilliseconds, world.TickMilliseconds);
        _anchor = self.Position;
        _anchorElevation = self.Elevation;
        _anchorHeading = self.HeadingRadians;
        self.MarkDying();
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_waiting) return;
        if (_ticksRemaining > 0) _ticksRemaining--;
        if (_ticksRemaining > 0) return;
        world.SpawnObject(self.TemplateName, self.Team, _anchor, _anchorElevation, _anchorHeading);
        self.MarkDead();
        _waiting = false;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        base.WriteState(writer);
        writer.WriteBool(_waiting);
        writer.WriteInt(_ticksRemaining);
        writer.WriteVector(_anchor);
        writer.WriteFixed(_anchorElevation);
        writer.WriteFixed(_anchorHeading);
    }

    public override void ReadState(CanonicalReader reader)
    {
        base.ReadState(reader);
        _waiting = reader.ReadBool();
        _ticksRemaining = reader.ReadInt();
        _anchor = reader.ReadVector();
        _anchorElevation = reader.ReadFixed();
        _anchorHeading = reader.ReadFixed();
    }

    private static int MillisecondsToTicks(long milliseconds, int tickMilliseconds) =>
        checked((int)Math.Max(1, (milliseconds + tickMilliseconds - 1) / tickMilliseconds));
}
