class_name RetailSelectionPick
extends RefCounted
## Retail-shaped mouse picking for units and structures.
##
## SAGE authors a selection/collision volume on every Object as a Geometry block
## (``Geometry = CYLINDER|BOX|SPHERE`` with ``GeometryMajorRadius``,
## ``GeometryMinorRadius``, ``GeometryHeight``) plus optional
## ``AdditionalGeometry`` pieces carrying a ``GeometryOffset``. Retail hit-tests
## the click against that footprint. It never uses a flat "everything within N
## world units" radius, which is what this slice used to do: a hardcoded 5.5
## (8.0 fortress) world-unit structure radius and a 6.0 world-unit battalion
## radius, on a battlefield whose player starts are normalised to 76 world units
## apart. On Fords of Isen the map transform scale is 76 / 2868.7 source units =
## 0.0265, so a Gondor barracks (45 x 50 source half-extents) is only 1.2 x 1.3
## world units of footprint while the pick circle was 5.5 -- and the camera only
## shows ~8.7 world units of ground across at minimum zoom. The pick circle was
## therefore wider than the visible battlefield.
##
## Radii here are SELECTION ONLY. Economy land-claim rings
## (TerrainResourceBehavior Radius, farms/mallorn effectiveness), rally points
## and aura/leadership radii are separate systems and are untouched.

## Below this a footprint is unclickable in practice; keeps tiny props and
## single-model objects reachable without inflating real footprints.
const MINIMUM_SELECTION_RADIUS := 0.32
## World-unit forgiveness added to a resolved footprint so a click that lands on
## the silhouette edge still hits. Deliberately small.
const SELECTION_MARGIN := 0.12
## Extra forgiveness for right-click attack targeting only. INVENTED — there
## is no retail oracle for this pad (OpenSAGE hit-tests the authored collider
## with no extra world-unit margin). 0.15 is one notch looser than
## SELECTION_MARGIN and stays inside the thinnest authored body in the slice
## (GondorArcher GeometryMajorRadius 8.0 → 0.21 world at the Fords scale).
## The old 0.45 was more than double that hitbox and lit the attack cursor
## over empty ground beside the target.
const ORDER_MARGIN := 0.15
## Retail infantry author `GeometryMajorRadius = 8.0`; used when a pack document
## carries no geometry for a horde member.
const DEFAULT_MEMBER_SOURCE_RADIUS := 8.0
## Conservative source-unit footprint for a structure with neither pack geometry
## nor a loaded visual. Roughly a Gondor barracks (45 x 50 half-extents).
const DEFAULT_STRUCTURE_SOURCE_RADIUS := 50.0
## Retail fortresses are the largest footprint in the slice. This is the
## AUTHORED main-body half-extent, verbatim:
##   object/goodfaction/structures/men/fortress.ini:1254-1256
##       Geometry              = BOX
##       GeometryMajorRadius   = 64
##       GeometryMinorRadius   = 64
## (pure RotWK 2.01 tree; every faction fortress authors the same 64.)
##
## It was 120.0 - a value retail authors nowhere, reached by adding the
## expansion plot ring to the main body. The plots are separate
## `AdditionalGeometry` pieces at `GeometryOffset = X:64.0 Y:-64.0` with their
## own 10.0 radius (fortress.ini:1259-1265); they are part of the union
## footprint the compiled geometry now carries, so folding them into the
## NO-GEOMETRY fallback double-counted them and made the least-informed case
## the most generous one. This constant is a floor for packs that ship no
## geometry at all; the real footprint always wins when it is present.
const DEFAULT_FORTRESS_SOURCE_RADIUS := 64.0


static func source_footprint_radius(geometry: Dictionary) -> float:
	## Union footprint radius, in retail SOURCE units, of a compiled geometry
	## document (`registration.gameplay.geometry`). Returns 0.0 when the
	## document carries nothing usable so callers can fall back.
	if geometry.is_empty():
		return 0.0
	var footprint: Dictionary = geometry.get("footprint", {}) as Dictionary
	var radius := float(footprint.get("radius", 0.0))
	if is_finite(radius) and radius > 0.0:
		return radius
	# No precomputed union: prefer the authored pieces (primary + Additional
	# Geometry at GeometryOffset) so a barracks whose document still has the
	# CYLINDER 8.0 rally probe as majorRadius is picked on the 45x50 body,
	# not the probe. Primary-only documents keep the old single-span fallback.
	var union := source_union_radius_from_pieces(geometry)
	if union > 0.0:
		return union
	var major := absf(float(_geometry_number(geometry, "majorRadius")))
	var minor := absf(float(_geometry_number(geometry, "minorRadius")))
	var span := maxf(major, minor)
	return span if is_finite(span) and span > 0.0 else 0.0


static func source_union_radius_from_pieces(geometry: Dictionary) -> float:
	## Same union as the importer's `_geometry_footprint`: max(|offset| + span)
	## on each ground axis, then the larger half-extent (not the half-diagonal).
	var pieces: Variant = geometry.get("pieces", [])
	if typeof(pieces) != TYPE_ARRAY or (pieces as Array).is_empty():
		return 0.0
	var half_x := 0.0
	var half_y := 0.0
	var measured := false
	for piece_value in pieces as Array:
		if typeof(piece_value) != TYPE_DICTIONARY:
			continue
		var piece := piece_value as Dictionary
		var major := absf(float(_geometry_number(piece, "majorRadius")))
		var minor := absf(float(_geometry_number(piece, "minorRadius")))
		if major <= 0.0 and minor <= 0.0:
			continue
		if minor <= 0.0:
			minor = major
		if major <= 0.0:
			major = minor
		var offset: Dictionary = piece.get("offset", {}) as Dictionary
		var offset_x := float(offset.get("x", 0.0)) if not offset.is_empty() else 0.0
		var offset_y := float(offset.get("y", 0.0)) if not offset.is_empty() else 0.0
		half_x = maxf(half_x, absf(offset_x) + major)
		half_y = maxf(half_y, absf(offset_y) + minor)
		measured = true
	if not measured:
		return 0.0
	var span := maxf(half_x, half_y)
	return span if is_finite(span) and span > 0.0 else 0.0


static func effective_world_pick_radius(compiled_world: float, visual_world: float) -> float:
	## Retail Geometry is a floor, never a ceiling against the body the player
	## can see. A fortress keep whose intact AABB outruns the 64-unit box
	## (fortress.ini:1254-1306 contact points at X:-90 Y:82) must pick at the
	## visual edge.
	var compiled := compiled_world if is_finite(compiled_world) and compiled_world > 0.0 else 0.0
	var visual := visual_world if is_finite(visual_world) and visual_world > 0.0 else 0.0
	return maxf(compiled, visual)


static func geometry_piece_candidates(
	entity_id: int,
	origin: Vector2,
	geometry: Dictionary,
	source_scale: float
) -> Array:
	## One pick candidate per authored Geometry / AdditionalGeometry piece.
	## BOX pieces keep their half-extents; CYLINDER / SPHERE stay circular.
	## SAGE ground Y maps onto Godot Z.
	var scale := source_scale if is_finite(source_scale) and source_scale > 0.0 else 0.0
	if entity_id == 0 or scale <= 0.0:
		return []
	var pieces: Variant = geometry.get("pieces", [])
	if typeof(pieces) != TYPE_ARRAY:
		return []
	var candidates: Array = []
	for piece_value in pieces as Array:
		if typeof(piece_value) != TYPE_DICTIONARY:
			continue
		var piece := piece_value as Dictionary
		var major := absf(float(_geometry_number(piece, "majorRadius")))
		var minor := absf(float(_geometry_number(piece, "minorRadius")))
		if major <= 0.0 and minor <= 0.0:
			continue
		if minor <= 0.0:
			minor = major
		if major <= 0.0:
			major = minor
		var offset: Dictionary = piece.get("offset", {}) as Dictionary
		var world := origin + Vector2(
			float(offset.get("x", 0.0)) * scale,
			float(offset.get("y", 0.0)) * scale
		)
		var shape := String(piece.get("shape", "CYLINDER")).to_upper()
		var candidate := {
			"id": entity_id,
			"position": world,
			"shape": shape,
			"radius": maxf(MINIMUM_SELECTION_RADIUS, maxf(major, minor) * scale + SELECTION_MARGIN),
		}
		if shape == "BOX":
			candidate["half_x"] = maxf(MINIMUM_SELECTION_RADIUS, major * scale + SELECTION_MARGIN)
			candidate["half_z"] = maxf(MINIMUM_SELECTION_RADIUS, minor * scale + SELECTION_MARGIN)
		candidates.append(candidate)
	return candidates


static func _geometry_number(geometry: Dictionary, key: String) -> float:
	var value: Variant = geometry.get(key, null)
	if typeof(value) == TYPE_DICTIONARY:
		return float((value as Dictionary).get("value", 0.0))
	if typeof(value) == TYPE_FLOAT or typeof(value) == TYPE_INT:
		return float(value)
	return 0.0


static func world_radius_from_source(source_radius: float, source_scale: float) -> float:
	## Project a source-unit footprint into battlefield world units.
	if not is_finite(source_radius) or source_radius <= 0.0:
		return 0.0
	var scale := source_scale if is_finite(source_scale) and source_scale > 0.0 else 0.0
	if scale <= 0.0:
		return 0.0
	return maxf(MINIMUM_SELECTION_RADIUS, source_radius * scale + SELECTION_MARGIN)


static func world_radius_from_visual_bounds(bounds: AABB, uniform_scale: float) -> float:
	## Fallback used when the selected pack carries no compiled geometry: take
	## the model's own horizontal bounds. The larger half-extent (not the
	## half-diagonal) keeps the circle tight against the silhouette instead of
	## bulging past its corners.
	var scale := uniform_scale if is_finite(uniform_scale) and uniform_scale > 0.0 else 0.0
	if scale <= 0.0:
		return 0.0
	var half_x := absf(bounds.size.x) * 0.5 * scale
	var half_z := absf(bounds.size.z) * 0.5 * scale
	var span := maxf(half_x, half_z)
	if not is_finite(span) or span <= 0.0:
		return 0.0
	return maxf(MINIMUM_SELECTION_RADIUS, span + SELECTION_MARGIN)


static func closest_hit(point: Vector2, candidates: Array, extra_margin: float = 0.0) -> int:
	## Nearest candidate whose footprint actually contains the click.
	##
	## `candidates` rows are {"id": int, "position": Vector2, "radius": float}.
	## Ties break on how deeply the click sits inside the footprint (relative to
	## that footprint's own size), so a battalion standing on a fortress plate
	## still wins over the fortress.
	var best_id := 0
	var best_score := INF
	var margin := extra_margin if is_finite(extra_margin) and extra_margin > 0.0 else 0.0
	for candidate_value in candidates:
		if typeof(candidate_value) != TYPE_DICTIONARY:
			continue
		var candidate := candidate_value as Dictionary
		var id := int(candidate.get("id", 0))
		if id == 0:
			continue
		var radius := float(candidate.get("radius", 0.0))
		if not is_finite(radius) or radius <= 0.0:
			continue
		radius = maxf(radius, MINIMUM_SELECTION_RADIUS) + margin
		var position := Vector2(candidate.get("position", Vector2.ZERO))
		var delta := point - position
		var score := 0.0
		if String(candidate.get("shape", "")) == "BOX":
			var half_x := float(candidate.get("half_x", radius)) + margin
			var half_z := float(candidate.get("half_z", radius)) + margin
			if not is_finite(half_x) or half_x <= 0.0 or not is_finite(half_z) or half_z <= 0.0:
				continue
			if absf(delta.x) > half_x or absf(delta.y) > half_z:
				continue
			score = Vector2(delta.x / half_x, delta.y / half_z).length()
		else:
			var distance := delta.length()
			if distance > radius:
				continue
			score = distance / radius
		if score < best_score:
			best_score = score
			best_id = id
	return best_id
