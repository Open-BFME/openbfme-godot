extends SceneTree
## Fast geometry gate for the fortress palantir command surface.
##
## THE DEFECT IT GATES, AND THE ONE IT DELIBERATELY PERMITS.
##
## GATED - the five production queue chips were authored at panel-local
## (60 + 40*i, 318) and the sixth retail command socket sits at (148, 296) and is
## 64px tall, so a producing fortress drew two opaque 36px chips through a live
## command button. Opaque over opaque, and the chip is clickable, so it was an
## input hazard as well as a visual one. Pure layout arithmetic; needs no pack.
##
## PERMITTED, ON PURPOSE - `_radial_button_position` clamps expanded-range
## buttons into the panel after spacing them on the arc, and at eight entries two
## 64px boxes overlap by 3x5 px = 15 px^2 ((684,957) and (623,1016), the men hero
## page with no created hero). This runner does NOT fail that, because retail's
## OWN authored slots 4 and 5 overlap by 17x4 px = 68 px^2 - four and a half
## times as much - and the converted socket art is transparent exactly there
## (proved below from the shipped PNG). A rectangle-intersection gate would fail
## retail itself. The bound is therefore retail's own worst authored overlap
## area, and the separation floor is retail's own tightest authored pair.
##
## This runner is deliberately cheap: it builds one RetailHud, reads the real
## production layout functions, and asserts rectangles. It is the fast gate in
## front of the full `fortress_command_surface_runner` match.
##
## Run:
##   OPENBFME_CONTENT=<repo>/.private/content-packs godot --headless --path game \
##     --script res://tests/retail_radial_layout_runner.gd

const RADIAL_BUTTON_SIZE := Vector2(64, 64)
## The reviewer's reproduction ran at the retail capture resolution; the bottom
## clamp is a function of the panel rectangle, so the panel must be the 1080p one.
const CAPTURE_VIEWPORT := Vector2i(1920, 1080)
## Every count the fortress pages can actually produce: six authored sockets,
## the seven-entry upgrade page, the eight-entry hero page with no created hero,
## and every larger authored range up to the hero range's ten entries plus a
## page selector and a back button.
const COVERED_COUNTS := [6, 7, 8, 9, 10, 11, 12]
## Counts laid out by the EXPANDED arc. Six entries are the authored constant
## itself, so holding it to a bound derived from the authored constant would be
## `min(S) >= min(S)` - the identity row below covers it instead.
const EXPANDED_COUNTS := [7, 8, 9, 10, 11, 12]

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

var passed := 0
var failed := 0


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_RADIAL_LAYOUT_RUNNER")
	call_deferred("_run")


func _run() -> void:
	root.size = CAPTURE_VIEWPORT
	await process_frame
	var hud_script := load("res://src/retail_slice/retail_hud.gd")
	if not _check("hud_script_loads", hud_script != null):
		_finish()
		return
	var hud = hud_script.new()
	root.add_child(hud)
	hud.build()
	# Parity chrome pins the command grid to the panel origin (retail_hud.gd
	# `_bind_retail_parity_chrome`); the queue chips are authored in those
	# coordinates, so the gate must read them there rather than in the public
	# VBox stack.
	var command_grid: Control = hud.command_grid
	var old_parent := command_grid.get_parent()
	if old_parent != hud.command_panel:
		old_parent.remove_child(command_grid)
		hud.command_panel.add_child(command_grid)
	await process_frame
	await process_frame
	command_grid.position = Vector2.ZERO
	command_grid.size = hud.command_panel.size
	hud._ensure_production_queue_chips()
	await process_frame

	var panel: Control = hud.command_panel
	var panel_rect := panel.get_global_rect()
	print("RETAIL_RADIAL_LAYOUT panel=%s viewport=%s" % [str(panel_rect), str(root.size)])
	_check(
		"panel_is_the_retail_dock_rectangle",
		panel_rect.size.is_equal_approx(Vector2(520, 360)),
		str(panel_rect)
	)

	var chip_rects := _queue_chip_rects(hud)
	_check("queue_chips_exist", chip_rects.size() == 5, str(chip_rects.size()))

	# --- the authored sockets must not sit under the queue chips --------------
	var socket_rects: Array[Rect2] = []
	for slot in hud_script.RETAIL_COMMAND_SLOT_SOURCE.size():
		socket_rects.append(Rect2(
			panel_rect.position + (hud_script.RETAIL_COMMAND_SLOT_SOURCE[slot] as Vector2),
			hud_script.RETAIL_COMMAND_SLOT_SIZE as Vector2
		))
	# BOTH BOUNDS COME OUT OF RETAIL'S OWN ARC, computed here rather than typed in:
	# its tightest authored pair centre to centre, and its worst authored pair by
	# overlap AREA. An expanded range may be no tighter and no more overlapping
	# than the six sockets retail itself ships.
	var authored_minimum_separation := INF
	var authored_maximum_overlap_area := 0.0
	for left in socket_rects.size():
		for right in range(left + 1, socket_rects.size()):
			authored_minimum_separation = minf(
				authored_minimum_separation,
				socket_rects[left].get_center().distance_to(socket_rects[right].get_center())
			)
			authored_maximum_overlap_area = maxf(
				authored_maximum_overlap_area, _overlap_area(socket_rects[left], socket_rects[right])
			)
	print("RETAIL_RADIAL_LAYOUT authored_minimum_separation=%.3f authored_maximum_overlap_area=%.1f" % [
		authored_minimum_separation, authored_maximum_overlap_area
	])
	_check(
		"authored_sockets_themselves_overlap_as_boxes",
		authored_maximum_overlap_area > 0.0,
		"if retail's own sockets stopped overlapping, the bound below is no longer retail's"
	)

	var socket_chip_overlaps: Array[String] = []
	for socket_index in socket_rects.size():
		for chip_index in chip_rects.size():
			if socket_rects[socket_index].intersects(chip_rects[chip_index]):
				socket_chip_overlaps.append("socket%d %s x chip%d %s" % [
					socket_index, str(socket_rects[socket_index]), chip_index, str(chip_rects[chip_index])
				])
	_check(
		"queue_chips_clear_the_authored_command_sockets",
		socket_chip_overlaps.is_empty(),
		str(socket_chip_overlaps)
	)
	var chips_inside := true
	for chip_rect in chip_rects:
		if not panel_rect.encloses(chip_rect):
			chips_inside = false
	_check("queue_chips_stay_inside_the_command_panel", chips_inside, str(chip_rects))

	# retail_hud.gd's RETAIL_QUEUE_CHIP_ORIGIN comment claims the chips are clear
	# of the palantir dish as well as of the sockets. Claims on this surface get
	# assertions: the dish is a CIRCLE (centre RETAIL_DISH_CENTER, radius
	# RETAIL_DISH_RADIUS) and the selection portrait fills it, so a rectangle test
	# against its bounding box would be the wrong shape.
	# Same expression production uses (`_radial_button_position`): the dish centre
	# is authored in dock x and panel-relative y.
	var dish_centre := Vector2(
		(hud_script.RETAIL_DISH_CENTER as Vector2).x,
		panel_rect.position.y + (hud_script.RETAIL_DISH_CENTER as Vector2).y
	)
	var dish_radius: float = hud_script.RETAIL_DISH_RADIUS
	var dish_overlaps: Array[String] = []
	for chip_index in chip_rects.size():
		var nearest := Vector2(
			clampf(dish_centre.x, chip_rects[chip_index].position.x, chip_rects[chip_index].end.x),
			clampf(dish_centre.y, chip_rects[chip_index].position.y, chip_rects[chip_index].end.y)
		)
		if nearest.distance_to(dish_centre) < dish_radius:
			dish_overlaps.append("chip%d %s is %.1f from the dish centre (radius %.1f)" % [
				chip_index, str(chip_rects[chip_index]), nearest.distance_to(dish_centre), dish_radius
			])
	print("RETAIL_RADIAL_LAYOUT dish_centre=%s radius=%.1f" % [str(dish_centre), dish_radius])
	_check("queue_chips_clear_the_palantir_dish", dish_overlaps.is_empty(), str(dish_overlaps))

	# --- every producible radial range lays out cleanly -----------------------
	for count in COVERED_COUNTS:
		var rects: Array[Rect2] = []
		for index in count:
			rects.append(Rect2(
				hud._radial_button_position(index, count, RADIAL_BUTTON_SIZE),
				RADIAL_BUTTON_SIZE
			))
		print("RETAIL_RADIAL_LAYOUT count=%d positions=%s" % [count, str(rects.map(func(r: Rect2) -> Vector2: return r.position))])

		# NO CONTAINMENT ROW HERE ON PURPOSE. `_radial_button_position` ends in two
		# `clampf` calls against this very rectangle, so asserting containment
		# would assert that clampf clamps. Containment is instead gated where it
		# can actually fail - on the live buttons, in
		# fortress_command_surface_runner's `_check_radial_is_in_the_palantir_wheel`.

		# TWO BOUNDS, BOTH RETAIL'S OWN (see the file header for why neither is a
		# plain rectangle-intersection test).
		var closest := INF
		var closest_detail := ""
		var worst_area := 0.0
		var worst_area_detail := "<none>"
		for index in rects.size():
			for other in range(index + 1, rects.size()):
				var separation := rects[index].get_center().distance_to(rects[other].get_center())
				if separation < closest:
					closest = separation
					closest_detail = "%d %s x %d %s" % [
						index, str(rects[index].position), other, str(rects[other].position)
					]
				var area := _overlap_area(rects[index], rects[other])
				if area > worst_area:
					worst_area = area
					worst_area_detail = "%d %s x %d %s = %s" % [
						index, str(rects[index].position), other, str(rects[other].position),
						str(rects[index].intersection(rects[other]).size)
					]
		print("RETAIL_RADIAL_LAYOUT count=%d closest_centres=%.2f (authored %.2f) worst_overlap_area=%.1f (authored %.1f) %s | %s" % [
			count, closest, authored_minimum_separation, worst_area, authored_maximum_overlap_area,
			closest_detail, worst_area_detail
		])
		if count in EXPANDED_COUNTS:
			_check(
				"radial_%d_entries_overlap_no_worse_than_retails_own_sockets" % count,
				worst_area <= authored_maximum_overlap_area + 0.01,
				"worst=%.1f px^2 authored=%.1f px^2 %s" % [worst_area, authored_maximum_overlap_area, worst_area_detail]
			)
			_check(
				"radial_%d_entries_keep_the_authored_socket_separation" % count,
				closest >= authored_minimum_separation - 0.01,
				"closest=%.2f authored=%.2f %s" % [closest, authored_minimum_separation, closest_detail]
			)

		var chip_overlaps: Array[String] = []
		for index in rects.size():
			for chip_index in chip_rects.size():
				if rects[index].intersects(chip_rects[chip_index]):
					chip_overlaps.append("radial%d %s x chip%d %s" % [
						index, str(rects[index]), chip_index, str(chip_rects[chip_index])
					])
		_check(
			"radial_%d_entries_clear_the_production_queue_chips" % count,
			chip_overlaps.is_empty(),
			str(chip_overlaps)
		)

	# --- why the separation oracle is a distance and not a rectangle ----------
	#
	# Retail's authored slots 4 and 5 overlap as 64px BOXES by 17x4 px. If that
	# were a visible defect, retail itself would show it. It does not, because
	# the socket graphic is round: the converted art is transparent exactly where
	# the boxes overlap. This row proves that from the shipped PNG so the metric
	# above is oracle-backed rather than asserted.
	_check_socket_art_corners_are_transparent(hud_script)

	# --- the six-entry page is still exactly the authored palantir arc --------
	var authored_match := true
	for index in hud_script.RETAIL_COMMAND_SLOT_SOURCE.size():
		var placed: Vector2 = hud._radial_button_position(index, hud_script.RETAIL_COMMAND_SLOT_SOURCE.size(), RADIAL_BUTTON_SIZE)
		if not placed.is_equal_approx(panel_rect.position + (hud_script.RETAIL_COMMAND_SLOT_SOURCE[index] as Vector2)):
			authored_match = false
	_check("radial_six_entries_use_the_authored_retail_sockets", authored_match)

	hud.free()
	_finish()


func _check_socket_art_corners_are_transparent(hud_script) -> void:
	var mod_loader := root.get_node_or_null("/root/ModLoader")
	if not _check("socket_art_mod_loader_available", mod_loader != null):
		return
	var atlas_path := ""
	for pack_root_value in mod_loader.list_pack_roots():
		var candidate: String = mod_loader.resolve_pack_path(
			String(pack_root_value), String(hud_script.RETAIL_PALANTIR_ATLAS)
		)
		if candidate != "" and FileAccess.file_exists(candidate):
			atlas_path = candidate
			break
	if not _check("socket_art_atlas_is_mounted", atlas_path != "", String(hud_script.RETAIL_PALANTIR_ATLAS)):
		return
	print("RETAIL_RADIAL_LAYOUT socket_atlas=%s" % atlas_path)
	var image := Image.new()
	if not _check("socket_art_atlas_loads", image.load(atlas_path) == OK, atlas_path):
		return
	var region: Rect2 = hud_script.RETAIL_EMPTY_SOCKET_REGION
	var slot_size: Vector2 = hud_script.RETAIL_COMMAND_SLOT_SIZE
	var slots: Array = hud_script.RETAIL_COMMAND_SLOT_SOURCE
	# The exact rectangle where retail's own slots 4 and 5 overlap, expressed in
	# each button's local pixels.
	var left := Rect2(slots[3] as Vector2, slot_size)
	var right := Rect2(slots[4] as Vector2, slot_size)
	var shared := left.intersection(right)
	if not _check("socket_art_authored_slots_overlap_as_boxes", shared.size.x > 0.0 and shared.size.y > 0.0, str(shared)):
		return
	var worst_alpha := 0
	for owner_rect in [left, right]:
		var local := Rect2(shared.position - owner_rect.position, shared.size)
		for offset_x in int(ceil(local.size.x)):
			for offset_y in int(ceil(local.size.y)):
				var button_point := local.position + Vector2(float(offset_x), float(offset_y))
				var source := Vector2i(
					int(region.position.x + button_point.x / slot_size.x * region.size.x),
					int(region.position.y + button_point.y / slot_size.y * region.size.y)
				)
				source.x = clampi(source.x, 0, image.get_width() - 1)
				source.y = clampi(source.y, 0, image.get_height() - 1)
				worst_alpha = maxi(worst_alpha, int(round(image.get_pixelv(source).a * 255.0)))
	var centre := Vector2i(
		int(region.position.x + region.size.x * 0.5), int(region.position.y + region.size.y * 0.5)
	)
	var centre_alpha := int(round(image.get_pixelv(centre).a * 255.0))
	print("RETAIL_RADIAL_LAYOUT socket_overlap=%s worst_corner_alpha=%d centre_alpha=%d" % [
		str(shared), worst_alpha, centre_alpha
	])
	_check(
		"socket_art_is_opaque_at_its_centre",
		centre_alpha >= 250,
		"centre alpha %d" % centre_alpha
	)
	_check(
		"socket_art_is_transparent_where_the_authored_boxes_overlap",
		worst_alpha <= 16,
		"worst corner alpha %d" % worst_alpha
	)


func _overlap_area(left: Rect2, right: Rect2) -> float:
	var shared := left.intersection(right)
	return maxf(0.0, shared.size.x) * maxf(0.0, shared.size.y)


func _queue_chip_rects(hud) -> Array[Rect2]:
	var rects: Array[Rect2] = []
	for button_value in hud.production_queue_buttons:
		var button := button_value as Button
		rects.append(Rect2(button.get_global_position(), button.size))
	return rects


func _check(name: String, condition: bool, detail: String = "") -> bool:
	if condition:
		passed += 1
		print("RETAIL_RADIAL_LAYOUT PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_RADIAL_LAYOUT FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	return condition


func _finish() -> void:
	print("RETAIL_RADIAL_LAYOUT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
