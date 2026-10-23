extends "res://scripts/game.gd"

var replay_folder := ""
var checked_loading_art := false
var loading_art_error := ""
var _previous_frame := -1.0
var _saw_visible := false

func _last_replay_path() -> String:
	return replay_folder.path_join("record.BfME2Replay")

func _ring_instancer(node: Node) -> Node:
	if node.is_class("W3DInstancer") and str(node.get_model_report(0).get("name", "")).to_lower() == "sfe_menubkgrnd":
		return node
	for child in node.get_children():
		var found := _ring_instancer(child)
		if found != null:
			return found
	return null

func _on_load_progress(percent: int) -> void:
	super._on_load_progress(percent)
	if not loading_art_error.is_empty():
		return
	var art: Dictionary = _shell.get_backdrop_state()
	if art.backdrop_image != "" or art.background_mode != 1 or art.background_paths.is_empty():
		loading_art_error = "loading backdrop layer missing or statue present"
		return
	# Inspect the actual View3D model, not just Background.apt's placeholder.
	# Animation can hide the model without removing that placeholder.
	var ring := _ring_instancer(_shell)
	if ring == null:
		loading_art_error = "ring model missing"
		return
	# The fade-in starts hidden. Require two visible samples and reject any
	# later disappearance; a ring that stays hidden never completes the check.
	if int(ring.get_stats().visible_draw_instances) <= 0:
		if _saw_visible:
			loading_art_error = "ring model became hidden"
		return
	_saw_visible = true
	var pose: PackedFloat32Array = ring.get_instance_palette(0)
	if pose.is_empty():
		loading_art_error = "ring pose missing"
		return
	var frame := -1.0
	for key in art.model_animation_frames:
		if str(key).to_lower().ends_with("|sfe_menubkgrnd"):
			frame = float(art.model_animation_frames[key])
	if frame < 0.0 or not is_finite(frame):
		loading_art_error = "ring animation frame missing"
		return
	# View3D uses real time: leave more than one 30 Hz frame between the two
	# progress presentations, without advancing the model from the test.
	if _previous_frame < 0.0:
		_previous_frame = frame
		OS.delay_msec(100)
	elif not checked_loading_art:
		if frame == _previous_frame:
			loading_art_error = "ring animation frozen across loading progress calls"
		else:
			checked_loading_art = true
