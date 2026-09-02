namespace OpenBfme.Sim;

/// <summary>Fixed-point mass, impulse, gravity, airborne integration, and landing.</summary>
[SageModule("PhysicsBehavior", ModuleTier.Cosmetic)]
public sealed class PhysicsBehaviorModule : ModuleBase
{
    public const string TypeName = "PhysicsBehavior";
    private static readonly Fixed64 DefaultGravity = Fixed64.FromInt(10);

    private readonly Fixed64 _mass;
    private readonly Fixed64 _gravityMultiplier;
    private readonly bool _allowBouncing;
    private FixedVector2 _horizontalVelocity;
    private Fixed64 _verticalVelocity;

    public PhysicsBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _mass = Fixed64.Max(ReadFixed(spec, "Mass", Fixed64.One), Fixed64.FromFraction(1, 1024));
        _gravityMultiplier = Fixed64.Max(ReadFixed(spec, "GravityMult", Fixed64.One), Fixed64.Zero);
        _allowBouncing = spec.GetLong("AllowBouncing", 0) != 0;
    }

    public Fixed64 Mass => _mass;
    public FixedVector2 HorizontalVelocity => _horizontalVelocity;
    public Fixed64 VerticalVelocity => _verticalVelocity;
    public bool IsAirborne => _verticalVelocity != Fixed64.Zero;

    public void ApplyImpulse(FixedVector2 force, Fixed64 verticalForce)
    {
        _horizontalVelocity += force * (Fixed64.One / _mass);
        _verticalVelocity += verticalForce / _mass;
    }

    public void ApplyKnockback(FixedVector2 origin, GameObject target, Fixed64 force)
    {
        var away = target.Position - origin;
        var direction = away == FixedVector2.Zero
            ? new FixedVector2(Fixed64.One, Fixed64.Zero)
            : away.Normalized();
        ApplyImpulse(direction * force, force * Fixed64.Half);
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_horizontalVelocity == FixedVector2.Zero
            && _verticalVelocity == Fixed64.Zero
            && self.Elevation <= Fixed64.Zero) return;

        var dt = Fixed64.FromFraction(world.TickMilliseconds, 1_000);
        var nextPosition = self.Position + _horizontalVelocity * dt;
        var nextElevation = self.Elevation + _verticalVelocity * dt;
        _verticalVelocity -= DefaultGravity * _gravityMultiplier * dt;
        if (nextElevation <= Fixed64.Zero)
        {
            nextElevation = Fixed64.Zero;
            _horizontalVelocity = FixedVector2.Zero;
            _verticalVelocity = _allowBouncing
                ? -_verticalVelocity * Fixed64.Half
                : Fixed64.Zero;
            if (_verticalVelocity < Fixed64.FromFraction(1, 10)) _verticalVelocity = Fixed64.Zero;
        }
        self.SetTransform(nextPosition, nextElevation, self.HeadingRadians);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteVector(_horizontalVelocity);
        writer.WriteFixed(_verticalVelocity);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _horizontalVelocity = reader.ReadVector();
        _verticalVelocity = reader.ReadFixed();
    }

    private static Fixed64 ReadFixed(ModuleSpec spec, string name, Fixed64 fallback)
    {
        if (spec.Data.TryGetValue(name + "Raw", out var raw)) return Fixed64.FromRaw(raw);
        if (spec.Data.TryGetValue(name, out var integer)) return Fixed64.FromInt64(integer);
        return fallback;
    }
}
