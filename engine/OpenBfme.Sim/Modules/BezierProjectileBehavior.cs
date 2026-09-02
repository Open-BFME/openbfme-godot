namespace OpenBfme.Sim;

/// <summary>
/// BezierProjectileBehavior-lite (237 objects in the union corpus): the module
/// TYPE maps now; trajectory fidelity is presentation. This tier flies a
/// straight line — Launch aims at a target object, the projectile closes the
/// remaining offset in equal fractions over FlightTicks updates (re-aiming at
/// the target's current position each tick while it lives), and on the arrival
/// update deals Damage of DamageType via world.DealDamage, then expires through
/// the death pipeline. The full bezier arc is a later fidelity pass.
/// Design data: FlightTicks (default 10), Damage (default 10),
/// string DamageType (default "default").
/// </summary>
[SageModule("BezierProjectileBehavior", ModuleTier.Structural)]
public sealed class BezierProjectileModule : ModuleBase
{
    public const string TypeName = "BezierProjectileBehavior";

    private readonly int _flightTicks;
    private readonly long _damage;
    private readonly string _damageType;

    private bool _inFlight;
    private int _targetId;
    private FixedVector2 _aimPoint;
    private int _ticksRemaining;

    public BezierProjectileModule(ModuleSpec spec) : base(spec)
    {
        _flightTicks = (int)Math.Max(1, spec.GetLong("FlightTicks", 10));
        _damage = Math.Max(0, spec.GetLong("Damage", 10));
        _damageType = spec.GetString("DamageType", DamageTypes.Default);
    }

    public bool InFlight => _inFlight;
    public int TargetId => _targetId;

    /// <summary>
    /// Arms the projectile at the target: it arrives (and deals damage) on its
    /// FlightTicks-th update after launch.
    /// </summary>
    public void Launch(SimWorld world, GameObject self, int targetId)
    {
        _inFlight = true;
        _targetId = targetId;
        _aimPoint = world.Objects.TryGetValue(targetId, out var target) ? target.Position : self.Position;
        _ticksRemaining = _flightTicks;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_inFlight || self.IsDying)
        {
            return;
        }
        var target = world.Objects.TryGetValue(_targetId, out var found) && !found.IsDead ? found : null;
        if (target != null)
        {
            _aimPoint = target.Position;
        }
        // Equal-fraction closing: step = remaining offset / remaining ticks.
        // The final tick's step is the whole remaining offset — exact arrival.
        var offset = _aimPoint - self.Position;
        var ticks = Fixed64.FromInt(_ticksRemaining);
        var step = new FixedVector2(offset.X / ticks, offset.Y / ticks);
        self.SetPosition(_ticksRemaining == 1 ? _aimPoint : self.Position + step);
        _ticksRemaining--;
        if (_ticksRemaining > 0)
        {
            return;
        }
        _inFlight = false;
        if (target != null && _damage > 0)
        {
            world.DealDamage(target, _damage, _damageType);
        }
        world.HandleDeath(self);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_inFlight);
        writer.WriteInt(_targetId);
        writer.WriteVector(_aimPoint);
        writer.WriteInt(_ticksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _inFlight = reader.ReadBool();
        _targetId = reader.ReadInt();
        _aimPoint = reader.ReadVector();
        _ticksRemaining = reader.ReadInt();
    }
}
