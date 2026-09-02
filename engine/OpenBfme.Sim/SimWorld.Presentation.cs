namespace OpenBfme.Sim;

/// <summary>
/// Deterministic, renderer-facing animation projection. This cache is derived
/// from authoritative object/combat/movement facts and is deliberately absent
/// from canonical state: adding presentation columns to StateHash would move
/// the lockstep pin for worlds whose gameplay state did not change.
/// </summary>
public sealed partial class SimWorld
{
    public const int PresentationMoving = 1 << 0;
    public const int PresentationAttacking = 1 << 1;
    public const int PresentationDying = 1 << 2;
    public const int PresentationIdleGuard = 1 << 3;
    public const int PresentationHold = 1 << 4;

    public const int AnimationIdle = 0;
    public const int AnimationMove = 1;
    public const int AnimationAttack = 2;
    public const int AnimationDie = 3;
    public const int AnimationCheer = 4;

    private static readonly Fixed64 IdleFrameCount = Fixed64.FromInt(60);
    private static readonly Fixed64 MoveFrameCount = Fixed64.FromInt(30);
    private static readonly Fixed64 AttackFrameCount = Fixed64.FromInt(30);
    private static readonly Fixed64 DieLastFrame = Fixed64.FromInt(44);

    private sealed class PresentationTrack
    {
        public int LastTick;
        public int Slot;
        public Fixed64 Frame;
        public FixedVector2 PreviousPosition;
        public WeaponCyclePhase PreviousCombatPhase;
        public bool Moving;
    }

    private readonly Dictionary<int, PresentationTrack> _presentationTracks = new();

    private void SynchronizePresentation(GameObject gameObject, int slot)
    {
        if (!_presentationTracks.TryGetValue(gameObject.Id, out var track))
        {
            track = new PresentationTrack
            {
                LastTick = TickIndex,
                PreviousPosition = gameObject.Position,
                PreviousCombatPhase = gameObject.Combat?.CyclePhase ?? WeaponCyclePhase.Ready,
                Moving = HasMovementOrder(gameObject),
            };
            _presentationTracks.Add(gameObject.Id, track);
        }

        if (track.LastTick != TickIndex)
        {
            AdvancePresentation(gameObject, track);
            track.LastTick = TickIndex;
            track.PreviousPosition = gameObject.Position;
            track.PreviousCombatPhase = gameObject.Combat?.CyclePhase ?? WeaponCyclePhase.Ready;
        }

        var moving = track.Moving;
        var attacking = track.Slot == AnimationAttack;
        var state = 0;
        if (moving) state |= PresentationMoving;
        if (attacking) state |= PresentationAttacking;
        if (gameObject.IsDying) state |= PresentationDying;
        if (!moving && !attacking && !gameObject.IsDying
            && gameObject.Combat?.Stance == UnitStance.Battle)
        {
            state |= PresentationIdleGuard;
        }
        if (gameObject.Combat?.Stance == UnitStance.HoldGround) state |= PresentationHold;

        ObjectStore.State[slot] = state | ModuleRuntime.PresentationBits(gameObject);
        ObjectStore.Anim[slot] = track.Slot;
        ObjectStore.AnimFrame[slot] = track.Frame;
    }

    private void AdvancePresentation(GameObject gameObject, PresentationTrack track)
    {
        var frameStep = Fixed64.FromFraction(checked(30L * TickMilliseconds), 1_000);
        var moving = IsPresentationMoving(gameObject, track);
        track.Moving = moving;
        var combatPhase = gameObject.Combat?.CyclePhase ?? WeaponCyclePhase.Ready;
        var fireThisTick = _eventsThisTick.Any(value =>
            value.Kind == "fire" && value.Object == gameObject.Id);
        var enteredPreAttack = combatPhase == WeaponCyclePhase.PreAttack
            && track.PreviousCombatPhase != WeaponCyclePhase.PreAttack;

        if (gameObject.IsDying)
        {
            if (track.Slot != AnimationDie)
            {
                track.Slot = AnimationDie;
                track.Frame = Fixed64.Zero;
            }
            else
            {
                track.Frame = Fixed64.Min(DieLastFrame, track.Frame + frameStep);
            }
            return;
        }

        if (fireThisTick || enteredPreAttack)
        {
            track.Slot = AnimationAttack;
            track.Frame = Fixed64.Zero;
            return;
        }

        if (track.Slot == AnimationAttack)
        {
            track.Frame += frameStep;
            if (track.Frame < AttackFrameCount) return;
            track.Slot = moving ? AnimationMove : AnimationIdle;
            track.Frame = Fixed64.Zero;
            return;
        }

        var desired = moving ? AnimationMove : AnimationIdle;
        if (track.Slot != desired)
        {
            track.Slot = desired;
            track.Frame = Fixed64.Zero;
            return;
        }
        var frameCount = desired == AnimationMove ? MoveFrameCount : IdleFrameCount;
        track.Frame = Fixed64.FromRaw((track.Frame + frameStep).Raw % frameCount.Raw);
    }

    private static bool IsPresentationMoving(GameObject gameObject, PresentationTrack track)
    {
        return HasMovementOrder(gameObject) || gameObject.Position != track.PreviousPosition;
    }

    private static bool HasMovementOrder(GameObject gameObject) =>
        gameObject.FindModule<LocomotorModule>() is { } locomotor
            && (locomotor.HasOrder || locomotor.CurrentSpeed > Fixed64.Zero);
}
