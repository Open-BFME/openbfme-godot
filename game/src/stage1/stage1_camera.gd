class_name Stage1Camera
extends Node3D
## Render-rate camera for the legal-safe Stage 1 arena.

@export var pan_speed: float = 34.0
@export var rotate_speed: float = 1.35
@export var edge_size: float = 14.0
@export var min_zoom: float = 16.0
@export var max_zoom: float = 58.0
@export var world_half_extent: float = 62.0

@onready var camera: Camera3D = $Camera3D

var command_mode: bool = false

func _ready() -> void:
	camera.look_at(global_position, Vector3.UP)

func _process(delta: float) -> void:
	var move := Vector2.ZERO
	if Input.is_action_pressed("cam_forward"):
		move.y -= 1.0
	if Input.is_action_pressed("cam_back"):
		move.y += 1.0
	# A is also the conventional attack-move hotkey. Once a command is armed,
	# arrows still pan but A no longer drags the camera underneath the cursor.
	if Input.is_action_pressed("cam_left") and not command_mode:
		move.x -= 1.0
	if Input.is_action_pressed("cam_right"):
		move.x += 1.0
	var mouse := get_viewport().get_mouse_position()
	var viewport_size := get_viewport().get_visible_rect().size
	if mouse.x >= 0.0 and mouse.y >= 0.0 and mouse.x <= viewport_size.x and mouse.y <= viewport_size.y:
		if mouse.x < edge_size:
			move.x -= 1.0
		elif mouse.x > viewport_size.x - edge_size:
			move.x += 1.0
		if mouse.y < edge_size:
			move.y -= 1.0
		elif mouse.y > viewport_size.y - edge_size:
			move.y += 1.0
	if move.length_squared() > 0.0:
		move = move.normalized()
		var world_move := Vector3(move.x, 0.0, move.y).rotated(Vector3.UP, rotation.y)
		global_position += world_move * pan_speed * delta
	if Input.is_action_pressed("cam_rotate_left"):
		rotate_y(rotate_speed * delta)
	if Input.is_action_pressed("cam_rotate_right"):
		rotate_y(-rotate_speed * delta)
	global_position.x = clampf(global_position.x, -world_half_extent, world_half_extent)
	global_position.z = clampf(global_position.z, -world_half_extent, world_half_extent)

func zoom_steps(steps: float) -> void:
	var direction := camera.position.normalized()
	var distance := camera.position.length()
	distance = clampf(distance + steps * 3.5, min_zoom, max_zoom)
	camera.position = direction * distance
	camera.look_at(global_position, Vector3.UP)

func focus_on(world_xz: Vector2) -> void:
	global_position.x = clampf(world_xz.x, -world_half_extent, world_half_extent)
	global_position.z = clampf(world_xz.y, -world_half_extent, world_half_extent)

func ground_point(screen_position: Vector2) -> Variant:
	var ray_origin := camera.project_ray_origin(screen_position)
	var ray_direction := camera.project_ray_normal(screen_position)
	if absf(ray_direction.y) < 0.00001:
		return null
	var distance := -ray_origin.y / ray_direction.y
	if distance < 0.0:
		return null
	var hit := ray_origin + ray_direction * distance
	return Vector2(hit.x, hit.z)

func screen_point(world_xz: Vector2, height: float = 0.0) -> Vector2:
	return camera.unproject_position(Vector3(world_xz.x, height, world_xz.y))
