class_name NativeHudBridge
extends CanvasLayer
## Read-only native snapshot -> retail HUD adapter. Gameplay writes leave this
## class only as command-v1 bundles submitted through SimHostClient.

signal command_bundle_emitted(bundle: Dictionary)
signal command_acknowledged(bundle: Dictionary)

const RETAIL_HUD_SCRIPT_PATH := "res://src/retail_slice/retail_hud.gd"
const MAX_BUNDLE_BYTES := 128 * 1024 * 1024
const STREAM_COMMAND_LEAD_TICKS := 16
const HUD_UPDATE_STRIDE_TICKS := 3
const COMMAND_KIND := {
	"UNIT_BUILD": "train",
	"DOZER_CONSTRUCT": "build",
	"PLAYER_UPGRADE": "upgrade",
	"OBJECT_UPGRADE": "upgrade",
	"PURCHASE_SCIENCE": "power",
	"SPECIAL_POWER": "power",
}


class SnapshotView extends RefCounted:
	var bridge: NativeHudBridge

	func _init(value: NativeHudBridge) -> void:
		bridge = value

	func entity_ids() -> Array[int]:
		return bridge.radar_entity_ids()

	func entity(id: int) -> Dictionary:
		return bridge.radar_entity(id)

	func structure_ids(_team: int = -1) -> Array[int]:
		return bridge.radar_structure_ids()

	func structure(id: int) -> Dictionary:
		return bridge.radar_structure(id)


class MapView extends RefCounted:
	var ready := false
	var playable_grid_min := Vector2i.ZERO
	var playable_grid_max := Vector2i.ZERO
	var standing_water_polygons: Array = []
	var river_strips: Array = []
	var ford_gates: Array = []
	var reference_elevation := 0.0
	var _cell_size := 1.0

	func configure(document) -> void:
		ready = document != null and document.grid_width > 1 and document.grid_height > 1
		if not ready:
			return
		_cell_size = float(document.cell_size)
		playable_grid_max = Vector2i(document.grid_width - 1, document.grid_height - 1)

	func grid_to_source_xy(grid_x: float, grid_y: float) -> Vector2:
		return Vector2(grid_x, grid_y) * _cell_size

	func local_to_source_horizontal(world: Vector2) -> Vector2:
		return Vector2(world.x, -world.y)

	func source_to_local(source: Vector3) -> Vector3:
		return Vector3(source.x, source.y, -source.z)


var hud
var last_bundle: Dictionary = {}
var last_acknowledged := false
var error := ""
var apt_pack_root := ""

var _host_match: Node
var _client
var _snapshot: Dictionary = {}
var _catalog: Array[Dictionary] = []
var _catalog_by_index: Dictionary = {}
var _catalog_by_name_index: Dictionary = {}
var _object_slots: Dictionary = {}
var _hordes_by_id: Dictionary = {}
var _radar_unit_ids: Array[int] = []
var _radar_units: Dictionary = {}
var _bundle_templates: Dictionary = {}
var _command_sets: Dictionary = {}
var _command_buttons: Dictionary = {}
var _selected_ids: Array[int] = []
var _command_rows: Array[Dictionary] = []
var _native_buttons: Array[Button] = []
var _tick := 0
var _last_indexed_tick := -1
var _seq := 0
var _pending_bundles: Dictionary = {}
var _content_db: Node
var _view: SnapshotView
var _map_view := MapView.new()
var _last_readout_signature := ""
var _last_selection_signature := ""
var _last_command_signature := ""
var _last_portrait_id := ""


func configure(
	host_match: Node,
	client,
	catalog: Array[Dictionary],
	bundle_path: String,
	map_document = null,
	camera: Camera3D = null
) -> bool:
	error = ""
	_host_match = host_match
	_client = client
	_catalog = catalog.duplicate(true)
	_index_catalog()
	if not _load_bundle(bundle_path):
		return false
	_map_view.configure(map_document)
	_view = SnapshotView.new(self)
	_build_hud(camera)
	return hud != null


func accept_snapshot(snapshot: Dictionary) -> void:
	# SimHostMatch and SimHostClient hand snapshots off immutably. Retaining the
	# document avoids a second deep copy of every object array for presentation.
	_snapshot = snapshot
	_tick = int(snapshot.get("tick", _tick))
	if _last_indexed_tick >= 0 and _tick - _last_indexed_tick < HUD_UPDATE_STRIDE_TICKS:
		return
	_last_indexed_tick = _tick
	_index_snapshot()
	_prune_selection()
	_refresh_hud()


func _process(_delta: float) -> void:
	_consume_command_acknowledgements()


func set_selection(ids: Array) -> void:
	_selected_ids.clear()
	for value in ids:
		var id := int(value)
		if id > 0 and not _selected_ids.has(id):
			_selected_ids.append(id)
	_selected_ids.sort()
	_last_selection_signature = ""
	_refresh_hud()


func selected_rows() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for id in _selected_ids:
		var horde := _horde(id)
		if not horde.is_empty():
			result.append(_horde_row(horde))
			continue
		var slot := _object_slot(id)
		if slot >= 0:
			result.append(_object_row(slot))
	return result


func selected_command_buttons() -> Array[Dictionary]:
	return _command_rows.duplicate(true)


func press_command(command: Variant) -> bool:
	var row: Dictionary = {}
	if command is Dictionary:
		row = command as Dictionary
	else:
		var requested := String(command)
		for value in _command_rows:
			if String(value.get("name", "")) == requested:
				row = value
				break
	if row.is_empty():
		return false
	var streaming: bool = _client != null and _client.is_streaming()
	var lead := STREAM_COMMAND_LEAD_TICKS if streaming else 1
	var bundle := make_command_bundle(row, _tick + lead, 0, _seq, _selected_ids)
	if bundle.is_empty():
		return false
	last_bundle = bundle.duplicate(true)
	last_acknowledged = false
	command_bundle_emitted.emit(last_bundle.duplicate(true))
	if _client == null or not _client.send_commands(bundle):
		return false
	_seq += 1
	if streaming:
		_pending_bundles[int(bundle.get("seq", -1))] = bundle.duplicate(true)
	else:
		last_acknowledged = true
		command_acknowledged.emit(last_bundle.duplicate(true))
	return true


func _consume_command_acknowledgements() -> void:
	if _client == null or not _client.has_method("take_stream_command_acknowledgements"):
		return
	for sequence in _client.take_stream_command_acknowledgements():
		var seq := int(sequence)
		if not _pending_bundles.has(seq):
			continue
		var bundle := _pending_bundles[seq] as Dictionary
		_pending_bundles.erase(seq)
		if int(last_bundle.get("seq", -2)) == seq:
			last_acknowledged = true
		command_acknowledged.emit(bundle.duplicate(true))


static func make_command_bundle(
	row: Dictionary, tick: int, seat: int, seq: int, selected_ids: Array
) -> Dictionary:
	var command_type := String(row.get("type", ""))
	if command_type not in ["train", "build", "upgrade", "power", "stop", "stance"]:
		return {}
	var ids := selected_ids.duplicate()
	if ids.is_empty() and command_type != "power":
		return {}
	var args: Dictionary = {}
	if not ids.is_empty():
		args["objects"] = ids
	match command_type:
		"train", "build":
			args["template"] = String(row.get("template", ""))
		"upgrade":
			args["upgrade"] = String(row.get("upgrade", ""))
		"power":
			args["power"] = String(row.get("power", ""))
		"stance":
			args["stance"] = String(row.get("stance", "Battle"))
	for key in args:
		if key != "objects" and String(args[key]).is_empty():
			return {}
	return {
		"schema": "openbfme.command.v1",
		"tick": maxi(0, tick),
		"seat": clampi(seat, 0, 7),
		"seq": maxi(0, seq),
		"commands": [{"type": command_type, "args": args}],
	}


func player_readouts(player_index: int = 0) -> Dictionary:
	for value in _snapshot.get("players", []) as Array:
		var player := value as Dictionary
		if int(player.get("index", -1)) == player_index:
			return player.duplicate(true)
	return {}


func radar_entity_ids() -> Array[int]:
	return _radar_unit_ids.duplicate()


func radar_entity(id: int) -> Dictionary:
	return _radar_units.get(id, {}) as Dictionary


func radar_structure_ids() -> Array[int]:
	var result: Array[int] = []
	for value in (_snapshot.get("objects", {}) as Dictionary).get("id", []) as Array:
		var id := int(value)
		if _is_structure_slot(_object_slot(id)):
			result.append(id)
	return result


func radar_structure(id: int) -> Dictionary:
	return _radar_row(id, true)


func _build_hud(camera: Camera3D) -> void:
	if hud != null:
		hud.queue_free()
	var retail_hud_script := load(RETAIL_HUD_SCRIPT_PATH)
	if retail_hud_script == null:
		error = "retail HUD script unavailable"
		return
	hud = retail_hud_script.new()
	_last_readout_signature = ""
	_last_selection_signature = ""
	_last_command_signature = ""
	_last_portrait_id = ""
	hud.name = "NativeRetailHud"
	hud.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(hud)
	hud.build()
	_content_db = get_node_or_null("/root/ContentDB")
	apt_pack_root = _find_apt_pack_root()
	if apt_pack_root != "" and _content_db != null:
		var roots: Array = (_content_db.get("pack_roots") as Array).duplicate()
		_content_db.call("prefetch_retail_ui_assets", [apt_pack_root] + roots)
		var bind_error: String = hud.bind_retail_train_commands(_content_db, apt_pack_root, true, roots)
		if bind_error != "":
			push_warning("Native HUD retail bind degraded: %s" % bind_error)
	for button_value in hud.train_buttons.values():
		var button := button_value as Button
		button.visible = false
		button.disabled = true
	hud.configure_minimap(_view, _map_view, camera)
	hud.stop_requested.connect(_press_simple.bind("stop"))
	hud.stance_requested.connect(_press_simple.bind("stance"))
	hud.main_menu_requested.connect(_return_to_shell)
	hud.quit_requested.connect(func() -> void: get_tree().quit())
	hud.set_objective("DESTROY THE ENEMY FORTRESS")
	hud.set_feedback("Native core online. Select a battalion or structure.")


func _refresh_hud() -> void:
	if hud == null:
		return
	var player := player_readouts()
	var readout_signature := JSON.stringify(player)
	if not player.is_empty() and readout_signature != _last_readout_signature:
		_last_readout_signature = readout_signature
		hud.set_resources(
			int(player.get("resources", 0)),
			int(player.get("command_points", 0)),
			int(player.get("command_points_max", 0))
		)
		hud.refresh_powers(int(player.get("power_points", 0)), [], {})
	var rows := selected_rows()
	var selection_signature := JSON.stringify(rows)
	if selection_signature == _last_selection_signature:
		return
	_last_selection_signature = selection_signature
	_command_rows = _commands_for_selection(rows)
	_sync_native_buttons()
	if rows.is_empty():
		hud.set_selection("No battalion selected")
		hud.set_dish_level("", 0.0)
		return
	var first := rows[0]
	var member_count := 0
	var health := 0.0
	var max_health := 0.0
	for row in rows:
		member_count += int(row.get("member_count", 1))
		health += float(row.get("health", 0.0))
		max_health += float(row.get("max_health", 0.0))
	var label := String(first.get("template_name", "Selection"))
	if rows.size() > 1:
		label = "%d selected" % rows.size()
	hud.set_selection("%s\n%d members | %d/%d health" % [label, member_count, int(health), int(max_health)])
	hud.set_dish_level(label, health / maxf(max_health, 1.0))
	_apply_portrait(String(first.get("portrait", "")))


func _commands_for_selection(rows: Array[Dictionary]) -> Array[Dictionary]:
	if rows.is_empty():
		return []
	var first_set := String(rows[0].get("command_set", ""))
	if first_set.is_empty() or not _command_sets.has(first_set):
		return _order_commands()
	var result: Array[Dictionary] = []
	for value in (_command_sets[first_set] as Dictionary).get("entries", []) as Array:
		var entry := value as Dictionary
		var button_name := String(entry.get("button", ""))
		if not _command_buttons.has(button_name):
			continue
		var fields := (_command_buttons[button_name] as Dictionary).get("fields", {}) as Dictionary
		var native_kind := String(fields.get("Command", ""))
		if not COMMAND_KIND.has(native_kind):
			continue
		var kind := String(COMMAND_KIND[native_kind])
		var row := {
			"name": button_name,
			"slot": int(entry.get("slot", 0)),
			"type": kind,
			"image": String(fields.get("ButtonImage", "")),
			"text_label": String(fields.get("TextLabel", button_name)),
		}
		if kind in ["train", "build"]:
			row["template"] = String(fields.get("Object", ""))
		elif kind == "upgrade":
			row["upgrade"] = String(fields.get("Upgrade", ""))
		else:
			var power_name := String(fields.get("Science", ""))
			if power_name.is_empty():
				power_name = String(fields.get("SpecialPower", ""))
			row["power"] = power_name
		result.append(row)
	result.append_array(_order_commands())
	return result


func _order_commands() -> Array[Dictionary]:
	return [
		{"name": "NativeStop", "slot": 5, "type": "stop", "text_label": "CONTROLBAR:Stop"},
		{"name": "NativeStance", "slot": 6, "type": "stance", "stance": "Battle", "text_label": "CONTROLBAR:Stance"},
	]


func _sync_native_buttons() -> void:
	var signature := JSON.stringify(_command_rows)
	if signature == _last_command_signature:
		return
	_last_command_signature = signature
	for button in _native_buttons:
		button.queue_free()
	_native_buttons.clear()
	if hud.command_socket_layer == null:
		return
	var used: Dictionary = {}
	for index in mini(_command_rows.size(), 10):
		var row := _command_rows[index]
		var slot := clampi(int(row.get("slot", index + 1)) - 1, 0, hud.RETAIL_COMMAND_SLOT_SOURCE.size() - 1)
		if used.has(slot):
			continue
		used[slot] = true
		var button := Button.new()
		button.name = "Native_%s" % String(row.get("name", index))
		button.tooltip_text = _localized_label(String(row.get("text_label", "")), String(row.get("name", "Command")))
		button.text = button.tooltip_text if String(row.get("image", "")).is_empty() else ""
		button.set_meta("native_command", row.duplicate(true))
		button.pressed.connect(press_command.bind(row))
		_apply_button_texture(button, String(row.get("image", "")))
		hud._apply_authored_command_socket_style(button)
		hud._place_command_button(button, slot)
		_native_buttons.append(button)


func _press_simple(command_type: String) -> void:
	for row in _order_commands():
		if String(row.type) == command_type:
			press_command(row)
			return


func _apply_button_texture(button: Button, image_id: String) -> void:
	if image_id.is_empty():
		return
	var path := String(_content_db.call("resolve_retail_ui_image_path", image_id)) if _content_db != null else ""
	if path.is_empty():
		return
	var image := Image.load_from_file(path)
	if image != null and not image.is_empty():
		button.icon = ImageTexture.create_from_image(image)
		button.expand_icon = true


func _apply_portrait(image_id: String) -> void:
	if hud.selection_portrait == null:
		return
	if image_id == _last_portrait_id:
		return
	_last_portrait_id = image_id
	var path := String(_content_db.call("resolve_retail_ui_image_path", image_id)) if _content_db != null and not image_id.is_empty() else ""
	if path.is_empty():
		hud.selection_portrait.visible = false
		return
	var image := Image.load_from_file(path)
	if image == null or image.is_empty():
		hud.selection_portrait.visible = false
		return
	hud.selection_portrait.texture = ImageTexture.create_from_image(image)
	hud.selection_portrait.visible = true


func _localized_label(string_id: String, fallback: String) -> String:
	var value := String(_content_db.call("get_retail_string", string_id, "")) if _content_db != null else ""
	return value if not value.is_empty() else fallback.replace("Command_", "").capitalize()


func _horde_row(horde: Dictionary) -> Dictionary:
	var template_index := int(horde.get("template", -1))
	var template_name := _template_name(template_index)
	var health := 0.0
	var max_health := 0.0
	var live_members := 0
	for value in horde.get("members", []) as Array:
		var slot := _object_slot(int(value))
		if slot < 0:
			continue
		live_members += 1
		health += _object_number("health", slot)
		max_health += _object_number("max_health", slot)
	var template := _bundle_templates.get(template_name, {}) as Dictionary
	var fields := template.get("fields", {}) as Dictionary
	return {
		"id": int(horde.get("id", 0)),
		"owner": int(horde.get("owner", -1)),
		"template_name": template_name,
		"member_count": live_members,
		"members": (horde.get("members", []) as Array).duplicate(),
		"health": health,
		"max_health": max_health,
		"portrait": String(fields.get("SelectPortrait", "")),
		"command_set": _effective_command_set(template_name, fields),
	}


func _object_row(slot: int) -> Dictionary:
	var objects := _snapshot.get("objects", {}) as Dictionary
	var template_name := _template_name(int((objects.get("template", []) as Array)[slot]))
	var template := _bundle_templates.get(template_name, {}) as Dictionary
	var fields := template.get("fields", {}) as Dictionary
	return {
		"id": int((objects.get("id", []) as Array)[slot]),
		"owner": int((objects.get("owner", []) as Array)[slot]),
		"template_name": template_name,
		"member_count": 1,
		"members": [int((objects.get("id", []) as Array)[slot])],
		"health": _object_number("health", slot),
		"max_health": _object_number("max_health", slot),
		"portrait": String(fields.get("SelectPortrait", "")),
		"command_set": _effective_command_set(template_name, fields),
	}


func _effective_command_set(template_name: String, fields: Dictionary) -> String:
	var declared := String(fields.get("CommandSet", ""))
	if not declared.is_empty():
		return declared
	# The cook keeps SAGE's effective command-set table but some root objects
	# (including MenFortress and horde carriers) omit the redundant root field.
	# Their retail identifier is the template identifier plus CommandSet.
	var conventional := "%sCommandSet" % template_name
	return conventional if _command_sets.has(conventional) else ""


func _radar_row(id: int, structure: bool) -> Dictionary:
	var slot := _object_slot(id)
	if slot < 0:
		return {}
	var objects := _snapshot.get("objects", {}) as Dictionary
	return {
		"id": id,
		"health": int(_object_number("health", slot)),
		"position": Vector2(
			float((objects.get("x", []) as Array)[slot]),
			float((objects.get("y", []) as Array)[slot])
		),
		"team": int((objects.get("owner", []) as Array)[slot]),
		"structure_kind": "native" if structure else "",
	}


func _is_structure_slot(slot: int) -> bool:
	if slot < 0:
		return false
	var objects := _snapshot.get("objects", {}) as Dictionary
	var name := _template_name(int((objects.get("template", []) as Array)[slot]))
	var catalog := _catalog_by_name(name)
	var kindof := catalog.get("kindof", []) as Array
	return kindof.has("STRUCTURE") or kindof.has("IMMOBILE")


func _prune_selection() -> void:
	var live: Array[int] = []
	for id in _selected_ids:
		if not _horde(id).is_empty() or _object_slot(id) >= 0:
			live.append(id)
	_selected_ids = live


func _horde(id: int) -> Dictionary:
	return _hordes_by_id.get(id, {}) as Dictionary


func _object_slot(id: int) -> int:
	return int(_object_slots.get(id, -1))


func _index_snapshot() -> void:
	_object_slots.clear()
	_hordes_by_id.clear()
	_radar_unit_ids.clear()
	_radar_units.clear()
	var objects := _snapshot.get("objects", {}) as Dictionary
	var ids := (_snapshot.get("objects", {}) as Dictionary).get("id", []) as Array
	for index in ids.size():
		_object_slots[int(ids[index])] = index
	for value in _snapshot.get("hordes", []) as Array:
		var horde := value as Dictionary
		var horde_id := int(horde.get("id", 0))
		_hordes_by_id[horde_id] = horde
		var position := Vector2.ZERO
		var health := 0.0
		var member_count := 0
		for member in horde.get("members", []) as Array:
			var slot := _object_slot(int(member))
			if slot < 0:
				continue
			position += Vector2(
				float((objects.get("x", []) as Array)[slot]),
				float((objects.get("y", []) as Array)[slot])
			)
			health += _object_number("health", slot)
			member_count += 1
		if member_count > 0:
			_radar_unit_ids.append(horde_id)
			_radar_units[horde_id] = {
				"id": horde_id,
				"health": int(health),
				"position": position / float(member_count),
				"team": int(horde.get("owner", -1)),
			}


func _object_number(column: String, slot: int) -> float:
	var values := (_snapshot.get("objects", {}) as Dictionary).get(column, []) as Array
	return float(values[slot]) if slot >= 0 and slot < values.size() else 0.0


func _template_name(index: int) -> String:
	return String((_catalog_by_index.get(index, {}) as Dictionary).get("name", ""))


func _catalog_by_name(name: String) -> Dictionary:
	return _catalog_by_name_index.get(name, {}) as Dictionary


func _index_catalog() -> void:
	_catalog_by_index.clear()
	_catalog_by_name_index.clear()
	for row in _catalog:
		_catalog_by_index[int(row.get("index", -1))] = row
		_catalog_by_name_index[String(row.get("name", ""))] = row


func _load_bundle(path: String) -> bool:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _fail("native HUD bundle could not be opened: %s" % path)
	var length := file.get_length()
	if length <= 0 or length > MAX_BUNDLE_BYTES:
		return _fail("native HUD bundle size is invalid: %d" % length)
	var value: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (value is Dictionary):
		return _fail("native HUD bundle root is invalid")
	var bundle := value as Dictionary
	for row_value in bundle.get("templates", []) as Array:
		var row := row_value as Dictionary
		_bundle_templates[String(row.get("name", ""))] = row
	for row_value in bundle.get("command_sets", []) as Array:
		var row := row_value as Dictionary
		_command_sets[String(row.get("name", ""))] = row
	for row_value in bundle.get("command_buttons", []) as Array:
		var row := row_value as Dictionary
		_command_buttons[String(row.get("name", ""))] = row
	return true


func _find_apt_pack_root() -> String:
	if _content_db == null:
		return ""
	for root_value in _content_db.get("pack_roots") as Array:
		var root := String(root_value)
		if FileAccess.file_exists(root.path_join("data/ui/palantir/scene-contract.json")):
			return root
	return ""


func _return_to_shell() -> void:
	if _host_match != null and _host_match.has_method("return_to_shell"):
		_host_match.call("return_to_shell")
		return
	get_tree().change_scene_to_file("res://scenes/boot.tscn")


func _fail(message: String) -> bool:
	error = message
	push_error("[NativeHudBridge] %s" % message)
	return false
