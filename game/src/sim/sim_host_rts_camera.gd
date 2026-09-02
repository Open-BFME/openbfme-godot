class_name SimHostRtsCamera
extends Node3D
## Standalone RTS camera for the native-core match scene.

const EDGE_PIXELS := 18.0
const MIN_DISTANCE := 220.0
const MAX_DISTANCE := 1800.0
const ROTATE_SENSITIVITY := 0.008

var target := Vector3(1500.0, 0.0, 950.0)
var distance := 950.0
var yaw := -0.35
var pitch := deg_to_rad(56.0)
var edge_pan_enabled := true
var _camera: Camera3D
var _middle_dragging := false


func _ready() -> void:
	_camera = get_node_or_null("Camera3D") as Camera3D
	if _camera == null:
		_camera = Camera3D.new()
		_camera.name = "Camera3D"
		add_child(_camera)
	_camera.current = true
	_camera.fov = 48.0
	_update_transform()


func _process(delta: float) -> void:
	var pan := Vector2.ZERO
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		pan.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		pan.x += 1.0
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		pan.y += 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		pan.y -= 1.0
	if edge_pan_enabled:
		var viewport_size := get_viewport().get_visible_rect().size
		var mouse := get_viewport().get_mouse_position()
		if mouse.x <= EDGE_PIXELS:
			pan.x -= 1.0
		elif mouse.x >= viewport_size.x - EDGE_PIXELS:
			pan.x += 1.0
		if mouse.y <= EDGE_PIXELS:
			pan.y += 1.0
		elif mouse.y >= viewport_size.y - EDGE_PIXELS:
			pan.y -= 1.0
	if pan != Vector2.ZERO:
		pan = pan.normalized()
		var forward := Vector3(-sin(yaw), 0.0, -cos(yaw))
		var right := Vector3(cos(yaw), 0.0, -sin(yaw))
		target += (right * pan.x + forward * pan.y) * distance * 0.75 * delta
		_update_transform()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mouse_button := event as InputEventMouseButton
		if mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP and mouse_button.pressed:
			distance = clampf(distance * 0.88, MIN_DISTANCE, MAX_DISTANCE)
			_update_transform()
		elif mouse_button.button_index == MOUSE_BUTTON_WHEEL_DOWN and mouse_button.pressed:
			distance = clampf(distance * 1.14, MIN_DISTANCE, MAX_DISTANCE)
			_update_transform()
		elif mouse_button.button_index == MOUSE_BUTTON_MIDDLE:
			_middle_dragging = mouse_button.pressed
	elif event is InputEventMouseMotion and _middle_dragging:
		yaw -= (event as InputEventMouseMotion).relative.x * ROTATE_SENSITIVITY
		_update_transform()


func camera() -> Camera3D:
	return _camera


func screen_to_ground(screen_position: Vector2) -> Variant:
	if _camera == null:
		return null
	var origin := _camera.project_ray_origin(screen_position)
	var direction := _camera.project_ray_normal(screen_position)
	return Plane(Vector3.UP, 0.0).intersects_ray(origin, direction)


func _update_transform() -> void:
	if _camera == null:
		return
	var horizontal := cos(pitch) * distance
	var offset := Vector3(
		sin(yaw) * horizontal,
		sin(pitch) * distance,
		cos(yaw) * horizontal
	)
	_camera.look_at_from_position(target + offset, target, Vector3.UP)
