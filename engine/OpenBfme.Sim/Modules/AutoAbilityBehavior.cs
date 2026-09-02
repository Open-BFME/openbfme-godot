namespace OpenBfme.Sim;

/// <summary>
/// Deterministic auto-cast controller for the authored SpecialAbility command
/// button. Query selects enemy/allied candidates, MaxScanRange bounds the scan,
/// AllowSelf controls self-targeting, and IdleTimeSeconds sets the rescan period.
/// It submits the ordinary command-v1 power command for the next tick, so
/// command-set, science, target, and reload validation stay centralized.
/// Complex object-filter predicates and melee-position adjustment are deferred.
/// </summary>
[SageModule("AutoAbilityBehavior", ModuleTier.Structural)]
public sealed class AutoAbilityBehaviorModule : ModuleBase
{
    public const string TypeName = "AutoAbilityBehavior";
    private readonly string _buttonName;
    private readonly string _query;
    private readonly Fixed64 _maximumRange;
    private readonly bool _allowSelf;
    private readonly long _idleMilliseconds;
    private int _ticksUntilScan;

    public AutoAbilityBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _buttonName = spec.GetString("SpecialAbility", "");
        _query = spec.GetString("Query", "ENEMIES");
        _maximumRange = spec.GetFixed("MaxScanRangeRaw", Fixed64.FromInt(200));
        _allowSelf = spec.GetLong("AllowSelf", 0) != 0;
        var idleRaw = spec.GetLong("IdleTimeSecondsRaw", 0);
        _idleMilliseconds = idleRaw == 0 ? 1_000 : Math.Max(1,
            (Fixed64.FromRaw(idleRaw) * Fixed64.FromInt(1_000)).ToIntFloor());
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_buttonName.Length == 0 || self.IsDying || self.IsUnderConstruction) return;
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
        var rangeSquared = _maximumRange * _maximumRange;
        GameObject? target = null;
        foreach (var candidate in world.Objects.Values)
        {
            if (candidate.IsDead || candidate.IsDying || (!_allowSelf && candidate.Id == self.Id)) continue;
            if (candidate.Id != self.Id && ((candidate.Team == self.Team && !allies)
                || (candidate.Team != self.Team && !enemies))) continue;
            if (self.Position.DistanceSquaredTo(candidate.Position) > rangeSquared) continue;
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

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_ticksUntilScan);
    public override void ReadState(CanonicalReader reader) => _ticksUntilScan = reader.ReadInt();
}
