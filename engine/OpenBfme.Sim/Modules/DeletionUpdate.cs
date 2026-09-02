namespace OpenBfme.Sim;

/// <summary>
/// Deletes the object after a deterministic inclusive lifetime selected from
/// authored MinLifetime/MaxLifetime milliseconds. As in the supplied SAGE
/// DeletionUpdate.cpp reference, expiry destroys rather than kills: it emits
/// no death event and invokes no OnDeath modules. The PCG draw and remaining
/// tick count are canonical state; zero lifetime deletes on the first update.
/// </summary>
[SageModule("DeletionUpdate", ModuleTier.Structural)]
public sealed class DeletionUpdateModule : ModuleBase
{
    public const string TypeName = "DeletionUpdate";
    private readonly long _minimumMilliseconds;
    private readonly long _maximumMilliseconds;
    private bool _initialized;
    private int _ticksRemaining;

    public DeletionUpdateModule(ModuleSpec spec) : base(spec)
    {
        _minimumMilliseconds = Math.Max(0, spec.GetLong("MinLifetime", 0));
        _maximumMilliseconds = Math.Max(_minimumMilliseconds, spec.GetLong("MaxLifetime", _minimumMilliseconds));
    }

    public int TicksRemaining => _ticksRemaining;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_initialized)
        {
            var span = checked(_maximumMilliseconds - _minimumMilliseconds + 1);
            var offset = span <= 1 ? 0 : world.NextRandomUInt32() % (ulong)span;
            _ticksRemaining = IniValueReader.MillisecondsToTicks(
                checked(_minimumMilliseconds + (long)offset), world.TickMilliseconds);
            _initialized = true;
        }
        if (_ticksRemaining > 0) _ticksRemaining--;
        if (_ticksRemaining == 0) world.DestroyObject(self);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_initialized);
        writer.WriteInt(_ticksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _initialized = reader.ReadBool();
        _ticksRemaining = reader.ReadInt();
    }
}
