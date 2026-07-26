class_name SkinnedPoseBaker
extends RefCounted
## Bake a member's skinned GLB subtree into a single static ArrayMesh at one pose.
##
## Distant battalion members are rendered as MultiMesh instances (see
## member_render_batcher.gd). MultiMesh cannot instance a skinned mesh: a
## MultiMesh draws raw vertex data with no Skeleton3D, so feeding it the source
## skinned surface would render every distant soldier in its bind pose. This
## baker therefore applies the skinning transform on the CPU once per
## (model, pose) and hands the batcher a static, correctly posed proxy.
##
## FAIL-CLOSED CONTRACT
## The skinning transform depends on Godot's skin/skeleton space conventions. A
## wrong convention does not error - it silently produces mangled geometry. So
## every bake first runs `validate_rest_identity`: baking at the skeleton's REST
## pose must reproduce the source vertices, because at rest the skinning matrix
## `global_rest(bone) * bind_pose(bone)` is the identity by construction. If that
## check fails the baker refuses and returns an explicit reason string; the
## caller keeps the full skinned node visible rather than drawing garbage.

## Max allowed per-vertex deviation (in mesh-local units) when re-baking at the
## rest pose. Retail member meshes are ~1.8 units tall, so this is ~0.05% of the
## model height - large enough to absorb float32 accumulation over 4 weights,
## far too small to hide a wrong-space bake (which misplaces limbs by whole units).
const REST_IDENTITY_TOLERANCE := 0.001

## Surfaces above this vertex count are refused. Retail members are 511-928
## verts; anything an order of magnitude larger is placeholder/base content that
## would make the CPU bake a load-time stall rather than a sub-millisecond cost.
const MAX_BAKE_VERTICES_PER_SURFACE := 20000


## One bake result. `mesh` is a static ArrayMesh in the member visual root's
## local space, ready to be handed to a MultiMesh.
class BakedPose:
	extends RefCounted
	var mesh: ArrayMesh
	var surface_count := 0
	var vertex_count := 0
	var source_mesh_instances := 0
	var skinned_surfaces := 0
	var static_surfaces := 0
	var rest_identity_max_deviation := 0.0


## Bake `member_visual` (an asset_factory `Object_<id>` root, already inside the
## tree so skeleton poses are resolved) into a single static ArrayMesh.
##
## Returns [BakedPose, ""] on success or [null, reason] on refusal. The reason is
## surfaced by the batcher and printed once - never swallowed.
static func bake(member_visual: Node3D) -> Array:
	if member_visual == null or not is_instance_valid(member_visual):
		return [null, "member visual is null"]
	if not member_visual.is_inside_tree():
		return [null, "member visual is not inside the tree, skeleton poses are unresolved"]
	var mesh_instances: Array[MeshInstance3D] = []
	_collect_mesh_instances(member_visual, mesh_instances)
	if mesh_instances.is_empty():
		return [null, "member visual has no MeshInstance3D to bake"]

	var baked := BakedPose.new()
	baked.mesh = ArrayMesh.new()
	baked.source_mesh_instances = mesh_instances.size()
	var root_inverse := member_visual.global_transform.affine_inverse()

	for mesh_instance in mesh_instances:
		var source_mesh := mesh_instance.mesh
		if source_mesh == null:
			continue
		if not (source_mesh is ArrayMesh):
			# QuadMesh/BoxMesh/etc. carry no surface arrays we can re-emit
			# faithfully. Refuse the whole model rather than dropping a limb.
			return [null, "source mesh '%s' is %s, not an ArrayMesh" % [mesh_instance.name, source_mesh.get_class()]]
		var array_mesh := source_mesh as ArrayMesh
		var skeleton := _resolve_skeleton(mesh_instance)
		var skin_matrices: Array[Transform3D] = []
		var rest_matrices: Array[Transform3D] = []
		var is_skinned := mesh_instance.skin != null and skeleton != null
		if is_skinned:
			var built := _build_skin_matrices(mesh_instance, skeleton)
			var skin_error := String(built[2])
			if skin_error != "":
				return [null, skin_error]
			skin_matrices = built[0]
			rest_matrices = built[1]

		for surface_index in array_mesh.get_surface_count():
			if (array_mesh.surface_get_primitive_type(surface_index)
					!= Mesh.PRIMITIVE_TRIANGLES):
				return [null, "surface %d of '%s' is not a triangle list" % [surface_index, mesh_instance.name]]
			var arrays: Array = array_mesh.surface_get_arrays(surface_index)
			var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			if vertices.size() > MAX_BAKE_VERTICES_PER_SURFACE:
				return [null, "surface %d of '%s' has %d vertices, over the %d bake budget" % [
					surface_index, mesh_instance.name, vertices.size(), MAX_BAKE_VERTICES_PER_SURFACE
				]]
			var surface_format := array_mesh.surface_get_format(surface_index)
			var surface_is_skinned := (
				is_skinned
				and (surface_format & Mesh.ARRAY_FORMAT_BONES) != 0
				and (surface_format & Mesh.ARRAY_FORMAT_WEIGHTS) != 0
			)

			# The bake target space is the member visual root, so a distant
			# instance transform is just the member's global transform.
			var static_transform := root_inverse * (
				skeleton.global_transform if surface_is_skinned else mesh_instance.global_transform
			)

			if surface_is_skinned:
				var bones: PackedInt32Array = arrays[Mesh.ARRAY_BONES]
				var weights: PackedFloat32Array = arrays[Mesh.ARRAY_WEIGHTS]
				var influences := bones.size() / maxi(1, vertices.size())
				if influences != 4 and influences != 8:
					return [null, "surface %d of '%s' has %d bone influences per vertex" % [
						surface_index, mesh_instance.name, influences
					]]
				# Fail-closed gate: prove the skinning convention on this exact
				# surface before trusting the animated bake.
				var deviation := _rest_identity_deviation(
					vertices, bones, weights, influences, rest_matrices
				)
				if deviation > REST_IDENTITY_TOLERANCE:
					return [null, "rest-pose identity check failed on surface %d of '%s' (max deviation %.5f > %.5f); skin/skeleton space convention does not hold, refusing to bake" % [
						surface_index, mesh_instance.name, deviation, REST_IDENTITY_TOLERANCE
					]]
				baked.rest_identity_max_deviation = maxf(baked.rest_identity_max_deviation, deviation)
				arrays = _apply_skinning(arrays, bones, weights, influences, skin_matrices)
				baked.skinned_surfaces += 1
			else:
				baked.static_surfaces += 1

			arrays = _transform_arrays(arrays, static_transform)
			# Bone/weight channels are meaningless on a static proxy and would
			# make Godot treat the surface as skinned again.
			arrays[Mesh.ARRAY_BONES] = null
			arrays[Mesh.ARRAY_WEIGHTS] = null
			baked.mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
			var material := _surface_material(mesh_instance, array_mesh, surface_index)
			if material != null:
				baked.mesh.surface_set_material(baked.mesh.get_surface_count() - 1, material)
			baked.vertex_count += vertices.size()

	if baked.mesh.get_surface_count() == 0:
		return [null, "bake produced no surfaces"]
	baked.surface_count = baked.mesh.get_surface_count()
	return [baked, ""]


## Build, per bind index, both the animated skinning matrix and the rest
## skinning matrix. Returns [skin_matrices, rest_matrices, error].
static func _build_skin_matrices(mesh_instance: MeshInstance3D, skeleton: Skeleton3D) -> Array:
	var skin: Skin = mesh_instance.skin
	var skin_matrices: Array[Transform3D] = []
	var rest_matrices: Array[Transform3D] = []
	for bind_index in skin.get_bind_count():
		# GLTF skins routinely leave bind_bone at -1 and identify the joint by
		# name instead, so resolve by name whenever the index is absent.
		var bone_index := skin.get_bind_bone(bind_index)
		if bone_index < 0:
			var bind_name := skin.get_bind_name(bind_index)
			if bind_name == "":
				return [[], [], "skin bind %d of '%s' has neither a bone index nor a bone name" % [bind_index, mesh_instance.name]]
			bone_index = skeleton.find_bone(bind_name)
		if bone_index < 0 or bone_index >= skeleton.get_bone_count():
			return [[], [], "skin bind %d of '%s' does not resolve to a bone on '%s'" % [bind_index, mesh_instance.name, skeleton.name]]
		var bind_pose := skin.get_bind_pose(bind_index)
		skin_matrices.append(skeleton.get_bone_global_pose(bone_index) * bind_pose)
		rest_matrices.append(skeleton.get_bone_global_rest(bone_index) * bind_pose)
	return [skin_matrices, rest_matrices, ""]


## Max distance between each source vertex and the same vertex skinned by the
## REST matrices. Correct convention => ~0 (the rest skinning matrix is identity).
static func _rest_identity_deviation(
	vertices: PackedVector3Array,
	bones: PackedInt32Array,
	weights: PackedFloat32Array,
	influences: int,
	rest_matrices: Array[Transform3D]
) -> float:
	var worst := 0.0
	var bind_count := rest_matrices.size()
	for vertex_index in vertices.size():
		var source := vertices[vertex_index]
		var accumulated := Vector3.ZERO
		var total_weight := 0.0
		var base := vertex_index * influences
		for influence in influences:
			var weight := weights[base + influence]
			if weight <= 0.0:
				continue
			var bone := bones[base + influence]
			if bone < 0 or bone >= bind_count:
				continue
			accumulated += (rest_matrices[bone] * source) * weight
			total_weight += weight
		if total_weight <= 0.0001:
			continue
		accumulated /= total_weight
		worst = maxf(worst, source.distance_to(accumulated))
	return worst


static func _apply_skinning(
	arrays: Array,
	bones: PackedInt32Array,
	weights: PackedFloat32Array,
	influences: int,
	skin_matrices: Array[Transform3D]
) -> Array:
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: Variant = arrays[Mesh.ARRAY_NORMAL]
	var has_normals := normals is PackedVector3Array and (normals as PackedVector3Array).size() == vertices.size()
	var source_normals: PackedVector3Array = normals if has_normals else PackedVector3Array()
	var out_vertices := PackedVector3Array()
	out_vertices.resize(vertices.size())
	var out_normals := PackedVector3Array()
	if has_normals:
		out_normals.resize(vertices.size())
	var bind_count := skin_matrices.size()

	for vertex_index in vertices.size():
		var source := vertices[vertex_index]
		var accumulated := Vector3.ZERO
		var accumulated_normal := Vector3.ZERO
		var total_weight := 0.0
		var base := vertex_index * influences
		for influence in influences:
			var weight := weights[base + influence]
			if weight <= 0.0:
				continue
			var bone := bones[base + influence]
			if bone < 0 or bone >= bind_count:
				continue
			var matrix := skin_matrices[bone]
			accumulated += (matrix * source) * weight
			if has_normals:
				accumulated_normal += (matrix.basis * source_normals[vertex_index]) * weight
			total_weight += weight
		if total_weight > 0.0001:
			out_vertices[vertex_index] = accumulated / total_weight
			if has_normals:
				var normal := accumulated_normal / total_weight
				out_normals[vertex_index] = normal.normalized() if normal.length_squared() > 0.000001 else source_normals[vertex_index]
		else:
			# Unweighted vertex: keep it where the artist put it rather than
			# collapsing it to the origin and stretching a triangle across the map.
			out_vertices[vertex_index] = source
			if has_normals:
				out_normals[vertex_index] = source_normals[vertex_index]

	arrays[Mesh.ARRAY_VERTEX] = out_vertices
	if has_normals:
		arrays[Mesh.ARRAY_NORMAL] = out_normals
	return arrays


static func _transform_arrays(arrays: Array, transform: Transform3D) -> Array:
	if transform.is_equal_approx(Transform3D.IDENTITY):
		return arrays
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var out_vertices := PackedVector3Array()
	out_vertices.resize(vertices.size())
	for index in vertices.size():
		out_vertices[index] = transform * vertices[index]
	arrays[Mesh.ARRAY_VERTEX] = out_vertices
	var normals: Variant = arrays[Mesh.ARRAY_NORMAL]
	if normals is PackedVector3Array:
		var source_normals: PackedVector3Array = normals
		var normal_basis := transform.basis.inverse().transposed()
		var out_normals := PackedVector3Array()
		out_normals.resize(source_normals.size())
		for index in source_normals.size():
			out_normals[index] = (normal_basis * source_normals[index]).normalized()
		arrays[Mesh.ARRAY_NORMAL] = out_normals
	return arrays


static func _surface_material(
	mesh_instance: MeshInstance3D, array_mesh: ArrayMesh, surface_index: int
) -> Material:
	# Override precedence matches what the live skinned node draws, so the
	# batched proxy keeps the member's house colour / team tint.
	var override_material := mesh_instance.get_surface_override_material(surface_index)
	if override_material != null:
		return override_material
	if mesh_instance.material_override != null:
		return mesh_instance.material_override
	return array_mesh.surface_get_material(surface_index)


static func _resolve_skeleton(mesh_instance: MeshInstance3D) -> Skeleton3D:
	var skeleton_path := mesh_instance.skeleton
	if skeleton_path.is_empty():
		return null
	return mesh_instance.get_node_or_null(skeleton_path) as Skeleton3D


static func _collect_mesh_instances(node: Node, out: Array[MeshInstance3D]) -> void:
	if node is MeshInstance3D:
		out.append(node as MeshInstance3D)
	for child in node.get_children():
		_collect_mesh_instances(child, out)
