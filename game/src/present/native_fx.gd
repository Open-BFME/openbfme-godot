class_name NativeFx
extends Node3D
## Snapshot/event adapter for retail FX-list, particle, and death cue presenters.

const AbilityFxControllerScript := preload("res://src/retail_slice/retail_ability_fx_controller.gd")

var configured := false
var error := ""
var ability_controller
var particle_controller
var play_counts := {
	"spawn": 0, "fire": 0, "impact": 0, "damage": 0, "death": 0,
	"ability": 0, "authored_sound_fx": 0, "ambient_particles": 0,
}
var route_log: Array[Dictionary] = []
var fx_resolution: Dictionary = {}

var _catalog: Array[Dictionary] = []
var _template_rows: Dictionary = {}
var _runtime_by_template: Dictionary = {}
var _fx_by_template: Dictionary = {}
var _object_positions: Dictionary = {}
var _object_template: Dictionary = {}
var _last_positions: Dictionary = {}
var _last_templates: Dictionary = {}
var _last_tick := -1
var _height_provider := Callable()


func configure(template_rows: Array[Dictionary], retail_map_data, map_pack_root: String) -> bool:
	error = ""
	_catalog = template_rows.duplicate(true)
	for index in _catalog.size():
		_template_rows[index] = _catalog[index]
	var documents := _matching_runtime_documents()
	if documents.is_empty():
		return _fail("no mounted FX documents matched native templates")
	var registry: Dictionary = AbilityFxControllerScript.collect_fx_registry(documents.values())
	var animation_registry: Dictionary = AbilityFxControllerScript.collect_ability_animation_registry(documents.values())
	_height_provider = func(point: Vector2) -> float:
		return 0.0 if retail_map_data == null else float(retail_map_data.local_ground_height(point))
	ability_controller = AbilityFxControllerScript.new()
	ability_controller.name = "RetailAbilityFxController"
	add_child(ability_controller)
	ability_controller.configure(_height_provider, registry, animation_registry)
	_resolve_template_fx(registry)
	if retail_map_data != null and retail_map_data.ready:
		var particle_script = load("res://src/retail_slice/retail_particle_controller.gd")
		if particle_script == null:
			return _fail("retail particle controller did not parse")
		particle_controller = particle_script.new()
		var particle_ok: bool = particle_controller.configure_from_pack(
			map_pack_root,
			retail_map_data.map_root,
			1.0,
			func(point: Vector3) -> Vector3: return point,
			self
		)
		if particle_ok and bool(particle_controller.contract_declared):
			particle_controller.name = "RetailParticleController"
			add_child(particle_controller)
			play_counts.ambient_particles = int(particle_controller.instantiated_emitter_count)
		else:
			if not particle_ok:
				print("NATIVE_FX_PARTICLE_DEGRADED %s" % particle_controller.error)
			particle_controller.free()
			particle_controller = null
	configured = not fx_resolution.is_empty()
	if not configured:
		return _fail("native FX template registry is empty")
	print("NATIVE_FX_READY templates=%d registry=%d ambient=%d" % [
		fx_resolution.size(), registry.size(), int(play_counts.ambient_particles)
	])
	return true


func submit_snapshot(snapshot: Dictionary) -> bool:
	if not configured:
		return false
	var tick := int(snapshot.get("tick", -1))
	if tick <= _last_tick:
		return false
	_last_tick = tick
	_index_snapshot(snapshot)
	var events := snapshot.get("events", []) as Array
	for event_index in events.size():
		var value: Variant = events[event_index]
		if value is Dictionary:
			_consume_event(value as Dictionary, tick, event_index)
	_last_positions = _object_positions.duplicate()
	_last_templates = _object_template.duplicate()
	return true


func resolve_fx_names(template_name: String) -> Dictionary:
	return (fx_resolution.get(template_name.to_lower(), {}) as Dictionary).duplicate(true)


func play_count_total() -> int:
	var total := 0
	for value in play_counts.values():
		total += int(value)
	return total


func _consume_event(event: Dictionary, tick: int, event_index: int) -> void:
	var kind := String(event.get("kind", ""))
	var object_id := int(event.get("object", 0))
	var target_id := int(event.get("target", 0))
	var authored_name := String(event.get("name", ""))
	var point_id := target_id if target_id > 0 and kind in ["damage", "death"] else object_id
	var point := _position(point_id)
	var fx_ids: Array[String] = []
	if authored_name.begins_with("FX_"):
		fx_ids.append(authored_name)
	else:
		fx_ids = _fx_ids_for_object(object_id if object_id > 0 else point_id)
	match kind:
		"spawn":
			_present("spawn", fx_ids, point, tick, event_index)
		"fire":
			_present("fire", fx_ids, point, tick, event_index)
		"damage":
			_present("impact", fx_ids, point, tick, event_index)
			_present_damage_flash(point)
			play_counts.damage = int(play_counts.damage) + 1
		"death":
			if fx_ids.is_empty():
				fx_ids = _fx_ids_for_object(point_id)
			_present("death", fx_ids, point, tick, event_index)
		"ability":
			if fx_ids.is_empty() and not authored_name.is_empty():
				fx_ids.append(authored_name)
			var cast := {
				"point": [point.x, point.y], "fx_lists": fx_ids,
				"effect_kind": "attribute-modifier", "ability_id": authored_name,
				"fx_radius": 24.0,
			}
			ability_controller.present_ability_cast(cast)
			play_counts.ability = int(play_counts.ability) + 1
			_record("ability", fx_ids, point, tick, event_index, {"applied": fx_ids.size()})
		"sound":
			if authored_name.begins_with("FX_"):
				_present("authored_sound_fx", [authored_name], point, tick, event_index)


func _present(kind: String, fx_ids: Array[String], point: Vector2, tick: int, event_index: int) -> void:
	if fx_ids.is_empty():
		_record(kind, fx_ids, point, tick, event_index, {"applied": 0, "reason": "no-authored-fx-list"})
		return
	var receipt: Dictionary = ability_controller.present_drawable_fx_lists(fx_ids, point, "native.%s" % kind)
	if int(receipt.get("applied", 0)) > 0:
		play_counts[kind] = int(play_counts.get(kind, 0)) + 1
	_record(kind, fx_ids, point, tick, event_index, receipt)


func _present_damage_flash(point: Vector2) -> void:
	var light := OmniLight3D.new()
	light.name = "NativeDamageFlash"
	light.position = Vector3(point.x, _ground_height(point) + 8.0, point.y)
	light.light_color = Color(1.0, 0.28, 0.08)
	light.light_energy = 2.5
	light.omni_range = 24.0
	light.shadow_enabled = false
	add_child(light)
	var tween := create_tween()
	tween.tween_property(light, "light_energy", 0.0, 0.18)
	tween.tween_callback(light.queue_free)


func _record(
	kind: String,
	fx_ids: Array[String],
	point: Vector2,
	tick: int,
	event_index: int,
	receipt: Dictionary
) -> void:
	var row := receipt.duplicate(true)
	row["native_kind"] = kind
	row["fx_lists"] = fx_ids.duplicate()
	row["point"] = point
	row["tick"] = tick
	row["event_index"] = event_index
	route_log.append(row)


func _matching_runtime_documents() -> Dictionary:
	_runtime_by_template.clear()
	var content_db := get_node_or_null("/root/ContentDB")
	if content_db == null:
		return {}
	var all: Dictionary = content_db.call("get_playable_unit_runtimes")
	var wanted: Dictionary = {}
	for row in _catalog:
		wanted[String(row.get("name", "")).to_lower()] = true
	var output: Dictionary = {}
	for key in all:
		var document := all[key] as Dictionary
		var horde_name := String(document.get("objectId", ""))
		var composition := (document.get("registration", {}) as Dictionary).get("composition", {}) as Dictionary
		var member_name := String(composition.get("primaryMemberObjectId", horde_name))
		if not wanted.has(horde_name.to_lower()) and not wanted.has(member_name.to_lower()):
			continue
		output[key] = document
		_runtime_by_template[horde_name.to_lower()] = document
		_runtime_by_template[member_name.to_lower()] = document
	return output


func _resolve_template_fx(registry: Dictionary) -> void:
	fx_resolution.clear()
	_fx_by_template.clear()
	for template_key in _runtime_by_template:
		var document := _runtime_by_template[template_key] as Dictionary
		var registration := document.get("registration", {}) as Dictionary
		var bindings := registration.get("fxBindings", {}) as Dictionary
		var authored: Array[String] = []
		for value in bindings.get("authoredFxListIds", []) as Array:
			var fx_id := String(value)
			if fx_id.contains("Weapon") or fx_id.contains("Sword") or fx_id.contains("Bow") or fx_id.contains("Hit"):
				authored.append(fx_id)
		if authored.is_empty():
			for value in bindings.get("authoredFxListIds", []) as Array:
				authored.append(String(value))
		_fx_by_template[template_key] = authored
		var resolved: Array[String] = []
		for fx_id in authored:
			if registry.has(fx_id):
				resolved.append(fx_id)
		fx_resolution[template_key] = {
			"authored": authored.duplicate(), "resolved": resolved,
			"weapon_fx": "" if authored.is_empty() else authored[0],
		}


func _index_snapshot(snapshot: Dictionary) -> void:
	_object_positions.clear()
	_object_template.clear()
	var objects := snapshot.get("objects", {}) as Dictionary
	var ids := objects.get("id", []) as Array
	var templates := objects.get("template", []) as Array
	var xs := objects.get("x", []) as Array
	var zs := objects.get("z", []) as Array
	for index in ids.size():
		var id := int(ids[index])
		_object_positions[id] = Vector2(float(xs[index]), float(zs[index]))
		_object_template[id] = int(templates[index])


func _position(object_id: int) -> Vector2:
	return _object_positions.get(object_id, _last_positions.get(object_id, Vector2.ZERO)) as Vector2


func _fx_ids_for_object(object_id: int) -> Array[String]:
	var template_index := int(_object_template.get(object_id, _last_templates.get(object_id, -1)))
	var row := _template_rows.get(template_index, {}) as Dictionary
	return (_fx_by_template.get(String(row.get("name", "")).to_lower(), []) as Array[String]).duplicate()


func _ground_height(point: Vector2) -> float:
	return float(_height_provider.call(point)) if _height_provider.is_valid() else 0.0


func _fail(message: String) -> bool:
	error = message
	push_error("[NativeFx] %s" % message)
	return false
