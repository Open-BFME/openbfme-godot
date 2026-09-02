namespace OpenBfme.Sim;

/// <summary>
/// Targeted special power that transfers DeliverUpgrade (or the caster's first
/// owned object upgrade when retail leaves that field dynamic) to the target,
/// then runs the target's normal upgrade evaluator and consumes the courier.
/// StartAbilityRange is enforced exactly. Approach LOS/timing and SpawnOutFX are
/// deferred until pathing/presentation expose those seams.
/// </summary>
[SageModule("GiveUpgradeUpdate", ModuleTier.Structural)]
public sealed class GiveUpgradeUpdateModule : SpecialPowerEffectModuleBase
{
    public const string TypeName = "GiveUpgradeUpdate";
    private readonly string _upgrade;
    private readonly Fixed64 _range;

    public GiveUpgradeUpdateModule(ModuleSpec spec) : base(spec)
    {
        _upgrade = spec.GetString("DeliverUpgrade", "");
        _range = spec.GetFixed("StartAbilityRangeRaw", Fixed64.FromInt(100));
    }

    internal override void Cast(SimWorld world, GameObject caster, int targetId, FixedVector2 targetPosition)
    {
        if (targetId == 0 || !world.Objects.TryGetValue(targetId, out var target)
            || caster.Position.DistanceSquaredTo(target.Position) > _range * _range) return;
        var upgrade = _upgrade.Length > 0 ? _upgrade : caster.OwnedUpgrades.FirstOrDefault() ?? "";
        if (upgrade.Length == 0 || !world.GrantObjectUpgrade(target, upgrade)) return;
        world.HandleDeath(caster);
    }
}
