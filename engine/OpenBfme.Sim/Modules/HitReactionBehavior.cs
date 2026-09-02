namespace OpenBfme.Sim;

/// <summary>Authored three-threshold stagger. The selected life timer pauses movement and raises a presentation bit.</summary>
[SageModule("HitReactionBehavior", ModuleTier.Structural)]
public sealed class HitReactionBehaviorModule : ModuleBase, IMovementModifierModule, IPresentationStateModule
{
    public const string TypeName = "HitReactionBehavior";
    public const int PresentationHitReaction = 1 << 5;

    private readonly Fixed64[] _thresholds;
    private readonly long[] _lifeMilliseconds;
    private readonly bool _fastHitsReset;
    private int _ticksRemaining;

    public HitReactionBehaviorModule(ModuleSpec spec) : base(spec)
    {
        _thresholds = Enumerable.Range(1, 3)
            .Select(index => ModuleRuntime.ReadFixed(spec, $"HitReactionThreshold{index}", Fixed64.MaxValue))
            .ToArray();
        _lifeMilliseconds = Enumerable.Range(1, 3)
            .Select(index => Math.Max(0, spec.GetLong($"HitReactionLifeTimer{index}", 0)))
            .ToArray();
        _fastHitsReset = ModuleRuntime.ReadBool(spec, "FastHitsResetReaction");
    }

    public bool IsStaggered => _ticksRemaining > 0;
    public Fixed64 MovementSpeedMultiplier => Fixed64.One;
    public bool PausesMovement => IsStaggered;
    public int PresentationStateBits => IsStaggered ? PresentationHitReaction : 0;

    public override void OnDamageReceived(SimWorld world, GameObject self, Fixed64 amount, string damageType)
    {
        var selected = -1;
        for (var index = 0; index < _thresholds.Length; index++)
            if (amount >= _thresholds[index]) selected = index;
        if (selected < 0 || _lifeMilliseconds[selected] == 0) return;
        var ticks = ModuleRuntime.MillisecondsToTicks(_lifeMilliseconds[selected], world.TickMilliseconds);
        _ticksRemaining = _fastHitsReset ? ticks : Math.Max(_ticksRemaining, ticks);
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_ticksRemaining > 0) _ticksRemaining--;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_ticksRemaining);
    public override void ReadState(CanonicalReader reader) => _ticksRemaining = reader.ReadInt();
}
