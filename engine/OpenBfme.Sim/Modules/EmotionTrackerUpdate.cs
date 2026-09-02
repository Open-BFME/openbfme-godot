namespace OpenBfme.Sim;

/// <summary>
/// Deterministic emotion mapping: nearby TERROR carriers select TERROR,
/// authored AlwaysAfraidOf targets select TERROR, AfraidOf/FEAR carriers and
/// allied HERO deaths select FEAR, and an authored
/// TauntAndPointDistance selects TAUNT. Authored duration fields are
/// TerrorDuration/FearDuration/TauntDuration (milliseconds). TERROR and FEAR
/// feed the shared movement/outgoing-damage modifier paths; TAUNT is cosmetic.
/// </summary>
[SageModule("EmotionTrackerUpdate", ModuleTier.Structural)]
public sealed class EmotionTrackerUpdateModule : ModuleBase,
    IMovementModifierModule, IOutgoingDamageModifierModule
{
    public const string TypeName = "EmotionTrackerUpdate";

    private readonly Fixed64 _fearRadius;
    private readonly Fixed64 _heroRadius;
    private readonly Fixed64 _tauntRadius;
    private readonly string _afraidOf;
    private readonly string _alwaysAfraidOf;
    private readonly long _fearMilliseconds;
    private readonly long _terrorMilliseconds;
    private readonly long _tauntMilliseconds;
    private int _fearTicks;
    private int _terrorTicks;
    private int _tauntTicks;

    public EmotionTrackerUpdateModule(ModuleSpec spec) : base(spec)
    {
        _fearRadius = ModuleRuntime.ReadFixed(spec, "FearScanDistance", Fixed64.Zero);
        _heroRadius = ModuleRuntime.ReadFixed(spec, "HeroScanDistance", _fearRadius);
        _tauntRadius = ModuleRuntime.ReadFixed(spec, "TauntAndPointDistance", Fixed64.Zero);
        _afraidOf = spec.GetString("AfraidOf", "");
        _alwaysAfraidOf = spec.GetString("AlwaysAfraidOf", "");
        _fearMilliseconds = Math.Max(0, spec.GetLong("FearDuration", 3_000));
        _terrorMilliseconds = Math.Max(0, spec.GetLong("TerrorDuration", 5_000));
        _tauntMilliseconds = Math.Max(0, spec.GetLong("TauntDuration",
            spec.GetLong("TauntAndPointUpdateDelay", 2_000)));
    }

    public string Emotion => _terrorTicks > 0 ? "TERROR" : _fearTicks > 0 ? "FEAR" : _tauntTicks > 0 ? "TAUNT" : "";
    public Fixed64 MovementSpeedMultiplier => _terrorTicks > 0
        ? Fixed64.FromFraction(1, 2)
        : _fearTicks > 0 ? Fixed64.FromFraction(3, 4) : Fixed64.One;
    public Fixed64 OutgoingDamageMultiplier => _terrorTicks > 0 || _fearTicks > 0
        ? Fixed64.FromFraction(1, 2)
        : Fixed64.One;
    public bool PausesMovement => false;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        Decay();
        var fearSquared = _fearRadius * _fearRadius;
        var tauntSquared = _tauntRadius * _tauntRadius;
        foreach (var candidate in world.Objects.Values)
        {
            if (candidate.Id == self.Id || candidate.IsDead || candidate.IsDying) continue;
            var distance = self.Position.DistanceSquaredTo(candidate.Position);
            if (candidate.Team != self.Team && distance <= fearSquared)
            {
                if ((_alwaysAfraidOf.Length > 0 && ModuleRuntime.MatchesKindOf(candidate, _alwaysAfraidOf))
                    || candidate.Template.KindOf.Contains("TERROR", StringComparer.Ordinal))
                    _terrorTicks = DurationTicks(_terrorMilliseconds, world);
                else if ((_afraidOf.Length > 0 && ModuleRuntime.MatchesKindOf(candidate, _afraidOf))
                    || candidate.Template.KindOf.Contains("FEAR", StringComparer.Ordinal))
                    _fearTicks = DurationTicks(_fearMilliseconds, world);
            }
            if (_tauntRadius > Fixed64.Zero && candidate.Team != self.Team && distance <= tauntSquared)
                _tauntTicks = DurationTicks(_tauntMilliseconds, world);
        }
        if (_heroRadius > Fixed64.Zero)
        {
            var heroSquared = _heroRadius * _heroRadius;
            foreach (var death in world.EventsThisTick.Where(value => value.Kind == "death"))
                if (world.Objects.TryGetValue(death.Object, out var hero)
                    && hero.Team == self.Team
                    && hero.Template.KindOf.Contains("HERO", StringComparer.Ordinal)
                    && self.Position.DistanceSquaredTo(hero.Position) <= heroSquared)
                    _fearTicks = DurationTicks(_fearMilliseconds, world);
        }
        self.TrySetConditionToken("EMOTION_TERROR", _terrorTicks > 0);
        self.TrySetConditionToken("EMOTION_FEAR", _terrorTicks == 0 && _fearTicks > 0);
        self.TrySetConditionToken("EMOTION_TAUNT", _terrorTicks == 0 && _fearTicks == 0 && _tauntTicks > 0);
    }

    private void Decay()
    {
        if (_fearTicks > 0) _fearTicks--;
        if (_terrorTicks > 0) _terrorTicks--;
        if (_tauntTicks > 0) _tauntTicks--;
    }

    private static int DurationTicks(long milliseconds, SimWorld world) =>
        ModuleRuntime.MillisecondsToTicks(milliseconds, world.TickMilliseconds);

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_fearTicks);
        writer.WriteInt(_terrorTicks);
        writer.WriteInt(_tauntTicks);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _fearTicks = reader.ReadInt();
        _terrorTicks = reader.ReadInt();
        _tauntTicks = reader.ReadInt();
    }
}
