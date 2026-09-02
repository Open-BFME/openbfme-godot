namespace OpenBfme.Sim;

/// <summary>Burning carriers periodically ignite deterministic, ascending-id flammable neighbours within SpreadTryRange.</summary>
[SageModule("FireSpreadUpdate", ModuleTier.Structural)]
public sealed class FireSpreadUpdateModule : ModuleBase
{
    public const string TypeName = "FireSpreadUpdate";
    private readonly Fixed64 _radius;
    private readonly long _intervalMilliseconds;
    private int _ticksRemaining;

    public FireSpreadUpdateModule(ModuleSpec spec) : base(spec)
    {
        _radius = ModuleRuntime.ReadFixed(spec, "SpreadTryRange",
            ModuleRuntime.ReadFixed(spec, "Radius", Fixed64.Zero));
        var minimum = Math.Max(0, spec.GetLong("MinSpreadDelay", 1_000));
        var maximum = Math.Max(minimum, spec.GetLong("MaxSpreadDelay", minimum));
        _intervalMilliseconds = minimum + (maximum - minimum) / 2;
    }

    public override void OnCreated(SimWorld world, GameObject self, GameObject? creator) =>
        _ticksRemaining = ModuleRuntime.MillisecondsToTicks(_intervalMilliseconds, world.TickMilliseconds);

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.FindModule<FlammableUpdateModule>() is not { IsBurning: true }) return;
        if (_ticksRemaining > 1)
        {
            _ticksRemaining--;
            return;
        }
        _ticksRemaining = ModuleRuntime.MillisecondsToTicks(_intervalMilliseconds, world.TickMilliseconds);
        var radiusSquared = _radius * _radius;
        foreach (var candidate in world.Objects.Values)
        {
            if (candidate.Id == self.Id || candidate.IsDead || candidate.IsDying
                || self.Position.DistanceSquaredTo(candidate.Position) > radiusSquared) continue;
            candidate.FindModule<FlammableUpdateModule>()?.Ignite(world, candidate);
        }
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_ticksRemaining);
    public override void ReadState(CanonicalReader reader) => _ticksRemaining = reader.ReadInt();
}
