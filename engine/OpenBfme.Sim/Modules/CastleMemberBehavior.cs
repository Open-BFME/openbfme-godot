namespace OpenBfme.Sim;

/// <summary>Links a castle piece to its spawning castle and deterministically follows its lifetime.</summary>
[SageModule("CastleMemberBehavior", ModuleTier.Structural)]
public sealed class CastleMemberBehaviorModule : ModuleBase
{
    public const string TypeName = "CastleMemberBehavior";
    private readonly bool _contributesHealth;
    private readonly bool _countsForEvaCastleBreached;
    private readonly bool _storeUpgradePrice;
    private readonly string _beingBuiltSound;
    private readonly string[] _destroyedEvaEvents;
    private int _castleId;
    private bool _destroyedPresentationRaised;

    public CastleMemberBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _contributesHealth = ModuleRuntime.ReadBool(spec, "ContributesToCastleHealth")
            || spec.Data.ContainsKey("HealthContribution");
        _countsForEvaCastleBreached = ModuleRuntime.ReadBool(spec, "CountsForEvaCastleBreached");
        _storeUpgradePrice = ModuleRuntime.ReadBool(spec, "StoreUpgradePrice");
        _beingBuiltSound = spec.GetString("BeingBuiltSound", "");
        _destroyedEvaEvents = new[]
        {
            spec.GetString("CampDestroyedOwnerEvaEvent", ""),
            spec.GetString("CampDestroyedAllyEvaEvent", ""),
            spec.GetString("CampDestroyedAttackerEvaEvent", ""),
        }.Where(value => value.Length > 0).ToArray();
    }

    public int CastleId => _castleId;
    public bool StoresUpgradePrice => _storeUpgradePrice;

    public override void OnCreated(SimWorld world, GameObject self, GameObject? creator)
    {
        if (creator?.FindModule<CastleBehaviorModule>() != null)
        {
            _castleId = creator.Id;
            ApplyAuthoredPresentation(world, self);
            return;
        }
        _castleId = world.Objects.Values
            .Where(value => value.Team == self.Team && value.Id != self.Id
                && value.FindModule<CastleBehaviorModule>() != null)
            .OrderBy(value => value.Position.DistanceSquaredTo(self.Position))
            .ThenBy(value => value.Id)
            .Select(value => value.Id)
            .FirstOrDefault();
        ApplyAuthoredPresentation(world, self);
    }

    private void ApplyAuthoredPresentation(SimWorld world, GameObject self)
    {
        if (_countsForEvaCastleBreached) self.TrySetConditionToken("CASTLE_MEMBER_COUNTS_FOR_EVA_BREACH");
        if (_storeUpgradePrice) self.TrySetConditionToken("CASTLE_MEMBER_STORE_UPGRADE_PRICE");
        if (_beingBuiltSound.Length > 0)
            world.RaiseEvent(new SimEvent("sound", self.Id, Target: _castleId == 0 ? null : _castleId, Name: _beingBuiltSound));
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_castleId == 0 || self.IsDying) return;
        if (!world.Objects.TryGetValue(_castleId, out var castle) || castle.IsDead)
        {
            if (!_destroyedPresentationRaised)
            {
                foreach (var eventName in _destroyedEvaEvents)
                    world.RaiseEvent(new SimEvent("sound", self.Id, Target: _castleId, Name: eventName));
                _destroyedPresentationRaised = true;
            }
            world.HandleDeath(self);
        }
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

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_castleId);
        writer.WriteBool(_destroyedPresentationRaised);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _castleId = reader.ReadInt();
        _destroyedPresentationRaised = reader.ReadBool();
    }
}
