namespace OpenBfme.Sim;

/// <summary>Maps stance command names to authored AI auto-acquire and guard parameters.</summary>
[SageModule("StancesBehavior", ModuleTier.Cosmetic)]
public sealed class StancesBehaviorModule : ModuleBase
{
    public const string TypeName = "StancesBehavior";

    public StancesBehaviorModule(ModuleSpec spec) : base(spec) { }

    internal void ApplyProfile(GameObject self, UnitStance stance)
    {
        var autoAcquire = stance switch
        {
            UnitStance.Aggressive => ReadBool("AggressiveAutoAcquire", true),
            UnitStance.Battle => ReadBool("BattleAutoAcquire", true),
            UnitStance.HoldGround => ReadBool("HoldGroundAutoAcquire", false),
            _ => false,
        };
        var radius = stance switch
        {
            UnitStance.Aggressive => ReadFixed("AggressiveGuardRadius", Fixed64.Zero),
            UnitStance.Battle => ReadFixed("BattleGuardRadius", Fixed64.Zero),
            UnitStance.HoldGround => ReadFixed("HoldGroundGuardRadius",
                ReadFixed("GuardRadius", Fixed64.FromInt(1))),
            _ => Fixed64.Zero,
        };
        self.FindModule<AIUpdateInterfaceModule>()?.ApplyStanceProfile(autoAcquire, radius);
        self.FindModule<HordeAIUpdateModule>()?.ApplyStanceProfile(autoAcquire, radius);
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.Combat != null) ApplyProfile(self, self.Combat.Stance);
    }

    private bool ReadBool(string name, bool fallback) =>
        Spec.Data.TryGetValue(name, out var value) ? value != 0 : fallback;

    private Fixed64 ReadFixed(string name, Fixed64 fallback)
    {
        if (Spec.Data.TryGetValue(name + "Raw", out var raw)) return Fixed64.FromRaw(raw);
        if (Spec.Data.TryGetValue(name, out var integer)) return Fixed64.FromInt64(integer);
        return fallback;
    }
}
