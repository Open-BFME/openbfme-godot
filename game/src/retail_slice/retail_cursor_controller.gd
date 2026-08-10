extends RefCounted
class_name RetailCursorController

## Presents the retail mouse cursor for the pointer's current intent.
##
## Retail RotWK does not tint the stock system arrow when you hover a valid
## enemy: it swaps in `SCCAttack`, a 32x32 six-frame animation of four red
## arrowheads converging on the target, bound in `data/ini/mouse.ini` to the
## `AttackObj` / `ForceAttackObj` / `ForceAttackGround` / `Target` / `OutRange`
## intents. The slice used to approximate that with `Input.CURSOR_CROSS`, the
## OS crosshair, which is neither red nor animated.
##
## The art arrives in a content pack as `openbfme.cursor-index`; see
## `ContentDB.get_retail_cursor`. When no mounted pack carries it this
## controller reports `has_art() == false` and the caller keeps the stock
## crosshair - an honest, visible fallback rather than an invented sprite.
##
## The custom image is bound to the CROSS shape rather than to ARROW so it can
## only ever appear where the slice already asked for a crosshair; HUD controls
## that request their own shapes (POINTING_HAND) are untouched.
##
## Multiplayer safety: everything here is local presentation. No method reads
## or writes simulation state and none is reachable from the lockstep codec, so
## two peers showing different cursors cannot diverge.

const INTENT_DEFAULT := "default"
const INTENT_ATTACK := "attack"

## Resource-exhaustion bounds on a pack document, not semantic limits.
const MAX_FRAMES := 64
const MAX_SEQUENCE_STEPS := 256
const MAX_DIMENSION := 256
const MIN_STEP_SECONDS := 0.01

var _frames: Array[Texture2D] = []
var _sequence: PackedInt32Array = PackedInt32Array()
var _frame_seconds: PackedFloat32Array = PackedFloat32Array()
var _cycle_seconds: float = 0.0
var _hotspot: Vector2 = Vector2.ZERO
var _last_error: String = ""
var _applied_frame: int = -1
var _applied_intent: String = ""


## Why the last `configure` refused, or "" after a successful one.
func last_error() -> String:
	return _last_error


func has_art() -> bool:
	return not _frames.is_empty()


func frame_count() -> int:
	return _frames.size()


func sequence() -> PackedInt32Array:
	return _sequence


func hotspot() -> Vector2:
	return _hotspot


func cycle_seconds() -> float:
	return _cycle_seconds


## Decode one `openbfme.cursor-index` row into presentable frames.
##
## Returns false and records `last_error()` for any malformed row: a cursor
## that is half-decoded would animate into a blank frame mid-swing, which is
## worse than not swapping the cursor at all.
func configure(entry: Dictionary) -> bool:
	_reset()
	if entry.is_empty():
		_last_error = "no cursor row"
		return false
	var raw_frames: Variant = entry.get("frames", null)
	if typeof(raw_frames) != TYPE_ARRAY or (raw_frames as Array).is_empty():
		_last_error = "cursor row carries no frames"
		return false
	var rows := raw_frames as Array
	if rows.size() > MAX_FRAMES:
		_last_error = "cursor row declares %d frames" % rows.size()
		return false
	var width := int(entry.get("width", 0))
	var height := int(entry.get("height", 0))
	if width <= 0 or height <= 0 or width > MAX_DIMENSION or height > MAX_DIMENSION:
		_last_error = "cursor row declares an out-of-range size %dx%d" % [width, height]
		return false

	var decoded: Array[Texture2D] = []
	for value in rows:
		if typeof(value) != TYPE_DICTIONARY:
			_last_error = "cursor frame row is not an object"
			return false
		var encoded := String((value as Dictionary).get("png", ""))
		if encoded == "":
			_last_error = "cursor frame carries no image"
			return false
		var buffer := Marshalls.base64_to_raw(encoded)
		if buffer.is_empty():
			_last_error = "cursor frame image is not valid base64"
			return false
		var image := Image.new()
		if image.load_png_from_buffer(buffer) != OK:
			_last_error = "cursor frame image is not a valid PNG"
			return false
		if image.get_width() != width or image.get_height() != height:
			_last_error = "cursor frame %dx%d disagrees with the declared %dx%d" % [
				image.get_width(), image.get_height(), width, height,
			]
			return false
		decoded.append(ImageTexture.create_from_image(image))

	var raw_sequence: Variant = entry.get("sequence", null)
	var steps: Array = raw_sequence as Array if typeof(raw_sequence) == TYPE_ARRAY else []
	if steps.is_empty():
		steps = range(decoded.size())
	if steps.size() > MAX_SEQUENCE_STEPS:
		_last_error = "cursor row declares %d sequence steps" % steps.size()
		return false
	var raw_seconds: Variant = entry.get("frameSeconds", null)
	var seconds: Array = raw_seconds as Array if typeof(raw_seconds) == TYPE_ARRAY else []
	if seconds.size() != steps.size():
		_last_error = "cursor timing has %d entries for %d steps" % [seconds.size(), steps.size()]
		return false

	var sequence_out := PackedInt32Array()
	var seconds_out := PackedFloat32Array()
	var total := 0.0
	for index in steps.size():
		var frame_index := int(steps[index])
		if frame_index < 0 or frame_index >= decoded.size():
			_last_error = "cursor sequence step %d has no frame" % frame_index
			return false
		# Clamp rather than refuse: a zero or negative retail rate would divide
		# the animation clock by nothing, and a floor is a truthful reading of
		# "as fast as this can be shown".
		var step := maxf(float(seconds[index]), MIN_STEP_SECONDS)
		sequence_out.append(frame_index)
		seconds_out.append(step)
		total += step

	var spot: Variant = entry.get("hotspot", null)
	var hotspot_x := 0
	var hotspot_y := 0
	if typeof(spot) == TYPE_DICTIONARY:
		hotspot_x = clampi(int((spot as Dictionary).get("x", 0)), 0, width - 1)
		hotspot_y = clampi(int((spot as Dictionary).get("y", 0)), 0, height - 1)

	_frames = decoded
	_sequence = sequence_out
	_frame_seconds = seconds_out
	_cycle_seconds = total
	_hotspot = Vector2(hotspot_x, hotspot_y)
	_last_error = ""
	return true


## Pure intent selection: what the pointer means right now.
##
## Kept static and free of engine state so the decision is unit-testable
## headless, where the OS cursor itself cannot be observed.
static func select_intent(
	has_selection: bool,
	command_armed: bool,
	enemy_under_cursor: bool
) -> String:
	if not has_selection:
		return INTENT_DEFAULT
	# An armed build/ability/power placement owns the pointer: retail shows the
	# placement cursor there, never the attack cursor.
	if command_armed:
		return INTENT_DEFAULT
	if not enemy_under_cursor:
		return INTENT_DEFAULT
	return INTENT_ATTACK


## Pure animation clock: which frame the retail sequence shows at `elapsed`.
func frame_index_at(elapsed_seconds: float) -> int:
	if _sequence.is_empty():
		return -1
	if _cycle_seconds <= 0.0:
		return _sequence[0]
	var position := fposmod(maxf(elapsed_seconds, 0.0), _cycle_seconds)
	for index in _sequence.size():
		position -= _frame_seconds[index]
		if position < 0.0:
			return _sequence[index]
	# Float drift at the very end of a cycle lands here; the last step is the
	# truthful answer, not the first.
	return _sequence[_sequence.size() - 1]


func frame_texture(index: int) -> Texture2D:
	if index < 0 or index >= _frames.size():
		return null
	return _frames[index]


## The stock shape the slice falls back to for an intent.
static func fallback_shape_for(intent: String) -> int:
	return Input.CURSOR_CROSS if intent == INTENT_ATTACK else Input.CURSOR_ARROW


## Present `intent`. Safe to call every frame: the OS cursor is only touched
## when the shape or the animation frame actually changes.
func apply(intent: String, elapsed_seconds: float) -> void:
	if DisplayServer.get_name() == "headless":
		return
	var shape := fallback_shape_for(intent)
	if intent == INTENT_ATTACK and has_art():
		var index := frame_index_at(elapsed_seconds)
		if index != _applied_frame or _applied_intent != intent:
			var texture := frame_texture(index)
			if texture != null:
				# Bound to CROSS, which is the only shape this slice asks for
				# on an attack intent, so the custom image cannot leak onto the
				# menus or the HUD buttons.
				Input.set_custom_mouse_cursor(texture, Input.CURSOR_CROSS, _hotspot)
				_applied_frame = index
	elif _applied_intent != intent:
		_applied_frame = -1
	_applied_intent = intent
	if Input.get_current_cursor_shape() != shape:
		Input.set_default_cursor_shape(shape)


func _reset() -> void:
	_frames.clear()
	_sequence = PackedInt32Array()
	_frame_seconds = PackedFloat32Array()
	_cycle_seconds = 0.0
	_hotspot = Vector2.ZERO
	_applied_frame = -1
	_applied_intent = ""
	_last_error = ""
