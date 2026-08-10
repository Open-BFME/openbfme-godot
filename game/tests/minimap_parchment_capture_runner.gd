extends SceneTree
## PHOTOGRAPH THE RADAR, so "it looks like retail's parchment" can be checked by
## looking at it rather than believed.
##
## `minimap_parchment_runner.gd` proves the bindings and the source-grid
## geometry with no renderer at all. It cannot prove the thing the owner
## actually reported, which is what the bottom-left disc LOOKS like. This one
## opens a real rendering context, draws `RetailMinimap` at its shipping size
## (362px, the HUD's `RETAIL_RADAR_RADIUS * 2`) and at 2x for inspection, over
## the real published ink art of a real cooked map, and writes PNGs.
##
## IT ALSO ASSERTS ON THE RENDERED PIXELS: the disc has to come out sepia (the
## retail capture's radar interior means (162,140,94)), the ink has to actually
## darken part of it, blips have to be findable, and nothing outside the bezel
## circle may be painted - the photographic preview used to hang its rectangle
## corners over the frame.
##
## Usage:
##   Godot_v4.7 --path game --script tests/minimap_parchment_capture_runner.gd \
##       -- --art <abs path to <slug>-art.png> --out <dir>

const MinimapScript := preload("res://src/retail_slice/retail_minimap.gd")
const ParchmentRunner := preload("res://tests/minimap_parchment_runner.gd")

const RADAR_PIXELS := 362
const INSPECT_PIXELS := 724
const SETTLE_FRAMES := 6
## Amon Sul Fortress's real player-start axis against source +X, so the shot is
## taken through the same rotated local frame the live match uses.
const AMON_SUL_AXIS_DEGREES := 155.30
## Bare parchment sits at luminance 0.59; a fully inked pixel (the authored
## (76,44,1) laid over it at INK_OPACITY) lands near 0.26, and the anti-aliased
## body of every stroke falls between. 0.45 counts the stroke, not the paper.
const INK_LUMINANCE := 0.45
## Fraction of the inner disc an inked map has to cover. A dozen blip outlines
## are a few hundred pixels of a 35k-pixel disc, so they cannot reach this.
const INK_COVERAGE := 0.04

var _out_dir := ""
var _art_path := ""
var _viewport: SubViewport = null
var _minimap: Control = null
var _sizes: Array[int] = [RADAR_PIXELS, INSPECT_PIXELS]
var _index := 0
var _frames := 0
var _stood_up := false
var _passed := 0
var _failed := 0


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		_passed += 1
		print("MINIMAP_CAPTURE PASS %s" % name)
	else:
		_failed += 1
		print("MINIMAP_CAPTURE FAIL %s | %s" % [name, detail])


func _argument(flag: String, fallback: String) -> String:
	var arguments := OS.get_cmdline_user_args()
	for index in arguments.size():
		if arguments[index] == flag and index + 1 < arguments.size():
			return arguments[index + 1]
	return fallback


func _initialize() -> void:
	_out_dir = _argument("--out", "user://minimap-capture")
	_art_path = _argument("--art", "")
	DirAccess.make_dir_recursive_absolute(_out_dir)
	var window := root
	window.borderless = true
	window.size = Vector2i(420, 420)
	window.title = "OpenBFME - radar capture"
	_viewport = SubViewport.new()
	_viewport.size = Vector2i(RADAR_PIXELS, RADAR_PIXELS)
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	# Transparent, because everything outside the bezel circle MUST stay unpainted
	# - that is what the corner-spill assertion measures.
	_viewport.transparent_bg = true
	window.add_child(_viewport)


func _load_ink_art() -> Texture2D:
	if _art_path == "":
		return null
	var image := Image.new()
	if image.load(_art_path) != OK:
		return null
	return ImageTexture.create_from_image(image)


func _stand_up(pixels: int) -> void:
	if _minimap != null:
		_viewport.remove_child(_minimap)
		_minimap.queue_free()
	_viewport.size = Vector2i(pixels, pixels)
	_minimap = MinimapScript.new()
	_minimap.size = Vector2(pixels, pixels)
	_minimap.position = Vector2.ZERO
	var map_data := ParchmentRunner.StubMapData.new(AMON_SUL_AXIS_DEGREES)
	# Amon Sul Fortress's cooked grid: 405x355 cells at 10 units, 40-cell border.
	map_data.border_width = 40
	map_data.playable_grid_min = Vector2i(40, 40)
	map_data.playable_grid_max = Vector2i(365, 315)
	var simulation := ParchmentRunner.StubSimulation.new()
	var identifier := 1
	for offset in [Vector2(0, 0), Vector2(14, 6), Vector2(-11, 9), Vector2(6, -13), Vector2(20, 18)]:
		simulation.entities[identifier] = {
			"position": map_data.local_at_grid(Vector2(120, 120) + offset),
			"team": 0,
			"health": 100,
		}
		identifier += 1
	for offset in [Vector2(0, 0), Vector2(18, -9), Vector2(-15, 14)]:
		simulation.entities[identifier] = {
			"position": map_data.local_at_grid(Vector2(280, 240) + offset),
			"team": 1,
			"health": 100,
		}
		identifier += 1
	simulation.structures[100] = {
		"position": map_data.local_at_grid(Vector2(105, 105)), "team": 0, "health": 500
	}
	simulation.structures[101] = {
		"position": map_data.local_at_grid(Vector2(300, 255)), "team": 1, "health": 500
	}
	_minimap.configure(simulation, map_data, _load_ink_art())
	_minimap.camera_center = map_data.local_at_grid(Vector2(150, 140))
	_viewport.add_child(_minimap)


func _assert_pixels(image: Image, pixels: int) -> void:
	var centre := Vector2(pixels, pixels) * 0.5
	# Same bezel geometry the control clips to, so "outside" means outside the
	# ring opening rather than outside the square control.
	var radius := float(pixels) * MinimapScript.BEZEL_RADIUS_RATIO
	var inside_total := Vector3.ZERO
	var inside_count := 0
	var ink_pixels := 0
	var inner_count := 0
	var corner_painted := 0
	for y in pixels:
		for x in pixels:
			var point := Vector2(x + 0.5, y + 0.5)
			var pixel := image.get_pixel(x, y)
			if point.distance_to(centre) <= radius - 2.0:
				if pixel.a > 0.5:
					inside_total += Vector3(pixel.r, pixel.g, pixel.b)
					inside_count += 1
					# Only the INNER disc counts as ink: the bezel vignette bands
					# darken the outer 30% of the radius below any ink threshold,
					# so measuring the whole disc would call a blank sheet inked.
					if point.distance_to(centre) < radius * 0.65:
						inner_count += 1
						if pixel.get_luminance() < INK_LUMINANCE and pixel.r > pixel.b:
							ink_pixels += 1
			elif point.distance_to(centre) > radius + 3.0 and pixel.a > 0.1:
				corner_painted += 1
	var mean := inside_total / maxf(float(inside_count), 1.0)
	_check(
		"%d_disc_is_painted" % pixels,
		inside_count > int(PI * radius * radius * 0.9),
		"painted=%d" % inside_count
	)
	_check(
		"%d_disc_reads_as_retail_sepia" % pixels,
		mean.x > mean.y and mean.y > mean.z and (mean.x - mean.z) > 0.15 and mean.x > 0.4,
		"mean=(%.3f,%.3f,%.3f)" % [mean.x, mean.y, mean.z]
	)
	# The ink is a much darker brown than the paper; if the bake silently dropped
	# it the interior would be a flat tan with no dark strokes anywhere. Point
	# this runner at a map that PUBLISHES ink art - run it with no `--art` and
	# this is the check that goes red, which is how its teeth were shown.
	# A COUNT, not a darkest pixel: every blip carries a dark brown outline, so
	# "something dark exists" is true even on a blank sheet. Ink covers percent
	# of the sheet; a dozen blips cover a few hundred pixels.
	_check(
		"%d_ink_strokes_present" % pixels,
		ink_pixels > int(float(inner_count) * INK_COVERAGE),
		"ink=%d of inner=%d" % [ink_pixels, inner_count]
	)
	_check(
		"%d_nothing_spills_outside_the_bezel" % pixels,
		corner_painted == 0,
		"outside_pixels=%d" % corner_painted
	)
	# Blips: team colours are far outside the parchment ramp (which has r>g>b and
	# no blue-dominant pixels at all), so counting blue-dominant pixels finds the
	# player's units and nothing else.
	var blue_blip := 0
	var red_blip := 0
	for y in pixels:
		for x in pixels:
			var pixel := image.get_pixel(x, y)
			if pixel.a < 0.5:
				continue
			if pixel.b > pixel.r + 0.15 and pixel.b > 0.5:
				blue_blip += 1
			elif pixel.r > 0.6 and pixel.g < pixel.r - 0.25 and pixel.b < pixel.r - 0.25:
				red_blip += 1
	_check("%d_player_blips_drawn" % pixels, blue_blip > 8, "blue_pixels=%d" % blue_blip)
	_check("%d_enemy_blips_drawn" % pixels, red_blip > 4, "red_pixels=%d" % red_blip)


func _process(_delta: float) -> bool:
	if _index >= _sizes.size():
		print("MINIMAP_CAPTURE_RESULT passed=%d failed=%d" % [_passed, _failed])
		quit(1 if _failed > 0 else 0)
		return true
	if not _stood_up:
		_stand_up(_sizes[_index])
		_stood_up = true
		_frames = 0
		return false
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	var image: Image = _viewport.get_texture().get_image()
	var path := "%s/radar-%dpx.png" % [_out_dir, _sizes[_index]]
	image.save_png(path)
	print("[minimap-capture] %s" % path)
	_assert_pixels(image, _sizes[_index])
	_index += 1
	_stood_up = false
	return false
