class_name RetailPalantirFrame
extends Control
## Draws either the public repository-authored shell or an explicitly-bound
## retail control-bar image. Private parity callers can also fail closed so the
## synthetic shell is never mistaken for converted retail presentation.

enum RenderMode {
	SYNTHETIC,
	HIDDEN,
	RETAIL,
}

var render_mode := RenderMode.SYNTHETIC
var retail_texture: Texture2D
var retail_image_id := ""
var retail_image_path := ""
var retail_source_size := Vector2i.ZERO


func _ready() -> void:
	# Click shield (see `_has_point` below): the bar swallows world clicks
	# wherever its art is opaque. Was MOUSE_FILTER_IGNORE, which let a click on
	# the bezel deselect or order units on the terrain behind the HUD.
	mouse_filter = Control.MOUSE_FILTER_STOP
	queue_redraw()


func _notification(what: int) -> void:
	if what == NOTIFICATION_RESIZED:
		queue_redraw()


func show_public_synthetic_shell() -> void:
	render_mode = RenderMode.SYNTHETIC
	retail_texture = null
	retail_image_id = ""
	retail_image_path = ""
	retail_source_size = Vector2i.ZERO
	queue_redraw()


func fail_closed_private_shell() -> void:
	render_mode = RenderMode.HIDDEN
	retail_texture = null
	retail_image_id = ""
	retail_image_path = ""
	retail_source_size = Vector2i.ZERO
	queue_redraw()


func bind_retail_shell(texture: Texture2D, image_id: String, image_path: String, source_size: Vector2i) -> bool:
	if texture == null or image_id == "" or image_path == "" or source_size.x <= 0 or source_size.y <= 0:
		fail_closed_private_shell()
		return false
	retail_texture = texture
	retail_image_id = image_id
	retail_image_path = image_path
	retail_source_size = source_size
	render_mode = RenderMode.RETAIL
	queue_redraw()
	return true


## Retail composition: the authored control-bar atlas renders as separately
## transformed pieces in retail 16:9 (radar+resource section and palantir dish
## ring use different scales), so each piece maps an atlas region to its own
## measured destination rectangle.
var retail_pieces: Array[Dictionary] = []
var _masked_textures: Dictionary = {}


func bind_retail_composition(texture: Texture2D, image_id: String, image_path: String, source_size: Vector2i, pieces: Array[Dictionary]) -> bool:
	if pieces.is_empty():
		fail_closed_private_shell()
		return false
	if not bind_retail_shell(texture, image_id, image_path, source_size):
		return false
	retail_pieces = pieces
	_masked_textures.clear()
	for index in pieces.size():
		var piece := pieces[index]
		if String(piece.get("kind", "")) != "dish_ring":
			continue
		# The atlas circles overlap, so cut the dish ring to a clean annulus of
		# the authored pixels; everything else in the rectangle belongs to the
		# radar assembly that retail draws on top.
		var region := piece["region"] as Rect2
		var piece_texture := piece.get("texture", texture) as Texture2D
		var source_image := piece_texture.get_image()
		var ring_image := source_image.get_region(Rect2i(region))
		ring_image.convert(Image.FORMAT_RGBA8)
		var annulus_center := piece["annulus_center"] as Vector2
		var inner := float(piece["annulus_inner"])
		var outer := float(piece["annulus_outer"])
		for y in ring_image.get_height():
			for x in ring_image.get_width():
				var distance := Vector2(x + 0.5, y + 0.5).distance_to(annulus_center)
				if distance < inner or distance > outer:
					ring_image.set_pixel(x, y, Color(0, 0, 0, 0))
		_masked_textures[index] = ImageTexture.create_from_image(ring_image)
	queue_redraw()
	return true


func uses_synthetic_shell() -> bool:
	return render_mode == RenderMode.SYNTHETIC


func has_retail_shell() -> bool:
	return render_mode == RenderMode.RETAIL and retail_texture != null


func _draw() -> void:
	if render_mode == RenderMode.HIDDEN:
		return
	if render_mode == RenderMode.RETAIL:
		if retail_texture == null:
			return
		if retail_pieces.is_empty():
			draw_texture_rect(retail_texture, Rect2(Vector2.ZERO, size), false)
			return
		for index in retail_pieces.size():
			var piece := retail_pieces[index]
			var kind := String(piece.get("kind", ""))
			if bool(piece.get("shield_only", false)):
				# The piece still click-shields (shields_point walks every
				# piece) but paints nothing: the recooked pack's stage-piece
				# art owns these pixels (owner 2026-08-26).
				continue
			if kind == "disc":
				# APT-style solid vector geometry: the dish void behind the
				# ring, matching retail's flat dark backing shape. A piece with
				# "half_extents" is an ELLIPSE: the authored opening is a circle
				# in sheet/stage space and the stage transform is non-uniform
				# (1.875 x 1.40625), so its dock-space footprint is not round.
				# A piece carrying "texture" draws retail's OWN glass sprite
				# (palantirmainglass, from the authored sheet split) stretched
				# over the opening instead of the flat stand-in colour.
				if piece.has("half_extents"):
					var half_extents := piece["half_extents"] as Vector2
					draw_set_transform(piece["center"] as Vector2, 0.0, half_extents)
					draw_circle(Vector2.ZERO, 1.0, piece["color"] as Color)
					draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
					# The authored glass is SEMI-TRANSPARENT (retail layers it
					# over the dark swirl render), so it draws OVER the dark
					# backing, never instead of it.
					var glass_texture := piece.get("texture") as Texture2D
					if glass_texture != null:
						var glass_center := piece["center"] as Vector2
						draw_texture_rect(
							glass_texture,
							Rect2(glass_center - half_extents, half_extents * 2.0),
							false
						)
				else:
					draw_circle(piece["center"] as Vector2, float(piece["radius"]), piece["color"] as Color)
			elif kind == "dish_ring" and _masked_textures.has(index):
				draw_texture_rect(_masked_textures[index] as Texture2D, piece["dest"] as Rect2, false)
			else:
				draw_texture_rect_region(retail_texture, piece["dest"] as Rect2, piece["region"] as Rect2)
		return
	var center := Vector2(size.x * 0.5, size.y * 0.46)
	var radius := minf(size.x * 0.47, size.y * 0.48)
	var outer := Color("9f8650")
	var bright := Color("e4ce8b")
	var shadow := Color("1a2028")
	draw_circle(center, radius + 10.0, Color(0.015, 0.025, 0.035, 0.96))
	draw_arc(center, radius + 8.0, 0.0, TAU, 96, shadow, 14.0, true)
	draw_arc(center, radius + 4.0, 0.0, TAU, 96, outer, 7.0, true)
	draw_arc(center, radius, 0.0, TAU, 96, bright, 2.0, true)
	draw_arc(center, radius - 7.0, 0.0, TAU, 96, Color("423c2c"), 4.0, true)
	# Side fins and the lower resource-scroll hooks give the dock the same visual
	# weight as a classic RTS command Palantir without reproducing retail art.
	var left := center + Vector2(-radius - 4.0, radius * 0.35)
	var right := center + Vector2(radius + 4.0, radius * 0.35)
	var left_fin := PackedVector2Array([
		left + Vector2(0, -42), left + Vector2(-34, -22), left + Vector2(-12, 0),
		left + Vector2(-42, 25), left + Vector2(4, 18),
	])
	var right_fin := PackedVector2Array()
	for point in left_fin:
		right_fin.append(Vector2(size.x - point.x, point.y))
	draw_colored_polygon(left_fin, shadow)
	draw_polyline(left_fin, outer, 4.0, true)
	draw_colored_polygon(right_fin, shadow)
	draw_polyline(right_fin, outer, 4.0, true)
	var jewel := center + Vector2(0, -radius - 4.0)
	draw_circle(jewel, 14.0, shadow)
	draw_circle(jewel, 9.0, Color("2f7caa"))
	draw_circle(jewel - Vector2(2, 2), 3.0, Color("bceaff"))


## CLICK SHIELD. Retail's control bar swallows every mouse event that lands on
## its art: you cannot select, deselect or order units on the terrain that
## happens to be drawn behind the palantir frame. This control used to be
## MOUSE_FILTER_IGNORE, so a click on the gold bezel fell through to
## `_unhandled_input` and became a world click - a deselect, or a move order to
## a point under the HUD. The shield is ALPHA-ACCURATE: `_has_point` reports a
## hit only where the bound retail art is opaque (or inside the solid dish
## backing disc), so the transparent corners of the rectangle still reach the
## world exactly as retail's non-rectangular bar does. The synthetic shell
## shields its whole circle.
const SHIELD_ALPHA_THRESHOLD := 0.05
var _shield_images: Dictionary = {}


func _shield_image_for(texture: Texture2D) -> Image:
	if texture == null:
		return null
	var key := texture.get_instance_id()
	if not _shield_images.has(key):
		var image := texture.get_image()
		if image == null:
			return null
		image = image.duplicate()
		if image.is_compressed():
			image.decompress()
		image.convert(Image.FORMAT_RGBA8)
		_shield_images[key] = image
	return _shield_images[key] as Image


func _shield_piece_hit(piece: Dictionary, index: int, point: Vector2) -> bool:
	var kind := String(piece.get("kind", ""))
	if kind == "disc":
		if piece.has("half_extents"):
			return ((point - (piece["center"] as Vector2)) / (piece["half_extents"] as Vector2)).length() <= 1.0
		return point.distance_to(piece["center"] as Vector2) <= float(piece["radius"])
	var dest := piece["dest"] as Rect2
	if not dest.has_point(point):
		return false
	var texture: Texture2D = _masked_textures.get(index) if kind == "dish_ring" and _masked_textures.has(index) else piece.get("texture", retail_texture)
	var image := _shield_image_for(texture)
	if image == null:
		return true
	var local := (point - dest.position) / dest.size
	var sample := Vector2.ZERO
	if kind == "dish_ring" and _masked_textures.has(index):
		sample = local * Vector2(image.get_width(), image.get_height())
	else:
		var region := piece["region"] as Rect2
		sample = region.position + local * region.size
	var x := clampi(int(sample.x), 0, image.get_width() - 1)
	var y := clampi(int(sample.y), 0, image.get_height() - 1)
	return image.get_pixel(x, y).a >= SHIELD_ALPHA_THRESHOLD


func shields_point(point: Vector2) -> bool:
	## Local-space hit test used by `_has_point` and by tests.
	if render_mode == RenderMode.HIDDEN:
		return false
	if render_mode == RenderMode.RETAIL:
		if retail_texture == null:
			return false
		if retail_pieces.is_empty():
			var image := _shield_image_for(retail_texture)
			if image == null or size.x <= 0.0 or size.y <= 0.0:
				return Rect2(Vector2.ZERO, size).has_point(point)
			var x := clampi(int(point.x / size.x * image.get_width()), 0, image.get_width() - 1)
			var y := clampi(int(point.y / size.y * image.get_height()), 0, image.get_height() - 1)
			return Rect2(Vector2.ZERO, size).has_point(point) and image.get_pixel(x, y).a >= SHIELD_ALPHA_THRESHOLD
		for index in retail_pieces.size():
			if _shield_piece_hit(retail_pieces[index], index, point):
				return true
		return false
	var center := Vector2(size.x * 0.5, size.y * 0.46)
	var radius := minf(size.x * 0.47, size.y * 0.48)
	return point.distance_to(center) <= radius + 10.0


func _has_point(point: Vector2) -> bool:
	return shields_point(point)


func _gui_input(event: InputEvent) -> void:
	# Everything that reaches the shield stops here; nothing under the bar
	# art is a legal world target in retail.
	if event is InputEventMouseButton or event is InputEventMouseMotion:
		accept_event()
