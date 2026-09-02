extends Node
## Menu→match bootstrap scene. Shows the retail loading screen immediately
## (this scene is tiny), threaded-loads the vertical slice scene behind it,
## then instances the slice. The slice adopts the already-visible screen via
## the retail_loading_screen group and drives real phase progress into it, so
## the menu transition and the direct-launch path share one loading screen.

const SLICE_SCENE_PATH := "res://scenes/retail_vertical_slice.tscn"
const NATIVE_SCENE_PATH := "res://scenes/sim_host_match.tscn"
const SCREEN_SCENE_PATH := "res://scenes/retail_loading_screen.tscn"
## Share of the bar the scene fetch itself represents; the slice's phase
## weights take over from this floor once it is instanced (monotonic handoff).
const SCENE_LOAD_SHARE := 0.04

var _screen: CanvasLayer = null
var _requested := false
var _match_scene_path := SLICE_SCENE_PATH


func _ready() -> void:
	var game_state := get_node_or_null("/root/GameState")
	if game_state != null and not (game_state.get_meta("native_match_launch", {}) as Dictionary).is_empty():
		_match_scene_path = NATIVE_SCENE_PATH
	var packed := load(SCREEN_SCENE_PATH)
	if packed != null:
		_screen = packed.instantiate()
		add_child(_screen)
	if ResourceLoader.load_threaded_request(_match_scene_path) == OK:
		_requested = true
	else:
		# Fallback: synchronous load keeps the transition functional.
		_apply_loaded_scene(load(_match_scene_path))


func _process(_delta: float) -> void:
	if not _requested or _screen == null:
		return
	var progress: Array = [0.0]
	var status := ResourceLoader.load_threaded_get_status(_match_scene_path, progress)
	if status == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
		_screen.call("set_load_progress", float(progress[0]) * SCENE_LOAD_SHARE, "Loading match scene")
	elif status == ResourceLoader.THREAD_LOAD_LOADED:
		_requested = false
		_apply_loaded_scene(ResourceLoader.load_threaded_get(_match_scene_path))
	elif status == ResourceLoader.THREAD_LOAD_FAILED or status == ResourceLoader.THREAD_LOAD_INVALID_RESOURCE:
		_requested = false
		_screen.call("set_load_progress", SCENE_LOAD_SHARE, "Match scene failed to load")


func _apply_loaded_scene(packed: PackedScene) -> void:
	if packed == null:
		return
	set_process(false)
	var match_scene := packed.instantiate()
	# The slice and the already-visible loading screen must live at the scene
	# root: current_scene rejects nodes parented to this boot node, and freeing
	# the boot node must not take the screen (the slice adopts it via the
	# retail_loading_screen group) down with it.
	var tree := get_tree()
	tree.root.add_child(match_scene)
	tree.root.move_child(match_scene, 0)
	if _screen != null:
		# reparent(), not add_child(): the screen is already parented to this
		# boot node. add_child refuses with "already has a parent", leaving the
		# screen to be destroyed by queue_free() below while the slice holds an
		# adopted reference to it (the broken non-men loading screen).
		_screen.reparent(tree.root)
	tree.current_scene = match_scene
	queue_free()
