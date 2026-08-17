> Promoted from `workspace/scratch/recook-checklist-20260816.md` on 2026-08-17 (stage-2a triage); verbatim.

# Recook checklist — from presentation lane wrap-up (2026-08-16, commits 024c69d..7874318)

When recooking packs (with the asset-lane cook + swap), the recook MUST emit these
moduleContracts kinds or tonight's presentation work stays unbound on shipped content.
Stamp line=assignment.line on every per-assignment row (ba1b701 regression) or
ship/hero catalogs collide.

| Kind | Emit | Notes |
|---|---|---|
| ModelConditionUpgrade | typed rows w/ authored condition flags | no unit-visual fallback; recook is the only live bind |
| AnimationState | one row per AnimationState/IdleAnimationState/TransitionState: conditions, animations (AnimationName, mode, blend), Flags, StateName, AnimationPriority | live select already reads visual.authoredAnimationStates; typed path needs recook; nested bones/FX compile as SIBLING kinds |
| ParticleSysBone (AnimationState-nested) | bone + particleSystem + FollowBone Yes/No; stateKind=AnimationState; conditions = parent state's tokens | MCS attachments already ship in visual.particleAttachments; state-nested do NOT. "FollowBone: no" (space) is valid |
| EnteringStateFX | executable rows: stateKind, conditions, fxList | sibling FXEvent assignments -> separate FXEvent row |
| FXEvent | rows for `Frame:N [FireWhenSkipped] Name:Id` AND obsolete `N FX_Name` | FireWhenSkipped -> skippedCuePolicy fire-when-skipped; default ignore |

Verification after recook:
- compile-ship-catalog both games: ship_count 13, runtime_deferred_count 0
- compile-hero-catalog both games: exit 0 (RotWK uses layered-install ROOT, not layer-1-bfme2)
- grok's runners: PARTICLE_SYS_BONE 10/0, ENTERING_STATE_FX 8/0, FX_EVENT 8/0,
  CLIP_FRAME_CLOCK 10/0, ANIMATION_SOUND_CLIENT_BEHAVIOR 12/0, DRAWABLE_FX_LIST 8/0,
  ANIMATION_STATE_SELECT 12/0, MODEL_CONDITION_UPGRADE 10/0, SUB_OBJECTS_UPGRADE 10/0

Open cross-lane contract: sim must expose live SAGE weapon-cycle tokens
(PREATTACK_A..D, FIRING_A..D, FIRING_OR_PREATTACK_*, FIRING_OR_RELOADING_*,
WEAPONSET_TOGGLE_*, SWAPPING_TO_WEAPONSET_1, CLOSE_RANGE) — assigned to opus lane
72489644. Until then attack poses use the semantic clip map.
