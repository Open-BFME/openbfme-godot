extends SceneTree
## Mounted-hero presentation: the MOUNTED skin must be LIT and ANIMATED.
##
## Owner playtest 2026-08-18 (reference/owner-playtest-2026-08-18/
## ours-eomer-mounted-black-tpose-stop-button.png): mounted Eomer rendered as a
## pure black silhouette in bind pose. Two independent defects:
##
##   1. LIGHT LAYERS. retail_vertical_slice.gd:2957 assigns
##      `INFANTRY_LIGHT_LAYER | OBJECT_LIGHT_LAYER` to a battalion's geometry
##      once, at spawn. The MOUNTED skin is instanced later (the mount toggle),
##      so it kept Godot's default layer 1, which no DirectionalLight3D in
##      `_build_source_lights` culls in -> zero light -> black.
##   2. ANIMATION. `member_animation_players` is filled in `_build_members`,
##      before the mounted subtree exists, and the condition set never carried
##      the MOUNTED token. Retail authors the mounted clips behind that flag
##      (eomer.ini:65 `ModelConditionState = MOUNTED` / `Model = RUEomrHrs_SKN`;
##      eomer.ini:231 `AnimationState = MOVING MOUNTED` ->
##      `RUHHs_Theo_SKL.RUHHs_Theo_RUNA`; eomer.ini:294 `AnimationState =
##      MOUNTED`). Neither the mounted AnimationPlayer nor the MOUNTED-qualified
##      clips were ever reached, so the horse stayed in bind pose.
##
## Plus the batcher regression that undoes the swap: member_render_batcher.gd
## `_apply_tier` rewrites the visibility of EVERY Node3D child of a member,
## which re-showed the foot form on top of the mounted one at the next tier
## transition.
##
## Numbers come from the shipped men pack rotwk-men-vslice/f177d1bd... (Eomer
## foot 00.glb / mounted 01.glb).

const AnimationStateSelectScript = preload("res://src/retail_slice/animation_state_select.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

const EOMER_OBJECT_ID := "bfme2.object.rohan-eomer"
## retail_vertical_slice.gd:114-116 INFANTRY_LIGHT_LAYER | OBJECT_LIGHT_LAYER.
const MEMBER_LIGHT_LAYERS := (1 << 3) | (1 << 2)
const EXPECTED_CHECKS := 9

var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "MOUNTED_HERO")
	call_deferred("_run")


func _run() -> void:
	var battalion: Node = _spawn_eomer()
	if battalion == null:
		for label in [
			"mounted_visual_inherits_member_light_layers",
			"mounted_visual_binds_its_own_animation_players",
			"mounted_conditions_carry_the_mounted_token",
			"mounted_move_selects_theoden_run_clip",
			"mount_toggle_drives_the_mounted_clip",
			"foot_player_is_idle_while_mounted",
			"batcher_tier_refresh_keeps_the_mount_swap",
			"unmounted_restores_the_foot_clip",
			"unmounted_conditions_drop_the_mounted_token",
		]:
			_check(label, false, "RohanEomer battalion could not be built from the selected packs")
		_finish()
		return
	_test_light_layers(battalion)
	_test_animation(battalion)
	_test_batcher(battalion)
	_test_unmount(battalion)
	battalion.queue_free()
	_finish()


func _spawn_eomer() -> Node:
	var db := root.get_node_or_null("ContentDB")
	if db == null:
		return null
	var definition: Dictionary = db.call("get_bundle_object", EOMER_OBJECT_ID)
	if definition.is_empty():
		return null
	# Runtime load: headless --script runners do not register global
	# class_name types, so preloading these scripts fails to compile.
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	if battalion_script == null:
		return null
	var battalion: Node = battalion_script.new()
	root.add_child(battalion)
	# One member: Eomer is a single-model hero. Empty capability keeps the test
	# on the authored AnimationState path the bug lives in.
	battalion.call("configure", 1, 0, EOMER_OBJECT_ID, {}, 1, 0.0, [])
	if int(battalion.get("member_count")) < 1:
		battalion.queue_free()
		return null
	# Mimic retail_vertical_slice.gd:2957 exactly: the slice stamps the light
	# domain onto the battalion subtree at spawn, before any mount toggle.
	_assign_geometry_light_layer(battalion, MEMBER_LIGHT_LAYERS)
	return battalion


func _assign_geometry_light_layer(node: Node, layer: int) -> void:
	if node is GeometryInstance3D:
		(node as GeometryInstance3D).layers = layer
	for child in node.get_children():
		_assign_geometry_light_layer(child, layer)


func _mounted_form(battalion: Node) -> Node3D:
	var mounted: Dictionary = battalion.get("mounted_member_visuals")
	return mounted.get(0, null) as Node3D


func _geometry_layers(node: Node, out: Array) -> void:
	if node is GeometryInstance3D:
		out.append(int((node as GeometryInstance3D).layers))
	for child in node.get_children():
		_geometry_layers(child, out)


func _players(node: Node, out: Array) -> void:
	if node is AnimationPlayer:
		out.append(node)
	for child in node.get_children():
		_players(child, out)


func _test_light_layers(battalion: Node) -> void:
	battalion.call("sync_mount_presentation", true)
	var state := String(battalion.get("mount_presentation_state"))
	var form := _mounted_form(battalion)
	if form == null:
		_check("mounted_visual_inherits_member_light_layers", false, "no MountedForm, state=%s" % state)
		return
	var layers: Array = []
	_geometry_layers(form, layers)
	var wrong: Array = []
	for value in layers:
		if int(value) != MEMBER_LIGHT_LAYERS:
			wrong.append(value)
	_check(
		"mounted_visual_inherits_member_light_layers",
		not layers.is_empty() and wrong.is_empty(),
		"surfaces=%d wrong=%s expected=%d state=%s" % [layers.size(), str(wrong), MEMBER_LIGHT_LAYERS, state]
	)


func _test_animation(battalion: Node) -> void:
	var mounted_players: Array = (battalion.get("mounted_member_animation_players") as Dictionary).get(0, []) if "mounted_member_animation_players" in battalion else []
	var clip_total := 0
	for player_value in mounted_players:
		clip_total += (player_value as AnimationPlayer).get_animation_list().size()
	_check(
		"mounted_visual_binds_its_own_animation_players",
		not mounted_players.is_empty() and clip_total > 0,
		"players=%d clips=%d" % [mounted_players.size(), clip_total]
	)

	var conditions: Array = battalion.call("_drawable_conditions_for_state", "run", 0)
	_check(
		"mounted_conditions_carry_the_mounted_token",
		conditions.has("MOUNTED") and conditions.has("MOVING"),
		str(conditions)
	)

	var authored: Dictionary = AnimationStateSelectScript.select(
		battalion.get("animation_state_contracts") as Array, conditions
	)
	# eomer.ini:231 AnimationState = MOVING MOUNTED -> RUHHs_Theo_SKL.RUHHs_Theo_RUNA
	_check(
		"mounted_move_selects_theoden_run_clip",
		String(authored.get("clip", "")).to_lower().ends_with("ruhhs_theo_runa"),
		str(authored.get("clip", ""))
	)

	battalion.call("_play_member_state", 0, "run", 0, true)
	var mounted_playing := ""
	for player_value in mounted_players:
		var player := player_value as AnimationPlayer
		if player.is_playing():
			mounted_playing = player.current_animation
	_check(
		"mount_toggle_drives_the_mounted_clip",
		mounted_playing.to_lower().contains("ruhhs_theo_runa"),
		"playing='%s'" % mounted_playing
	)

	var foot_playing := ""
	for player_value in (battalion.get("member_animation_players") as Dictionary).get(0, []) as Array:
		var player := player_value as AnimationPlayer
		if player.is_playing():
			foot_playing = player.current_animation
	_check(
		"foot_player_is_idle_while_mounted",
		foot_playing == "",
		"foot playing='%s'" % foot_playing
	)


func _test_batcher(battalion: Node) -> void:
	var batcher_script: GDScript = load("res://src/view/member_render_batcher.gd") as GDScript
	if batcher_script == null:
		_check("batcher_tier_refresh_keeps_the_mount_swap", false, "MemberRenderBatcher script failed to load")
		return
	var batcher: Node3D = batcher_script.new()
	root.add_child(batcher)
	batcher.call("register_battalion", battalion as Node3D)
	# No camera: MemberLodPolicy.classify reports NEAR, which is exactly the
	# tier transition that used to re-show the hidden foot form.
	batcher.call("update", 0.016)
	var form := _mounted_form(battalion)
	var foot_visible := false
	for child_value in (battalion.get("_foot_form_children") as Dictionary).get(0, []) as Array:
		if (child_value as Node3D).visible:
			foot_visible = true
	_check(
		"batcher_tier_refresh_keeps_the_mount_swap",
		form != null and form.visible and not foot_visible,
		"mounted=%s foot_visible=%s" % [form != null and form.visible, foot_visible]
	)
	batcher.queue_free()


func _test_unmount(battalion: Node) -> void:
	battalion.call("sync_mount_presentation", false)
	battalion.call("_play_member_state", 0, "run", 0, true)
	var foot_playing := ""
	for player_value in (battalion.get("member_animation_players") as Dictionary).get(0, []) as Array:
		var player := player_value as AnimationPlayer
		if player.is_playing():
			foot_playing = player.current_animation
	# eomer.ini:411 AnimationState = MOVING -> RUEomer_SKL.RUEomer_RUNA
	_check(
		"unmounted_restores_the_foot_clip",
		foot_playing.to_lower().contains("rueomer_run"),
		"foot playing='%s'" % foot_playing
	)
	var conditions: Array = battalion.call("_drawable_conditions_for_state", "run", 0)
	_check(
		"unmounted_conditions_drop_the_mounted_token",
		not conditions.has("MOUNTED") and conditions.has("MOVING"),
		str(conditions)
	)


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("MOUNTED_HERO PASS %s" % label)
	else:
		failed += 1
		printerr("MOUNTED_HERO FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("MOUNTED_HERO FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("MOUNTED_HERO_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
