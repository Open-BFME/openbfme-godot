extends Decal
## Retail SAGE renders unit/structure contact shadows as dark decals
## (environment source: shadow volumes + decals, shadow mapping disabled,
## shadow color ARGB(64,0,0,0)). This node is the approved Godot equivalence:
## a radial blob decal at the exact source shadow color, projected onto the
## ground beneath the owner.

const SOURCE_SHADOW_COLOR := Color(0.0, 0.0, 0.0, 64.0 / 255.0)

static var _blob_texture: GradientTexture2D


static func _shared_texture() -> GradientTexture2D:
	if _blob_texture == null:
		var gradient := Gradient.new()
		gradient.colors = PackedColorArray([Color(1, 1, 1, 1), Color(1, 1, 1, 0)])
		gradient.offsets = PackedFloat32Array([0.55, 1.0])
		_blob_texture = GradientTexture2D.new()
		_blob_texture.gradient = gradient
		_blob_texture.fill = GradientTexture2D.FILL_RADIAL
		_blob_texture.fill_from = Vector2(0.5, 0.5)
		_blob_texture.fill_to = Vector2(0.5, 0.0)
		_blob_texture.width = 64
		_blob_texture.height = 64
	return _blob_texture


static func create(footprint: Vector2, vertical_reach: float) -> Decal:
	var decal: Decal = load("res://src/retail_slice/retail_shadow_decal.gd").new()
	decal.name = "RetailShadowDecal"
	decal.texture_albedo = _shared_texture()
	decal.modulate = SOURCE_SHADOW_COLOR
	decal.size = Vector3(maxf(footprint.x, 0.05), maxf(vertical_reach, 0.5), maxf(footprint.y, 0.05))
	return decal
