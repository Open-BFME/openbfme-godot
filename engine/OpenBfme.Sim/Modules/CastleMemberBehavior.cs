namespace OpenBfme.Sim;

/// <summary>Links a castle piece to its spawning castle and deterministically follows its lifetime.</summary>
[SageModule("CastleMemberBehavior", ModuleTier.Structural)]
public sealed class CastleMemberBehaviorModule : ModuleBase
{
    public const string TypeName = "CastleMemberBehavior";
    private readonly bool _contributesHealth;
    private int _castleId;

    public CastleMemberBehaviorModule(ModuleSpec spec) : base(spec) =>
        _contributesHealth = ModuleRuntime.ReadBool(spec, "ContributesToCastleHealth")
            || spec.Data.ContainsKey("HealthContribution");

    public int CastleId => _castleId;

    public override void OnCreated(SimWorld world, GameObject self, GameObject? creator)
    {
        if (creator?.FindModule<CastleBehaviorModule>() != null)
        {
            _castleId = creator.Id;
            return;
        }
        _castleId = world.Objects.Values
            .Where(value => value.Team == self.Team && value.Id != self.Id
                && value.FindModule<CastleBehaviorModule>() != null)
            .OrderBy(value => value.Position.DistanceSquaredTo(self.Position))
            .ThenBy(value => value.Id)
            .Select(value => value.Id)
            .FirstOrDefault();
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_castleId == 0 || self.IsDying) return;
        if (!world.Objects.TryGetValue(_castleId, out var castle) || castle.IsDead)
            world.HandleDeath(self);
    }

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (!_contributesHealth || _castleId == 0
            || !world.Objects.TryGetValue(_castleId, out var castle)) return false;
        world.DealDamage(castle, amount);
        return true;
    }

    public override void OnDamageReceived(
        SimWorld world,
        GameObject self,
        Fixed64 amount,
        string damageType)
    {
        if (!_contributesHealth || self.MaxHealth <= Fixed64.Zero || _castleId == 0 || amount <= Fixed64.Zero
            || !world.Objects.TryGetValue(_castleId, out var castle)) return;
        world.Heal(self, amount);
        world.DealDamage(castle, Math.Max(1, amount.ToIntFloor()), damageType);
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_castleId);
    public override void ReadState(CanonicalReader reader) => _castleId = reader.ReadInt();
}
