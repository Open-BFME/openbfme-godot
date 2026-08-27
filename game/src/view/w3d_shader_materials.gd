class_name W3DShaderMaterials
extends RefCounted
## Retail W3D per-material SHADER state, applied to the imported Godot material.
##
## glTF has no additive alpha mode, so a W3D material authored src=One /
## dest=One arrives in Godot as an ordinary LIT ALPHA-BLEND surface: the Men
## fortress brazier (GBFortress.FLAMES over EXFireTorchSeq.tga) and its halo
## (FIREGLOW.LightGrow over PG02.tga) then paint as dull dark quads instead of
## glowing, which is what the owner reads as "the fortress has no smoke /
## fire coming from it" (playtest 2026-08-27).
##
## The converter already preserves the exact W3D shader enums in the GLB as
## material extras (`extras.shader`), so nothing here is invented:
##   src_blend  1 = One      (io_mesh_w3d/custom_properties.py source blend enum)
##   dest_blend 1 = One      -> One/One is additive
##   depth_mask 0 = DISABLE  -> no depth-buffer write
## Only that exact proven pair is touched. Alpha-blend, alpha-test and opaque
## states keep whatever the glTF import chose; a missing or partial
## `extras.shader` is left alone rather than guessed at.
##
## Runs beside `w3d_texture_mappers.gd` (which animates the same two materials'
## GRID flip-book) at the same AssetFactory load points.

const BLEND_ONE := 1
const DEPTH_MASK_DISABLE := 0
## W3D primary gradient: 0 = Disable (OpenGL "decal", the fragment is NOT lit),
## 1 = Modulate (fragment modulated by the lighting gradient, then blended).
const PRI_GRADIENT_DISABLE := 0
## The vertex material's own ambient term, preserved in the same extras. A
## GRADIENT_MODULATE material whose ambient is BLACK collects nothing from the
## scene's ambient light. GBFortress.FIREGLOW is exactly that case - the W3D
## vertex material "LightGrow" authors ambient 0/0/0, diffuse 0/0/0 and
## emissive 161/15/0, i.e. a self-lit halo - so lighting it with this map's
## 0.086 ambient renders it black. Treating a black-ambient additive surface as
## unlit is read from the file, not chosen.
const META_KEY := "w3d_shader_state"


static func tag_gltf_materials(state: GLTFState) -> int:
	## Apply every proven additive W3D shader state in this GLB. Returns how
	## many materials were changed.
	var json_materials: Array = (state.json as Dictionary).get("materials", []) as Array
	var materials: Array = state.get_materials()
	var changed := 0
	for index in mini(json_materials.size(), materials.size()):
		var material: Material = materials[index]
		if material == null or not (material is BaseMaterial3D):
			continue
		if material.has_meta(META_KEY):
			continue
		var extras_value: Variant = (json_materials[index] as Dictionary).get("extras", {})
		if typeof(extras_value) != TYPE_DICTIONARY:
			continue
		var shader_value: Variant = (extras_value as Dictionary).get("shader", {})
		if typeof(shader_value) != TYPE_DICTIONARY:
			continue
		var shader := shader_value as Dictionary
		if not is_proven_additive(shader):
			continue
		apply_additive(material as BaseMaterial3D, shader, extras_value as Dictionary)
		material.set_meta(META_KEY, "additive")
		changed += 1
	return changed


static func is_proven_additive(shader: Dictionary) -> bool:
	## True only for the exact authored One/One pair. Absent enums are not a
	## guess: SAGE defaults dest_blend to Zero, so a partial row is not proof.
	if not shader.has("src_blend") or not shader.has("dest_blend"):
		return false
	return int(shader["src_blend"]) == BLEND_ONE and int(shader["dest_blend"]) == BLEND_ONE


static func apply_additive(material: BaseMaterial3D, shader: Dictionary, extras: Dictionary = {}) -> void:
	## One/One: the fragment is ADDED to the frame buffer. It still receives no
	## shadow (an additive surface cannot be darkened), but it is only unlit
	## when the material says so - the fortress brazier authors
	## `pri_gradient = 1` (GRADIENT_MODULATE), i.e. WW3D modulates the texel by
	## the primary lighting gradient BEFORE the additive blend. Forcing every
	## additive surface unshaded looks brighter but is not what retail draws
	## (consult 2026-08-27 refuted the unconditional-unshaded first attempt).
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	material.disable_receive_shadows = true
	if int(shader.get("pri_gradient", 1)) == PRI_GRADIENT_DISABLE or ambient_is_black(extras):
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if int(shader.get("depth_mask", 1)) == DEPTH_MASK_DISABLE:
		material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED


static func ambient_is_black(extras: Dictionary) -> bool:
	## True when the preserved W3D vertex material authors a zero ambient term.
	## An absent ambient is NOT black: the converter only omits what it never
	## read, and guessing there is how a lit surface would be wrongly unlit.
	var value: Variant = extras.get("ambient")
	if typeof(value) != TYPE_ARRAY:
		return false
	var channels := value as Array
	if channels.size() < 3:
		return false
	for index in 3:
		if not is_zero_approx(float(channels[index])):
			return false
	return true
