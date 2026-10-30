## Godot shadow maps as a presentation substitute for SAGE volume shadows (S-3304).
## The scene's existing main object light supplies coverage; the shaders retain their retail lighting.
extends RefCounted

static func configure(sun: DirectionalLight3D, lighting: Dictionary, lod: int = 3) -> void:
	var enabled := lod >= 2
	if lighting.has("direction"):
		var direction: Vector3 = lighting.direction
		enabled = enabled and direction.is_finite() and direction.length_squared() > 0.00000001
	sun.shadow_enabled = enabled
	if enabled:
		# RotWK's shadow map is MapSize² (ShadowMap INI block, retail 1024, RW 0x47B69E), at least 2048² at ShadowLOD
		# UltraHigh (RW 0x47B2B5). PARALLEL_4_SPLITS gives each split a quarter of the atlas, so the edge is twice that.
		# Medium keeps High's size: retail draws no shadow map there.
		RenderingServer.directional_shadow_atlas_set_size(4096 if lod >= 4 else 2048, false)
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_blend_splits = true
	# SAGE world units: the tactical camera is hundreds of units above the ground.
	sun.directional_shadow_max_distance = 4000.0
	sun.directional_shadow_pancake_size = 0.0
	sun.shadow_bias = 0.05
	sun.shadow_normal_bias = 0.5
	var packed := int(lighting.shadow_color) # GlobalLighting's ShadowColor (the engine's lighting report)
	RenderingServer.global_shader_parameter_set("w3d_shadow", Vector4(
		float((packed >> 16) & 255) / 255.0,
		float((packed >> 8) & 255) / 255.0,
		float(packed & 255) / 255.0, 1.0 if enabled else 0.0))
	ClassDB.class_call_static("W3DInstancer", "set_shadow_maps_enabled", enabled)

static func disable() -> void:
	RenderingServer.directional_shadow_atlas_set_size(4096, false) # Godot's default, so a later scene does not inherit ours
	RenderingServer.global_shader_parameter_set("w3d_shadow", Vector4(1.0, 1.0, 1.0, 0.0))
	ClassDB.class_call_static("W3DInstancer", "set_shadow_maps_enabled", false)
