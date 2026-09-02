class_name NativeLoadingBoot
extends Node
## Retail loading-screen transition for the native-core match scene.

const MATCH_SCENE_PATH := "res://scenes/sim_host_match.tscn"
const SCREEN_SCENE_PATH := "res://scenes/retail_loading_screen.tscn"
const SCENE_LOAD_SHARE := 0.12

var _screen: CanvasLayer
var _requested := false


func _ready() -> void:
	var screen_scene := load(SCREEN_SCENE_PATH) as PackedScene
	if screen_scene != null:
		_screen = screen_scene.instantiate()
		add_child(_screen)
		_screen.call("set_load_progress", 0.0, "Preparing native battle")
	if ResourceLoader.load_threaded_request(MATCH_SCENE_PATH) == OK:
		_requested = true
	else:
		_apply_loaded_scene(load(MATCH_SCENE_PATH))


func _process(_delta: float) -> void:
	if not _requested:
		return
	var progress: Array = [0.0]
	var status := ResourceLoader.load_threaded_get_status(MATCH_SCENE_PATH, progress)
	if status == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
		if _screen != null:
			_screen.call("set_load_progress", float(progress[0]) * SCENE_LOAD_SHARE, "Loading native match")
	elif status == ResourceLoader.THREAD_LOAD_LOADED:
		_requested = false
		_apply_loaded_scene(ResourceLoader.load_threaded_get(MATCH_SCENE_PATH))
	elif status in [ResourceLoader.THREAD_LOAD_FAILED, ResourceLoader.THREAD_LOAD_INVALID_RESOURCE]:
		_requested = false
		if _screen != null:
			_screen.call("set_load_progress", SCENE_LOAD_SHARE, "Native match failed to load")


func _apply_loaded_scene(packed: PackedScene) -> void:
	if packed == null:
		return
	set_process(false)
	var native_match := packed.instantiate()
	var tree := get_tree()
	tree.root.add_child(native_match)
	tree.root.move_child(native_match, 0)
	if _screen != null:
		_screen.reparent(tree.root)
		_screen.call("set_load_progress", SCENE_LOAD_SHARE, "Starting native core")
	tree.current_scene = native_match
	queue_free()
