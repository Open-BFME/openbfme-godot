extends Node
## Loads JSON content packs. Mods override by id (later packs win).

var units: Dictionary = {}
var buildings: Dictionary = {}
var factions: Dictionary = {}
var abilities: Dictionary = {}
var powers: Dictionary = {}
var research: Dictionary = {}
var maps: Dictionary = {}
var globals: Dictionary = {}
var damage_matrix: Dictionary = {}
var pack_meta: Array = []
var asset_roots: Array[String] = []

func _ready() -> void:
	reload()

func reload() -> void:
	units.clear()
	buildings.clear()
	factions.clear()
	abilities.clear()
	powers.clear()
	research.clear()
	maps.clear()
	globals.clear()
	damage_matrix.clear()
	pack_meta.clear()
	asset_roots.clear()
	for root in ModLoader.list_pack_roots():
		_load_pack(root)
	Events.content_reloaded.emit()
	print("[ContentDB] packs=%d units=%d buildings=%d factions=%d powers=%d research=%d maps=%d" % [
		pack_meta.size(), units.size(), buildings.size(), factions.size(), powers.size(), research.size(), maps.size()
	])

func _load_pack(root: String) -> void:
	var pack_path := root.path_join("pack.json")
	var raw: Variant = ModLoader._read_json(pack_path)
	var meta: Dictionary = {}
	if typeof(raw) == TYPE_DICTIONARY:
		meta = (raw as Dictionary).duplicate(true)
	else:
		meta = {"id": root.get_file(), "priority": 100}
	meta["root"] = root
	pack_meta.append(meta)
	asset_roots.append(root.path_join("assets"))
	_load_dir_into(root.path_join("units"), units)
	_load_dir_into(root.path_join("buildings"), buildings)
	_load_dir_into(root.path_join("factions"), factions)
	_load_dir_into(root.path_join("abilities"), abilities)
	_load_dir_into(root.path_join("powers"), powers)
	_load_dir_into(root.path_join("research"), research)
	_load_dir_into(root.path_join("maps"), maps)
	var gpath := root.path_join("globals.json")
	if FileAccess.file_exists(gpath):
		var g: Variant = ModLoader._read_json(gpath)
		if typeof(g) == TYPE_DICTIONARY:
			globals.merge(g as Dictionary, true)
	var mpath := root.path_join("damage_matrix.json")
	if FileAccess.file_exists(mpath):
		var m: Variant = ModLoader._read_json(mpath)
		if typeof(m) == TYPE_DICTIONARY:
			damage_matrix = m as Dictionary

func _load_dir_into(dir_path: String, table: Dictionary) -> void:
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return
	dir.list_dir_begin()
	var fname: String = dir.get_next()
	while fname != "":
		if not dir.current_is_dir() and fname.ends_with(".json"):
			var full := dir_path.path_join(fname)
			var data: Variant = ModLoader._read_json(full)
			if typeof(data) == TYPE_DICTIONARY:
				var d: Dictionary = data
				var id := String(d.get("id", fname.get_basename()))
				d["id"] = id
				d["_source"] = full
				if table.has(id):
					var merged: Dictionary = (table[id] as Dictionary).duplicate(true)
					merged.merge(d, true)
					table[id] = merged
				else:
					table[id] = d
		fname = dir.get_next()
	dir.list_dir_end()

func get_unit(id: String) -> Dictionary:
	return units.get(id, {})

func get_building(id: String) -> Dictionary:
	return buildings.get(id, {})

func get_faction(id: String) -> Dictionary:
	return factions.get(id, {})

func get_ability(id: String) -> Dictionary:
	return abilities.get(id, {})

func get_power(id: String) -> Dictionary:
	return powers.get(id, {})

func get_research(id: String) -> Dictionary:
	return research.get(id, {})

func get_map(id: String) -> Dictionary:
	return maps.get(id, {})

func matrix_mul(damage_type: String, armor_type: String) -> float:
	if damage_matrix.is_empty():
		return 1.0
	var row: Variant = damage_matrix.get(damage_type, {})
	if typeof(row) != TYPE_DICTIONARY:
		return 1.0
	return float((row as Dictionary).get(armor_type, 1.0))

func unit_ids_for_faction(faction_id: String) -> Array[String]:
	var out: Array[String] = []
	for id in units.keys():
		var u: Dictionary = units[id]
		if String(u.get("faction", "")) == faction_id:
			out.append(id)
	return out

func train_build_roster_ids() -> Dictionary:
	## All unit/building ids used by four-faction skirmish rosters.
	var unit_ids: Dictionary = {}
	var building_ids: Dictionary = {}
	for fid in ["gondor", "mordor", "elves", "goblins"]:
		var f: Dictionary = get_faction(fid)
		for b in f.get("buildings", []):
			building_ids[String(b)] = true
			var bd: Dictionary = get_building(String(b))
			for t in bd.get("trains", []):
				unit_ids[String(t)] = true
			for h in bd.get("heroes", []):
				unit_ids[String(h)] = true
		for su in f.get("starting_units", []):
			unit_ids[String(su)] = true
	return {"units": unit_ids.keys(), "buildings": building_ids.keys()}

func resolve_asset(rel_path: String) -> String:
	if rel_path == "" or rel_path == null:
		return ""
	if rel_path.begins_with("res://") or rel_path.begins_with("user://"):
		return rel_path if ResourceLoader.exists(rel_path) or FileAccess.file_exists(rel_path) else ""
	var found := ""
	for root in asset_roots:
		for ext in ["", ".glb", ".obj", ".png", ".wav", ".ogg"]:
			var candidate := root.path_join(rel_path if ext == "" or rel_path.contains(".") else rel_path + ext)
			# also try without adding ext if already has
			if rel_path.contains(".") and ext != "":
				continue
			if FileAccess.file_exists(candidate) or ResourceLoader.exists(candidate):
				found = candidate
				break
		if found != "":
			break
		# direct
		var c2 := root.path_join(rel_path)
		if FileAccess.file_exists(c2) or ResourceLoader.exists(c2):
			found = c2
	return found

func resolve_mesh_path(def: Dictionary) -> String:
	var mesh: String = String(def.get("mesh", ""))
	var id := String(def.get("id", ""))
	# Prefer substantial OBJ (runtime-safe) then GLB. Skip tiny OBJ stubs.
	var candidates: Array[String] = []
	if mesh != "":
		candidates.append(mesh + ".obj")
		candidates.append(mesh + ".glb")
		candidates.append(mesh)
	if id != "":
		candidates.append("models/units/%s.obj" % id)
		candidates.append("models/buildings/%s.obj" % id)
		candidates.append("models/units/%s.glb" % id)
		candidates.append("models/buildings/%s.glb" % id)
		candidates.append("models/units/%s" % id)
		candidates.append("models/buildings/%s" % id)
	var best_glb := ""
	for c in candidates:
		var p := resolve_asset(c)
		if p == "":
			continue
		if p.ends_with(".obj"):
			var abs_p := ProjectSettings.globalize_path(p) if p.begins_with("res://") else p
			if FileAccess.file_exists(abs_p):
				var f := FileAccess.open(abs_p, FileAccess.READ)
				if f and f.get_length() >= 1000:
					return p
			continue
		if (p.ends_with(".glb") or p.ends_with(".gltf")) and best_glb == "":
			best_glb = p
	return best_glb

func resolve_icon_path(def: Dictionary) -> String:
	var icon: String = String(def.get("icon", ""))
	if icon == "":
		return ""
	return resolve_asset(icon)
