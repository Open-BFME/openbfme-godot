class_name SnapshotBoneAtlas
extends RefCounted
## Load-time GPU skinning bake for one template.
##
## Layout: one RGBA32F image row per authored animation frame. Each bone owns
## three adjacent texels in that row, containing the affine matrix rows
## (m00,m01,m02,tx), (m10,m11,m12,ty), (m20,m21,m22,tz). Slot offsets/counts
## select rows in the shader. Godot's conservative 16,384 texel 2D limit means
## one atlas supports at most floor(16384 / 3) = 5,461 bones and 16,384 total
## sampled frames. Retail members measured here use 22 bones and fit easily.

const ClipResolver := preload("res://src/view/snapshot_anim_clips.gd")
const FRAMES_PER_SECOND := 30
const MAX_TEXTURE_DIMENSION := 16384
const TEXELS_PER_BONE := 3


static func bake(owner: Node, path: String, template_name: String) -> Dictionary:
	if owner == null or path.is_empty():
		return _failure("missing owner or GLB path")
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return _failure("GLB parse failed: %s" % path)
	var scene := document.generate_scene(state) as Node3D
	if scene == null:
		return _failure("GLB scene generation failed: %s" % path)
	scene.visible = false
	owner.add_child(scene)

	var player := _first_animation_player(scene)
	var mesh_instance := _first_skinned_mesh(scene)
	var skeleton := _resolve_skeleton(mesh_instance) if mesh_instance != null else null
	if player == null or mesh_instance == null or skeleton == null or mesh_instance.skin == null:
		scene.queue_free()
		return _failure("template has no AnimationPlayer/Skeleton3D/skinned mesh")

	var animation_names: Array[String] = []
	for value in player.get_animation_list():
		animation_names.append(String(value))
	var mapping := ClipResolver.resolve(template_name, animation_names)
	var slots := mapping.get("slots", []) as Array[String]
	if slots.is_empty() or slots[0].is_empty():
		scene.queue_free()
		return _failure("template has no usable animation clips")

	var unique_clips: Array[String] = []
	for clip in slots:
		if not clip.is_empty() and clip not in unique_clips:
			unique_clips.append(clip)
	var offsets_by_clip: Dictionary = {}
	var counts_by_clip: Dictionary = {}
	var total_frames := 0
	for clip in unique_clips:
		var animation := player.get_animation(clip)
		if animation == null:
			continue
		var count := maxi(1, ceili(animation.length * float(FRAMES_PER_SECOND)) + 1)
		offsets_by_clip[clip] = total_frames
		counts_by_clip[clip] = count
		total_frames += count

	var skin := mesh_instance.skin
	var rigid_meshes: Array[MeshInstance3D] = []
	_collect_rigid_meshes(scene, mesh_instance, rigid_meshes)
	var skin_bone_count := skin.get_bind_count()
	var bone_count := skin_bone_count + rigid_meshes.size()
	var width := bone_count * TEXELS_PER_BONE
	if bone_count < 1 or width > MAX_TEXTURE_DIMENSION or total_frames > MAX_TEXTURE_DIMENSION:
		scene.queue_free()
		return _failure(
			"atlas exceeds %dx%d limit (bones=%d width=%d frames=%d)"
			% [MAX_TEXTURE_DIMENSION, MAX_TEXTURE_DIMENSION, bone_count, width, total_frames]
		)

	var bind_bones: Array[int] = []
	var bind_poses: Array[Transform3D] = []
	for bind_index in skin_bone_count:
		var bone := skin.get_bind_bone(bind_index)
		if bone < 0:
			bone = skeleton.find_bone(skin.get_bind_name(bind_index))
		if bone < 0 or bone >= skeleton.get_bone_count():
			scene.queue_free()
			return _failure("skin bind %d does not resolve on skeleton" % bind_index)
		bind_bones.append(bone)
		bind_poses.append(skin.get_bind_pose(bind_index))

	var image := Image.create(width, total_frames, false, Image.FORMAT_RGBAF)
	for clip in unique_clips:
		if not offsets_by_clip.has(clip):
			continue
		var animation := player.get_animation(clip)
		var offset := int(offsets_by_clip[clip])
		var count := int(counts_by_clip[clip])
		player.play(clip)
		for frame in count:
			var seconds := minf(float(frame) / float(FRAMES_PER_SECOND), animation.length)
			player.seek(seconds, true, true)
			player.advance(0.0)
			skeleton.force_update_all_bone_transforms()
			for bind_index in skin_bone_count:
				var matrix: Transform3D = (
					skeleton.get_bone_global_pose(bind_bones[bind_index])
					* bind_poses[bind_index]
				)
				_write_matrix(image, bind_index * TEXELS_PER_BONE, offset + frame, matrix)
			for rigid_index in rigid_meshes.size():
				var rigid_matrix := (
					skeleton.global_transform.affine_inverse()
					* rigid_meshes[rigid_index].global_transform
				)
				_write_matrix(
					image,
					(skin_bone_count + rigid_index) * TEXELS_PER_BONE,
					offset + frame,
					rigid_matrix
				)
	player.stop()

	var slot_offsets := PackedInt32Array()
	var slot_counts := PackedInt32Array()
	for clip in slots:
		slot_offsets.append(int(offsets_by_clip.get(clip, 0)))
		slot_counts.append(int(counts_by_clip.get(clip, 1)))
	var atlas := ImageTexture.create_from_image(image)
	var mesh := _copy_instanced_meshes(
		mesh_instance, rigid_meshes, skin_bone_count
	)
	var base_texture := _base_texture(mesh_instance)
	scene.queue_free()
	if mesh == null or atlas == null:
		return _failure("atlas texture or skinned mesh creation failed")
	print(
		"SNAPSHOT_BONE_ATLAS template=%s bones=%d skin_bones=%d rigid=%d clips=%d frames=%d size=%dx%d"
		% [
			template_name,
			bone_count,
			skin_bone_count,
			rigid_meshes.size(),
			unique_clips.size(),
			total_frames,
			width,
			total_frames,
		]
	)
	return {
		"ok": true,
		"reason": "",
		"mesh": mesh,
		"texture": atlas,
		"base_texture": base_texture,
		"slot_offsets": slot_offsets,
		"slot_counts": slot_counts,
		"slot_names": slots,
		"bone_count": bone_count,
		"frame_count": total_frames,
		"size": Vector2i(width, total_frames),
		"mapping": mapping,
	}


static func _copy_instanced_meshes(
	mesh_instance: MeshInstance3D,
	rigid_meshes: Array[MeshInstance3D],
	skin_bone_count: int
) -> ArrayMesh:
	var source := mesh_instance.mesh
	if not (source is ArrayMesh):
		return null
	var result := ArrayMesh.new()
	for surface in source.get_surface_count():
		if source.surface_get_primitive_type(surface) != Mesh.PRIMITIVE_TRIANGLES:
			continue
		var arrays := source.surface_get_arrays(surface)
		if arrays[Mesh.ARRAY_BONES] == null or arrays[Mesh.ARRAY_WEIGHTS] == null:
			continue
		result.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var material := source.surface_get_material(surface)
		if material != null:
			result.surface_set_material(result.get_surface_count() - 1, material)
	for rigid_index in rigid_meshes.size():
		var rigid := rigid_meshes[rigid_index]
		if not (rigid.mesh is ArrayMesh):
			continue
		var rigid_source := rigid.mesh as ArrayMesh
		for surface in rigid_source.get_surface_count():
			if rigid_source.surface_get_primitive_type(surface) != Mesh.PRIMITIVE_TRIANGLES:
				continue
			var arrays := rigid_source.surface_get_arrays(surface)
			var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
			var bones := PackedInt32Array()
			var weights := PackedFloat32Array()
			bones.resize(vertices.size() * 4)
			weights.resize(vertices.size() * 4)
			for vertex in vertices.size():
				bones[vertex * 4] = skin_bone_count + rigid_index
				weights[vertex * 4] = 1.0
			arrays[Mesh.ARRAY_BONES] = bones
			arrays[Mesh.ARRAY_WEIGHTS] = weights
			result.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
			var material := rigid_source.surface_get_material(surface)
			if material != null:
				result.surface_set_material(result.get_surface_count() - 1, material)
	return result if result.get_surface_count() > 0 else null


static func _write_matrix(image: Image, x: int, y: int, matrix: Transform3D) -> void:
	image.set_pixel(x, y, Color(matrix.basis.x.x, matrix.basis.y.x, matrix.basis.z.x, matrix.origin.x))
	image.set_pixel(x + 1, y, Color(matrix.basis.x.y, matrix.basis.y.y, matrix.basis.z.y, matrix.origin.y))
	image.set_pixel(x + 2, y, Color(matrix.basis.x.z, matrix.basis.y.z, matrix.basis.z.z, matrix.origin.z))


static func _base_texture(mesh_instance: MeshInstance3D) -> Texture2D:
	var material := mesh_instance.material_override
	if material == null and mesh_instance.mesh != null and mesh_instance.mesh.get_surface_count() > 0:
		material = mesh_instance.mesh.surface_get_material(0)
	return material.albedo_texture if material is BaseMaterial3D else null


static func _resolve_skeleton(mesh_instance: MeshInstance3D) -> Skeleton3D:
	if mesh_instance == null or mesh_instance.skeleton.is_empty():
		return null
	return mesh_instance.get_node_or_null(mesh_instance.skeleton) as Skeleton3D


static func _first_animation_player(node: Node) -> AnimationPlayer:
	if node is AnimationPlayer and not (node as AnimationPlayer).get_animation_list().is_empty():
		return node as AnimationPlayer
	for child in node.get_children():
		var found := _first_animation_player(child)
		if found != null:
			return found
	return null


static func _first_skinned_mesh(node: Node) -> MeshInstance3D:
	if node is MeshInstance3D:
		var mesh_instance := node as MeshInstance3D
		if mesh_instance.skin != null and mesh_instance.mesh is ArrayMesh:
			return mesh_instance
	for child in node.get_children():
		var found := _first_skinned_mesh(child)
		if found != null:
			return found
	return null


static func _collect_rigid_meshes(
	node: Node, skinned: MeshInstance3D, out: Array[MeshInstance3D]
) -> void:
	if node is MeshInstance3D:
		var mesh_instance := node as MeshInstance3D
		if (
			mesh_instance != skinned
			and mesh_instance.skin == null
			and mesh_instance.mesh is ArrayMesh
		):
			out.append(mesh_instance)
	for child in node.get_children():
		_collect_rigid_meshes(child, skinned, out)


static func _failure(reason: String) -> Dictionary:
	return {"ok": false, "reason": reason}
