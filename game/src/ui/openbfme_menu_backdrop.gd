class_name OpenBFMEMenuBackdrop
extends Control
## Deterministic, code-native menu atmosphere composed after the retail shell's
## Argonath river-gorge scene (REF-07): a bright hazy blue-grey gorge, colossal
## robed sentinels cropped by the frame on either side, receding rock walls that
## dissolve into aerial haze toward a luminous gap in the centre distance, soft
## light shafts, a thin veil-like fall on the left wall and still water at the
## base.
##
## Everything is drawn with Godot draw calls over procedurally generated noise
## textures built at runtime (rock, drape, mist, water, glow, shaft). There are
## no imported bytes, no embedded images and nothing traced from retail art.
## Depth comes from aerial perspective: each mass is tinted toward HAZE by its
## distance, so silhouettes lighten and lose contrast as they recede.
##
## PLACEHOLDER NOTE: retail renders a live 3D shellmap here. No converted
## backdrop resource exists in the content packs yet (data/ui_manifest.json
## carries only HUD atlas crops). When the importer ships a converted shell
## backdrop image, main_menu.gd's _apply_converted_backdrop() displays it in the
## BackdropArt TextureRect and hides this drawing — private parity art is never
## silently substituted by this placeholder when it exists.

# --- Palette (sampled by eye from REF-07: high-key, blue-grey, never green) ---
const HAZE_CORE := Color("#eef3f6")
const HAZE := Color("#c2d4e2")
const SKY_TOP := Color("#7590a8")
const SKY_MID := Color("#9db4c6")
const ROCK_TOP := Color("#727d6f")
const ROCK_BOTTOM := Color("#2d3739")
const NEAR_TOP := Color("#212a1d")
const NEAR_BOTTOM := Color("#060a0c")
const FOLIAGE := Color("#101a0d")
const FOLIAGE_LIT := Color("#334528")
const STONE_TOP := Color("#a9b2b1")
const STONE_BOTTOM := Color("#525d5f")
const STONE_SHADE := Color("#2d3739")
const WATER_TOP := Color("#7d8c94")
const WATER_BOTTOM := Color("#2e3a38")

## Horizontal centre of the luminous gorge gap, in screen fractions.
const GAP_X := 0.478
## Waterline; everything above dissolves into it.
const WATER_Y := 0.905

# Generated once per process and shared by every shell instance.
static var _sky_texture: ImageTexture
static var _rock_texture: ImageTexture
static var _drape_texture: ImageTexture
static var _mist_texture: ImageTexture
static var _falls_texture: ImageTexture
static var _shaft_texture: ImageTexture
static var _glow_texture: ImageTexture
static var _vignette_texture: ImageTexture

var _time := 0.0


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	texture_repeat = CanvasItem.TEXTURE_REPEAT_ENABLED
	_build_textures()
	resized.connect(queue_redraw)
	set_process(true)
	queue_redraw()


func _process(delta: float) -> void:
	# Slow drift only: mist creep, falling water and a lazy water shimmer. The
	# scene is otherwise static, so this stays well under a millisecond a frame.
	if not is_visible_in_tree():
		return
	_time += delta
	queue_redraw()


func _draw() -> void:
	if size.x <= 1.0 or size.y <= 1.0:
		return
	_draw_sky()
	_draw_distant_walls()
	_draw_midground()
	_draw_falls()
	_draw_right_colossus()
	_draw_left_colossus()
	_draw_valley_mist()
	# Shafts sit behind the near walls: the dark frame is what sells the depth,
	# so nothing is allowed to wash it out.
	_draw_light_shafts()
	_draw_near_cliffs()
	_draw_water()
	_draw_air()


# ---------------------------------------------------------------- sky & depth

func _draw_sky() -> void:
	draw_texture_rect(_sky_texture, Rect2(Vector2.ZERO, size), false)


func _draw_distant_walls() -> void:
	## The far end of the gorge: pale buttes barely separated from the haze,
	## leaving a bright slot on the gap axis that the eye reads as distance.
	_draw_mass(_ridge([
		Vector2(-0.05, 0.74), Vector2(0.16, 0.62), Vector2(0.30, 0.66),
		Vector2(0.41, 0.60), Vector2(0.452, 0.72),
	], 0.008, 21, WATER_Y + 0.02), 0.90, 0.94)
	_draw_mass(_ridge([
		Vector2(0.505, 0.74), Vector2(0.532, 0.46), Vector2(0.562, 0.38),
		Vector2(0.61, 0.45), Vector2(0.72, 0.41), Vector2(1.05, 0.53),
	], 0.010, 57, WATER_Y + 0.02), 0.86, 0.90)
	# A second, slightly nearer shoulder on the right of the slot.
	_draw_mass(_ridge([
		Vector2(0.514, 0.82), Vector2(0.548, 0.57), Vector2(0.59, 0.49),
		Vector2(0.67, 0.53), Vector2(0.78, 0.59), Vector2(1.05, 0.67),
	], 0.009, 91, WATER_Y + 0.02), 0.66, 0.78)


func _draw_midground() -> void:
	## The big pale butte right of the gap (REF-07's centre-right rock mass) and
	## the darker wall that carries the fall on the left.
	_draw_mass(_ridge([
		Vector2(0.500, 0.88), Vector2(0.522, 0.66), Vector2(0.545, 0.50),
		Vector2(0.566, 0.40), Vector2(0.592, 0.355), Vector2(0.620, 0.345),
		Vector2(0.648, 0.375), Vector2(0.680, 0.44), Vector2(0.760, 0.55),
		Vector2(0.920, 0.72),
	], 0.008, 133, WATER_Y + 0.02), 0.50, 0.64)
	_draw_mass(_ridge([
		Vector2(0.60, 0.92), Vector2(0.645, 0.62), Vector2(0.69, 0.50),
		Vector2(0.74, 0.55), Vector2(0.83, 0.66), Vector2(1.05, 0.80),
	], 0.010, 177, WATER_Y + 0.03), 0.24, 0.40)
	# Left gorge wall behind the near colossus: darker, catching rim light. Its
	# inner face carries the fall, so it must stay high across the fall's run.
	_draw_mass(_ridge([
		Vector2(-0.05, 0.28), Vector2(0.08, 0.10), Vector2(0.18, 0.17),
		Vector2(0.28, 0.08), Vector2(0.355, 0.15), Vector2(0.405, 0.21),
		Vector2(0.432, 0.33), Vector2(0.444, 0.62), Vector2(0.452, 0.90),
	], 0.012, 211, WATER_Y + 0.03), 0.06, 0.22, Color("#434b3d"), Color("#1d241f"), 0.26)
	# A deep shadowed cleft between that wall and the gap: the fall drops through
	# it, and the darkness is what gives the near sentinel something to sit
	# against.
	_blob(Vector2(0.428, 0.52), Vector2(0.055, 0.40), Color(0.07, 0.10, 0.10, 0.50))
	_blob(Vector2(0.437, 0.80), Vector2(0.035, 0.20), Color(0.07, 0.10, 0.10, 0.36))


func _draw_valley_mist() -> void:
	## Haze pooling in the gorge floor: dissolves every silhouette base so no
	## mass ends on a hard line, and keys the whole frame bright like REF-07.
	var drift := fmod(_time * 0.006, 1.0)
	var band := Rect2(0.0, size.y * 0.56, size.x, size.y * 0.40)
	var region := Rect2(drift * 128.0, 0.0, 128.0, 64.0)
	draw_texture_rect_region(_mist_texture, band, region, Color(HAZE_CORE, 0.08))
	region = Rect2(-drift * 210.0, 0.0, 128.0, 64.0)
	draw_texture_rect_region(
		_mist_texture, Rect2(0.0, size.y * 0.70, size.x, size.y * 0.26), region,
		Color(HAZE_CORE, 0.11)
	)
	# The luminous slot itself.
	_blob(Vector2(GAP_X, 0.74), Vector2(0.055, 0.24), Color(HAZE_CORE, 0.46))
	_blob(Vector2(GAP_X, 0.83), Vector2(0.030, 0.13), Color(1.0, 1.0, 1.0, 0.34))


func _draw_air() -> void:
	## Final global grade: a whisper of haze over everything for atmosphere, a
	## soft vignette, and a restrained scrim so the button bar stays legible.
	draw_rect(Rect2(Vector2.ZERO, size), Color(HAZE, 0.02))
	draw_texture_rect(_vignette_texture, Rect2(Vector2.ZERO, size), false, Color(1, 1, 1, 0.85))
	var scrim_top := size.y * 0.80
	var steps := 26
	for index in range(steps):
		var t := float(index) / float(steps - 1)
		var y := scrim_top + (size.y - scrim_top) * (float(index) / float(steps))
		var height := (size.y - scrim_top) / float(steps) + 1.5
		draw_rect(Rect2(0.0, y, size.x, height), Color(0.03, 0.05, 0.05, t * t * 0.30))


# ------------------------------------------------------------------- the fall

func _draw_falls() -> void:
	## A tall thin veil on the left wall (REF-07 keeps it slender and mostly
	## dissolved). Feathered on both sides and faded top and bottom by the
	## generated texture, so no hard rectangle can ever appear.
	var top := size.y * 0.290
	var bottom := size.y * 0.760
	var width := size.x * 0.0105
	var scroll := fmod(_time * 46.0, 192.0)
	var rect := Rect2(size.x * 0.4225 - width * 0.5, top, width, bottom - top)
	draw_texture_rect_region(
		_falls_texture, rect, Rect2(0.0, scroll, 48.0, 192.0), Color(1.0, 1.0, 1.0, 0.74)
	)
	# A wider, softer ghost of the same fall reads as spray drifting off it.
	var ghost := rect.grow_individual(width * 1.4, size.y * 0.01, width * 1.4, size.y * 0.02)
	draw_texture_rect_region(
		_falls_texture, ghost, Rect2(0.0, scroll * 0.6, 48.0, 192.0), Color(HAZE_CORE, 0.17)
	)
	# Bloom where it lands, breathing very slowly.
	var pulse := 0.85 + 0.15 * sin(_time * 0.45)
	_blob(Vector2(0.4225, 0.762), Vector2(0.040, 0.058), Color(HAZE_CORE, 0.32 * pulse))
	_blob(Vector2(0.4225, 0.735), Vector2(0.020, 0.034), Color(1.0, 1.0, 1.0, 0.24 * pulse))


# ----------------------------------------------------------------- the giants

func _draw_left_colossus() -> void:
	## Near sentinel: a hooded, mantled colossus whose cloak falls in one
	## enormous drape into the river. Original silhouette, no retail geometry.
	var outline := _scaled([
		Vector2(0.2660, 0.056), Vector2(0.2695, 0.026), Vector2(0.2760, 0.004),
		Vector2(0.2870, 0.000), Vector2(0.2960, 0.020), Vector2(0.3010, 0.060),
		Vector2(0.3020, 0.098), Vector2(0.3200, 0.116), Vector2(0.3390, 0.150),
		Vector2(0.3480, 0.205), Vector2(0.3520, 0.300), Vector2(0.3600, 0.470),
		Vector2(0.3720, 0.680), Vector2(0.3870, 0.880), Vector2(0.3960, 1.010),
		Vector2(0.1940, 1.010), Vector2(0.1980, 0.740), Vector2(0.2030, 0.500),
		Vector2(0.2100, 0.310), Vector2(0.2160, 0.208), Vector2(0.2280, 0.148),
		Vector2(0.2530, 0.114), Vector2(0.2660, 0.096),
	])
	_draw_contour(outline, Color(0.26, 0.30, 0.28, 0.32), maxf(1.5, size.y * 0.0022))
	_draw_stone(outline, 0.02, 0.0, 1.01)
	# Shadowed side of the drape, away from the light in the gap.
	_draw_shade(_scaled([
		Vector2(0.2530, 0.114), Vector2(0.2720, 0.130), Vector2(0.2640, 0.430),
		Vector2(0.2570, 1.010), Vector2(0.1940, 1.010), Vector2(0.2030, 0.480),
		Vector2(0.2200, 0.190),
	]), Color(STONE_SHADE, 0.42))
	# The cowl's inner shadow, and the clasp below the throat.
	_draw_shade(_scaled([
		Vector2(0.2700, 0.038), Vector2(0.2955, 0.038), Vector2(0.3010, 0.080),
		Vector2(0.2860, 0.104), Vector2(0.2680, 0.082),
	]), Color(0.17, 0.20, 0.19, 0.52))
	_draw_shade(_scaled([
		Vector2(0.2540, 0.124), Vector2(0.2860, 0.112), Vector2(0.3160, 0.128),
		Vector2(0.2900, 0.212), Vector2(0.2620, 0.180),
	]), Color(STONE_SHADE, 0.34))
	# Cloth folds fanning from the shoulders down into the water.
	for fold in range(6):
		var t := float(fold) / 5.0
		var skew := _rand(4400 + fold) * 0.016
		var x_top := 0.2360 + t * 0.0960 + skew * 0.30
		var x_bottom := 0.2060 + t * 0.1780 + skew
		var tone := 0.20 + 0.22 * absf(t - 0.30)
		_draw_fold(x_top, 0.175, x_bottom, 1.005, size.x * (0.0026 + 0.0044 * t), tone)
	# Rim light down the inner edge, facing the bright gap.
	_draw_fold(0.3480, 0.205, 0.3930, 0.990, size.x * 0.0028, 0.0, Color(HAZE_CORE, 0.42))
	_draw_fold(0.3060, 0.070, 0.3320, 0.150, size.x * 0.0020, 0.0, Color(HAZE_CORE, 0.30))


func _draw_right_colossus() -> void:
	## Far sentinel: crowned, one hand closed on a grounded staff, heavy fluted
	## robe columns falling to the water. Reads paler than its twin — it stands
	## further down the gorge, so more air sits in front of it.
	var outline := _scaled([
		Vector2(0.7020, 0.060), Vector2(0.7050, 0.028), Vector2(0.7075, -0.004),
		Vector2(0.7120, 0.018), Vector2(0.7180, -0.012), Vector2(0.7240, 0.014),
		Vector2(0.7300, -0.008), Vector2(0.7355, 0.022), Vector2(0.7390, 0.062),
		Vector2(0.7400, 0.100), Vector2(0.7560, 0.122), Vector2(0.7700, 0.160),
		Vector2(0.7780, 0.220), Vector2(0.7845, 0.360), Vector2(0.7930, 0.560),
		Vector2(0.8030, 0.780), Vector2(0.8130, 1.010), Vector2(0.6460, 1.010),
		Vector2(0.6510, 0.760), Vector2(0.6560, 0.540), Vector2(0.6630, 0.350),
		Vector2(0.6690, 0.222), Vector2(0.6770, 0.160), Vector2(0.6900, 0.124),
		Vector2(0.7020, 0.102),
	])
	_draw_contour(outline, Color(0.40, 0.44, 0.42, 0.26), maxf(1.5, size.y * 0.0020))
	_draw_stone(outline, 0.10, 0.0, 1.01)
	_draw_shade(_scaled([
		Vector2(0.7460, 0.118), Vector2(0.7690, 0.170), Vector2(0.7880, 0.430),
		Vector2(0.8130, 1.010), Vector2(0.7550, 1.010), Vector2(0.7480, 0.440),
		Vector2(0.7420, 0.150),
	]), Color(STONE_SHADE, 0.30))
	_draw_shade(_scaled([
		Vector2(0.7060, 0.040), Vector2(0.7350, 0.040), Vector2(0.7395, 0.082),
		Vector2(0.7230, 0.106), Vector2(0.7045, 0.084),
	]), Color(0.27, 0.31, 0.30, 0.38))
	_draw_shade(_scaled([
		Vector2(0.6910, 0.132), Vector2(0.7220, 0.118), Vector2(0.7530, 0.134),
		Vector2(0.7300, 0.222), Vector2(0.7010, 0.192),
	]), Color(STONE_SHADE, 0.24))
	# Fluted robe columns.
	for column in range(7):
		var t := float(column) / 6.0
		var skew := _rand(8800 + column) * 0.012
		var x_top := 0.6820 + t * 0.0840 + skew * 0.30
		var x_bottom := 0.6540 + t * 0.1500 + skew
		var tone := 0.13 + 0.16 * absf(t - 0.32)
		_draw_fold(x_top, 0.195, x_bottom, 1.005, size.x * (0.0024 + 0.0038 * t), tone)
	_draw_fold(0.6770, 0.160, 0.6480, 0.995, size.x * 0.0026, 0.0, Color(HAZE_CORE, 0.32))
	# Staff grounded in the river, and the hand closed around it.
	_draw_stone(_scaled([
		Vector2(0.6905, 0.300), Vector2(0.6980, 0.300), Vector2(0.7075, 1.010),
		Vector2(0.6840, 1.010),
	]), 0.22, 0.29, 1.01)
	_draw_contour(_scaled([
		Vector2(0.6905, 0.300), Vector2(0.6980, 0.300), Vector2(0.7075, 1.010),
		Vector2(0.6840, 1.010),
	]), Color(0.38, 0.42, 0.40, 0.22), maxf(1.0, size.y * 0.0014))
	_draw_stone(_scaled([
		Vector2(0.6790, 0.318), Vector2(0.7000, 0.302), Vector2(0.7150, 0.318),
		Vector2(0.7140, 0.362), Vector2(0.6920, 0.376), Vector2(0.6760, 0.356),
	]), 0.17, 0.30, 0.38)
	_draw_shade(_scaled([
		Vector2(0.6760, 0.350), Vector2(0.7140, 0.350), Vector2(0.7130, 0.366),
		Vector2(0.6920, 0.376),
	]), Color(STONE_SHADE, 0.40))


# --------------------------------------------------------------- near framing

func _draw_near_cliffs() -> void:
	## Darkest, sharpest masses: the frame. They read almost black against the
	## bright gorge, which is what sells the depth in REF-07.
	var left_profile := [
		Vector2(-0.05, -0.05), Vector2(0.046, 0.01), Vector2(0.079, 0.13),
		Vector2(0.108, 0.31), Vector2(0.127, 0.53), Vector2(0.143, 0.75),
		Vector2(0.157, 0.98),
	]
	_draw_mass(_ridge(left_profile, 0.016, 313, 1.05), 0.01, 0.06, NEAR_TOP, NEAR_BOTTOM, 0.22)
	var right_profile := [
		Vector2(0.851, 0.98), Vector2(0.866, 0.72), Vector2(0.882, 0.48),
		Vector2(0.903, 0.26), Vector2(0.936, 0.09), Vector2(0.979, -0.02),
		Vector2(1.05, -0.06),
	]
	_draw_mass(_ridge(right_profile, 0.016, 401, 1.05), 0.01, 0.06, NEAR_TOP, NEAR_BOTTOM, 0.22)
	# Vegetation crowding the lit inner lips of both walls, as in the reference.
	_draw_foliage_edge(left_profile, 1.0, 0.028, 611)
	_draw_foliage_edge(right_profile, -1.0, 0.026, 733)


func _draw_foliage_edge(profile: Array, direction: float, spread: float, seed_value: int) -> void:
	## Canopy spilling over a cliff's inner lip: one continuous mass whose outer
	## boundary is broken at two frequencies, so it reads as scrub clinging to
	## rock rather than a string of blobs.
	var samples := 96
	var outer := PackedVector2Array()
	var inner := PackedVector2Array()
	for step in range(samples):
		var t := float(step) / float(samples - 1)
		var scaled := t * float(profile.size() - 1)
		var index := clampi(int(scaled), 0, profile.size() - 2)
		var a: Vector2 = profile[index]
		var b: Vector2 = profile[index + 1]
		var anchor := a.lerp(b, scaled - float(index))
		# Two-octave ragged boundary plus a slow envelope that thins the canopy
		# in places, leaving bare rock showing through.
		var envelope := 0.30 + 0.70 * clampf(
			0.5 + 0.5 * sin(t * 9.1 + _rand(seed_value) * 3.0), 0.0, 1.0
		)
		var bump := absf(_rand(seed_value + step * 3)) * 0.62
		bump += absf(_rand(seed_value * 5 + step * 29)) * 0.38
		var reach := spread * envelope * (0.30 + bump * 0.72)
		outer.append(_point(anchor.x + direction * reach, anchor.y))
		inner.append(_point(anchor.x - direction * spread * 0.55, anchor.y))
	var mass := outer.duplicate()
	for step in range(inner.size() - 1, -1, -1):
		mass.append(inner[step])
	_draw_mass(mass, 0.03, 0.07, FOLIAGE_LIT, FOLIAGE, 0.20, -0.05, 1.02)
	# A few darker clusters give the canopy interior some form.
	for cluster in range(11):
		var t := (float(cluster) + 0.35) / 11.0
		var scaled := t * float(profile.size() - 1)
		var index := clampi(int(scaled), 0, profile.size() - 2)
		var anchor: Vector2 = (profile[index] as Vector2).lerp(
			profile[index + 1], scaled - float(index)
		)
		var radius := spread * (0.34 + 0.30 * absf(_rand(seed_value + cluster * 13)))
		var center := Vector2(
			anchor.x + direction * radius * 0.35,
			anchor.y + _rand(seed_value + cluster * 7) * 0.022
		)
		var points := PackedVector2Array()
		var lobes := 13
		for lobe in range(lobes):
			var angle := TAU * float(lobe) / float(lobes)
			var lobe_reach := radius * (0.55 + 0.45 * absf(_rand(seed_value + cluster * 31 + lobe)))
			points.append(_point(
				center.x + cos(angle) * lobe_reach * 0.95, center.y + sin(angle) * lobe_reach * 1.15
			))
		_draw_mass(
			points, 0.10, 0.16, FOLIAGE_LIT.darkened(0.25), FOLIAGE, 0.18,
			center.y - radius, center.y + radius
		)


# ------------------------------------------------------------------- light

func _draw_light_shafts() -> void:
	## Broad, low-contrast beams raking down and left out of the bright gap, the
	## way the reference frame's sun sits just past the right sentinel.
	var origin := _point(0.615, -0.20)
	# Angle in radians measured from straight down; positive rakes to the left.
	var shafts := [
		[0.62, 0.028, 0.045], [0.50, 0.019, 0.038], [0.38, 0.036, 0.030],
		[0.27, 0.015, 0.042], [0.15, 0.024, 0.028], [0.70, 0.017, 0.032],
	]
	var length := size.length() * 1.30
	for index in range(shafts.size()):
		var shaft: Array = shafts[index]
		var width := size.x * float(shaft[1])
		var alpha := float(shaft[2]) * (0.84 + 0.16 * sin(_time * 0.25 + float(index)))
		draw_set_transform(origin, float(shaft[0]), Vector2.ONE)
		draw_texture_rect(
			_shaft_texture, Rect2(-width * 0.5, 0.0, width, length), false,
			Color(1.0, 0.99, 0.93, alpha)
		)
		draw_set_transform_matrix(Transform2D.IDENTITY)
	# The sun's own bloom, mostly hidden behind the right sentinel.
	_blob(Vector2(0.618, 0.02), Vector2(0.17, 0.20), Color(1.0, 0.99, 0.93, 0.13))


# -------------------------------------------------------------------- water

func _draw_water() -> void:
	## Still river: an opaque plane that mirrors the bright gap, darkens toward
	## the viewer, and carries soft vertical reflections of everything standing
	## in it. Nothing here is allowed to alias into hard scan lines.
	var top := size.y * WATER_Y
	var rect := Rect2(0.0, top, size.x, size.y - top + 1.0)
	draw_polygon(
		PackedVector2Array([
			rect.position, Vector2(rect.end.x, rect.position.y), rect.end,
			Vector2(rect.position.x, rect.end.y),
		]),
		PackedColorArray([
			WATER_TOP.lerp(HAZE, 0.05), WATER_TOP.lerp(HAZE, 0.05),
			WATER_BOTTOM, WATER_BOTTOM,
		])
	)
	# Shadowed far bank where the walls meet their own reflections.
	draw_texture_rect(
		_glow_texture, Rect2(-size.x * 0.1, top - size.y * 0.010, size.x * 1.2, size.y * 0.050),
		false, Color(0.10, 0.14, 0.14, 0.34)
	)
	# Reflections: soft vertical smears of what stands above the waterline.
	_smear(0.075, 0.090, Color(0.07, 0.10, 0.09, 0.52))
	_smear(0.930, 0.100, Color(0.07, 0.10, 0.09, 0.50))
	_smear(0.295, 0.098, Color(STONE_TOP, 0.34))
	_smear(0.730, 0.084, Color(STONE_TOP, 0.28))
	_smear(0.185, 0.060, Color(0.14, 0.18, 0.15, 0.34))
	_smear(0.428, 0.036, Color(0.09, 0.12, 0.12, 0.34))
	# The lit lane running back toward the gorge mouth.
	_smear(GAP_X, 0.055, Color(HAZE_CORE, 0.52))
	_smear(0.4225, 0.024, Color(HAZE_CORE, 0.34))
	# Ripple structure: a horizontally stretched wisp field, drifting.
	draw_texture_rect_region(
		_mist_texture, rect, Rect2(fmod(_time * 6.0, 128.0), 0.0, 460.0, 64.0),
		Color(HAZE_CORE, 0.13)
	)
	draw_texture_rect_region(
		_mist_texture, Rect2(rect.position.x, rect.position.y + rect.size.y * 0.45,
		rect.size.x, rect.size.y * 0.55),
		Rect2(fmod(_time * -9.0, 128.0), 0.0, 300.0, 64.0), Color(0.13, 0.18, 0.18, 0.34)
	)
	# Waterline mist so nothing meets the water on a hard edge.
	draw_texture_rect_region(
		_mist_texture, Rect2(0.0, top - size.y * 0.085, size.x, size.y * 0.130),
		Rect2(fmod(_time * 9.0, 128.0), 0.0, 190.0, 64.0), Color(HAZE_CORE, 0.36)
	)


func _smear(x: float, half_width: float, color: Color) -> void:
	var top := size.y * WATER_Y
	var rect := Rect2(
		size.x * (x - half_width), top - size.y * 0.006,
		size.x * half_width * 2.0, (size.y - top) * 1.1
	)
	draw_texture_rect(_glow_texture, rect, false, color)


# --------------------------------------------------------------- draw helpers

func _draw_mass(
	points: PackedVector2Array, haze_top: float, haze_bottom: float,
	top_color: Color = ROCK_TOP, bottom_color: Color = ROCK_BOTTOM,
	uv_scale: float = 0.34, y_top: float = NAN, y_bottom: float = NAN
) -> void:
	## Fills a silhouette with the generated rock texture, shaded vertically and
	## tinted toward HAZE by distance. `haze_*` are 0 (near) to 1 (dissolved).
	if points.size() < 3:
		return
	var low := INF
	var high := -INF
	for point in points:
		low = minf(low, point.y)
		high = maxf(high, point.y)
	if not is_nan(y_top):
		low = size.y * y_top
	if not is_nan(y_bottom):
		high = size.y * y_bottom
	var span := maxf(high - low, 1.0)
	var colors := PackedColorArray()
	var uvs := PackedVector2Array()
	for point in points:
		var t := clampf((point.y - low) / span, 0.0, 1.0)
		var tone := top_color.lerp(bottom_color, t * t * 0.65 + t * 0.35)
		colors.append(tone.lerp(HAZE, lerpf(haze_top, haze_bottom, t)))
		uvs.append(point / (size.y * uv_scale))
	draw_polygon(points, colors, uvs, _rock_texture)


func _draw_stone(points: PackedVector2Array, haze: float, y_top: float, y_bottom: float) -> void:
	## Statue stone: paler than rock, folded drape texture, and dissolving a
	## little more toward the waterline where the valley mist sits.
	var low := size.y * y_top
	var span := maxf(size.y * y_bottom - low, 1.0)
	var colors := PackedColorArray()
	var uvs := PackedVector2Array()
	for point in points:
		var t := clampf((point.y - low) / span, 0.0, 1.0)
		var tone := STONE_TOP.lerp(STONE_BOTTOM, t * 0.85)
		colors.append(tone.lerp(HAZE, haze + (1.0 - haze) * 0.28 * pow(t, 3.0)))
		uvs.append(Vector2(point.x / (size.y * 0.30), point.y / (size.y * 0.78)))
	draw_polygon(points, colors, uvs, _drape_texture)


func _draw_shade(points: PackedVector2Array, color: Color) -> void:
	var colors := PackedColorArray()
	var uvs := PackedVector2Array()
	var low := INF
	var high := -INF
	for point in points:
		low = minf(low, point.y)
		high = maxf(high, point.y)
	var span := maxf(high - low, 1.0)
	for point in points:
		var t := clampf((point.y - low) / span, 0.0, 1.0)
		var shade := color
		shade.a *= 1.0 - 0.55 * t
		colors.append(shade)
		uvs.append(Vector2(point.x / (size.y * 0.30), point.y / (size.y * 0.78)))
	draw_polygon(points, colors, uvs, _drape_texture)


func _draw_fold(
	x_top: float, y_top: float, x_bottom: float, y_bottom: float,
	width: float, darkness: float, override_color: Color = Color(0, 0, 0, 0)
) -> void:
	## One cloth crease: a tapering sliver that fades out at both ends so it
	## never reads as a drawn line on top of the figure.
	var color := override_color
	if color.a <= 0.0:
		color = Color(STONE_SHADE, darkness)
	var top := _point(x_top, y_top)
	var bottom := _point(x_bottom, y_bottom)
	var faded := color
	faded.a = 0.0
	var segments := 8
	for segment in range(segments):
		var t0 := float(segment) / float(segments)
		var t1 := float(segment + 1) / float(segments)
		var a := top.lerp(bottom, t0)
		var b := top.lerp(bottom, t1)
		var w0 := width * (0.35 + 0.65 * t0)
		var w1 := width * (0.35 + 0.65 * t1)
		var f0 := color.lerp(faded, absf(t0 - 0.45) * 1.5)
		var f1 := color.lerp(faded, absf(t1 - 0.45) * 1.5)
		draw_polygon(
			PackedVector2Array([
				a + Vector2(-w0, 0.0), a + Vector2(w0, 0.0),
				b + Vector2(w1, 0.0), b + Vector2(-w1, 0.0),
			]),
			PackedColorArray([f0, f0, f1, f1])
		)


func _draw_contour(points: PackedVector2Array, color: Color, width: float) -> void:
	## A soft darker edge just inside a silhouette, so pale stone still separates
	## from a pale sky without hardening into an outline.
	var closed := points.duplicate()
	closed.append(points[0])
	draw_polyline(closed, color, width, true)


func _blob(center: Vector2, radius: Vector2, color: Color) -> void:
	var rect := Rect2(
		size.x * (center.x - radius.x), size.y * (center.y - radius.y),
		size.x * radius.x * 2.0, size.y * radius.y * 2.0
	)
	draw_texture_rect(_glow_texture, rect, false, color)


func _ridge(control: Array, jag: float, seed_value: int, bottom: float) -> PackedVector2Array:
	## Builds a closed silhouette from a left-to-right crest profile, breaking
	## the straight runs with deterministic two-octave jitter so no edge of the
	## scene is a clean line.
	var points := PackedVector2Array()
	var aspect := maxf(size.x / maxf(size.y, 1.0), 0.5)
	var index := 0
	for segment in range(control.size() - 1):
		var a: Vector2 = control[segment]
		var b: Vector2 = control[segment + 1]
		var delta := b - a
		var stretched := Vector2(delta.x * aspect, delta.y)
		var steps := maxi(4, int(stretched.length() * 40.0))
		var normal := Vector2(0.0, 1.0)
		if stretched.length() > 0.0001:
			var direction := stretched.normalized()
			normal = Vector2(-direction.y / aspect, direction.x)
		for step in range(steps):
			var t := float(step) / float(steps)
			var p := a.lerp(b, t)
			var wobble := _rand(seed_value + (index / 4) * 7) * jag
			wobble += _rand(seed_value * 3 + (index / 2) * 11) * jag * 0.42
			wobble += _rand(seed_value * 7 + index * 13) * jag * 0.20
			index += 1
			points.append(_point(p.x + normal.x * wobble, p.y + normal.y * wobble))
	var last: Vector2 = control[control.size() - 1]
	points.append(_point(last.x, last.y))
	points.append(_point(last.x, bottom))
	points.append(_point(control[0].x, bottom))
	return points


func _scaled(fractions: Array) -> PackedVector2Array:
	var points := PackedVector2Array()
	for fraction in fractions:
		points.append(_point(fraction.x, fraction.y))
	return points


func _point(x_fraction: float, y_fraction: float) -> Vector2:
	return Vector2(size.x * x_fraction, size.y * y_fraction)


static func _rand(seed_value: int) -> float:
	## Deterministic hash in -1..1, so the scene is identical every launch.
	var value := (seed_value * 374761393 + 668265263) & 0x7FFFFFFF
	value = ((value ^ (value >> 13)) * 1274126177) & 0x7FFFFFFF
	return float((value ^ (value >> 16)) & 0xFFFF) / 32767.5 - 1.0


# --------------------------------------------------- procedural texture bakery

static func _build_textures() -> void:
	if _sky_texture != null:
		return
	_sky_texture = _bake_sky(224, 126)
	_rock_texture = _bake_rock(160, 160)
	_drape_texture = _bake_drape(128, 128)
	_mist_texture = _bake_mist(128, 64)
	_falls_texture = _bake_falls(48, 192)
	_shaft_texture = _bake_shaft(48, 48)
	_glow_texture = _bake_glow(64, 64)
	_vignette_texture = _bake_vignette(80, 45)


static func _noise(frequency: float, octaves: int, noise_seed: int) -> FastNoiseLite:
	var generator := FastNoiseLite.new()
	generator.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	generator.seed = noise_seed
	generator.frequency = frequency
	generator.fractal_octaves = octaves
	return generator


static func _tileable(generator: FastNoiseLite, x: float, y: float, w: float, h: float) -> float:
	## Bilinear wrap blend of four samples: makes any FastNoiseLite field tile,
	## so scrolling and repeating never shows a seam.
	var fx := x / w
	var fy := y / h
	return (
		generator.get_noise_2d(x, y) * (1.0 - fx) * (1.0 - fy)
		+ generator.get_noise_2d(x - w, y) * fx * (1.0 - fy)
		+ generator.get_noise_2d(x, y - h) * (1.0 - fx) * fy
		+ generator.get_noise_2d(x - w, y - h) * fx * fy
	)


static func _finish(width: int, height: int, data: PackedByteArray) -> ImageTexture:
	return ImageTexture.create_from_image(
		Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, data)
	)


static func _byte(value: float) -> int:
	return clampi(int(value * 255.0), 0, 255)


static func _bake_sky(width: int, height: int) -> ImageTexture:
	## High-key gorge sky: cool blue-grey overhead washing down to a near-white
	## haze pool, with a luminous bloom centred on the gap.
	var data := PackedByteArray()
	data.resize(width * height * 4)
	var clouds := _noise(0.020, 3, 4021)
	var index := 0
	for y in range(height):
		var v := float(y) / float(height - 1)
		var vertical := SKY_TOP.lerp(SKY_MID, smoothstep(0.0, 0.62, v))
		vertical = vertical.lerp(HAZE, smoothstep(0.34, 0.92, v))
		for x in range(width):
			var u := float(x) / float(width - 1)
			var color := vertical
			# Bloom on the gorge slot.
			var dx := (u - GAP_X) / 0.34
			var dy := (v - 0.72) / 0.46
			var bloom := clampf(1.0 - sqrt(dx * dx + dy * dy), 0.0, 1.0)
			color = color.lerp(HAZE_CORE, pow(bloom, 1.7) * 0.85)
			# A second, wider glow keeps the upper sky from going flat.
			var gx := (u - 0.60) / 0.55
			var gy := (v - 0.02) / 0.60
			var sun := clampf(1.0 - sqrt(gx * gx + gy * gy), 0.0, 1.0)
			color = color.lerp(Color(1.0, 0.99, 0.95), pow(sun, 2.2) * 0.55)
			# Faint cloud structure, strongest high up where the sky is darkest.
			var wisp := clouds.get_noise_2d(float(x) * 1.6, float(y) * 3.4) * 0.05
			wisp *= 1.0 - smoothstep(0.20, 0.75, v)
			color = color.lightened(maxf(wisp, 0.0)).darkened(maxf(-wisp, 0.0))
			data[index] = _byte(color.r)
			data[index + 1] = _byte(color.g)
			data[index + 2] = _byte(color.b)
			data[index + 3] = 255
			index += 4
	return _finish(width, height, data)


static func _bake_rock(width: int, height: int) -> ImageTexture:
	## Mottled stone with columnar jointing: a fractal base, vertical strata and
	## a sparse ridged crack network. White-keyed so vertex colours tint it.
	var data := PackedByteArray()
	data.resize(width * height * 4)
	var grain := _noise(0.115, 4, 7717)
	var strata := _noise(0.026, 2, 3319)
	var index := 0
	for y in range(height):
		for x in range(width):
			var fx := float(x)
			var fy := float(y)
			var value := 1.0 + _tileable(grain, fx, fy, float(width), float(height)) * 0.11
			# Vertical jointing, the signature of the reference's cliff faces:
			# noise stretched hard along y so it reads as irregular strata.
			value += _tileable(strata, fx * 0.9, fy * 0.14, float(width), float(height)) * 0.13
			value += _tileable(strata, fx * 3.3, fy * 0.28, float(width), float(height)) * 0.055
			value = clampf(value, 0.74, 1.20)
			var byte := _byte(value * 0.78)
			data[index] = byte
			data[index + 1] = byte
			data[index + 2] = byte
			data[index + 3] = 255
			index += 4
	return _finish(width, height, data)


static func _bake_drape(width: int, height: int) -> ImageTexture:
	## Carved cloth: soft vertical fold shading plus a fine weathering grain.
	var data := PackedByteArray()
	var grain := _noise(0.070, 3, 5501)
	var warp := _noise(0.018, 2, 1213)
	data.resize(width * height * 4)
	var index := 0
	for y in range(height):
		for x in range(width):
			var fx := float(x)
			var fy := float(y)
			var bend := _tileable(warp, fx, fy, float(width), float(height)) * 2.6
			var folds := sin((fx / float(width)) * TAU * 5.0 + bend)
			var value := 1.0 + folds * 0.085
			value += _tileable(grain, fx, fy, float(width), float(height)) * 0.10
			# Long weathering runs down the stone.
			value -= maxf(0.0, sin(fx * 0.42 + bend * 1.4)) * 0.030
			value = clampf(value, 0.70, 1.22)
			var byte := _byte(value * 0.92)
			data[index] = byte
			data[index + 1] = byte
			data[index + 2] = byte
			data[index + 3] = 255
			index += 4
	return _finish(width, height, data)


static func _bake_mist(width: int, height: int) -> ImageTexture:
	## Drifting haze wisps: white with a soft, tileable alpha field that fades
	## out top and bottom so bands blend into whatever they are laid over.
	var data := PackedByteArray()
	data.resize(width * height * 4)
	var wisps := _noise(0.035, 3, 2609)
	var index := 0
	for y in range(height):
		var v := float(y) / float(height - 1)
		var envelope := sin(v * PI)
		for x in range(width):
			var raw := _tileable(wisps, float(x), float(y) * 2.2, float(width), float(height))
			var alpha := clampf(raw * 0.5 + 0.5, 0.0, 1.0)
			alpha = pow(alpha, 1.9) * envelope
			data[index] = 255
			data[index + 1] = 255
			data[index + 2] = 255
			data[index + 3] = _byte(alpha)
			index += 4
	return _finish(width, height, data)


static func _bake_falls(width: int, height: int) -> ImageTexture:
	## Falling water: strands stretched vertically, feathered to nothing at both
	## sides, fading in at the lip and dissolving into spray at the bottom.
	var data := PackedByteArray()
	data.resize(width * height * 4)
	var strands := _noise(0.090, 3, 8081)
	var index := 0
	for y in range(height):
		var v := float(y) / float(height - 1)
		var along := smoothstep(0.0, 0.26, v) * (1.0 - smoothstep(0.58, 1.0, v))
		for x in range(width):
			var u := float(x) / float(width - 1)
			var feather := pow(sin(clampf(u, 0.0, 1.0) * PI), 1.9)
			var strand := _tileable(
				strands, float(x) * 2.6, float(y) * 0.45, float(width) * 2.6, float(height) * 0.45
			)
			var alpha := feather * along * (0.62 + 0.38 * clampf(strand * 0.5 + 0.5, 0.0, 1.0))
			data[index] = 255
			data[index + 1] = 255
			data[index + 2] = 255
			data[index + 3] = _byte(clampf(alpha, 0.0, 1.0))
			index += 4
	return _finish(width, height, data)


static func _bake_shaft(width: int, height: int) -> ImageTexture:
	## One light shaft: a squared-sine lateral profile that falls off along its
	## length, so beams have no edges anywhere.
	var data := PackedByteArray()
	data.resize(width * height * 4)
	var index := 0
	for y in range(height):
		var v := float(y) / float(height - 1)
		var along := smoothstep(0.0, 0.18, v) * pow(1.0 - v, 1.15)
		for x in range(width):
			var u := float(x) / float(width - 1)
			var across := pow(sin(u * PI), 2.6)
			data[index] = 255
			data[index + 1] = 255
			data[index + 2] = 255
			data[index + 3] = _byte(clampf(across * along, 0.0, 1.0))
			index += 4
	return _finish(width, height, data)


static func _bake_glow(width: int, height: int) -> ImageTexture:
	var data := PackedByteArray()
	data.resize(width * height * 4)
	var index := 0
	for y in range(height):
		var v := float(y) / float(height - 1) * 2.0 - 1.0
		for x in range(width):
			var u := float(x) / float(width - 1) * 2.0 - 1.0
			var falloff := clampf(1.0 - sqrt(u * u + v * v), 0.0, 1.0)
			data[index] = 255
			data[index + 1] = 255
			data[index + 2] = 255
			data[index + 3] = _byte(pow(falloff, 2.1))
			index += 4
	return _finish(width, height, data)


static func _bake_vignette(width: int, height: int) -> ImageTexture:
	## Cool corner shading; keeps the eye in the gorge without darkening it.
	var data := PackedByteArray()
	data.resize(width * height * 4)
	var index := 0
	for y in range(height):
		var v := float(y) / float(height - 1) * 2.0 - 1.0
		for x in range(width):
			var u := float(x) / float(width - 1) * 2.0 - 1.0
			var radius := sqrt(u * u * 0.72 + v * v * 0.90)
			var alpha := pow(clampf(smoothstep(0.55, 1.35, radius), 0.0, 1.0), 1.5) * 0.42
			data[index] = 18
			data[index + 1] = 26
			data[index + 2] = 28
			data[index + 3] = _byte(alpha)
			index += 4
	return _finish(width, height, data)
