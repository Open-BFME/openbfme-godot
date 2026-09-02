namespace OpenBfme.Sim;

/// <summary>Separates overlapping members of different hordes in ascending-id pair order.</summary>
[SageModule("HordeMemberCollide", ModuleTier.Cosmetic)]
public sealed class HordeMemberCollideModule : ModuleBase
{
    public const string TypeName = "HordeMemberCollide";
    private readonly Fixed64 _radius;

    public HordeMemberCollideModule(ModuleSpec spec) : base(spec)
    {
        _radius = Fixed64.Max(ReadFixed(spec, "Radius",
            ReadFixed(spec, "CollisionRadius", Fixed64.One)), Fixed64.Zero);
    }

    public Fixed64 Radius => _radius;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        var ownHorde = HordeFor(world, self.Id);
        if (ownHorde == null || _radius <= Fixed64.Zero) return;
        foreach (var other in world.Objects.Values)
        {
            if (other.Id <= self.Id || other.IsDead || other.IsDying
                || other.FindModule<HordeMemberCollideModule>() is not { } otherCollision) continue;
            var otherHorde = HordeFor(world, other.Id);
            if (otherHorde == null || otherHorde.Id == ownHorde.Id) continue;
            var separation = Fixed64.Max(_radius, otherCollision._radius);
            var offset = other.Position - self.Position;
            var distanceSquared = offset.LengthSquared();
            if (distanceSquared >= separation * separation) continue;
            var direction = distanceSquared == Fixed64.Zero
                ? new FixedVector2(Fixed64.One, Fixed64.Zero)
                : offset * (Fixed64.One / Fixed64.Sqrt(distanceSquared));
            var distance = distanceSquared == Fixed64.Zero ? Fixed64.Zero : Fixed64.Sqrt(distanceSquared);
            var push = (separation - distance) * Fixed64.Half;
            self.SetPosition(self.Position - direction * push);
            other.SetPosition(other.Position + direction * push);
        }
    }

    private static SnapshotHorde? HordeFor(SimWorld world, int memberId)
    {
        foreach (var horde in world.Hordes)
            if (horde.Id == memberId || horde.Members.Contains(memberId)) return horde;
        return null;
    }

    private static Fixed64 ReadFixed(ModuleSpec spec, string name, Fixed64 fallback)
    {
        if (spec.Data.TryGetValue(name + "Raw", out var raw)) return Fixed64.FromRaw(raw);
        if (spec.Data.TryGetValue(name, out var integer)) return Fixed64.FromInt64(integer);
        return fallback;
    }
}
