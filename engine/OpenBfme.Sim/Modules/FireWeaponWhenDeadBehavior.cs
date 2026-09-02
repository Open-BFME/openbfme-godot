namespace OpenBfme.Sim;

/// <summary>Fires DeathWeapon at the carrier transform after authored DelayTime through CombatSystem.</summary>
[SageModule("FireWeaponWhenDeadBehavior", ModuleTier.Structural)]
public sealed class FireWeaponWhenDeadBehaviorModule : ModuleBase
{
    public const string TypeName = "FireWeaponWhenDeadBehavior";
    private readonly string _weapon;
    private readonly long _delayMilliseconds;
    private readonly bool _startsActive;
    private readonly string _requiredStatus;
    private readonly string _exemptStatus;
    private int _ticksRemaining;
    private bool _pending;
    private bool _claimed;
    private bool _fired;

    public FireWeaponWhenDeadBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _weapon = spec.GetString("DeathWeapon", "");
        _delayMilliseconds = Math.Max(0, spec.GetLong("DelayTime", 0));
        _startsActive = ModuleRuntime.ReadBool(spec, "StartsActive", true);
        _requiredStatus = spec.GetString("RequiredStatus", "");
        _exemptStatus = spec.GetString("ExemptStatus", "");
    }

    public bool HasFired => _fired;

    public override void OnDeathStarted(SimWorld world, GameObject self)
    {
        if (_fired || !_startsActive || _weapon.Length == 0 || !StatusAllows(self)) return;
        if (_delayMilliseconds == 0) Fire(world, self);
        else
        {
            _pending = true;
            _ticksRemaining = ModuleRuntime.MillisecondsToTicks(_delayMilliseconds, world.TickMilliseconds);
        }
    }

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (!_pending) return false;
        _claimed = true;
        self.MarkDying();
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_pending || --_ticksRemaining > 0) return;
        _pending = false;
        Fire(world, self);
        if (_claimed) world.CompleteClaimedDeath(self, this);
    }

    private bool StatusAllows(GameObject self)
    {
        var required = ModuleRuntime.Tokens(_requiredStatus);
        var exempt = ModuleRuntime.Tokens(_exemptStatus);
        return required.All(self.ConditionTokens.Contains) && !exempt.Any(self.ConditionTokens.Contains);
    }

    private void Fire(SimWorld world, GameObject self)
    {
        world.Combat.FireWeaponAtPosition(world, self, _weapon, self.Position);
        _fired = true;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksRemaining);
        writer.WriteBool(_pending);
        writer.WriteBool(_claimed);
        writer.WriteBool(_fired);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksRemaining = reader.ReadInt();
        _pending = reader.ReadBool();
        _claimed = reader.ReadBool();
        _fired = reader.ReadBool();
    }
}
