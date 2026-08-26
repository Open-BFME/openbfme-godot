extends SceneTree
## Owner playtest 2026-08-26: A panned RIGHT and D panned LEFT. The screen-right
## vector used by keyboard and edge scroll was the mirror of view-forward x up.
## This pins the cross-product truth so the mirror cannot ship again: for a
## camera looking along ground forward (fx, fz) with Y up, screen-right is
## (-fz, fx) - verified here against Godot's own Camera3D basis.

## load(), not preload(): the slice script references autoload singletons at
## compile time and only compiles once the boot has registered them.
var SliceScript = null

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	await process_frame
	SliceScript = load("res://src/retail_slice/retail_vertical_slice.gd")
	# Looking "up-screen" toward -Z: right must be +X.
	_check(
		SliceScript.camera_screen_right(Vector2(0.0, -1.0)) == Vector2(1.0, 0.0),
		"facing -Z, screen-right is +X (D pans east, A pans west)"
	)
	# Looking toward +X: right must be +Z.
	_check(
		SliceScript.camera_screen_right(Vector2(1.0, 0.0)) == Vector2(0.0, 1.0),
		"facing +X, screen-right is +Z"
	)
	# Against the engine's own camera basis for a sweep of yaws: place a
	# Camera3D looking along the forward, ask ITS +X basis column, compare.
	for yaw_degrees in [0.0, 37.0, 90.0, 145.0, 180.0, 260.0, 333.0]:
		var yaw := deg_to_rad(float(yaw_degrees))
		var forward := Vector2(sin(yaw), -cos(yaw))
		var camera := Camera3D.new()
		root.add_child(camera)
		camera.global_position = Vector3.ZERO
		camera.look_at(Vector3(forward.x, 0.0, forward.y), Vector3.UP)
		var engine_right_3d: Vector3 = camera.global_transform.basis.x
		var engine_right := Vector2(engine_right_3d.x, engine_right_3d.z)
		var ours: Vector2 = SliceScript.camera_screen_right(forward)
		_check(
			engine_right.distance_to(ours) < 0.0001,
			"yaw %.0f: helper matches Camera3D basis.x (engine=%s ours=%s)"
			% [float(yaw_degrees), str(engine_right), str(ours)]
		)
		camera.queue_free()
	print("CAMERA_SCROLL_DIRECTION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("CAMERA_SCROLL_DIRECTION PASS %s" % label)
	else:
		failed += 1
		push_error("CAMERA_SCROLL_DIRECTION FAIL %s" % label)
