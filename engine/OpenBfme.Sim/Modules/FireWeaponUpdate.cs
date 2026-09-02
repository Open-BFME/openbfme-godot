namespace OpenBfme.Sim;

/// <summary>
/// Executes each authored FireWeaponNugget after FireDelay through the cooked
/// weapon table and normal damage/armor path. OneShot controls repetition;
/// AliveOnly, ChargingModeTrigger, and HeroModeTrigger gate firing using
/// authoritative object state/condition tokens. Offsets and weapon presentation
/// are deferred because bundle-v1 has no 3D scripted-impact command.
/// </summary>
[SageModule("FireWeaponUpdate", ModuleTier.Structural)]
public sealed class FireWeaponUpdateModule : ModuleBase
{
    public const string TypeName = "FireWeaponUpdate";
    private readonly bool _aliveOnly;
    private readonly bool _chargingOnly;
    private readonly bool _heroModeOnly;
    private readonly NuggetState[] _nuggets;

    public FireWeaponUpdateModule(ModuleSpec spec) : base(spec)
    {
        _aliveOnly = spec.GetLong("AliveOnly", 0) != 0;
        _chargingOnly = spec.GetLong("ChargingModeTrigger", 0) != 0;
        _heroModeOnly = spec.GetLong("HeroModeTrigger", 0) != 0;
        _nuggets = spec.Blocks.Where(block => block.Type.Equals("FireWeaponNugget",
                StringComparison.OrdinalIgnoreCase)).Select(Parse).ToArray();
    }

    public int FiredCount => _nuggets.Count(value => value.Fired);

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if ((_aliveOnly && (self.IsDead || self.IsDying))
            || (_chargingOnly && !self.HasConditionToken("CHARGING"))
            || (_heroModeOnly && !self.HasConditionToken("HERO_MODE"))) return;
        foreach (var nugget in _nuggets)
        {
            if (nugget.OneShot && nugget.Fired) continue;
            if (nugget.TicksRemaining < 0)
                nugget.TicksRemaining = IniValueReader.MillisecondsToTicks(nugget.DelayMilliseconds, world.TickMilliseconds);
            if (nugget.TicksRemaining > 0) nugget.TicksRemaining--;
            if (nugget.TicksRemaining > 0) continue;
            nugget.Fired = world.Combat.FireScriptedWeapon(world, self, nugget.WeaponName) || nugget.Fired;
            nugget.TicksRemaining = nugget.OneShot ? 0
                : IniValueReader.MillisecondsToTicks(nugget.DelayMilliseconds, world.TickMilliseconds);
        }
    }

    private static NuggetState Parse(BundleBlock block)
    {
        var weapon = Text(block, "WeaponName");
        var delay = Whole(block, "FireDelay");
        var oneShot = Flag(block, "OneShot");
        return new NuggetState(weapon, Math.Max(0, delay), oneShot);
    }

    private static string Text(BundleBlock block, string name) =>
        block.Fields.TryGetValue(name, out var value) && value.Kind == BundleValueKind.String
            ? value.String ?? "" : "";
    private static long Whole(BundleBlock block, string name) =>
        block.Fields.TryGetValue(name, out var value) && value.Kind == BundleValueKind.Integer
            ? value.Integer : 0;
    private static bool Flag(BundleBlock block, string name) =>
        block.Fields.TryGetValue(name, out var value) && value.Kind == BundleValueKind.Boolean && value.Boolean;

    public override void WriteState(CanonicalWriter writer)
    {
        foreach (var nugget in _nuggets)
        {
            writer.WriteInt(nugget.TicksRemaining);
            writer.WriteBool(nugget.Fired);
        }
    }

    public override void ReadState(CanonicalReader reader)
    {
        foreach (var nugget in _nuggets)
        {
            nugget.TicksRemaining = reader.ReadInt();
            nugget.Fired = reader.ReadBool();
        }
    }

    private sealed class NuggetState(string weaponName, long delayMilliseconds, bool oneShot)
    {
        public string WeaponName { get; } = weaponName;
        public long DelayMilliseconds { get; } = delayMilliseconds;
        public bool OneShot { get; } = oneShot;
        public int TicksRemaining { get; set; } = -1;
        public bool Fired { get; set; }
    }
}
