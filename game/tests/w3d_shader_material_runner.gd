extends SceneTree
## Retail W3D additive shader state survives the GLB import (owner playtest
## 2026-08-27: "the fortress also doesnt have the smoke particles coming form
## it"). Two consults agreed the Men fortress authors NO idle particle system;
## its brazier is model geometry - FLAMES (EXFireTorchSeq.tga) and
## FIREGLOW.LightGrow (PG02.tga) - authored src=One/dest=One with the depth
## mask DISABLED. glTF has no additive mode, so Godot imported them as lit
## alpha-blend and the fire rendered as dull dark quads.
##
## This runner pins:
##   1. the proof rule (only the exact One/One pair counts);
##   2. what an additive material becomes (add blend, no depth write, and lit
##      or unlit exactly as the authored primary gradient says);
##   3. the shipped Men fortress citadel GLB through AssetFactory: FLAMES and
##      FIREGLOW go additive, GBFORTRESS and GBFFLAG do not.
##
##   OPENBFME_CONTENT=<repo>\workspace\content-packs <godot> --headless \
##     --path game --script res://tests/w3d_shader_material_runner.gd

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const ShaderScript := preload("res://src/view/w3d_shader_materials.gd")
var AssetFactoryScript: GDScript = null

## The exact preserved extras of the two retail fortress fire materials.
const RETAIL_FLAMES_SHADER := {
	"depth_compare": 3, "depth_mask": 0, "color_mask": 0, "dest_blend": 1,
	"fog_func": 2, "pri_gradient": 1, "sec_gradient": 0, "src_blend": 1,
	"alpha_test": 0, "shader_preset": 2,
}
## GBFFLAG: src=One but dest=Zero - alpha-tested cutout, never additive.
const RETAIL_FLAG_SHADER := {
	"depth_compare": 3, "depth_mask": 1, "color_mask": 0, "dest_blend": 0,
	"fog_func": 2, "pri_gradient": 1, "sec_gradient": 0, "src_blend": 1,
	"alpha_test": 1, "shader_preset": 2,
}

var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0


func _init() -> void:
	_runner_watchdog.start(self, "W3D_SHADER_MATERIAL_RUNNER", 300000)
	await process_frame
	AssetFactoryScript = load("res://src/view/asset_factory.gd")
	# ---- 1. the proof rule ----
	_check("retail_flames_state_is_additive", ShaderScript.is_proven_additive(RETAIL_FLAMES_SHADER))
	_check("retail_flag_state_is_not_additive", not ShaderScript.is_proven_additive(RETAIL_FLAG_SHADER))
	_check("empty_shader_state_is_not_additive", not ShaderScript.is_proven_additive({}))
	_check("partial_shader_state_is_not_guessed", not ShaderScript.is_proven_additive({"src_blend": 1}))
	# ---- 2. what the material becomes ----
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL
	ShaderScript.apply_additive(material, RETAIL_FLAMES_SHADER)
	_check("additive_material_adds", material.blend_mode == BaseMaterial3D.BLEND_MODE_ADD)
	_check("gradient_modulate_material_stays_lit",
		material.shading_mode != BaseMaterial3D.SHADING_MODE_UNSHADED)
	var decal := StandardMaterial3D.new()
	var unlit := RETAIL_FLAMES_SHADER.duplicate()
	unlit["pri_gradient"] = 0
	ShaderScript.apply_additive(decal, unlit)
	_check("gradient_disable_material_is_unshaded",
		decal.shading_mode == BaseMaterial3D.SHADING_MODE_UNSHADED)
	## FIREGLOW's vertex material "LightGrow": ambient 0, diffuse 0, emissive
	## 161/15/0 - self-lit, so the scene's ambient must not black it out.
	var halo := StandardMaterial3D.new()
	ShaderScript.apply_additive(halo, RETAIL_FLAMES_SHADER, {"ambient": [0.0, 0.0, 0.0, 0.0]})
	_check("black_ambient_additive_material_is_unshaded",
		halo.shading_mode == BaseMaterial3D.SHADING_MODE_UNSHADED)
	var torch := StandardMaterial3D.new()
	ShaderScript.apply_additive(torch, RETAIL_FLAMES_SHADER, {"ambient": [1.0, 1.0, 1.0, 0.0]})
	_check("white_ambient_additive_material_stays_lit",
		torch.shading_mode != BaseMaterial3D.SHADING_MODE_UNSHADED)
	_check("absent_ambient_is_not_read_as_black", not ShaderScript.ambient_is_black({}))
	_check("depth_mask_disable_stops_depth_writes",
		material.depth_draw_mode == BaseMaterial3D.DEPTH_DRAW_DISABLED)
	var depth_writer := StandardMaterial3D.new()
	var writes := RETAIL_FLAMES_SHADER.duplicate()
	writes["depth_mask"] = 1
	ShaderScript.apply_additive(depth_writer, writes)
	_check("depth_mask_enable_keeps_depth_writes",
		depth_writer.depth_draw_mode != BaseMaterial3D.DEPTH_DRAW_DISABLED)
	# ---- 3. the shipped citadel GLB ----
	var content_root := OS.get_environment("OPENBFME_CONTENT")
	var glb := _find_citadel_glb(content_root)
	_check("citadel_glb_present", glb != "", "content=%s" % content_root)
	if glb != "":
		var model: Node3D = AssetFactoryScript._try_load_model(glb)
		_check("citadel_glb_loads", model != null)
		var found := {}
		if model != null:
			var stack: Array = [model]
			while not stack.is_empty():
				var node = stack.pop_back()
				if node is MeshInstance3D:
					var mi := node as MeshInstance3D
					found[String(mi.name)] = mi.get_active_material(0)
				for c in node.get_children():
					stack.append(c)
		var glow: Material = found.get("FIREGLOW")
		_check("citadel_halo_is_unshaded", glow is BaseMaterial3D
			and (glow as BaseMaterial3D).shading_mode == BaseMaterial3D.SHADING_MODE_UNSHADED)
		var torch_mesh: Material = found.get("FLAMES")
		_check("citadel_torch_keeps_its_authored_lighting", torch_mesh is BaseMaterial3D
			and (torch_mesh as BaseMaterial3D).shading_mode != BaseMaterial3D.SHADING_MODE_UNSHADED)
		for mesh_name in ["FLAMES", "FIREGLOW"]:
			var m: Material = found.get(mesh_name)
			_check("%s_is_additive" % mesh_name.to_lower(), m is BaseMaterial3D
				and (m as BaseMaterial3D).blend_mode == BaseMaterial3D.BLEND_MODE_ADD
				and (m as BaseMaterial3D).depth_draw_mode == BaseMaterial3D.DEPTH_DRAW_DISABLED,
				"material=%s" % str(m))
		for mesh_name in ["GBFORTRESS", "GBFFLAG"]:
			var m: Material = found.get(mesh_name)
			_check("%s_is_left_alone" % mesh_name.to_lower(), m is BaseMaterial3D
				and (m as BaseMaterial3D).blend_mode != BaseMaterial3D.BLEND_MODE_ADD,
				"material=%s" % str(m))
		if model != null:
			model.free()
	_finish()


func _find_citadel_glb(content_root: String) -> String:
	if content_root == "":
		return ""
	var family := content_root.path_join("rotwk-men-vslice")
	var packs := DirAccess.open(family)
	if packs == null:
		return ""
	var newest := ""
	var newest_time := 0
	for digest in packs.get_directories():
		var candidate := family.path_join(digest).path_join(
			"assets/models/structures/menfortresscitadel/intact-damaged-gbfortress.glb")
		if FileAccess.file_exists(candidate):
			var stamp := FileAccess.get_modified_time(candidate)
			if stamp > newest_time:
				newest_time = stamp
				newest = candidate
	return newest


func _check(name: String, ok: bool, detail: String = "") -> bool:
	if ok:
		_passed += 1
		print("W3D_SHADER_MATERIAL PASS %s" % name)
	else:
		_failed += 1
		print("W3D_SHADER_MATERIAL FAIL %s%s" % [name, (" (%s)" % detail) if detail != "" else ""])
	return ok


func _finish() -> void:
	_runner_watchdog.stop()
	print("W3D_SHADER_MATERIAL_RESULT passed=%d failed=%d" % [_passed, _failed])
	quit(0 if _failed == 0 else 1)
