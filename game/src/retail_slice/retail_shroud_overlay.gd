extends RefCounted

## Presentation side of the retail shroud: turns the local player's grid
## (retail_fog_of_war.gd) into the three things the screen needs.
##
##   1. a texture the terrain shader modulates by, so unexplored ground is
##      black and remembered ground is half-lit terrain;
##   2. a per-object visibility answer, so units in fog disappear and
##      structures do not;
##   3. the same answer for the minimap, so the radar cannot leak what the
##      battlefield hides.
##
## THE LOCAL PLAYER IS THE ONLY PLAYER THIS DRAWS. The grid is per team and the
## simulation computes every team's grid on every peer (that is what makes it
## lockstep-safe), but only `local_team`'s ever reaches a texture. An observer
## or replay changes `local_team`; nothing else moves.
##
## THE UNIT/STRUCTURE SPLIT, and the deviation it carries.
## Retail remembers a structure you have seen as a GhostObject: a per-player
## frozen copy of its models, bone poses and house colour at the moment you
## last saw it, so a building that is destroyed while you are not looking stays
## on your screen until you look again (OpenSAGE GhostObject._modelsPerPlayer).
## This version does NOT do that. A structure is drawn whenever its cell is
## explored, reading its CURRENT state - so a razed building vanishes from a
## fogged base the moment it dies, where retail would leave the ghost standing.
## That is a named deviation, not an oversight; the fix is a ghost snapshot
## registry and it is on the follow-up list. Units are the easy half and are
## exact: a unit is drawn only while its cell is CLEAR.
##
## THE GHOST GAP CUTS BOTH WAYS, and the second direction leaks information the
## player has not earned:
##   * a structure DESTROYED in fog vanishes from your view, where retail leaves
##     the ghost standing until you look again - you learn it died;
##   * a structure BUILT in fog on ground you once explored pops into existence
##     live, where retail shows you nothing until you look - you learn it exists;
##   * a structure you HAVE seen streams its CURRENT health bar, production
##     queue and upgrade level through the fog, where retail freezes all of it
##     at the moment you last saw it.
## In a competitive match the last one is the worst: fog stops hiding an enemy
## base's economy the instant you scout it once. Until the ghost registry lands,
## fog is a visual effect over structures, not an information barrier.
##
## Cost control: the texture is rebuilt on a fixed frame cadence rather than
## every frame, AND only when the grid's revision has actually moved. The first
## cut had only the cadence, so a match with nothing moving still paid a full
## rebuild seven times a second - 6.6ms of it, measured, because both byte
## planes were built by a per-cell GDScript loop over a 33,489-cell grid. The
## planes are now maintained by the model as it stamps, so a rebuild is two
## memcpys and two texture uploads, and a still battlefield skips even those.

const FogScript = preload("res://src/retail_slice/retail_fog_of_war.gd")

## Frames between texture rebuilds. At 60fps this is ~7 rebuilds a second,
## comfortably faster than a unit can cross a 40-source-unit cell.
const REBUILD_INTERVAL_FRAMES := 8

var enabled := false
var local_team := 0

var _fog = null
var _texture: ImageTexture = null
var _minimap_texture: ImageTexture = null
var _frames_since_rebuild := REBUILD_INTERVAL_FRAMES
var _rebuild_count := 0
var _last_bounds := Rect2()
var _last_revision := -1


func configure(fog, team: int) -> void:
	## `fog` is the simulation's own model. Held by reference on purpose: the
	## overlay must never keep a second copy of the grid that could disagree
	## with the one gameplay reads.
	_fog = fog
	local_team = team
	enabled = fog != null and bool(fog.enabled)
	_texture = null
	_minimap_texture = null
	_frames_since_rebuild = REBUILD_INTERVAL_FRAMES
	_rebuild_count = 0
	_last_revision = -1


func release() -> void:
	_fog = null
	_texture = null
	_minimap_texture = null
	enabled = false


func texture() -> ImageTexture:
	return _texture


func bounds() -> Rect2:
	return _last_bounds


func grid_cells() -> Vector2i:
	if _fog == null:
		return Vector2i.ZERO
	return Vector2i(int(_fog.cells_x), int(_fog.cells_y))


func rebuild_count() -> int:
	## Observability, the way FordsLinearFog exposes `dispatch_count`: a capture
	## can prove the overlay actually ran rather than merely being configured.
	return _rebuild_count


func update(force: bool = false) -> bool:
	## Returns true when the texture was rebuilt this call.
	if not enabled or _fog == null:
		return false
	_frames_since_rebuild += 1
	if not force and _texture != null:
		if _frames_since_rebuild < REBUILD_INTERVAL_FRAMES:
			return false
		# Nothing moved: the grid is byte-for-byte what it was at the last
		# upload, so there is nothing to upload. The cadence counter is still
		# consumed so an idle match does not re-test on every single frame.
		if int(_fog.revision(local_team)) == _last_revision:
			_frames_since_rebuild = 0
			return false
	_frames_since_rebuild = 0
	_last_revision = int(_fog.revision(local_team))
	var image: Image = _fog.alpha_image(local_team)
	if image == null:
		return false
	if _texture == null or _texture.get_width() != image.get_width() \
			or _texture.get_height() != image.get_height():
		_texture = ImageTexture.create_from_image(image)
	else:
		_texture.update(image)
	var radar: Image = _fog.radar_image(local_team)
	if radar != null:
		if _minimap_texture == null \
				or _minimap_texture.get_width() != radar.get_width() \
				or _minimap_texture.get_height() != radar.get_height():
			_minimap_texture = ImageTexture.create_from_image(radar)
		else:
			_minimap_texture.update(radar)
	_last_bounds = _fog.grid_bounds()
	_rebuild_count += 1
	return true


func apply_to_terrain(material: ShaderMaterial) -> void:
	if material == null:
		return
	if not enabled or _texture == null:
		material.set_shader_parameter("shroud_enabled", false)
		return
	material.set_shader_parameter("shroud_texture", _texture)
	material.set_shader_parameter(
		"shroud_bounds",
		Vector4(_last_bounds.position.x, _last_bounds.position.y, _last_bounds.size.x, _last_bounds.size.y)
	)
	material.set_shader_parameter("shroud_enabled", true)


# --- Per-object visibility --------------------------------------------------


func unit_visible(position: Vector2) -> bool:
	## A unit is on screen only while the ground it stands on is CLEAR. Fogged
	## ground remembers terrain, never armies.
	if not enabled or _fog == null:
		return true
	return state_at(position) == FogScript.CLEAR


func structure_visible(position: Vector2) -> bool:
	## Explored is enough - see the class comment's named GhostObject deviation.
	if not enabled or _fog == null:
		return true
	return state_at(position) != FogScript.SHROUDED


func visible_unit_ids(ids: Array, position_lookup: Callable) -> Array:
	## Filter a pick candidate list down to the units the local player can
	## actually see. An enemy in fog is not drawn, so it must not be found by the
	## mouse either - otherwise the attack cursor lights up over apparently empty
	## ground and promises an order on something invisible.
	##
	## Ordering is preserved: `SelectionPick.closest_hit` resolves ties by first
	## occurrence, so reordering here would silently change which of two
	## overlapping enemies the cursor names.
	return _filter_ids(ids, position_lookup, true)


func visible_structure_ids(ids: Array, position_lookup: Callable) -> Array:
	## Structures use the EXPLORED test, matching the battlefield and the radar
	## (see the class comment's named GhostObject deviation).
	return _filter_ids(ids, position_lookup, false)


func _filter_ids(ids: Array, position_lookup: Callable, units: bool) -> Array:
	if not enabled or _fog == null or not position_lookup.is_valid():
		return ids.duplicate()
	var out: Array = []
	for id in ids:
		var position: Vector2 = position_lookup.call(id)
		if units:
			if unit_visible(position):
				out.append(id)
		elif structure_visible(position):
			out.append(id)
	return out


func state_at(position: Vector2) -> int:
	## The hot query: called for every presented battalion, every structure, every
	## radar blip and every scenery placement.
	##
	## IT ASKS THE MODEL, and does not keep its own handle on the visibility
	## plane. Caching that handle looks free - the plane is a Godot packed array,
	## refcounted, and a handle taken out of the Dictionary DOES share the
	## buffer (review-probed on 4.7; the model's _bind_team optimisation depends
	## on exactly that). The real hazard is wholesale ARRAY REPLACEMENT: the
	## model installs brand-new arrays on configure()/from_state()/_ensure_team(),
	## and a cached handle keeps pointing at the orphaned old one - which is why
	## the model carries _unbind_team(). A stale cache shipped for the length of
	## one test run and made a battalion standing in plain sight unclickable,
	## because the pick asked the orphaned copy and was told the ground was
	## shrouded. Asking the model every call sidesteps the lifecycle entirely.
	if not enabled or _fog == null:
		return FogScript.CLEAR
	return _fog.state_at(local_team, position)


# --- Map scenery ------------------------------------------------------------
#
# THE TERRAIN SHADER ONLY DARKENS TERRAIN. Trees, rocks and ruins are separate
# meshes and the shroud modulate never touched them, so the owner's first fog-on
# playtest showed a black map border with a full forest standing brightly on top
# of it: "I can see trees and objects in the fog of war."
#
# Retail's treatment is the one the terrain already gets, because scenery IS
# terrain as far as knowledge goes: nothing renders under UNEXPLORED shroud, and
# once ground has been explored its trees stay drawn even when the ground goes
# back to fog. That is the same explored test structures use, and it is why a
# scouted forest does not blink out when the scout walks home.
#
# Cost: the scenery list is bound once and answered on the texture's own
# cadence - so at most every eighth frame, and only when the grid changed at all.


var _scenery: Array = []


func bind_scenery(containers: Array) -> int:
	## `containers` are Node3Ds whose direct children are one placement each.
	## Returns how many placements were bound, so a caller can prove the shroud
	## actually reached the scenery rather than silently binding nothing.
	_scenery.clear()
	for container_value in containers:
		var container := container_value as Node3D
		if container == null:
			continue
		var write_to_child := bool(container.get_meta("shroud_gates_child", false))
		for child in container.get_children():
			var placement := child as Node3D
			if placement == null:
				continue
			# An animated prop's own controller writes `placement_root.visible`
			# on every state transition, so the shroud gate is written one level
			# down on the placement's visual instead of fighting it. Bound
			# lifecycle structures have no such writer and take the root.
			var target: Node3D = placement
			if write_to_child and placement.get_child_count() > 0:
				var visual := placement.get_child(0) as Node3D
				if visual != null:
					target = visual
			var world := placement.global_position if placement.is_inside_tree() \
				else placement.transform.origin
			_scenery.append({
				"node": target,
				"position": Vector2(world.x, world.z),
				# Retail castle walls carrying DONT_HIDE_IF_FOGGED or
				# NEVER_CULL_FOR_MP remain part of the skyline even over
				# unexplored ground. The fixture binder stamps this from KindOf.
				"dont_hide_if_fogged": bool(placement.get_meta("dont_hide_if_fogged", false)),
			})
	return _scenery.size()


func scenery_count() -> int:
	return _scenery.size()


func apply_to_scenery() -> int:
	## Returns how many placements are currently hidden by the shroud - the
	## number a rendered capture can assert against.
	if _scenery.is_empty():
		return 0
	var hidden := 0
	if not enabled or _fog == null:
		for entry_value in _scenery:
			(entry_value as Dictionary)["node"].visible = true
		return 0
	for entry_value in _scenery:
		var entry: Dictionary = entry_value
		var visible_now := bool(entry.get("dont_hide_if_fogged", false)) \
			or state_at(Vector2(entry["position"])) != FogScript.SHROUDED
		(entry["node"] as Node3D).visible = visible_now
		if not visible_now:
			hidden += 1
	return hidden


func minimap_texture() -> ImageTexture:
	## The radar's layer, built from the SAME grid the terrain samples: black
	## with alpha = 255 - retail visibility, so it can be drawn in one pass with
	## no shader and no inversion at the draw site. Rebuilt alongside the terrain
	## texture (same cadence, same `update()` call) so the two can never show
	## different frames of the same match.
	return _minimap_texture


func minimap_dim(position: Vector2) -> float:
	## The radar draws terrain ink through the same three stops the battlefield
	## uses, so the two cannot disagree about what the player knows.
	match state_at(position):
		FogScript.CLEAR:
			return float(FogScript.RETAIL_CLEAR_ALPHA) / 255.0
		FogScript.FOGGED:
			return float(FogScript.RETAIL_FOG_ALPHA) / 255.0
		_:
			return float(FogScript.RETAIL_SHROUD_ALPHA) / 255.0
