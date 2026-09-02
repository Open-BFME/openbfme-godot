namespace OpenBfme.Sim;

/// <summary>Idle auto-acquisition cadence plus deterministic guard-anchor return.</summary>
[SageModule("AIUpdateInterface", ModuleTier.Cosmetic)]
public sealed class AIUpdateInterfaceModule : ModuleBase
{
    public const string TypeName = "AIUpdateInterface";

    private readonly bool _authoredAutoAcquire;
    private readonly long _moodAttackCheckMilliseconds;
    private readonly Fixed64 _authoredGuardRadius;
    private bool _anchorSet;
    private FixedVector2 _guardAnchor;
    private int _ticksUntilScan;
    private int _stanceAutoAcquire = -1;
    private Fixed64 _stanceGuardRadius;

    public AIUpdateInterfaceModule(ModuleSpec spec) : base(spec)
    {
        _authoredAutoAcquire = ReadYes(spec, "AutoAcquireEnemiesWhenIdle");
        _moodAttackCheckMilliseconds = Math.Max(0, spec.GetLong("MoodAttackCheckRate", 0));
        _authoredGuardRadius = ReadFixed(spec, "GuardRadius",
            ReadFixed(spec, "StopChaseDistance",
                ReadFixed(spec, "HoldGroundCloseRangeDistance", Fixed64.Zero)));
    }

    public bool AutoAcquireEnabled => _stanceAutoAcquire < 0
        ? _authoredAutoAcquire
        : _stanceAutoAcquire != 0;
    public FixedVector2 GuardAnchor => _guardAnchor;
    public Fixed64 GuardRadius => _stanceGuardRadius > Fixed64.Zero
        ? _stanceGuardRadius
        : _authoredGuardRadius;

    internal bool ShouldAutoAcquire(SimWorld world, GameObject self)
    {
        EnsureAnchor(self);
        if (!AutoAcquireEnabled || self.Combat?.Stance == UnitStance.HoldGround) return false;
        if (_ticksUntilScan > 0)
        {
            _ticksUntilScan--;
            return false;
        }
        _ticksUntilScan = _moodAttackCheckMilliseconds <= 0
            ? 0
            : checked((int)Math.Max(1,
                (_moodAttackCheckMilliseconds + world.TickMilliseconds - 1) / world.TickMilliseconds)) - 1;
        return true;
    }

    internal void ApplyStanceProfile(bool autoAcquire, Fixed64 guardRadius)
    {
        _stanceAutoAcquire = autoAcquire ? 1 : 0;
        _stanceGuardRadius = Fixed64.Max(Fixed64.Zero, guardRadius);
        _ticksUntilScan = 0;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        EnsureAnchor(self);
        var combat = self.Combat;
        if (combat == null || combat.OrderKind != CombatOrderKind.None || combat.EngagedTargetId != 0) return;
        var radius = GuardRadius;
        if (radius <= Fixed64.Zero
            || self.Position.DistanceSquaredTo(_guardAnchor) <= radius * radius) return;
        if (self.FindModule<LocomotorModule>() is { } locomotor)
            locomotor.SetOrder(_guardAnchor, MoveOrderKind.Move);
        else
            self.FindModule<LinearMoverModule>()?.SetDestination(_guardAnchor);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_anchorSet);
        writer.WriteVector(_guardAnchor);
        writer.WriteInt(_ticksUntilScan);
        writer.WriteInt(_stanceAutoAcquire);
        writer.WriteFixed(_stanceGuardRadius);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _anchorSet = reader.ReadBool();
        _guardAnchor = reader.ReadVector();
        _ticksUntilScan = reader.ReadInt();
        _stanceAutoAcquire = reader.ReadInt();
        _stanceGuardRadius = reader.ReadFixed();
    }

    private void EnsureAnchor(GameObject self)
    {
        if (_anchorSet) return;
        _guardAnchor = self.Position;
        _anchorSet = true;
    }

    private static bool ReadYes(ModuleSpec spec, string name)
    {
        if (spec.Data.TryGetValue(name, out var value)) return value != 0;
        var text = spec.GetString(name, "").Trim();
        return text.Equals("yes", StringComparison.OrdinalIgnoreCase)
            || text.StartsWith("yes ", StringComparison.OrdinalIgnoreCase)
            || text.Equals("true", StringComparison.OrdinalIgnoreCase);
    }

    private static Fixed64 ReadFixed(ModuleSpec spec, string name, Fixed64 fallback)
    {
        if (spec.Data.TryGetValue(name + "Raw", out var raw)) return Fixed64.FromRaw(raw);
        if (spec.Data.TryGetValue(name, out var integer)) return Fixed64.FromInt64(integer);
        return fallback;
    }
}
