namespace OpenBfme.Sim;

/// <summary>
/// Command-v1 special-power effect that toggles MOUNTED state or replaces the
/// object with MountedTemplate when authored. Replacement preserves transform,
/// experience level, and object upgrades. TriggerInstantlyOnCreate is applied
/// once and serialized. Pack/preparation timing, opacity, disguise cancellation,
/// and animation are presentation/command-animation deferrals.
/// </summary>
[SageModule("ToggleMountedSpecialAbilityUpdate", ModuleTier.Structural)]
public sealed class ToggleMountedSpecialAbilityUpdateModule : SpecialPowerEffectModuleBase
{
    public const string TypeName = "ToggleMountedSpecialAbilityUpdate";
    private readonly string _mountedTemplate;
    private readonly bool _instant;
    private bool _mounted;
    private bool _instantApplied;

    public ToggleMountedSpecialAbilityUpdateModule(ModuleSpec spec) : base(spec)
    {
        _mountedTemplate = spec.GetString("MountedTemplate", "");
        _instant = spec.GetLong("TriggerInstantlyOnCreate", 0) != 0;
    }

    public bool IsMounted => _mounted;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_instant || _instantApplied) return;
        _instantApplied = true;
        Toggle(world, self);
    }

    internal override void Cast(SimWorld world, GameObject caster, int targetId, FixedVector2 targetPosition) =>
        Toggle(world, caster);

    private void Toggle(SimWorld world, GameObject self)
    {
        _mounted = !_mounted;
        self.SetConditionToken("MOUNTED", _mounted);
        if (_mountedTemplate.Length == 0 || !world.TryGetTemplate(_mountedTemplate, out _)) return;
        var replacement = world.SpawnObject(
            _mountedTemplate, self.Team, self.Position, self.Elevation, self.HeadingRadians);
        var oldLevel = self.FindModule<ExperienceLevelModule>()?.Level ?? 1;
        var newLevel = replacement.FindModule<ExperienceLevelModule>();
        if (newLevel != null && oldLevel > 1) newLevel.GrantLevels(oldLevel - 1);
        foreach (var upgrade in self.OwnedUpgrades) world.GrantObjectUpgrade(replacement, upgrade);
        world.HandleDeath(self);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_mounted);
        writer.WriteBool(_instantApplied);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _mounted = reader.ReadBool();
        _instantApplied = reader.ReadBool();
    }
}
