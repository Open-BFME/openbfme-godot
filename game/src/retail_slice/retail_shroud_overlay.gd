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
## Cost control: the texture is rebuilt on a fixed frame cadence rather than
## every frame. The grid only changes when something moved, and a 40-source-unit
## cell does not change fast; the cadence is a presentation constant with no
## simulation consequence whatever.

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
	if not force and _texture != null and _frames_since_rebuild < REBUILD_INTERVAL_FRAMES:
		return false
	_frames_since_rebuild = 0
	var image: Image = _fog.alpha_image(local_team)
	if image == null:
		return false
	if _texture == null or _texture.get_width() != image.get_width() \
			or _texture.get_height() != image.get_height():
		_texture = ImageTexture.create_from_image(image)
	else:
		_texture.update(image)
	_rebuild_minimap_texture(image)
	_last_bounds = _fog.grid_bounds()
	_rebuild_count += 1
	return true


func _rebuild_minimap_texture(alpha: Image) -> void:
	var width := alpha.get_width()
	var height := alpha.get_height()
	var pixels := PackedByteArray()
	pixels.resize(width * height * 4)
	var source := alpha.get_data()
	for index in range(width * height):
		var offset := index * 4
		pixels[offset] = 0
		pixels[offset + 1] = 0
		pixels[offset + 2] = 0
		# Retail's byte says how much you can SEE; the radar layer draws how much
		# is hidden, so it is the complement.
		pixels[offset + 3] = 255 - source[index]
	var image := Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, pixels)
	if _minimap_texture == null \
			or _minimap_texture.get_width() != width \
			or _minimap_texture.get_height() != height:
		_minimap_texture = ImageTexture.create_from_image(image)
	else:
		_minimap_texture.update(image)


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
	return _fog.state_at(local_team, position) == FogScript.CLEAR


func structure_visible(position: Vector2) -> bool:
	## Explored is enough - see the class comment's named GhostObject deviation.
	if not enabled or _fog == null:
		return true
	return _fog.state_at(local_team, position) != FogScript.SHROUDED


func state_at(position: Vector2) -> int:
	if not enabled or _fog == null:
		return FogScript.CLEAR
	return _fog.state_at(local_team, position)


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
