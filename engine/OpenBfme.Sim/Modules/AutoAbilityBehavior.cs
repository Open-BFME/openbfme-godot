namespace OpenBfme.Sim;

/// <summary>
/// Deterministic auto-cast controller for the authored SpecialAbility command
/// button. Query selects enemy/allied candidates, MinScanRange/MaxScanRange
/// bound the scan, BaseMaxRangeFromStartPos keeps it near the spawn anchor,
/// ForbiddenStatus suppresses casting, StartsActive initializes the serialized
/// activation latch, AllowSelf controls self-targeting, and IdleTimeSeconds sets
/// the rescan period.
/// It submits the ordinary command-v1 power command for the next tick, so
/// command-set, science, target, and reload validation stay centralized.
/// Complex KindOf predicates and AdjustAttackMeleePosition are deferred.
/// </summary>
[SageModule("AutoAbilityBehavior", ModuleTier.Structural)]
public sealed class AutoAbilityBehaviorModule : ModuleBase
{
    public const string TypeName = "AutoAbilityBehavior";
    private readonly string _buttonName;
    private readonly string _query;
    private readonly string[] _forbiddenStatuses;
    private readonly Fixed64 _minimumRange;
    private readonly Fixed64 _maximumRange;
    private readonly Fixed64 _maximumRangeFromStart;
    private readonly bool _allowSelf;
    private readonly long _idleMilliseconds;
    private int _ticksUntilScan;
    private bool _active;
    private bool _hasStartPosition;
    private FixedVector2 _startPosition;

    public AutoAbilityBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _buttonName = spec.GetString("SpecialAbility", "");
        _query = spec.GetString("Query", "ENEMIES");
        _forbiddenStatuses = spec.GetString("ForbiddenStatus", "")
            .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        _minimumRange = spec.GetFixed("MinScanRangeRaw", Fixed64.Zero);
        _maximumRange = spec.GetFixed("MaxScanRangeRaw", Fixed64.FromInt(200));
        _maximumRangeFromStart = spec.GetFixed("BaseMaxRangeFromStartPosRaw", Fixed64.Zero);
        _allowSelf = spec.GetLong("AllowSelf", 0) != 0;
        _active = spec.GetLong("StartsActive", 1) != 0;
        var idleRaw = spec.GetLong("IdleTimeSecondsRaw", 0);
        _idleMilliseconds = idleRaw == 0 ? 1_000 : Math.Max(1,
            (Fixed64.FromRaw(idleRaw) * Fixed64.FromInt(1_000)).ToIntFloor());
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_hasStartPosition)
        {
            _startPosition = self.Position;
            _hasStartPosition = true;
        }
        if (!_active || _buttonName.Length == 0 || self.IsDying || self.IsUnderConstruction
            || _forbiddenStatuses.Any(self.HasConditionToken)) return;
        if (_ticksUntilScan > 0)
        {
            _ticksUntilScan--;
            if (_ticksUntilScan > 0) return;
        }
        _ticksUntilScan = IniValueReader.MillisecondsToTicks(_idleMilliseconds, world.TickMilliseconds);
        if (!world.AiConfig.Tech.CommandButtons.TryGetValue(_buttonName, out var button)
            || button.SpecialPower.Length == 0
            || world.PowerReadyTick(self.Team, button.SpecialPower) > world.TickIndex) return;
        var allies = _query.Contains("ALLIES", StringComparison.OrdinalIgnoreCase);
        var enemies = _query.Contains("ENEMIES", StringComparison.OrdinalIgnoreCase) || !allies;
        var minimumRangeSquared = _minimumRange * _minimumRange;
        var maximumRangeSquared = _maximumRange * _maximumRange;
        var anchorRangeSquared = _maximumRangeFromStart * _maximumRangeFromStart;
        GameObject? target = null;
        foreach (var candidate in world.Objects.Values)
        {
            if (candidate.IsDead || candidate.IsDying || (!_allowSelf && candidate.Id == self.Id)) continue;
            if (candidate.Id != self.Id && ((candidate.Team == self.Team && !allies)
                || (candidate.Team != self.Team && !enemies))) continue;
            var distanceSquared = self.Position.DistanceSquaredTo(candidate.Position);
            if (distanceSquared < minimumRangeSquared || distanceSquared > maximumRangeSquared) continue;
            if (_maximumRangeFromStart > Fixed64.Zero
                && _startPosition.DistanceSquaredTo(candidate.Position) > anchorRangeSquared) continue;
            target = candidate;
            break;
        }
        if (target == null && _allowSelf) target = self;
        if (target == null) return;
        world.SubmitCommand(new SimCommand(
            checked(world.TickIndex + 1), self.Team, checked(1_000_000 + self.Id), "power",
            new Dictionary<string, CommandValue>
            {
                ["objects"] = CommandValue.OfLongList(new long[] { self.Id }),
                ["name"] = CommandValue.OfString(button.SpecialPower),
                ["target"] = CommandValue.OfLong(target.Id),
            }));
    }

    public void SetActive(bool active) => _active = active;

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksUntilScan);
        writer.WriteBool(_active);
        writer.WriteBool(_hasStartPosition);
        writer.WriteVector(_startPosition);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksUntilScan = reader.ReadInt();
        _active = reader.ReadBool();
        _hasStartPosition = reader.ReadBool();
        _startPosition = reader.ReadVector();
    }
}
