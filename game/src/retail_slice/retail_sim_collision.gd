extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Movement collision carved out of retail_slice_sim.gd (drawer 20): structure footprints, castle pass-through, gate discs, deflection and tangential slides, eviction, reform/turn-rate/heading, route stepping, crush/trample, knockback recovery.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _structure_footprint_radius(structure_row: Dictionary) -> float:
	## The structure's BOUNDING-CIRCLE radius in sim units — SAGE's
	## `FROM_BOUNDINGSPHERE_2D` radius, not the movement block radius.
	##
	## THE TWO RADII ARE DIFFERENT NUMBERS AND BOTH ARE CORRECT.
	## sim.STRUCTURE_BLOCK_RADIUS is a MOVEMENT footprint: placement radius plus a
	## step of walkway, so units path politely around finished buildings (a
	## fortress is 4.6). This is the AUTHORED Geometry block: MenFortress is
	## `Geometry = BOX / GeometryMajorRadius = 64` plus four AdditionalGeometry
	## plot pieces of radius 10 at GeometryOffset 64/-64
	## (object/goodfaction/sim.structures/men/fortress.ini:1254-1265), which the
	## importer projects into a union `footprint.radius` of 74 source units —
	## 1.9604 sim at the Fords of Isen II transform 0.02649232738129. Using the
	## movement radius here would hand every weapon in the game 2.6 extra units of
	## reach against a fortress.
	##
	## RESOLUTION ORDER: an explicit row value (fixtures, and any future seeding
	## that wants to pin a footprint) -> the selected pack's compiled geometry via
	## ContentDB -> a fallback, see sim.COMBAT_FALLBACK_STRUCTURE_SOURCE_RADIUS.
	##
	## Returns 0.0 (no expansion, i.e. the old centre-to-centre behaviour) when
	## the map carries no source transform, so a fixture that never set one is
	## never handed a 74-SIM-unit disc.
	##
	## MEMOISED PER STRUCTURE ID. This is on the per-tick attack path — the range
	## gate calls it for every attacker against every structure target, every
	## tick — and the resolution underneath it built a formatted string key on
	## every call. The inputs (the row's authored value, its source object id,
	## its kind, and the map transform) are all fixed for a structure's lifetime,
	## so the result is cached against the integer structure id, which allocates
	## nothing. Cleared by setup() and by _restore_authoritative_state(), the two
	## places the structure table is replaced wholesale.
	if structure_row.is_empty():
		return 0.0
	var structure_id := int(structure_row.get("id", 0))
	if structure_id != 0 and sim._structure_footprint_radius_cache.has(structure_id):
		return float(sim._structure_footprint_radius_cache[structure_id])
	var scale = float(sim._rules.get("source_map_transform_scale", 0.0))
	if not is_finite(scale) or scale <= 0.0:
		return 0.0
	var source_radius := float(structure_row.get("footprint_radius_source", 0.0))
	if not is_finite(source_radius) or source_radius <= 0.0:
		source_radius = _resolved_footprint_source_radius(
			String(structure_row.get("source_object_id", "")),
			String(structure_row.get("structure_kind", "")),
		)
	if not is_finite(source_radius) or source_radius <= 0.0:
		return 0.0
	var radius = source_radius * scale
	if structure_id != 0:
		sim._structure_footprint_radius_cache[structure_id] = radius
	return radius


## Source-unit footprint used when a structure document carries no compiled
## geometry at all. NON-FORTRESS ONLY; a fortress keeps
## sim.SelectionPick.DEFAULT_FORTRESS_SOURCE_RADIUS (64.0), which is MenFortress's
## authored GeometryMajorRadius verbatim.
##
## WHY IT IS NOT 50.0 ANY MORE. The selection round published 50.0 "roughly a
## Gondor barracks", and for SELECTION that direction is forgiving: an oversized
## pick radius makes a building easier to click. On the COMBAT path the same
## number is a gift of free weapon reach and free acquisition range against
## exactly the sim.structures whose real size is unknown. Censused across every
## playable-structure document in every pack on disk
## (workspace/scratch/opus29-footprint-census.txt, 196 documents that carry
## geometry): the median authored radius is 48, the 10th percentile is 15, and
## 104 of the 196 are BELOW 50. A 50.0 fallback over-expands most of them.
##
## 5.0 IS A FLOOR WITH A DERIVATION, not a smaller guess. The fallback must
## never exceed a structure's true footprint, or it hands out reach the geometry
## does not support; the greatest value that satisfies that for every structure
## the packs ship is the SMALLEST authored radius, and that is 5.0 — the
## fortress expansion pads (Dwarven/Isengard/Men/Mordor/Wild
## FortressExpansionPad{Corner,Side}, same census). Erring small degrades toward
## the pre-round-20 centre-to-centre behaviour, which is the safe direction.
##
## HOW OFTEN IT FIRES, measured rather than assumed: 6 of the 182 structure
## documents in the current workspace selection carry no geometry — MenWallGate,
## DwarvenCastleWallGate, Isengard/Mordor/Wild LumberMill. The stale
## bfme2-men-vslice supplemental has 22 more, all superseded by rotwk-men-vslice.


func _resolved_footprint_source_radius(source_object_id: String, structure_kind: String) -> float:
	## MEMO KEY IS THE EXACT ID, not a lowered one. ContentDB's registry lookup
	## is an exact Dictionary hit (see get_playable_structure_runtime), so a
	## lowered memo key answered for a DIFFERENT string than the one the lookup
	## would have used: two ids differing only in case shared one memo entry
	## while resolving differently — one hitting the document, one missing it and
	## taking the fallback. Keying on the same string the lookup uses makes the
	## memo incapable of disagreeing with the thing it memoises.
	var key := "%s|%s" % [source_object_id, structure_kind]
	if sim._structure_footprint_source_cache.has(key):
		return float(sim._structure_footprint_source_cache[key])
	var resolved := 0.0
	if source_object_id != "":
		var db = sim._content_db_ref()
		if db != null and db.has_method("get_playable_structure_runtime"):
			var document: Variant = db.get_playable_structure_runtime(source_object_id)
			if typeof(document) == TYPE_DICTIONARY:
				var gameplay: Dictionary = (
					((document as Dictionary).get("registration", {}) as Dictionary)
					.get("gameplay", {}) as Dictionary
				)
				var geometry: Variant = gameplay.get("geometry", {})
				if typeof(geometry) == TYPE_DICTIONARY:
					resolved = sim.SelectionPick.source_footprint_radius(geometry as Dictionary)
	if not is_finite(resolved) or resolved <= 0.0:
		# NAMED, ONCE PER OBJECT ID. The fallback used to fire in total silence,
		# so a pack shipped without geometry looked exactly like a pack with it.
		# Once per id (not per call) keeps a per-tick path from flooding the log.
		if source_object_id != "" and not sim._footprint_fallback_reported.has(source_object_id):
			sim._footprint_fallback_reported[source_object_id] = true
			push_warning(
				"structure footprint: '%s' (kind=%s) carries no compiled geometry; using the %s source-unit fallback"
				% [
					source_object_id,
					structure_kind,
					"fortress" if structure_kind == "fortress" else "non-fortress",
				]
			)
		resolved = (
			sim.SelectionPick.DEFAULT_FORTRESS_SOURCE_RADIUS
			if structure_kind == "fortress"
			else sim.COMBAT_FALLBACK_STRUCTURE_SOURCE_RADIUS
		)
	sim._structure_footprint_source_cache[key] = resolved
	return resolved


func _target_footprint_radius(target_id: int, target_kind: String) -> float:
	## The radius to subtract from a centre-to-centre distance before comparing it
	## with a weapon range.
	##
	## STRUCTURES ONLY, DELIBERATELY. Real SAGE subtracts BOTH objects' bounding
	## radii, and every horde member in the selected pack authors
	## `GeometryMajorRadius = 8.0` (measured across every
	## data/playable-units/*.json in the men pack). It is not applied here, for a
	## reason that is about this sim's model rather than about SAGE:
	##
	##   The sim's authoritative unit position is the HORDE CENTRE, and the range
	##   gate is horde-centre to horde-centre. SAGE's bounding-sphere test is
	##   between two individual SOLDIER objects. Subtracting 2 x 8 source units
	##   from a horde-centre distance applies a soldier-scale correction to a
	##   horde-scale measurement — it would move every engagement 0.42 sim units
	##   earlier without matching anything retail does. `_step_member_attacks`
	##   (this file) never range-tests a member at all; members exist in the
	##   combat path only through `_member_world_position`, and only to assign
	##   victims. Doing this properly means moving the gate itself down to the
	##   member level, which is its own change with its own failing-first evidence
	##   and its own re-derivation of the member-combat suite's 98 authored
	##   expectations.
	##
	## A structure has no such gap: it is a single object, its authoritative
	## position IS its centre, and its authored Geometry IS the bounding circle
	## SAGE measures from. The correction applies exactly.
	if target_kind != "structure":
		return 0.0
	return _structure_footprint_radius(sim.structures.get(target_id, {}) as Dictionary)


## Hysteresis width added to a castle member's own block radius when deciding
## whether the walking line crosses it. The corridor is recomputed every tick
## from the unit's current position, so a zero-width test would let a member
## flip to BLOCKING while the unit is still inside its disc — it would close on
## top of the unit rather than behind it.
##
## OWN CONSTANT, WITH ITS OWN DERIVATION. Round 18 wrote this as an alias of
## sim.BATTALION_SEPARATION_PUSH, which made the two move together for no reason:
## the separation push is a per-tick displacement between two battalions, this
## is a geometric tolerance on a segment/disc test. They are numerically equal
## by coincidence, and the alias hid the actual bound.
##
## DERIVED from the one castle the pack ships (MenFortress on Fords of Isen II,
## measured in workspace/scratch/opus24-probe1.out.log): every castle piece
## carries the default 2.8 sim.STRUCTURE_BLOCK_RADIUS; attacking the fortress centre
## from outside puts the furthest corner pad 2.398 off the walking line, and
## attacking an east corner pad from the east puts the two west pads 3.392 off
## it (recomputed to full precision this round: 3.391 and the corner spacing
## 4.796 — see _test_castle_corridor_is_bounded). The margin must therefore
## satisfy
##     2.398 - 2.8 < margin < 3.391 - 2.8   i.e.   (negative) < margin < 0.591
## — the lower bound is already met by any non-negative value because 2.398 is
## inside the bare radius, so the binding constraint is the upper one. It must
## also exceed one tick of travel or the hysteresis buys nothing.
##
## ONE TICK OF TRAVEL, RECONCILED (round 21). This file carried two different
## answers to that question — "~0.03" here and "0.55" at the transit budget in
## _deflect_around_structures — and neither was right. 0.55 was the castle
## fixture's mis-scaled step; 0.03 is a factor of ten under the real figure, and
## reads like a per-tick value derived from an already-per-tick speed. The
## measured answer, from every playable-unit document in the workspace selection
## (159 rows with a resolved speed): median 55 source units/second = 0.1457 sim
## per tick, ceiling 115 source = 0.3047 sim per tick. Both derivations now cite
## this same census.
##
## 0.35 still holds, and now for a stated reason: it clears the 0.3047 ceiling
## (so the corridor cannot close on a unit mid-step even at the game's top
## authored speed) while sitting 0.24 under the 0.591 geometric ceiling above.
const CASTLE_CORRIDOR_MARGIN := 0.35


func _point_segment_distance(point: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var length_squared := ab.length_squared()
	if length_squared <= 0.000001:
		return point.distance_to(a)
	var t := clampf((point - a).dot(ab) / length_squared, 0.0, 1.0)
	return point.distance_to(a + ab * t)


func _castle_footprint_pass_through(position: Vector2, attack_target_id: int, attack_target_kind: String) -> Dictionary:
	## The set of structure ids a battalion standing at `position` and attacking
	## `attack_target_id` may walk through: the target itself, plus exactly those
	## members of the target's castle whose footprint the WALKING LINE from the
	## unit to the target actually crosses.
	##
	## BOUNDED, not blanket. Round 17 opened the ENTIRE castle group on any
	## attack order onto any member of it, which made a far-side attack dissolve
	## the near wall as well and left the group open for as long as the order
	## lasted. This model opens only what is in the way, is recomputed every tick
	## from the unit's current position, and closes behind it.
	##
	## WHY THE WHOLE CASTLE, NOT JUST THE TARGET. CastleBehavior authors its
	## pieces INSIDE the fortress footprint, not around it: MenFortressCitadel
	## sits on the fortress origin (offset_source 0,0) and the six expansion
	## pads within 64 source units of it. At the Fords of Isen II transform
	## (0.02649232738129) that is a citadel exactly on the fortress centre and
	## pads 1.64-2.40 sim units out — measured live, every enemy castle piece in
	## workspace/scratch/opus24-probe1.out.log. Each piece carries the default
	## 2.8 sim.STRUCTURE_BLOCK_RADIUS, so exempting only the ordered target left the
	## fortress ringed by a ~5.2-unit wall of its own sub-sim.structures. Retail melee
	## ranges are ~11.5 source units = 0.305 sim, and the MOVEMENT ring is 5.2, so
	## no melee horde could ever reach a fortress or a pad — that gap is far wider
	## than the 1.9604 footprint the range gate now subtracts (round 20), so the
	## corridor is still required: they parked on the ring at distance 4.4-5.1 in state
	## `run` and only ranged units ever landed a blow
	## (workspace/scratch/opus09-live1.out.log:35,52 — 17,521 ticks to kill a
	## fortress, all of it archer damage).
	##
	## THE RULE: a member is passable only if the segment [unit -> target centre]
	## comes within `member block radius + CASTLE_CORRIDOR_MARGIN` of that
	## member's centre — i.e. the walking line actually crosses its footprint.
	## The target itself is always passable (that is the order). Everything
	## outside the group deflects normally, and a battalion with no STRUCTURE
	## attack target (every plain move order, friendly castles included) gets an
	## empty set on the first line, so that path stays byte-identical.
	##
	## MEASURED against the one castle the pack ships (MenFortress on Fords of
	## Isen II, workspace/scratch/opus24-probe1.out.log: fortress radius 4.6 at
	## the origin, citadel radius 2.8 exactly on it, two side pads 1.643 out and
	## four corner pads 2.398 out, all radius 2.8):
	##   attacking the fortress centre from outside  -> every piece is on the
	##     line (0.000-2.398 <= 2.8 + 0.35), so the whole group opens, which is
	##     correct: they genuinely overlap the target.
	##   attacking the EAST corner pad from the east -> the two WEST pads sit
	##     3.391 and 4.796 off the line, above the 3.15 threshold, and stay
	##     BLOCKING. Under round 17 they opened too.
	## Those numbers are the bound: 2.398 passes, 3.391 does not.
	##
	## CORRECTION (round 19): round 18 reported BOTH west pads at "3.392". They
	## are not equidistant and neither figure was exact. At the retail transform
	## 0.02649232738129 a 64-source pad offset is 1.6955090 sim units, so a corner
	## pad sits 2.3978 out along its diagonal. With the attacker on the
	## fortress->east-corner ray, the SW pad's nearest point on the SEGMENT is the
	## east pad endpoint at 2 * 1.6955090 = 3.3910179; the NW pad lies on the
	## infinite line but on the far side of the fortress, so the segment clamps to
	## the same endpoint and it measures 3.3910179 * sqrt(2) = 4.7956. Both are
	## now asserted to 0.001 in _test_castle_corridor_is_bounded instead of being
	## quoted from a report.
	##
	## ID-ALIAS GUARD: battalion ids and structure ids come from the same counter
	## space but are separate tables, so a battalion target whose id happens to
	## match a structure id would have opened that structure. The caller passes
	## the row's `target_kind` and anything but "structure" returns empty.
	var passable: Dictionary = {}
	if attack_target_kind != "structure" or attack_target_id == 0 or not sim.structures.has(attack_target_id):
		return passable
	passable[attack_target_id] = true
	var target_row: Dictionary = sim.structures[attack_target_id]
	var target_center := Vector2(target_row.get("position", Vector2.ZERO))
	# Three ways into the same group: the castle owner itself, one of its
	# CastleBehavior pieces, or an expansion raised on one of its pads.
	var castle_id := int(target_row.get("castle_piece_of_fortress", 0))
	if castle_id == 0:
		castle_id = int(target_row.get("expansion_of_fortress", 0))
	if castle_id == 0 and (
		target_row.has("castle_piece_structure_ids")
		or String(target_row.get("structure_kind", "")) == "fortress"
	):
		castle_id = attack_target_id
	if castle_id == 0 or not sim.structures.has(castle_id):
		return passable
	var members: Array[int] = [castle_id]
	for piece_value in (sim.structures[castle_id] as Dictionary).get("castle_piece_structure_ids", []) as Array:
		members.append(int(piece_value))
	for pad_value in sim.expansion_pads.get(castle_id, []) as Array:
		var expansion_structure_id := int((pad_value as Dictionary).get("expansion_structure_id", 0))
		if expansion_structure_id != 0:
			members.append(expansion_structure_id)
	for member_id in members:
		if passable.has(member_id) or not sim.structures.has(member_id):
			continue
		var member_row: Dictionary = sim.structures[member_id]
		var member_radius = float(sim.STRUCTURE_BLOCK_RADIUS.get(
			String(member_row.get("structure_kind", "")), 2.8
		))
		var member_center := Vector2(member_row.get("position", Vector2.ZERO))
		if _point_segment_distance(member_center, position, target_center) <= member_radius + CASTLE_CORRIDOR_MARGIN:
			passable[member_id] = true
	return passable


## The furthest a footprint may move a unit in one tick. Deflection used to
## SNAP: a unit sitting on a fortress centre was projected 4.6 units in a single
## step, which is a teleport, and a unit sitting EXACTLY on the centre was
## skipped entirely by the `distance > 0.001` guard and stayed clipped forever.
## Both are reachable now that an attack order can put a melee horde inside a
## castle footprint and then end (stop, retarget, target death), at which point
## the exemption disappears and the unit has to be evicted. Bounding the
## displacement makes that eviction a walk, not a jump; the unit keeps being
## pushed every tick until it is clear.


func _castle_gate_blocking_discs(structure_row: Dictionary, mover: Dictionary) -> Array[Dictionary]:
	var policy: Dictionary = structure_row.get("gate_behavior", {})
	var geometries: Dictionary = structure_row.get("gate_geometries", {})
	if policy.is_empty() or geometries.is_empty():
		return []
	var use_open_geometry := bool(policy.get("pathing_open", false))
	var same_team := int(mover.get("team", -1)) == int(structure_row.get("team", -2))
	if structure_row.has("fake_pathfind_portal"):
		var portal: Dictionary = structure_row.get("fake_pathfind_portal", {})
		if use_open_geometry and not same_team and not bool(portal.get("allow_enemies", false)):
			# FakePathfindPortalBehaviour AllowEnemies=No: hostiles never get
			# the passage even while open; they must destroy the gate (L3 pin).
			use_open_geometry = false
		elif not use_open_geometry:
			# Retail's fake pathfind PORTAL is a shortcut THROUGH a closed
			# gate: qualified movers path as if it were open and AIGateUpdate
			# swings it before they arrive. AllowNonSkirmishAIUnits=No
			# (helmsdeepbuildings.ini:6288) reserves that shortcut for
			# skirmish-AI-controlled friendlies; a human owner's troops wait
			# for the doors like retail. An OPEN gate is never impassable for
			# its owner - the rule only widens closed-gate passage.
			if same_team and (bool(portal.get("allow_non_skirmish_ai", false)) or (sim.ai_enabled and sim.team_is_ai(int(mover.get("team", -1))))):
				use_open_geometry = true
			elif not same_team and bool(portal.get("allow_enemies", false)):
				use_open_geometry = true
	var geometry_names: Array[String] = []
	if use_open_geometry:
		geometry_names.assign(["OpenLeft", "OpenRight"])
	else:
		geometry_names.append("Closed")
	var scale = float(sim._rules.get("source_map_transform_scale", 0.1))
	var facing := float(structure_row.get("facing_radians", 0.0))
	var origin := Vector2(structure_row.get("position", Vector2.ZERO))
	var discs: Array[Dictionary] = []
	for geometry_name in geometry_names:
		var geometry: Dictionary = geometries.get(geometry_name, {})
		if geometry.is_empty() or String(geometry.get("shape", "")).to_upper() != "BOX":
			continue
		var major = float(geometry.get("majorRadius", 0.0)) * scale
		var minor = float(geometry.get("minorRadius", 0.0)) * scale
		if major <= 0.0 or minor <= 0.0:
			continue
		var long_radius := maxf(major, minor)
		var authored_short_radius := minf(major, minor)
		# Minkowski-expand the authored box by the battalion's collision body;
		# otherwise a centre-point test lets the formation clip through the leaf.
		var disc_radius = authored_short_radius + sim.BATTALION_SEPARATION_PUSH
		var local_axis := Vector2.RIGHT if major >= minor else Vector2.DOWN
		var axis := local_axis.rotated(facing)
		var offset_value: Array = geometry.get("offset", [])
		var local_offset := Vector2.ZERO
		if offset_value.size() == 3:
			local_offset = Vector2(float(offset_value[0]), float(offset_value[1])) * scale
		var center := origin + local_offset.rotated(facing)
		# A long authored BOX is represented by a deterministic chain of discs,
		# never by the old single tiny structure disc. Adjacent discs overlap.
		var span := maxf(0.0, long_radius - authored_short_radius)
		var disc_count := maxi(2, int(ceili(span / maxf(disc_radius, 0.001))) + 1)
		for disc_index in range(disc_count):
			var along := 0.0 if disc_count == 1 else lerpf(-span, span, float(disc_index) / float(disc_count - 1))
			discs.append({"center": center + axis * along, "radius": disc_radius})
	return discs


func _deflect_around_structures(
	position: Vector2,
	row: Dictionary,
	travel_step: Vector2 = Vector2.ZERO,
	structure_id_list: Array[int] = []
) -> Vector2:
	# Battalions slide around building footprints instead of clipping through
	# them. The battalion's own attack target — and, when that target is part of
	# a castle, the members of that castle the walking line actually crosses —
	# is exempt so melee can close in. See _castle_footprint_pass_through.
	#
	# `travel_step` is the displacement this tick ALREADY applied to `position`
	# by _step_route. A non-zero value selects the TANGENTIAL SLIDE below; the
	# stationary eviction pass passes zero and keeps the radial push.
	#
	# `structure_id_list` used to let a caller hoist sim.structure_ids() out of a
	# multi-entity pass. The spatial-hash gather is a complete, cheaper
	# substitute for that full list (a centre outside the gathered box cannot
	# overlap any blocking disc), so a provided list is ignored — honouring
	# it would let the eviction hoist bypass the broad-phase.
	var attack_target_id := int(row.get("target_id", 0))
	var attack_target_kind := String(row.get("target_kind", "battalion"))
	var passable := _castle_footprint_pass_through(
		position,
		attack_target_id,
		attack_target_kind,
	)
	# BROAD-PHASE (lane L2b item 6): only sim.structures whose blocking disc can
	# overlap `position` are visited, in the same ascending id order the full
	# scan used. A centre outside the gathered box is further than the maximum
	# block radius away, and the loop below skips such rows with no side
	# effects, so this is byte-identical to scanning sim.structure_ids().
	var ids: Array[int] = sim._structure_ids_near(position)
	# TOTAL push bound. Round 18 clamped each structure's push to
	# sim.STRUCTURE_EVICTION_STEP separately, so N overlapping discs could compound
	# into an N * step jump in one tick — and overlapping discs are the NORMAL
	# case inside a castle, where the citadel sits exactly on the fortress centre
	# and six pads sit 1.64-2.40 out with 2.8 radii. A unit on the fortress
	# origin is inside four of them at once. The budget below is spent across all
	# of them, so eviction is one step per tick however many footprints claim it.
	#
	# The budget bounds the OUTWARD (radial) component only. A tangential slide
	# is not displacement the sim is inventing — it is the unit's own travel
	# step redirected along the disc — and charging it to the eviction budget
	# left a unit that walked in at full speed unable to recover its clearance in
	# the same tick.
	#
	# A TRANSIT step raises the budget to its own length. sim.STRUCTURE_EVICTION_STEP
	# exists so a unit that is already deep inside a footprint WALKS out instead
	# of teleporting; it was never meant to stop a moving unit from undoing the
	# penetration it just created. Recovering at most exactly what this tick
	# moved is still not a teleport.
	#
	# RE-DERIVED FROM A CORRECTED MEASUREMENT (round 21). Round 19 justified this
	# with "a slice melee steps 0.55 per tick at the retail transform". That
	# number was WRONG, and wrong by 3.8x: it came from banner_castle_sim_runner's
	# castle fixture, which swapped the map transform to retail but left its unit
	# rules at the 0.1 scale they were authored for (see _rescale_unit_rules
	# there, fixed in the same round). The AUTHORED ceiling, censused across every
	# playable-unit document in every pack in the workspace selection (159 rows
	# with a resolved speed): the fastest unit in the game authors 115 source
	# units/second — Rivendell Lancers, Haradrim Riders, Warg riders, Knights of
	# Dol Amroth — which at 0.02649232738129 and TICK_SECONDS 0.1 is 0.3047 sim
	# units per tick. The median is 55 source = 0.1457. NOT ONE authored base
	# speed exceeds sim.STRUCTURE_EVICTION_STEP (0.35).
	#
	# SO WHY KEEP IT. Because `travel_step` is not a base speed: _step_route
	# composes it as base * stance speedMultiplier * formation speed_multiplier *
	# ability SPEED modifiers (see the max_speed line there). A mounted or
	# leadership-boosted lancer at any multiplier above 1.149 clears 0.35, and
	# those multipliers are authored data, not a hypothetical. The rule is
	# therefore inert for every unit at base speed and binding exactly where a
	# boosted one would otherwise be left clipped inside a wall it walked into.
	# Deleting it would trade a measured no-op for an unmeasured regression.
	var push_budget = maxf(sim.STRUCTURE_EVICTION_STEP, travel_step.length())
	for structure_id in ids:
		if not sim.structures.has(structure_id):
			continue
		var structure_row: Dictionary = sim.structures[structure_id]
		if int(structure_row.get("health", 0)) <= 0:
			continue
		# Construction sites do not block movement: builders must reach their
		# own site, and scaffolding is passable until the structure completes.
		if float(structure_row.get("construction_progress", 1.0)) < 1.0:
			continue
		var gate_discs := _castle_gate_blocking_discs(structure_row, row)
		if not gate_discs.is_empty():
			for disc in gate_discs:
				var gate_center := Vector2(disc.get("center", Vector2.ZERO))
				var gate_radius := float(disc.get("radius", 0.0))
				var gate_offset := position - gate_center
				var gate_distance := gate_offset.length()
				# Sweep the just-applied travel segment as well as testing its end;
				# fast battalions must not tunnel through a thin Closed door slab.
				if gate_distance >= gate_radius and travel_step.length_squared() > 0.000001:
					var gate_start := position - travel_step
					var gate_t := clampf((gate_center - gate_start).dot(travel_step) / travel_step.length_squared(), 0.0, 1.0)
					var gate_closest := gate_start + travel_step * gate_t
					var closest_offset := gate_closest - gate_center
					if closest_offset.length() < gate_radius:
						position = gate_closest
						gate_offset = closest_offset
						gate_distance = closest_offset.length()
				if gate_distance >= gate_radius:
					continue
				var incoming_offset := position - travel_step - gate_center
				var gate_direction := incoming_offset.normalized() if incoming_offset.length_squared() > 0.000001 else (gate_offset / gate_distance if gate_distance > 0.001 else _eviction_fallback_direction(row))
				var gate_applied := minf(gate_radius - gate_distance, push_budget)
				push_budget -= gate_applied
				var gate_seated_radius := gate_distance + gate_applied
				position = gate_center + gate_direction * gate_seated_radius
				if push_budget <= 0.0:
					break
			continue
		var radius = float(sim.STRUCTURE_BLOCK_RADIUS.get(String(structure_row.get("structure_kind", "")), 2.8))
		if String(row.get("order_kind", "")) == "construct" and structure_id >= sim.CASTLE_FIXTURE_FIRST_ID:
			# Castle props sit densely around their authored build plots. A porter
			# travelling to one uses each fixture's compiled footprint, not the
			# generic 2.8-unit dynamic-building walkway disc that seals the keep.
			var fixture_radius := _structure_footprint_radius(structure_row)
			if fixture_radius > 0.0:
				radius = fixture_radius
		if passable.has(structure_id):
			# THE TARGET'S OWN FOOTPRINT STILL STOPS THE ATTACKER AT ITS WALL.
			#
			# The corridor exists because the MOVEMENT block radius is an inflated
			# walkway ring (a fortress is 4.6 against an authored footprint of
			# 1.9604) and a castle's pieces are authored INSIDE it, so leaving it
			# up walled melee out of every fortress it was ordered onto. But
			# opening it completely let the attacker walk to the target's CENTRE —
			# measured d=0.24 then d=0.00 in the live slice
			# (workspace/scratch/opus24-probe2.out.log).
			#
			# Now that the range gate is surface-to-surface the wall is reachable
			# and standing off it is correct, so the target reasserts its OWN
			# authored footprint. `minf` keeps this a strict relaxation of the
			# ordinary rule: the corridor can never block harder than the plain
			# movement disc would have (relevant for wall towers, whose 98-source
			# geometry projects wider than their 2.2 block radius).
			#
			# CROSSED CASTLE MEMBERS STAY FULLY OPEN. Only the ordered target is
			# re-blocked. A pad or citadel the walking line happens to cross is
			# not what the unit is trying to hit, and re-blocking those would put
			# the fortress's own 1.96 disc between an attacker and a pad standing
			# 1.64 from the fortress centre — unreachable from outside.
			if structure_id != attack_target_id or attack_target_kind != "structure":
				continue
			radius = minf(radius, _structure_footprint_radius(structure_row))
			if radius <= 0.0:
				continue
		var center := Vector2(structure_row.get("position", Vector2.ZERO))
		var offset := position - center
		var distance := offset.length()
		if distance >= radius:
			continue
		var direction := offset / distance if distance > 0.001 else _eviction_fallback_direction(row)
		# Bounded: keep walking out, one step per tick, instead of teleporting to
		# the ring. Ordinary deflection never reaches the clamp — a unit moving at
		# slice speeds penetrates far less than one step per tick — so it only
		# bites on the eviction case it exists for.
		var applied := minf(radius - distance, push_budget)
		push_budget -= applied
		var seated_radius := distance + applied
		position = center + direction * seated_radius
		if travel_step.length_squared() > 0.000001:
			var slide_dest := Vector2(row.get("destination", position))
			var live_route: Array = row.get("route", [])
			if not live_route.is_empty():
				slide_dest = Vector2(live_route[0])
			position = _tangential_slide_point(
				center, seated_radius, direction, travel_step, slide_dest
			)
		if push_budget <= 0.0:
			break
	return position


func _tangential_slide_point(
	center: Vector2,
	radius: float,
	radial_direction: Vector2,
	travel_step: Vector2,
	destination: Vector2 = Vector2.INF
) -> Vector2:
	## TANGENTIAL SLIDE. The radial push alone deadlocks whenever a blocking
	## structure's centre sits on the line of travel: the push
	## `center + offset/|offset| * radius` is then exactly ANTI-PARALLEL to the
	## step, so every tick moves the unit forward by one step and shoves it back
	## onto the same ring point. Measured on the seeded fixture: an attacker at
	## (80, 200) ordered onto a fortress at (100, 200) with a barracks at
	## (90, 200) parks at (87.2, 200.0) — exactly barracks + (-2.8, 0) — with its
	## route still length 1 after 600 ticks. _step_route's 3-tick stall escape
	## never fires because the attack state re-assigns the route every tick,
	## which resets route_stall_ticks before it can reach the threshold.
	##
	## The fix walks the unit AROUND the disc instead of standing it off:
	## project the step onto the tangent at the unit's current bearing and
	## re-seat the result on the ring, so the unit keeps its clearance while
	## making angular progress toward the far side.
	##
	## DETERMINISTIC SIDE CHOICE. Which way round is decided by the sign of the
	## 2-D cross product of the OBSTACLE OFFSET (centre -> unit) with the travel
	## direction. Off-axis that sign is the side the unit is already drifting
	## toward, so the slide never fights the approach. ON-AXIS — the deadlock
	## case — the cross product is zero and a fixed fallback side takes over.
	##
	## WHY A FIXED FALLBACK IS THE RIGHT ANSWER, stated correctly. An earlier
	## version of this comment justified it as "any position-derived tie-break
	## would make two lockstep peers disagree". That is wrong on its face:
	## position IS replicated state, every peer holds the same value, and a
	## position-derived choice would replicate fine. The real reason is
	## NUMERICAL: near the axis the cross product is a difference of two nearly
	## equal products, so its SIGN is the least trustworthy bit in the whole
	## computation — it is exactly the quantity that a fused multiply-add
	## contracts differently on different CPUs, and that ordinary rounding flips
	## from tick to tick on a single CPU. A fixed side is stable under both.
	##
	## THE NEAR-ZERO BAND IS PART OF THE FIX, not padding. Snapping only the
	## exact 0.0 left a band around the axis where |cross| is nonzero but its
	## sign is FMA-dependent: two peers on different microarchitectures compute
	## the same inputs, contract `x1*y2 - y1*x2` differently, and pick opposite
	## sides — the unit walks round the disc clockwise on one peer and
	## counter-clockwise on the other, and the sim desyncs. The lockstep runners
	## cannot catch this: both peers in those tests are the SAME binary on the
	## SAME machine, so they always contract identically and always agree. The
	## band is therefore a hazard that only a heterogeneous match exposes, which
	## is why it is closed by construction rather than by test.
	##
	## 1e-6 is the same tolerance this file already uses for "this vector is
	## degenerate" (`length_squared() <= 0.000001`, both in this function and in
	## _deflect_around_structures), so the geometry has one epsilon, not two.
	## Inside the band the fixed side (+1, counter-clockwise in the sim's X/Z
	## frame) applies; it is a pure function of the two vectors, so every peer
	## computes it identically.
	## Dest-side when a remaining waypoint is known: both lockstep peers hold
	## the same destination, so this does not re-open the FMA sign hazard that
	## forced a fixed +1 on-axis. Walking a 1-point LOS through a building
	## is exactly on-axis; +1 then orbits the long way. The shorter remaining
	## distance is the short side of the disc.
	var cross := radial_direction.cross(travel_step)
	var side := signf(cross) if absf(cross) > 0.000001 else 1.0
	if destination != Vector2.INF:
		var perp := Vector2(-radial_direction.y, radial_direction.x)
		var left := center + radial_direction * radius + perp * travel_step.length()
		var right := center + radial_direction * radius - perp * travel_step.length()
		var left_gap := left.distance_squared_to(destination)
		var right_gap := right.distance_squared_to(destination)
		if absf(left_gap - right_gap) > 0.000001:
			side = 1.0 if left_gap < right_gap else -1.0
	var tangent := Vector2(-radial_direction.y, radial_direction.x) * side
	var slid := center + radial_direction * radius + tangent * travel_step.length()
	var seated := slid - center
	if seated.length_squared() <= 0.000001:
		return center + radial_direction * radius
	return center + seated.normalized() * radius


func _step_structure_eviction() -> void:
	## Standing still is exactly when a footprint has to reassert itself.
	##
	## The castle pass-through ends with the ORDER, not with the route, and
	## `_step_entity` only reaches `_step_route` while a unit is moving: an
	## attacking, idle, stopped, or freshly-untargeted battalion never touches the
	## deflection at all. A melee horde that walked inside a castle to attack it
	## and then stopped, retargeted, or outlived its target therefore sat clipped
	## inside the wall for the rest of the match. This pass runs every tick for
	## every living battalion and pushes it back out at
	## sim.STRUCTURE_EVICTION_STEP per tick.
	##
	## It consults the SAME pass-through set as movement, so a battalion whose
	## order still opens a corridor is not fought against while it is attacking —
	## only once the order ends does the set empty and the walk-out begin.
	##
	## Deterministic: ascending entity id, fixed step, no wall clock, no RNG.
	##
	## HOISTED. sim.structure_ids() allocates a new array and SORTS it on every call;
	## calling it once per entity made this pass O(sim.entities * sim.structures) with a
	## sort inside the entity loop. Structures are neither added nor removed by
	## this pass (it only moves sim.entities), so the id list is stable for its whole
	## duration and _deflect_around_structures re-checks sim.structures.has() anyway.
	var ids: Array[int] = sim.structure_ids()
	if ids.is_empty():
		return
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		if int(row.get("health", 0)) <= 0 or bool(row.get("flying", false)) or sim.entity_container.has(id):
			continue
		if bool(row.get("is_banner_carrier", false)):
			# A BANNER CARRIER HAS NO POSITION OF ITS OWN. It is glued to its
			# parent horde by _sync_banner_entity_transform, which runs in
			# _step_banner_carriers — the pass IMMEDIATELY BEFORE this one, in
			# the same tick (see the tick order at _step_banner_carriers /
			# _step_structure_eviction). Evicting it therefore fights a value
			# that is not the eviction pass's to own: the nudge is overwritten
			# by the next tick's glue, and in the window between the two the
			# authoritative banner position is wrong for the spatial index and
			# for presentation.
			#
			# It is not hypothetical. A horde parked against its own castle wall
			# — the ordinary defensive posture — puts its banner inside a
			# structure footprint, so this pass nudged it EVERY TICK for as long
			# as the horde stood there, and every one of those nudges was
			# discarded by the following tick's glue.
			#
			# The parent horde is still evicted normally; the banner follows it
			# out because it follows it everywhere.
			continue
		if not (row.get("route", []) as Array).is_empty():
			continue  # already deflected this tick inside _step_route
		if int(row.get("production_exit_start_tick", -1)) >= 0:
			# THE DOORWAY IS INSIDE THE FOOTPRINT, BY CONTRACT. QueueProductionExit
			# creates a horde at production_origin + PRODUCTION_DOOR_INSET_RADIUS
			# (0.9) along the exit direction and walks it out to
			# PRODUCTION_EXIT_RADIUS (4.25) — see _step_production. A producer's
			# block radius is 2.6-3.0, so the authored create point is 1.7-2.1
			# units INSIDE its own disc and _step_production_exit owns the unit's
			# position for the whole animation (it lerps origin -> destination
			# every tick and leaves route empty with state "run"). Round 18's pass
			# therefore fought the doorway on every single unit the game produced.
			#
			# MEASURED, and measured PRECISELY — the effect is real but bounded,
			# and overstating it would be as wrong as missing it. In the
			# retail_state_pin fixture the produced horde's AUTHORITATIVE
			# end-of-tick position is exactly sim.STRUCTURE_EVICTION_STEP (0.35) off
			# the authored lerp for the whole time it is inside the producer's
			# disc, and then the two reconverge byte for byte
			# (workspace/scratch/opus26-pin-positions-{new,revert-prodexit}.json.tick*):
			#   t=211  penetration 1.7851 vs 1.4351   (0.3500 apart)
			#   t=214  penetration 1.2681 vs 0.9181   (0.3500 apart)
			#   t=217  penetration 0.5030 vs 0.1530   (0.3500 apart)
			#   t=220  identical, and identical at every later sample
			# It does NOT accumulate, because the next tick's lerp overwrites the
			# nudge — which is exactly why neither pinned hash moves. What it does
			# corrupt is the end-of-tick state everything else reads inside that
			# window: the spatial index, presentation, and any query against the
			# authoritative position while a unit is emerging.
			#
			# The exit is a bounded, self-terminating animation that ends OUTSIDE
			# the footprint; eviction resumes the tick it completes.
			continue
		if _is_engaged_in_range(row):
			# A unit that is attacking something it can actually hit must not be
			# shoved out of its own weapon range. The castle corridor only exempts
			# STRUCTURE targets in the target's own castle group, so a melee horde
			# fighting an enemy BATTALION that happens to stand on a footprint —
			# defenders backed against their own barracks, a fight spilling onto a
			# wall — was evicted out of contact and had to walk back in, every tick.
			continue
		var position := Vector2(row.get("position", Vector2.ZERO))
		# An IDLE battalion is not executing its order, whatever `target_id` still
		# says, so it gets no corridor. Measured need: the live slice parks a unit
		# with attack_range 0.0 in state `idle` at d=0.00 on the enemy fortress
		# CENTRE while still holding it as a target
		# (workspace/scratch/opus25-probe1.out.log:`3:idle:t2001:d0.00`) — the
		# weapon-mode gate returns "unsupported-close", drops the route and idles,
		# but never clears the target, so a permanent corridor kept a unit clipped
		# inside the wall for the whole match. Gating on state, not on target,
		# is what makes "the exemption ends with the order" actually true.
		var executing := String(row.get("state", "")) in ["run", "attack"]
		var evicted := _deflect_around_structures(
			position,
			row if executing else {"facing": row.get("facing", Vector2.ZERO)},
			Vector2.ZERO,
			ids
		)
		if evicted == position:
			continue
		row["position"] = evicted
		sim._spatial_sync(row)


func _is_engaged_in_range(row: Dictionary) -> bool:
	## True when this battalion is in the attack state against a LIVING target it
	## is currently within weapon range of — of ANY kind, battalion or structure.
	## It must use EXACTLY the test _step_attacks uses to enter the state, or a
	## unit that is legitimately engaged gets evicted out of its own weapon range
	## every tick: surface-to-surface against a structure (the target's authored
	## bounding circle subtracted), centre-to-centre against a battalion. See the
	## citation block at that range test.
	if String(row.get("state", "")) != "attack":
		return false
	var target_id := int(row.get("target_id", 0))
	if target_id == 0:
		return false
	var target_kind := String(row.get("target_kind", "battalion"))
	var target_row: Dictionary = (
		sim.structures.get(target_id, {}) if target_kind == "structure" else sim.entities.get(target_id, {})
	)
	if target_row.is_empty() or int(target_row.get("health", 0)) <= 0:
		return false
	var attack_range := float(row.get("attack_range", 0.0))
	if attack_range <= 0.0:
		return false
	var distance := maxf(
		0.0,
		Vector2(row.get("position", Vector2.ZERO)).distance_to(
			Vector2(target_row.get("position", Vector2.ZERO))
		) - _target_footprint_radius(target_id, target_kind)
	)
	return distance <= attack_range


func _eviction_fallback_direction(row: Dictionary) -> Vector2:
	## A unit standing EXACTLY on a footprint centre has no radial direction to
	## be pushed along, and the old code left it there permanently (the
	## `distance > 0.001` guard skipped it). Push it along its own facing, which
	## is deterministic state already replicated in lockstep; a zero facing falls
	## back to a fixed axis so the result never depends on iteration order, wall
	## clock, or RNG.
	var facing := Vector2(row.get("facing", Vector2.ZERO))
	if facing.length_squared() > 0.000001:
		return facing.normalized()
	return Vector2.RIGHT


## Locomotion has no invented constants. Every number below comes from the
## authored Locomotor template the object's LocomotorSet binds, compiled by
## importer/openbfme_importer/locomotor_compiler.py and carried on the row.


func _should_honor_turn_rate(row: Dictionary) -> bool:
	## Authored TurnTime reaches the row as a positive
	## turn_rate_degrees_per_second. Old/synthetic fixtures also carry the
	## positive field explicitly, so they exercise the same arithmetic. A zero or
	## absent value is the only missing-data sentinel and retains snap/direct
	## movement; no provenance label or category can fabricate a rate.
	if float(row.get("turn_rate_degrees_per_second", 0.0)) <= 0.0:
		_report_turn_rate_fallback(row)
		return false
	return true


func _report_turn_rate_fallback(row: Dictionary) -> void:
	var unit_type := String(row.get(
		"unit_type", row.get("source_object_id", row.get("horde_id", "<unknown>"))
	))
	if sim._turn_rate_fallback_unit_types.has(unit_type):
		return
	sim._turn_rate_fallback_unit_types[unit_type] = true
	print(
		"RETAIL_TURN_MODEL missing_authored_turn_rate unit_type=%s fallback=pre_change_snap_direct"
		% unit_type
	)


func _should_reform(row: Dictionary) -> bool:
	## The reform gate exists only where retail authors MaxTurnWithoutReform.
	## Absence means "no reform gate", not a guessed arc.
	if sim.retail_formation_movement:
		return true
	return float(row.get("max_turn_without_reform_degrees", 0.0)) > 0.0


func _retail_reform_threshold_degrees(row: Dictionary) -> float:
	return sim._movement_subsystem()._retail_reform_threshold_degrees(row)


func _retail_turn_rate_degrees(row: Dictionary) -> float:
	return sim._movement_subsystem()._retail_turn_rate_degrees(row)


func _step_retail_heading(
	row: Dictionary,
	movement_direction: Vector2,
	braking: float,
	effective_turn_rate_degrees_per_second: float
) -> bool:
	return sim._movement_subsystem()._step_retail_heading(row, movement_direction, braking, effective_turn_rate_degrees_per_second)


func _step_route(row: Dictionary) -> void:
	sim._movement_subsystem()._step_route(row)


func _consume_route_point_layer(row: Dictionary) -> void:
	if not row.has("route_point_layers"):
		return
	var layers: Array = row.get("route_point_layers", []) as Array
	if layers.is_empty():
		return
	var layer := String(layers.pop_front())
	row["route_point_layers"] = layers
	var elevations: Array = row.get("route_point_elevations", []) as Array
	var elevation := 0.0
	if not elevations.is_empty():
		elevation = float(elevations.pop_front())
		row["route_point_elevations"] = elevations
	if layer in ["ground", "ramp", "deck"]:
		row["pathing_layer"] = layer
		if layer == "ground":
			row.erase("pathing_elevation")
		else:
			row["pathing_elevation"] = elevation


func _should_attempt_crush(row: Dictionary, translation_speed: float, max_speed: float) -> bool:
	if max_speed <= 0.0:
		return false
	if row.has("crush_damage") and int(row.get("crush_damage", 0)) > 0:
		var min_percent := float(row.get("min_crush_velocity_percent", 40.0))
		return translation_speed + 0.0001 >= max_speed * (min_percent / 100.0)
	# Descriptor-backed units must supply their retail CrusherLevel/CrushWeapon
	# inputs. The category fallback exists only for old synthetic fixtures whose
	# rules predate the compiled crush fields.
	if row.has("module_contracts"):
		return false
	return String(row.get("category", "")) == "cavalry" and translation_speed > max_speed * 0.4


func _try_cavalry_trample(row: Dictionary) -> void:
	## Authored crush when crush_damage/crusher_level are present. Otherwise
	## the legacy cavalry 0.5 pulse so the pin and slice trample checks stay
	## put on packs that predate the fields.
	var cooldown := int(row.get("trample_cooldown", 0))
	if cooldown > 0:
		row["trample_cooldown"] = cooldown - 1
		return
	var authored_damage := int(row.get("crush_damage", 0))
	var has_authored := row.has("crush_damage") and authored_damage > 0
	if has_authored:
		var max_speed := float(row.get("speed", 0.0))
		var min_percent := float(row.get("min_crush_velocity_percent", 40.0))
		if max_speed > 0.0 and float(row.get("current_speed", 0.0)) + 0.0001 < max_speed * (min_percent / 100.0):
			return
	elif row.has("module_contracts") or String(row.get("category", "")) != "cavalry":
		return
	var team = int(row.get("team", sim.PLAYER_TEAM))
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var best_id = sim._spatial_nearest_hostile(
		row, team, origin, sim.TRAMPLE_COLLISION_RADIUS, sim.SPATIAL_FILTER_NOT_FLYING
	)
	if best_id == 0:
		return
	var victim: Dictionary = sim.entities[best_id] as Dictionary
	if not sim._squish_collision_admitted(victim):
		sim._emit_event("combat.crush_refused", int(row.get("id", 0)), best_id, {"reason": "victim-missing-squish-collide"})
		return
	if has_authored:
		var crusher_level := int(row.get("crusher_level", 0))
		var victim_level := int(victim.get("crushable_level", 0))
		if crusher_level <= victim_level:
			return
	var damage := 0
	if has_authored:
		damage = maxi(
			1,
			int(round(float(authored_damage) * sim._timed_modifier_product(row, "CRUSH")))
		)
	else:
		damage = maxi(1, int(round(float(row.get("member_damage", 1)) * float(row.get("member_count", 1)) * sim.TRAMPLE_DAMAGE_FACTOR * sim._timed_modifier_product(row, "CRUSH"))))
	# A braced shield wall blunts the charge (retail pike/shield counterplay).
	damage = maxi(1, roundi(float(damage) * float(sim._formation_effects(victim).get("trample_damage_multiplier", 1.0))))
	sim._apply_damage(int(row.get("id", 0)), best_id, damage, "battalion")
	row["trample_cooldown"] = sim.TRAMPLE_COOLDOWN_TICKS
	var payload := {
		"amount": damage,
		"category": String(row.get("category", "cavalry")),
	}
	if has_authored:
		payload["weapon"] = String(row.get("crush_weapon_id", ""))
		sim._emit_event("combat.crush", int(row.get("id", 0)), best_id, payload)
	# Alias kept so existing slice/knockback listeners still see a pulse.
	sim._emit_event("combat.trample", int(row.get("id", 0)), best_id, payload)
	_apply_crush_deceleration(row, victim)
	# CrushRevengeWeapon: victim reflects authored nugget damage at the
	# crusher. Weapon id without authored damage is fail-closed (no invent).
	if victim.has("crush_revenge_damage"):
		var revenge := int(victim.get("crush_revenge_damage", 0))
		if revenge > 0 and int(row.get("health", 0)) > 0:
			sim._apply_damage(best_id, int(row.get("id", 0)), revenge, "battalion")
			sim._emit_event("combat.crush_revenge", best_id, int(row.get("id", 0)), {
				"amount": revenge,
				"weapon": String(victim.get("crush_revenge_weapon_id", "")),
			})
	var knockback_strength = sim.TRAMPLE_KNOCKBACK_STRENGTH
	if has_authored and row.has("crush_knockback"):
		knockback_strength = maxf(0.0, float(row.get("crush_knockback", sim.TRAMPLE_KNOCKBACK_STRENGTH)))
	_apply_knockback(origin, sim.TRAMPLE_COLLISION_RADIUS, knockback_strength, team, 0, "trample", int(row.get("id", 0)))


func _apply_crush_deceleration(crusher: Dictionary, victim: Dictionary) -> void:
	## The crusher pays for the crush in speed, and a braced formation makes it
	## pay much more.
	##
	## RETAIL ORACLE, two halves:
	##  * the crusher's locomotor authors `CrushDecelerationPercent`, and retail
	##    annotates the number itself:
	##    object/cinematic/cinematicobjects.ini:2264
	##    `CrushDecelerationPercent = 20 ; Lose 80 percent of max velocity when
	##    crushing.` -- so the authored percent is the fraction of speed KEPT
	##    and (100 - percent) is the loss.
	##  * the victim's FORMATION ModifierList authors `CRUSHED_DECELERATE`, and
	##    attributemodifier.ini:49 documents it as "Multiplicitive. The
	##    percentage that things crushing you slow" -- it scales that loss. A
	##    porcupine horde authors `CRUSHED_DECELERATE 1000%`
	##    (attributemodifier.ini:762), i.e. ten times the loss, which stops the
	##    charge dead.
	##
	## No authored CrushDecelerationPercent means no deceleration term at all:
	## absent stays absent, never a default. Until this landed, the compiled
	## `crush_deceleration_percent` was stored on the row and read by nothing.
	if not crusher.has("crush_deceleration_percent"):
		return
	var kept := clampf(float(crusher.get("crush_deceleration_percent", 100.0)) / 100.0, 0.0, 1.0)
	var scale = maxf(0.0, sim._timed_modifier_product(victim, "CRUSHED_DECELERATE"))
	var loss := clampf((1.0 - kept) * scale, 0.0, 1.0)
	if loss <= 0.0:
		return
	crusher["current_speed"] = maxf(0.0, float(crusher.get("current_speed", 0.0)) * (1.0 - loss))


func _resume_order_after_knockdown(row: Dictionary) -> bool:
	## Re-path a battalion that just stood up back onto the order it was
	## carrying when it was knocked down: its live attack target first, then a
	## pending move destination. Returns false when there is nothing left to
	## resume (order complete, target dead, or the route is now unreachable),
	## in which case the caller settles it into idle.
	var target_id := int(row.get("target_id", 0))
	if target_id != 0:
		var target: Dictionary = {}
		if String(row.get("target_kind", "battalion")) == "structure":
			target = sim.structures.get(target_id, {}) as Dictionary
		else:
			target = sim.entities.get(target_id, {}) as Dictionary
		if not target.is_empty() and int(target.get("health", 0)) > 0:
			if sim._assign_target_route(row, Vector2(target["position"])):
				row["state"] = "run"
				return true
		row["target_id"] = 0
		row["target_kind"] = "battalion"
	var destination := Vector2(row.get("destination", row["position"]))
	if destination.distance_to(Vector2(row["position"])) > 0.001 and sim._assign_route(row, destination):
		row["state"] = "run"
		return true
	sim._clear_pending_route(row, true)
	return false


func _apply_knockback(center: Vector2, radius: float, strength: float, source_team: int, damage: int, damage_reason: String, source_id: int = 0, taper_off: float = 0.0, z_mult: float = 1.0) -> int:
	## Deterministic radial knockback: sweep enemy battalions in ascending id
	## order, throw each away from the center (clamped to walkable ground),
	## knock them down for sim.KNOCKDOWN_DURATION_TICKS, and apply the optional
	## damage through the existing damage path. Allies and flyers are immune.
	var affected := 0
	# Only battalions inside the blast disc can be thrown, so the old full sweep
	# is a neighbourhood query. Sorted to keep the documented ascending-id order,
	# which damage application and event emission both observe.
	for id in sim._spatial_gather_sorted(center, radius):
		if not sim.entities.has(id):
			continue
		var row: Dictionary = sim.entities[id]
		if int(row.get("team", -1)) == source_team or int(row.get("health", 0)) <= 0:
			continue
		if bool(row.get("flying", false)):
			# Airborne units cannot be bowled over by ground shockwaves.
			continue
		var position := Vector2(row.get("position", Vector2.ZERO))
		var distance := position.distance_to(center)
		if distance > radius:
			continue
		if int(row.get("knockdown_ticks", 0)) > 0:
			# Already sprawled on the ground: a charge cannot fling a battalion
			# that is still lying there, and it cannot refresh the timer either.
			# sim.KNOCKDOWN_DURATION_TICKS(25) outlives sim.TRAMPLE_COOLDOWN_TICKS(10)
			# and sim.TRAMPLE_KNOCKBACK_STRENGTH(2.0) throws the victim less far
			# than sim.TRAMPLE_COLLISION_RADIUS(2.5), so without this guard a single
			# cavalry battalion re-downs the same clump every 10 ticks forever:
			# the victims never stand, never retaliate, and are ground to dust
			# for free. Damage still lands on a prone target.
			if damage > 0:
				sim._apply_damage(source_id, id, damage, "battalion")
			continue
		var direction := (position - center) / distance if distance > 0.001 else Vector2.RIGHT
		# The generic deterministic MetaImpact representation applies the proven
		# radial amount. ShockWaveTaperOff and ShockWaveZMult are retained in the
		# event receipt, but their retail force curve is not projected into this
		# 2D sim until the remaining engine helper semantics are proven.
		var applied_strength := strength
		# Try the full throw first, then shorter deterministic fractions so a
		# victim near water/cliff lands on the nearest walkable spot instead
		# of being stranded on unwalkable cells.
		var landed := position
		for fraction in [1.0, 0.5, 0.25]:
			var candidate := position + direction * applied_strength * float(fraction)
			if sim._position_walkable(candidate):
				landed = candidate
				break
		row["position"] = landed
		sim._spatial_sync(row)
		row["knockdown_ticks"] = sim.KNOCKDOWN_DURATION_TICKS
		row["knocked_down"] = true
		row["current_speed"] = 0.0
		row["attack_windup"] = 0
		# The order survives the fall. Being bowled over interrupts a battalion,
		# it does not make it forget what it was told to do; the route is
		# dropped (the victim was displaced) and re-pathed on stand-up. Wiping
		# target_id/destination here made every knockdown a permanent
		# disarm, because nothing ever re-issues the player's order.
		sim._clear_member_attack_schedule(row)
		sim._clear_member_targets(row)
		sim._clear_pending_route(row, false)
		row["state"] = "knocked_down"
		if damage > 0:
			sim._apply_damage(source_id, id, damage, "battalion")
		var knockback_event := {
			"reason": damage_reason,
			"center": [snappedf(center.x, 0.001), snappedf(center.y, 0.001)],
			"landed": [snappedf(landed.x, 0.001), snappedf(landed.y, 0.001)],
			"knockdown_ticks": sim.KNOCKDOWN_DURATION_TICKS,
		}
		if taper_off > 0.0:
			knockback_event["shockwave_taper_off"] = taper_off
			knockback_event["shockwave_z_mult"] = z_mult
			knockback_event["generic_metaimpact_projection"] = true
		sim._emit_event("combat.knockback", source_id, id, knockback_event)
		affected += 1
	return affected



