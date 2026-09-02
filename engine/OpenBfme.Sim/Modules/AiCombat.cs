namespace OpenBfme.Sim;

/// <summary>
/// AIUpdateInterface-lite (539 objects): acquires the nearest living enemy
/// within VisionRangeRaw (ties broken by lowest id — deterministic), hands it
/// to the weapon, and walks into weapon range via the LinearMover when needed.
/// </summary>
[SageModule("AiCombat", ModuleTier.Structural)]
public sealed class AiCombatModule : ModuleBase
{
    public const string TypeName = "AiCombat";

    private readonly Fixed64 _visionRange;
    private readonly int _scanIntervalTicks;
    private int _ticksUntilScan;

    public AiCombatModule(ModuleSpec spec) : base(spec)
    {
        _visionRange = spec.GetFixed("VisionRangeRaw", Fixed64.FromInt(12));
        _scanIntervalTicks = (int)Math.Max(1, spec.GetLong("ScanIntervalTicks", 5));
        _ticksUntilScan = 1;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying)
        {
            return;
        }
        var weapon = self.FindModule<WeaponModule>();
        if (weapon == null)
        {
            return;
        }
        _ticksUntilScan--;
        if (_ticksUntilScan <= 0)
        {
            _ticksUntilScan = _scanIntervalTicks;
            if (weapon.TargetId == 0)
            {
                weapon.SetTarget(FindNearestEnemyId(world, self));
            }
        }
        if (weapon.TargetId == 0 || !world.Objects.TryGetValue(weapon.TargetId, out var target))
        {
            return;
        }
        var mover = self.FindModule<LinearMoverModule>();
        if (mover == null)
        {
            return;
        }
        var rangeSquared = weapon.Range * weapon.Range;
        if (self.Position.DistanceSquaredTo(target.Position) > rangeSquared)
        {
            mover.SetDestination(target.Position);
        }
    }

    private int FindNearestEnemyId(SimWorld world, GameObject self)
    {
        var bestId = 0;
        var bestDistanceSquared = _visionRange * _visionRange;
        foreach (var candidate in world.Objects.Values)
        {
            if (candidate.Team == self.Team || candidate.IsDead || candidate.IsDying)
            {
                continue;
            }
            var distanceSquared = self.Position.DistanceSquaredTo(candidate.Position);
            // Strict < with ascending-id iteration = lowest id wins ties. Deterministic.
            if (distanceSquared < bestDistanceSquared || (bestId == 0 && distanceSquared == bestDistanceSquared))
            {
                bestId = candidate.Id;
                bestDistanceSquared = distanceSquared;
            }
        }
        return bestId;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_ticksUntilScan);
    public override void ReadState(CanonicalReader reader) => _ticksUntilScan = reader.ReadInt();
}
