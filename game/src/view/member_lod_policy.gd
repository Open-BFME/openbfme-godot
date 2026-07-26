class_name MemberLodPolicy
extends RefCounted
## Distance-based level-of-detail tiers for battalion member presentation.
##
## Pure policy: no nodes, no scene access, no sim reads. It maps a camera
## distance plus a visibility flag onto a tier, and each tier names exactly which
## presentation features stay on. The batcher (member_render_batcher.gd) applies
## it; the runner (tests/render_member_batching_runner.gd) tests it directly.
##
## Tiers are classified per BATTALION, not per member. A battalion's members span
## a few metres while the tier boundaries are tens of metres, so per-member
## distance maths would cost 10x more and buy a boundary refinement no player can
## see. This matches the cadence LOD already used by retail_formation.gd.

enum Tier {
	## Full skinned members, contact shadow decals, per-member overlays.
	NEAR = 0,
	## Full skinned members and animation, but per-member decoration is dropped:
	## contact shadows and selection/health overlays stop drawing.
	MID = 1,
	## Skinned geometry is replaced by a baked static pose drawn from a shared
	## MultiMesh, and looping animation stops advancing.
	FAR = 2,
	## Nothing is drawn and looping animation stops advancing.
	CULLED = 3,
}

## Members closer than this keep every presentation feature.
const DEFAULT_NEAR_DISTANCE := 70.0
## Beyond this, per-member decals and overlays stop drawing.
const DEFAULT_MID_DISTANCE := 150.0
## Beyond this, members become MultiMesh instances of a baked static pose.
## Chosen so a batched soldier is a handful of pixels tall at the slice's
## default camera pitch - the frozen pose is not resolvable at that size.
const DEFAULT_FAR_DISTANCE := 320.0

var near_distance := DEFAULT_NEAR_DISTANCE
var mid_distance := DEFAULT_MID_DISTANCE
var far_distance := DEFAULT_FAR_DISTANCE
## When false the policy always reports NEAR, which restores the pre-LOD
## presentation exactly. Used to A/B the feature and by runners that assert the
## unbatched node structure.
var enabled := true


func configure(near: float, mid: float, far: float) -> void:
	# Kept monotonic so a mis-set threshold cannot silently invert the tiers.
	near_distance = maxf(0.0, near)
	mid_distance = maxf(near_distance, mid)
	far_distance = maxf(mid_distance, far)


## `battalion_visible` is the battalion node's own visibility. An invisible
## battalion is CULLED regardless of distance - it draws nothing, so there is no
## reason to keep its skeletons animating.
func classify(distance: float, battalion_visible: bool = true) -> Tier:
	if not enabled:
		return Tier.NEAR
	if not battalion_visible:
		return Tier.CULLED
	if not is_finite(distance) or distance < 0.0:
		# A camera that has not produced a usable distance yet must not silently
		# demote presentation. Draw everything until it does.
		return Tier.NEAR
	if distance < near_distance:
		return Tier.NEAR
	if distance < mid_distance:
		return Tier.MID
	if distance < far_distance:
		return Tier.FAR
	return Tier.CULLED


## Does this tier draw the full skinned GLB subtree?
static func draws_skinned_geometry(tier: Tier) -> bool:
	return tier == Tier.NEAR or tier == Tier.MID


## Does this tier draw members as MultiMesh instances of the baked static pose?
static func draws_batched_instances(tier: Tier) -> bool:
	return tier == Tier.FAR


## Per-member contact shadow decals.
static func draws_member_decals(tier: Tier) -> bool:
	return tier == Tier.NEAR


## Per-member selection rings and health bars (legal-safe content only).
static func draws_member_overlays(tier: Tier) -> bool:
	return tier == Tier.NEAR


## May looping skeletal animation stop advancing at this tier? One-shot states
## (death, attack) are never culled regardless - see member_render_batcher.gd,
## whose callers depend on those clips actually reaching their end.
static func culls_looping_animation(tier: Tier) -> bool:
	return tier == Tier.FAR or tier == Tier.CULLED


static func tier_name(tier: Tier) -> String:
	match tier:
		Tier.NEAR:
			return "near"
		Tier.MID:
			return "mid"
		Tier.FAR:
			return "far"
		Tier.CULLED:
			return "culled"
	return "unknown"
