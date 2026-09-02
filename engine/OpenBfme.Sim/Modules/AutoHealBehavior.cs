namespace OpenBfme.Sim;

/// <summary>Authored delayed pulse healing for self or allied radius/player targets.</summary>
[SageModule("AutoHealBehavior", ModuleTier.Structural)]
public sealed class AutoHealBehaviorModule : ModuleBase
{
    public const string TypeName = "AutoHealBehavior";
    private readonly Fixed64 _amount;
    private readonly long _delayMilliseconds;
    private readonly long _startMilliseconds;
    private readonly Fixed64 _radius;
    private readonly string _kindOf;
    private readonly string _triggeredBy;
    private readonly bool _startsActive;
    private readonly bool _notInCombat;
    private readonly bool _notUnderAttack;
    private readonly bool _wholePlayer;
    private readonly bool _onlyOthers;
    private readonly bool _singleBurst;
    private readonly bool _buttonTriggered;
    private int _ticksRemaining;
    private int _lastDamagedTick = int.MinValue;
    private bool _initialized;
    private bool _burstDone;
    private bool _buttonArmed;

    public AutoHealBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _amount = ModuleRuntime.ReadFixed(spec, "HealingAmount", Fixed64.Zero);
        _delayMilliseconds = Math.Max(1, spec.GetLong("HealingDelay", 1_000));
        _startMilliseconds = Math.Max(0, spec.GetLong("StartHealingDelay", 0));
        _radius = ModuleRuntime.ReadFixed(spec, "Radius", Fixed64.Zero);
        _kindOf = spec.GetString("KindOf", "");
        _triggeredBy = spec.GetString("TriggeredBy", "").Split((char[]?)null,
            StringSplitOptions.RemoveEmptyEntries).FirstOrDefault() ?? "";
        _startsActive = ModuleRuntime.ReadBool(spec, "StartsActive", true);
        _notInCombat = ModuleRuntime.ReadBool(spec, "HealOnlyIfNotInCombat");
        _notUnderAttack = ModuleRuntime.ReadBool(spec, "HealOnlyIfNotUnderAttack");
        _wholePlayer = ModuleRuntime.ReadBool(spec, "AffectsWholePlayer");
        _onlyOthers = ModuleRuntime.ReadBool(spec, "HealOnlyOthers");
        _singleBurst = ModuleRuntime.ReadBool(spec, "SingleBurst");
        _buttonTriggered = ModuleRuntime.ReadBool(spec, "ButtonTriggered");
    }

    public override void OnDamageReceived(SimWorld world, GameObject self, Fixed64 amount, string damageType) =>
        _lastDamagedTick = world.TickIndex;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_initialized)
        {
            _initialized = true;
            _ticksRemaining = ModuleRuntime.MillisecondsToTicks(_startMilliseconds, world.TickMilliseconds);
        }
        if (_burstDone || !IsActive(world, self)) return;
        if (_notInCombat && self.Combat is { EngagedTargetId: not 0 }) return;
        if (_notUnderAttack && _lastDamagedTick != int.MinValue && world.TickIndex - _lastDamagedTick
            < ModuleRuntime.MillisecondsToTicks(_startMilliseconds, world.TickMilliseconds)) return;
        if (_ticksRemaining > 1)
        {
            _ticksRemaining--;
            return;
        }
        HealPulse(world, self);
        _ticksRemaining = ModuleRuntime.MillisecondsToTicks(_delayMilliseconds, world.TickMilliseconds);
        _burstDone = _singleBurst;
        if (_singleBurst && _buttonTriggered) _buttonArmed = false;
    }

    private bool IsActive(SimWorld world, GameObject self) =>
        _buttonArmed
        || (_startsActive && _triggeredBy.Length == 0)
        || (_triggeredBy.Length > 0 && world.ObjectHasUpgrade(self, _triggeredBy));

    internal bool TriggerButton(SimWorld world)
    {
        if (!_buttonTriggered) return false;
        _buttonArmed = true;
        _burstDone = false;
        _initialized = true;
        _ticksRemaining = ModuleRuntime.MillisecondsToTicks(_startMilliseconds, world.TickMilliseconds);
        return true;
    }

    private void HealPulse(SimWorld world, GameObject self)
    {
        if (!_wholePlayer && _radius <= Fixed64.Zero)
        {
            if (!_onlyOthers) world.Heal(self, _amount);
            return;
        }
        var limit = _radius * _radius;
        foreach (var candidate in world.Objects.Values)
        {
            if (candidate.Team != self.Team || candidate.IsDead || candidate.IsDying
                || (_onlyOthers && candidate.Id == self.Id)
                || (_kindOf.Length > 0 && !ModuleRuntime.MatchesKindOf(candidate, _kindOf))) continue;
            if (!_wholePlayer && self.Position.DistanceSquaredTo(candidate.Position) > limit) continue;
            world.Heal(candidate, _amount);
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksRemaining);
        writer.WriteInt(_lastDamagedTick);
        writer.WriteBool(_initialized);
        writer.WriteBool(_burstDone);
        writer.WriteBool(_buttonArmed);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksRemaining = reader.ReadInt();
        _lastDamagedTick = reader.ReadInt();
        _initialized = reader.ReadBool();
        _burstDone = reader.ReadBool();
        _buttonArmed = reader.ReadBool();
    }
}
