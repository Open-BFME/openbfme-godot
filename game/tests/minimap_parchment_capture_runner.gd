extends SceneTree
## PHOTOGRAPH THE RADAR, so "it looks like retail's parchment" can be checked by
## looking at it rather than believed.
##
## `minimap_parchment_runner.gd` proves the bindings, the source-grid geometry
## and the paper's bytes with no renderer at all. It cannot prove the thing the
## owner actually reported, which is what the bottom-left disc LOOKS like once
## composed. This one opens a real rendering context, draws `RetailMinimap` at
## its shipping size (362px, the HUD's `RETAIL_RADAR_RADIUS * 2`) and at 2x for
## inspection, over the real published ink art of a real cooked map AND over
## retail's own parchment bitmap, and writes PNGs.
##
## THE PAPER IS RETAIL'S, NOT OURS. An earlier version of this file said the
## radar's fill was synthesized because "no retail archive ships a parchment
## bitmap". That was false: the cooked palantir atlas
## (`assets/ui/palantir/atlases/apt-palantir-1-*.png`) carries the authored sheet
## at `RetailMinimap.RETAIL_PARCHMENT_REGION`, a lit disc that falls off to near
## black at its rim. This runner binds that bitmap and asserts the rendered disc
## against ITS measured values, so a return to a hand-made fill goes red.
##
## IT ALSO ASSERTS ON THE RENDERED PIXELS: the disc has to come out sepia at the
## bitmap's own lit-centre value, it has to darken toward the rim on its own, the
## ink has to actually mark it IN PROPORTION TO HOW MUCH INK THAT MAP AUTHORS,
## blips have to be findable, and nothing outside the bezel circle may be painted
## - the photographic preview used to hang its rectangle corners over the frame.
##
## Usage:
##   Godot_v4.7 --path game --script tests/minimap_parchment_capture_runner.gd \
##       -- --art <abs path to <slug>-art.png> --out <dir> [--atlas <path>]
##
## `--atlas` is optional: with OPENBFME_CONTENT set the palantir atlas is found
## in the mounted packs automatically.

const MinimapScript := preload("res://src/retail_slice/retail_minimap.gd")
const ParchmentRunner := preload("res://tests/minimap_parchment_runner.gd")

const RADAR_PIXELS := 362
const INSPECT_PIXELS := 724
const SETTLE_FRAMES := 6
## Hard ceiling on frames spent per capture. A SubViewport whose texture never
## reads back (headless, or a renderer that never presents) used to leave
## `_index` frozen and this runner spinning forever, which reads as a hang and
## was diagnosed as one. It now fails, loudly, with a result line.
const FRAME_BUDGET := 240
## Amon Sul Fortress's real player-start axis against source +X, so the shot is
## taken through the same rotated local frame the live match uses.
const AMON_SUL_AXIS_DEGREES := 155.30
## Retail's parchment lit centre, straight off the atlas region.
const PARCHMENT_CENTRE_RGB := Vector3(179.0, 160.3, 118.2)
## Bare parchment sits near luminance 0.6; a fully inked pixel (the authored
## (76,44,1) laid over it at INK_OPACITY) lands near 0.2, and the anti-aliased
## body of every stroke falls between. 0.45 counts the stroke, not the paper.
const INK_LUMINANCE := 0.45
## Alpha at which an authored art texel is dark enough, after INK_OPACITY, to
## cross INK_LUMINANCE over the paper. Used to measure what the SOURCE map
## actually draws.
const INK_ALPHA_FLOOR := 0.45
## The rendered inner disc must carry at least this share of the ink coverage the
## art itself authors. RELATIVE, not the old flat 0.04: how much of a radar a map
## inks is the map's business (Amon Sul draws far less than Fords), and a fixed
## fraction either lets a dropped overlay pass on a busy map or fails a sparse
## one for being sparse. Ratio, not equality, because the sheet is inscribed in
## the bezel while this counts only the inner 65% of the disc.
const INK_COVERAGE_RATIO := 0.30

var _out_dir := ""
var _art_path := ""
var _atlas_path := ""
var _viewport: SubViewport = null
var _minimap: Control = null
var _sizes: Array[int] = [RADAR_PIXELS, INSPECT_PIXELS]
var _index := 0
var _frames := 0
var _total_frames := 0
var _stood_up := false
var _atlas_resolved := false
var _passed := 0
var _failed := 0
var _art_ink_coverage := 0.0


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
	# NOT the atlas: `_initialize` runs before the autoloads exist, so ContentDB
	# is not there to be asked yet. Resolving it here silently found nothing and
	# the radar rendered its flat fallback while every sepia check stayed green -
	# which is exactly why `_centre_is_retails_lit_parchment` compares against the
	# BITMAP's value and not against "looks tan".
	_atlas_path = _argument("--atlas", "")
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


func _discover_palantir_atlas() -> String:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		return ""
	for root_value in (content_db.get("pack_roots") as Array):
		var directory := String(root_value).path_join("assets/ui/palantir/atlases")
		var names := DirAccess.get_files_at(directory)
		if names == null:
			continue
		for name_value in names:
			var file_name := String(name_value)
			if file_name.begins_with("apt-palantir-1-") and file_name.ends_with(".png"):
				return directory.path_join(file_name)
	return ""


func _load_texture(path: String) -> Texture2D:
	if path == "":
		return null
	var image := Image.new()
	if image.load(path) != OK:
		return null
	image.convert(Image.FORMAT_RGBA8)
	return ImageTexture.create_from_image(image)


func _load_ink_art() -> Texture2D:
	var texture := _load_texture(_art_path)
	_art_ink_coverage = 0.0
	if texture == null:
		return null
	# What this MAP actually inks, so the rendered coverage floor below is the
	# art's own number rather than a constant somebody picked.
	var image := texture.get_image()
	var inked := 0
	for y in image.get_height():
		for x in image.get_width():
			if image.get_pixel(x, y).a >= INK_ALPHA_FLOOR:
				inked += 1
	_art_ink_coverage = float(inked) / maxf(float(image.get_width() * image.get_height()), 1.0)
	return texture


func _resolve_atlas_once() -> void:
	## Run on the first frame, once the autoloads are up.
	if _atlas_resolved:
		return
	_atlas_resolved = true
	if _atlas_path == "":
		_atlas_path = _discover_palantir_atlas()
	_check(
		"palantir_atlas_available",
		_atlas_path != "",
		"no mounted pack carries apt-palantir-1-*.png; pass --atlas or set OPENBFME_CONTENT"
	)


func _stand_up(pixels: int) -> void:
	_resolve_atlas_once()
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
	# Retail's own paper, bound exactly the way `retail_hud` binds it in a match.
	_minimap.bind_retail_parchment(_load_texture(_atlas_path))
	_minimap.configure(simulation, map_data, _load_ink_art())
	_minimap.camera_center = map_data.local_at_grid(Vector2(150, 140))
	_viewport.add_child(_minimap)


func _parchment_disc_mean(radius: float) -> Vector3:
	## The mean of RETAIL'S OWN sheet over the same disc the render is measured
	## across, so the assertion above has a source of truth instead of a
	## remembered constant. Returns a zero vector when no sheet is bound.
	if _minimap == null or _minimap.radar_paper == null:
		return Vector3.ZERO
	var sheet: Image = _minimap.radar_paper.get_image()
	var half := Vector2(sheet.get_width() - 1, sheet.get_height() - 1) * 0.5
	# The render samples inside `radius - 2`; the sheet is drawn at `radius`.
	var sample_radius := half.x * (radius - 2.0) / maxf(radius, 1.0)
	var total := Vector3.ZERO
	var count := 0
	for y in sheet.get_height():
		for x in sheet.get_width():
			if Vector2(x, y).distance_to(half) > sample_radius:
				continue
			var pixel := sheet.get_pixel(x, y)
			total += Vector3(pixel.r, pixel.g, pixel.b)
			count += 1
	if count == 0:
		return Vector3.ZERO
	return total / float(count)


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
					# Only the INNER disc counts as ink: retail's parchment bitmap
					# carries its own rim falloff, which takes the outer third of
					# the radius below any ink threshold on its own, so measuring
					# the whole disc would call a blank sheet inked.
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
	# AGAINST THE SOURCE BITMAP, not against a remembered number. The whole-disc
	# mean of retail's sheet is much darker than the synthesized paper this
	# runner used to photograph, because the authored rim falloff is far deeper
	# than the fourteen arcs that stood in for it - so the old flat `> 0.4` floor
	# would now reject the real thing. The expected value is read out of the
	# atlas region every run, and the render may only sit DARKER than it (ink
	# subtracts light; nothing on the radar adds any).
	var expected := _parchment_disc_mean(radius)
	_check(
		"%d_disc_reads_as_retail_sepia" % pixels,
		mean.x > mean.y and mean.y > mean.z and (mean.x - mean.z) > 0.15
			and expected.x > 0.0
			and mean.x <= expected.x * 1.02 and mean.x >= expected.x * 0.75,
		"mean=(%.3f,%.3f,%.3f) atlas disc mean=(%.3f,%.3f,%.3f)" % [
			mean.x, mean.y, mean.z, expected.x, expected.y, expected.z
		]
	)
	# THE PAPER IS RETAIL'S BITMAP, and the rendered centre has to prove it: a
	# hand-mixed fill would have to hit the atlas's lit-centre value by accident.
	# Sampled away from the blip cluster and the view wedge, and INK IS EXCLUDED
	# from the window - a coastline that happens to cross the middle of the sheet
	# must not be read as the paper being the wrong colour.
	var centre_sample := Vector3.ZERO
	var centre_count := 0
	var window := maxi(4, int(float(pixels) * 0.03))
	for y in range(int(centre.y) - window, int(centre.y) + window):
		for x in range(int(centre.x) - window, int(centre.x) + window):
			var pixel := image.get_pixel(x, y)
			if pixel.get_luminance() < INK_LUMINANCE:
				continue
			centre_sample += Vector3(pixel.r, pixel.g, pixel.b) * 255.0
			centre_count += 1
	centre_sample /= maxf(float(centre_count), 1.0)
	_check(
		"%d_centre_is_retails_lit_parchment" % pixels,
		centre_sample.distance_to(PARCHMENT_CENTRE_RGB) < 24.0,
		"centre=(%.1f,%.1f,%.1f) expected=%s" % [
			centre_sample.x, centre_sample.y, centre_sample.z, PARCHMENT_CENTRE_RGB
		]
	)
	# The ink is a much darker brown than the paper; if the compose silently
	# dropped it the interior would be a flat tan with no dark strokes anywhere.
	# Point this runner at a map that PUBLISHES ink art - run it with no `--art`
	# and this is the check that goes red, which is how its teeth were shown.
	# A COUNT, not a darkest pixel: every blip carries a dark brown outline, so
	# "something dark exists" is true even on a blank sheet. The floor is a share
	# of what the ART ITSELF inks, so a sparse map is not failed for being sparse
	# and a busy map cannot hide a dropped overlay behind a low constant.
	var ink_fraction := float(ink_pixels) / maxf(float(inner_count), 1.0)
	var ink_floor := _art_ink_coverage * INK_COVERAGE_RATIO
	_check(
		"%d_ink_strokes_present" % pixels,
		_art_ink_coverage > 0.0 and ink_fraction > ink_floor,
		"ink=%.4f of inner=%d floor=%.4f (art authors %.4f)" % [
			ink_fraction, inner_count, ink_floor, _art_ink_coverage
		]
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


func _finish() -> bool:
	print("MINIMAP_CAPTURE_RESULT passed=%d failed=%d" % [_passed, _failed])
	quit(1 if _failed > 0 else 0)
	return true


func _process(_delta: float) -> bool:
	if _index >= _sizes.size():
		return _finish()
	# THE BUDGET IS THE POINT. Under `--headless` the SubViewport never presents,
	# so `get_texture().get_image()` returns null, `save_png` throws, `_index`
	# never advances and this runner spun forever looking exactly like a hung
	# Godot. It now spends a bounded number of frames and reports.
	_total_frames += 1
	if _total_frames > FRAME_BUDGET:
		_check(
			"viewport_readback_within_frame_budget",
			false,
			"no readable frame after %d frames at size %d - run WINDOWED (drop --headless); %s" % [
				FRAME_BUDGET, _sizes[_index], DisplayServer.get_name()
			]
		)
		return _finish()
	if not _stood_up:
		_stand_up(_sizes[_index])
		_stood_up = true
		_frames = 0
		return false
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	var viewport_texture := _viewport.get_texture()
	var image: Image = viewport_texture.get_image() if viewport_texture != null else null
	if image == null or image.is_empty():
		# Not fatal on its own: keep spending frames until the budget runs out,
		# in case the renderer simply has not presented yet.
		return false
	var path := "%s/radar-%dpx.png" % [_out_dir, _sizes[_index]]
	var save_error := image.save_png(path)
	_check(
		"%d_capture_written" % _sizes[_index],
		save_error == OK,
		"save_png(%s) -> %d" % [path, save_error]
	)
	print("[minimap-capture] %s" % path)
	_assert_pixels(image, _sizes[_index])
	_index += 1
	_stood_up = false
	return false
