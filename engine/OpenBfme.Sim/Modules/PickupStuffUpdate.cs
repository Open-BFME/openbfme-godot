namespace OpenBfme.Sim;

/// <summary>Periodic authored StuffToPickUp scan; SalvageCrateCollide grants its exact min/max midpoint resources.</summary>
[SageModule("PickupStuffUpdate", ModuleTier.Structural)]
public sealed class PickupStuffUpdateModule : ModuleBase
{
    public const string TypeName = "PickupStuffUpdate";
    private readonly Fixed64 _range;
    private readonly string _filter;
    private readonly long _scanMilliseconds;
    private readonly bool _aiOnly;
    private int _ticksRemaining;

    public PickupStuffUpdateModule(ModuleSpec spec) : base(spec)
    {
        _range = ModuleRuntime.ReadFixed(spec, "ScanRange", Fixed64.Zero);
        _filter = spec.GetString("StuffToPickUp", "NONE +CRATE");
        _scanMilliseconds = Math.Max(1, ModuleRuntime.SecondsFieldToMilliseconds(spec, "ScanIntervalSeconds", 1_000));
        _aiOnly = ModuleRuntime.ReadBool(spec, "SkirmishAIOnly");
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_aiOnly && !world.IsAiTeam(self.Team)) return;
        if (_ticksRemaining > 1)
        {
            _ticksRemaining--;
            return;
        }
        _ticksRemaining = ModuleRuntime.MillisecondsToTicks(_scanMilliseconds, world.TickMilliseconds);
        var limit = _range * _range;
        var pickup = world.Objects.Values.FirstOrDefault(value => value.Id != self.Id
            && !value.IsDead && !value.IsDying
            && ModuleRuntime.MatchesKindOf(value, _filter)
            && self.Position.DistanceSquaredTo(value.Position) <= limit);
        if (pickup == null) return;
        var salvage = pickup.Template.Modules.FirstOrDefault(value =>
            value.TypeName.Equals("SalvageCrateCollide", StringComparison.Ordinal));
        if (salvage != null)
        {
            var minimum = salvage.GetLong("MinResource", salvage.GetLong("ResourceAmount", 0));
            var maximum = salvage.GetLong("MaxResource", minimum);
            world.AddTeamResources(self.Team, minimum + (maximum - minimum) / 2);
        }
        pickup.MarkDead();
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_ticksRemaining);
    public override void ReadState(CanonicalReader reader) => _ticksRemaining = reader.ReadInt();
}
