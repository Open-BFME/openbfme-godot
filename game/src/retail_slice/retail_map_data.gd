class_name RetailMapData
extends RefCounted
## Bounded runtime bridge for the cooker's engine-neutral Fords of Isen II data.
##
## Godot never parses or packages the proprietary SAGE .map. The importer cooks
## a height grid, passability bits, and JSON geometry outside the repository;
## this class validates those files and exposes a small source-derived runtime
## view to the private retail slice.

# 8 MiB for the per-map cooked documents (terrain, objects, roads, waypoints,
# water). These are sized by ONE map and do not grow with the faction count.
const MAX_DOCUMENT_BYTES := 8 * 1024 * 1024
const MAX_TERRAIN_CELLS := 1_000_000
const MAX_TERRAIN_BINARY_BYTES := MAX_TERRAIN_CELLS * 4
const MAX_TERRAIN_TEXTURES := 256
# Shared multi-map packs publish a union catalog. It may exceed one map's GPU
# texture-table limit; only the current map's <=256 referenced rows are loaded.
const MAX_SHARED_TERRAIN_MATERIALS := 4096
const MAX_TERRAIN_TEXTURE_DIMENSION := 4096
const MAX_TERRAIN_TEXTURE_BYTES := 16 * 1024 * 1024
const MAX_TERRAIN_TEXTURE_TOTAL_BYTES := 128 * 1024 * 1024
const BLEND_DESCRIPTION_RECORD_BYTES := 18
const CLIFF_MAPPING_RECORD_BYTES := 38
const MAX_MAP_OBJECTS := 5000
## Mirror of sage_map.py MAX_PROPERTIES_PER_OBJECT: a single cooked object
## never authors more than this many properties.
const MAX_OBJECT_PROPERTIES := 512
## Mirror of sage_map.py MAX_OBJECT_BINDING_TEXT for property names/values.
const MAX_OBJECT_PROPERTY_TEXT := 1024
const CASTLE_SIEGE_FAMILY := "retail-castle-siege-skirmish"
const CASTLE_SIEGE_STATUS := "blocked-named-gaps"
const CASTLE_SIEGE_ADMISSION_POLICY := "document-loadable-lobby-visible-gameplay-fails-closed"
const CASTLE_SIEGE_BLOCKERS: Array[String] = [
	"walkable-walls",
	"defendable-gates",
	"wall-garrisons",
	"wall-mounted-defenses",
	"skirmish-ai-libraries",
]
## v2 contract schema: the document carries the derived per-map requirement
## set ("required") instead of the one-size-fits-all v1 blocker array, and the
## runtime computes blockers = required - implemented at load time. Canonical
## order: the v1 order is preserved as a subsequence, with "scaleable-walls"
## next to its sibling wall-traversal capability.
const CASTLE_SIEGE_CONTRACT_VERSION := 2
const CASTLE_SIEGE_CAPABILITIES: Array[String] = [
	"walkable-walls",
	"scaleable-walls",
	"defendable-gates",
	"wall-garrisons",
	"wall-mounted-defenses",
	"skirmish-ai-libraries",
]
## Capabilities this runtime implements. Empty today: every required
## capability is still a named gap. Lanes L3+ move names from required into
## this list; a map ships when its computed blockers reach empty.
const CASTLE_SIEGE_IMPLEMENTED: Array[String] = []
## Lane L2a: maps/<slug>/fixtures.json — the gameplay counterpart to
## object-bindings.json. Validated strictly; a malformed document is a named
## load failure, never a silent degrade to decoration.
const MAP_FIXTURES_SCHEMA := "openbfme.sage-map-fixtures"
const MAP_FIXTURE_ROLES: Array[String] = [
	"gate",
	"garrison",
	"static-gate",
	"wall-mounted",
	"wall",
	"structure",
]
const MAX_MAP_FIXTURES := 2048
const MAX_WATER_VERTICES := 4096
## BFME2 1.06 CREEP_OBJECTFILTER lair set (gamedata.ini line 87): every lair
## placement on the five converted maps carries originalOwner PlyrCreeps.
const CREEP_LAIR_TYPE_NAMES: Array[String] = [
	"BarrowWightLair", "CaveTrollLair", "CaveTrollLairSnow", "FireDrakeLair",
	"MoriarGoblinLair", "MoriarGoblinLairSnow", "SpiderLair", "WargLair",
]
const MAX_WAYPOINTS := 256
const MAX_GENERIC_PROPS := 72
const MAX_OBJECT_BINDING_RECORDS := 512
const MAX_BOUND_MODEL_BYTES := 256 * 1024 * 1024
const MAX_ROAD_MATERIALS := 256
const MAX_ROAD_TEXTURE_DIMENSION := 4096
const MAX_ROAD_TEXTURE_BYTES := 16 * 1024 * 1024
const MAX_ROAD_TEXTURE_TOTAL_BYTES := 64 * 1024 * 1024
## Row bound for the PACK-WIDE provenance inventory. Raised from 20,000 to
## 65,536 on 2026-07-28 against measurement, not assumption. Every manifest.json
## in the workspace was counted:
##   men v-slice (1 faction)      5,275 rows
##   six-faction v-slice (live)  16,230 rows   <- the selected pack
##   rotwk angmar supplement      2,164 rows   (separate pack, own manifest)
## A composed 7-faction pack therefore projects to ~18,400 rows, which the old
## 20,000 admitted with 8% to spare - and the six-faction manifests already grew
## 15,655 -> 16,230 across recent rebuilds. A bound that close to the real value
## is a tripwire, not a resource guard, and this project has already shipped one
## fail-closed regression from exactly that (the 8 MiB document bound refusing
## the owner's own pack). 65,536 is ~3.6x the 7-faction projection: still hard,
## no longer standing on the working set's toes.
const MAX_PROVENANCE_BUNDLE_FILES := 65_536
## Byte bound for that same inventory, derived from the row bound so the two
## guards cannot disagree: previously the shared 8 MiB document bound refused
## inventories the row bound explicitly permits, and the pack that tripped it
## was the owner's own. The per-map MAX_DOCUMENT_BYTES above stays at 8 MiB -
## cooked map documents are sized by ONE map and never scale with faction count,
## so raising them wholesale (as the audit lane did, to 64 MiB) would loosen a
## bound that was never the one under pressure.
##
## Arithmetic. Measured record cost, path + sha256 + size per row:
##   men v-slice (1 faction):   5,275 rows /  5,087,289 B = 964 B/row
##   six-faction v-slice:      16,230 rows / 16,059,309 B = 989 B/row
## 1,600 B/row is ~1.6x the observed worst case, covering longer object paths.
## 65,536 x 1,600 = 104,857,600 B, so the byte bound is reached only by an
## inventory the row bound would already refuse. Both stay fail-closed, and the
## real 16.06 MB document is admitted with room for the factions still to come.
const MAX_PROVENANCE_MANIFEST_BYTES := MAX_PROVENANCE_BUNDLE_FILES * 1600
const LOCAL_START_SEPARATION := 76.0
const FORD_CORRIDOR_DILATION_CELLS := 5
const MAX_ROUTE_CELLS := 1024
# Search budget for snapping home-layout anchors (fortress, farms, spawns) to
# the nearest walkable cell. Fords' anchors are walkable as computed, so the
# budget never engages there; maps with border-hugging starts (Rivendell's
# team base sits ~34 cells outside its playable border) need the reach.
const WALKABLE_SNAP_RADIUS_CELLS := 64
const REQUIRED_FORD_NAMES: Array[String] = ["ford1", "ford2", "ford3"]
## Per-map runtime contract for the facts the cooked documents cannot declare
## themselves. The Fords of Isen II slice contract requires its three authored
## ford crossings, re-reviews one source-impassable cell, and enforces the
## reviewed retail crossing rule (cooked water blocks navigation except at the
## named ford corridors). Every other map navigates on the source-authored
## passability grid exactly as cooked: their water still rasterizes for
## presentation and diagnostics, but the Fords-only mask rule is not applied —
## Mordor's moat gap and Rivendell's shores are source-passable by design.
const MAP_RUNTIME_PROFILES := {
	"bfme2.map.fords-of-isen-ii": {
		"required_ford_names": REQUIRED_FORD_NAMES,
		"known_impassable_cells": [Vector2i(208, 142)],
		"water_blocks_navigation": true,
	},
}
const DEFAULT_MAP_RUNTIME_PROFILE := {
	"required_ford_names": [],
	"known_impassable_cells": [],
	"water_blocks_navigation": false,
}

var ready := false
var error := ""
var pack_root := ""
var map_root := ""
var map_id := ""
var source_virtual_path := ""
var source_sha256 := ""
var source_bytes := 0
## Authored only by the castle/siege admission profile. Existing maps have an
## empty array, so their deterministic runtime path is byte-for-byte unchanged.
## A non-empty array means the source document is loadable and inspectable, but
## battlefield configuration and menu availability must refuse gameplay by name.
var castle_gameplay_blockers: Array[String] = []
## The castle contract's requirement set (v2 "required"; v1's blocker array),
## retained so a fixtures document's capabilities can be cross-checked
## against it — both are the same L1 derivation and must agree.
var castle_required_capabilities: Array[String] = []
## Lane L2a: validated rows from maps/<slug>/fixtures.json. Empty on pre-L2a
## packs (the document pointer is absent there); absence is legal, a
## malformed document never is.
var map_fixtures: Array = []
var fixtures_omitted: Array = []
## Lane L2b: the sim-seeded subset of map_fixtures, translated to sim-local
## space, plus the named defer tally (reason -> placement count) for the rows
## that never seed. Both surface through simulation_configuration(); the sim
## only consumes them when its "enable_castle_fixtures" rule is on.
var castle_fixture_placements: Array[Dictionary] = []
var castle_fixture_deferred: Dictionary = {}
## source object index -> typeName, for cross-checking fixture rows against
## the cooked objects they claim to describe (never trust a document pair).
var _object_type_by_index: Array[String] = []
var _map_runtime_profile: Dictionary = DEFAULT_MAP_RUNTIME_PROFILE
var width := 0
var height := 0
var heightmap_bytes := 0
var passability_bytes := 0
var impassable_count := 0
var terrain_texture_count := 0
var terrain_texture_width := 0
var terrain_texture_height := 0
var terrain_texture_array_dimension := 0
var terrain_material_catalog: Array[Dictionary] = []
var terrain_tile_indices := PackedInt32Array()
var terrain_tile_escape_count := 0
var terrain_blend_cells := PackedInt32Array()
var terrain_three_way_blend_cells := PackedInt32Array()
var terrain_cliff_cells := PackedInt32Array()
var terrain_blend_descriptions: Array[Dictionary] = []
var terrain_cliff_mappings: Array[Dictionary] = []
var terrain_nonzero_blend_cell_count := 0
var terrain_nonzero_three_way_blend_cell_count := 0
var terrain_nonzero_cliff_cell_count := 0
var terrain_source_layer_sha256: Dictionary = {}
var _terrain_cell_texture_indices := PackedInt32Array()
var border_width := 0
var playable_world_extent := Vector2.ZERO
var playable_grid_min := Vector2i.ZERO
var playable_grid_max := Vector2i.ZERO
var navigation_grid_min := Vector2i.ZERO
var navigation_grid_max := Vector2i.ZERO
var raw_elevation_min := 0
var raw_elevation_max := 0
var computed_raw_elevation_min := 0
var computed_raw_elevation_max := 0
var computed_impassable_count := 0
var heightmap_sha256 := ""
var passability_sha256 := ""
var object_count := 0
var nonroad_object_count := 0
var road_type_count := 0
var road_control_point_count := 0
var road_segment_count := 0
var road_unresolved_control_point_count := 0
var road_unique_endpoint_count := 0
var road_shared_node_count := 0
var road_curve_candidate_node_count := 0
var road_crossing_candidate_node_count := 0
var road_modifier_flags_or := 0
var road_type_ids: Array[String] = []
var road_control_points: Array[Dictionary] = []
var road_segments: Array[Dictionary] = []
var roads_path := ""
var road_material_count := 0
var road_material_catalog: Array[Dictionary] = []
var road_materials_path := ""
var road_source_report_aggregate_sha256 := ""
var provenance_manifest_path := ""
var waypoint_count := 0
var local_named_waypoints: Dictionary = {}
var player_start_count := 0
var standing_water_count := 0
var river_count := 0
var source_binary_packaged := true
var horizontal_scale := 0.0
var vertical_scale := 0.0
var passability_row_stride := 0
var reference_elevation := 0.0
var local_transform_scale := 0.0
var local_transform_origin := Vector2.ZERO
var local_axis_x := Vector2.RIGHT
var local_axis_z := Vector2.DOWN
var local_bounds := Rect2()
var height_samples := PackedByteArray()
var passability_bits := PackedByteArray()
var player_starts: Dictionary = {}
var local_player_starts: Dictionary = {}
var standing_water_polygons: Array = []
var river_strips: Array[Dictionary] = []
var ford_gates: Array[Dictionary] = []
var generic_prop_placements: Array[Dictionary] = []
var bound_prop_placements: Array[Dictionary] = []
var bound_structure_placements: Array[Dictionary] = []
## Authored creep-lair placements (retail PlyrCreeps camps), exposed to the
## deterministic simulation independent of visual binding: bound lair types
## keep their lifecycle visual in bound_structure_placements, unconverted lair
## families (goblin/spider/wight/drake) still surface here so the sim can fail
## closed into a recorded provisional instead of silently dropping the camp.
var creep_lair_placements: Array[Dictionary] = []
var bound_prop_type_ids: Array[String] = []
var bound_structure_type_ids: Array[String] = []
var logical_prop_type_ids: Array[String] = []
var unresolved_prop_type_ids: Array[String] = []
var bound_prop_placement_count := 0
var bound_structure_placement_count := 0
var logical_prop_placement_count := 0
var unresolved_prop_placement_count := 0
var object_binding_record_count := 0
var object_binding_resolution_status := ""
var object_bindings_path := ""
var _object_binding_by_type: Dictionary = {}
var map_outline := PackedVector2Array()
var navigation_ready := false
var navigation_walkable_count := 0
var navigation_water_blocked_count := 0
var navigation_ford_corridor_count := 0
var navigation_build_count := 0
var route_query_count := 0
var _navigation_grid: AStarGrid2D
var _water_cells := PackedByteArray()
var _ford_corridor_cells: Dictionary = {}


func load_from_pack(
	selected_pack_root: String,
	map_definition: Dictionary,
	cancel_check: Callable = Callable()
) -> bool:
	var profile_init := OS.get_environment("OPENBFME_PROFILE_INIT") == "1"
	var profile_last_ms := Time.get_ticks_msec()
	_reset()
	_validation_cancel_check = cancel_check
	if _validation_cancelled():
		return false
	pack_root = selected_pack_root
	var source_path := String(map_definition.get("_source", ""))
	if source_path == "" or not ModLoader.path_is_within(pack_root, source_path):
		return _fail("map document escaped the selected pack")
	map_root = source_path.get_base_dir()
	source_binary_packaged = bool(map_definition.get("sourceBinaryPackaged", true))
	if source_binary_packaged:
		return _fail("retail source map must not be packaged")
	map_id = String(map_definition.get("id", ""))
	if not _load_castle_siege_contract(map_definition.get("castleSiege", null)):
		return false
	_map_runtime_profile = MAP_RUNTIME_PROFILES.get(map_id, DEFAULT_MAP_RUNTIME_PROFILE)
	var source_identity := _dictionary(map_definition.get("source", {}))
	source_virtual_path = String(source_identity.get("virtualPath", ""))
	source_sha256 = String(source_identity.get("sha256", ""))
	source_bytes = int(source_identity.get("sourceBytes", 0))

	# Roads are an optional cooked layer. When both roads + roadMaterials are
	# declared, both documents must validate. Cooks that emit an empty roads
	# document without materials (roadSummary status=empty) are treated as
	# "no road layer" rather than half-layer failure — several RotWK/BFME2
	# catalog maps ship exactly that shape.
	var roads_raw: Variant = map_definition.get("roads", "")
	var road_materials_raw: Variant = map_definition.get("roadMaterials", "")
	# JSON null must not become the literal string "<null>" / non-empty path.
	var roads_rel := "" if roads_raw == null else String(roads_raw).strip_edges()
	var road_materials_rel := "" if road_materials_raw == null else String(road_materials_raw).strip_edges()
	if roads_rel == "<null>" or roads_rel.to_lower() == "null":
		roads_rel = ""
	if road_materials_rel == "<null>" or road_materials_rel.to_lower() == "null":
		road_materials_rel = ""
	var roads_declared := roads_rel != ""
	var road_materials_declared := road_materials_rel != ""
	var terrain := _read_document(String(map_definition.get("terrain", "")), "terrain")
	if _validation_cancelled():
		return false
	var objects := _read_document(String(map_definition.get("objects", "")), "objects")
	if _validation_cancelled():
		return false
	var roads := _read_document(roads_rel, "roads") if roads_declared else {}
	if _validation_cancelled():
		return false
	var road_summary := _dictionary(map_definition.get("roadSummary", {}))
	# Normalize an exact empty network before reading materials. Empty roads have
	# no road IDs to materialize, so a stale roadMaterials pointer is irrelevant;
	# non-empty roads still require and validate their complete material layer.
	var roads_geometry_only := false
	if roads_declared and _roads_document_is_empty_network(roads, road_summary, objects):
		# Vacuous empty roads inventory — treat as no road layer.
		roads_declared = false
		road_materials_declared = false
		roads = {}
		road_summary = {}
	elif roads_declared and not road_materials_declared:
		# Incomplete cook: road geometry present without material catalog.
		# Load geometry so the match boots; road textures stay absent (no fake art).
		roads_geometry_only = true
	elif road_materials_declared and not roads_declared:
		return _fail("cooked map declares only half of its road layer")
	var road_materials := {}
	if road_materials_declared and not roads_geometry_only:
		var resolved_road_materials := _resolve(road_materials_rel)
		if resolved_road_materials == "":
			return _fail("missing or unsafe road materials document")
		if not FileAccess.file_exists(resolved_road_materials):
			# A safe catalog pointer may name a file the cook omitted. Preserve the
			# geometry-only boot fallback, but never use it to forgive an escaped,
			# malformed, oversized, or otherwise invalid document.
			road_materials_declared = false
			roads_geometry_only = true
		else:
			road_materials = _read_document(road_materials_rel, "road materials")
			if road_materials.is_empty():
				return false
	var object_bindings := _read_document(String(map_definition.get("objectBindings", "")), "object bindings")
	if _validation_cancelled():
		return false
	var waypoints := _read_document(String(map_definition.get("waypoints", "")), "waypoints")
	if _validation_cancelled():
		return false
	var water := _read_document(String(map_definition.get("water", "")), "water")
	if _validation_cancelled():
		return false
	# Lane L2a fixtures are optional: pre-L2a packs declare no pointer and load
	# exactly as before. A DECLARED document must validate strictly below.
	var fixtures_raw: Variant = map_definition.get("fixtures", "")
	var fixtures_rel := "" if fixtures_raw == null else String(fixtures_raw).strip_edges()
	if fixtures_rel == "<null>" or fixtures_rel.to_lower() == "null":
		fixtures_rel = ""
	var fixtures_declared := fixtures_rel != ""
	var fixtures := _read_document(fixtures_rel, "fixtures") if fixtures_declared else {}
	if _validation_cancelled():
		return false
	if profile_init:
		print("RETAIL_MAP_DATA_PHASE name=documents delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	if terrain.is_empty() or objects.is_empty() or object_bindings.is_empty() or waypoints.is_empty() or water.is_empty():
		return false
	if fixtures_declared and fixtures.is_empty():
		# A declared fixtures pointer that resolves to nothing readable is a
		# hard failure (the diagnostic was named by _read_document).
		return false
	if roads_declared and roads.is_empty():
		return _fail("roads document missing or unreadable")
	if String(terrain.get("schema", "")) != "openbfme.sage-terrain" or int(terrain.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked terrain schema")
	if String(objects.get("schema", "")) != "openbfme.sage-map-objects" or int(objects.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked object schema")
	if roads_declared and (String(roads.get("schema", "")) != "openbfme.sage-roads" or int(roads.get("schemaVersion", -1)) != 0):
		return _fail("unexpected cooked roads schema")
	if String(object_bindings.get("schema", "")) != "openbfme.sage-object-bindings" or int(object_bindings.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked object-binding schema")
	if String(waypoints.get("schema", "")) != "openbfme.sage-waypoints" or int(waypoints.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked waypoint schema")
	if String(water.get("schema", "")) != "openbfme.sage-water" or int(water.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked water schema")

	if not _load_terrain(terrain, String(map_definition.get("terrainMaterials", ""))):
		return false
	if _validation_cancelled():
		return false
	if profile_init:
		print("RETAIL_MAP_DATA_PHASE name=terrain delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	if not _load_waypoints(waypoints):
		return false
	if not _load_water(water):
		return false
	if _validation_cancelled():
		return false
	if profile_init:
		print("RETAIL_MAP_DATA_PHASE name=waypoints_water delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	object_bindings_path = _resolve(String(map_definition.get("objectBindings", "")))
	if object_bindings_path == "":
		return _fail("object-binding document escaped the selected map")
	if roads_declared:
		roads_path = _resolve(roads_rel)
		if roads_path == "":
			return _fail("roads document escaped the selected map")
		if road_materials_declared and not roads_geometry_only:
			road_materials_path = _resolve(road_materials_rel)
			if road_materials_path == "":
				return _fail("road-material document escaped the selected map")
	if not _load_objects(objects, roads if roads_declared else {}, object_bindings, road_summary):
		return false
	if fixtures_declared and not _load_fixtures(fixtures):
		return false
	if fixtures_declared and not _cross_check_fixtures_against_objects():
		return false
	_derive_castle_fixture_placements()
	if _validation_cancelled():
		return false
	if roads_declared and road_materials_declared and not roads_geometry_only and road_unresolved_control_point_count == 0 and not _load_road_materials(road_materials):
		return false
	if profile_init:
		print("RETAIL_MAP_DATA_PHASE name=objects_roads delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	_build_map_outline()
	if _validation_cancelled():
		return false
	if not _build_navigation():
		return false
	if profile_init:
		print("RETAIL_MAP_DATA_PHASE name=navigation delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
	ready = true
	return true


func _load_castle_siege_contract(value: Variant) -> bool:
	castle_gameplay_blockers.clear()
	castle_required_capabilities.clear()
	if value == null:
		return true
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("castleSiege must be an object")
	var contract := value as Dictionary
	if contract.has("version"):
		return _load_castle_siege_contract_v2(contract)
	# v1: the admission lane's seal. The mounted pack predates per-map
	# capability derivation, so this exact shape must keep loading.
	if (
		contract.size() != 4
		or String(contract.get("family", "")) != CASTLE_SIEGE_FAMILY
		or String(contract.get("gameplayStatus", "")) != CASTLE_SIEGE_STATUS
		or String(contract.get("admissionPolicy", "")) != CASTLE_SIEGE_ADMISSION_POLICY
		or typeof(contract.get("blockers", null)) != TYPE_ARRAY
	):
		return _fail("invalid castleSiege admission contract")
	var blockers := contract.get("blockers", []) as Array
	if blockers.size() != CASTLE_SIEGE_BLOCKERS.size():
		return _fail("invalid castleSiege blocker inventory")
	for index in range(CASTLE_SIEGE_BLOCKERS.size()):
		if typeof(blockers[index]) != TYPE_STRING or String(blockers[index]) != CASTLE_SIEGE_BLOCKERS[index]:
			return _fail("invalid castleSiege blocker inventory")
	castle_gameplay_blockers.assign(blockers)
	# v1's exact blocker array is also the requirement set (nothing was
	# implemented when the seal shipped); a v2 document states it explicitly.
	castle_required_capabilities.assign(blockers)
	return true


func _load_castle_siege_contract_v2(contract: Dictionary) -> bool:
	# v2: the document carries the derived per-map requirement set; blockers
	# are computed as required - implemented, never authored.
	var version: Variant = contract.get("version", null)
	if (
		contract.size() != 5
		or (typeof(version) != TYPE_INT and typeof(version) != TYPE_FLOAT)
		or float(version) != float(CASTLE_SIEGE_CONTRACT_VERSION)
		or String(contract.get("family", "")) != CASTLE_SIEGE_FAMILY
		or String(contract.get("gameplayStatus", "")) != CASTLE_SIEGE_STATUS
		or String(contract.get("admissionPolicy", "")) != CASTLE_SIEGE_ADMISSION_POLICY
		or typeof(contract.get("required", null)) != TYPE_ARRAY
	):
		return _fail("invalid castleSiege admission contract")
	var required := contract.get("required", []) as Array
	if required.is_empty():
		return _fail("invalid castleSiege capability inventory")
	var previous_rank := -1
	var seen := {}
	for entry in required:
		if typeof(entry) != TYPE_STRING:
			return _fail("invalid castleSiege capability inventory")
		var capability := String(entry)
		var rank: int = CASTLE_SIEGE_CAPABILITIES.find(capability)
		if rank < 0 or rank <= previous_rank or seen.has(capability):
			return _fail("invalid castleSiege capability inventory")
		seen[capability] = true
		previous_rank = rank
	for entry in required:
		var capability := String(entry)
		if not CASTLE_SIEGE_IMPLEMENTED.has(capability):
			castle_gameplay_blockers.append(capability)
	castle_required_capabilities.assign(required)
	return true


func _load_fixtures(value: Variant) -> bool:
	## Lane L2a: validate maps/<slug>/fixtures.json. Absent (null) is a pre-L2a
	## pack and loads as no fixtures; a present document validates strictly and
	## refuses every malformed shape with a named diagnostic — it never
	## silently degrades castle structures back to decoration.
	map_fixtures.clear()
	fixtures_omitted.clear()
	if value == null:
		return true
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("fixtures must be an object")
	var document := value as Dictionary
	if String(document.get("schema", "")) != MAP_FIXTURES_SCHEMA or int(document.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked fixtures schema")
	if castle_required_capabilities.is_empty():
		return _fail("fixtures document without a castleSiege contract")
	var capabilities := _array(document.get("capabilities", null))
	if capabilities.is_empty():
		return _fail("invalid fixtures capability inventory")
	var declared: Array[String] = []
	var previous_rank := -1
	var seen_capabilities := {}
	for entry in capabilities:
		if typeof(entry) != TYPE_STRING:
			return _fail("invalid fixtures capability inventory")
		var capability := String(entry)
		var rank: int = CASTLE_SIEGE_CAPABILITIES.find(capability)
		if rank < 0 or rank <= previous_rank or seen_capabilities.has(capability):
			return _fail("invalid fixtures capability inventory")
		seen_capabilities[capability] = true
		previous_rank = rank
		declared.append(capability)
	if declared != castle_required_capabilities:
		return _fail("fixtures capabilities disagree with the castleSiege contract")
	var rows := _array(document.get("fixtures", null))
	if rows.is_empty():
		return _fail("cooked fixtures document has no fixture rows")
	# Oversize is its own refusal (L2a follow-up F5): a document whose count
	# metadata matches a too-large rows list used to share the mismatch
	# diagnostic, which named the wrong defect.
	if rows.size() > MAX_MAP_FIXTURES:
		return _fail("cooked fixtures document exceeds its bound")
	if int(document.get("count", -1)) != rows.size():
		return _fail("cooked fixtures count does not match its metadata")
	var seen_indices := {}
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail("invalid castle fixture record")
		var record := row_value as Dictionary
		var type_name := String(record.get("typeName", ""))
		if type_name == "" or type_name.length() > 128:
			return _fail("invalid castle fixture record")
		var role := String(record.get("role", ""))
		if not MAP_FIXTURE_ROLES.has(role):
			return _fail("castle fixture has an unknown role")
		if not _exact_integer(record.get("index", null)) or int(record.get("index")) < 0:
			return _fail("invalid castle fixture placement")
		var index := int(record.get("index"))
		if seen_indices.has(index):
			return _fail("castle fixture placement index is duplicated")
		seen_indices[index] = true
		if not _valid_fixture_vector(record.get("position", null)) or not _valid_fixture_number(record.get("angle", null)):
			return _fail("invalid castle fixture placement")
		if String(record.get("originalOwner", "")) == "":
			return _fail("castle fixture has an invalid originalOwner")
		var kind_of := _array(record.get("kindOf", null))
		var kind_of_valid := not kind_of.is_empty()
		for kind in kind_of:
			if typeof(kind) != TYPE_STRING or String(kind) == "":
				kind_of_valid = false
		if not kind_of_valid:
			return _fail("invalid castle fixture record")
		if not _valid_fixture_number(record.get("maxHealth", null)) or float(record.get("maxHealth")) <= 0.0:
			return _fail("castle fixture has an invalid maxHealth")
		var armor: Variant = record.get("armor", null)
		# JSON null is the recorded SAGE passthrough (no authored ArmorSet,
		# e.g. CaptureFlag); anything else malformed fails closed.
		if armor != null and (typeof(armor) != TYPE_STRING or String(armor) == ""):
			return _fail("castle fixture has an invalid armor")
		if record.has("initialHealth") and not _valid_fixture_number(record.get("initialHealth")):
			return _fail("invalid castle fixture placement")
		for flag in ["indestructible", "enabled", "targetable"]:
			if record.has(flag) and typeof(record.get(flag)) != TYPE_BOOL:
				return _fail("invalid castle fixture placement")
		match role:
			"gate":
				if record.has("garrison"):
					return _fail("castle fixture role disagrees with its module block")
				if not _valid_fixture_gate_block(record.get("gate", null)):
					return false
			"garrison":
				if record.has("gate"):
					return _fail("castle fixture role disagrees with its module block")
				if not _valid_fixture_garrison_block(record.get("garrison", null)):
					return false
			_:
				if record.has("gate") or record.has("garrison"):
					return _fail("castle fixture role disagrees with its module block")
		map_fixtures.append(record)
	for entry in _array(document.get("omitted", [])):
		if typeof(entry) != TYPE_DICTIONARY:
			return _fail("map fixtures document has an invalid omitted entry")
		var omission := entry as Dictionary
		if String(omission.get("typeName", "")) == "" or String(omission.get("reason", "")) != "no-authored-body-maxhealth":
			return _fail("map fixtures document has an invalid omitted entry")
		fixtures_omitted.append(omission)
	return true


func _valid_fixture_number(value: Variant) -> bool:
	if typeof(value) != TYPE_INT and typeof(value) != TYPE_FLOAT:
		return false
	return _finite_number(float(value))


func _valid_fixture_vector(value: Variant) -> bool:
	if typeof(value) != TYPE_ARRAY:
		return false
	var parts := value as Array
	if parts.size() != 3:
		return false
	for part in parts:
		if not _valid_fixture_number(part):
			return false
	return true


func _valid_fixture_gate_block(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("gate fixture has an invalid gate module block")
	var block := value as Dictionary
	if typeof(block.get("openByDefault", null)) != TYPE_BOOL:
		return _fail("gate fixture has an invalid gate module block")
	if not _valid_fixture_number(block.get("resetMilliseconds", null)) or float(block.get("resetMilliseconds")) <= 0.0:
		return _fail("gate fixture has an invalid gate module block")
	if not _valid_fixture_number(block.get("percentOpenForPathing", null)) or float(block.get("percentOpenForPathing")) < 0.0:
		return _fail("gate fixture has an invalid gate module block")
	var geometries: Variant = block.get("geometries", null)
	if typeof(geometries) != TYPE_DICTIONARY or (geometries as Dictionary).is_empty():
		return _fail("gate fixture has invalid named geometries")
	for geometry_name in (geometries as Dictionary):
		if String(geometry_name) == "":
			return _fail("gate fixture has invalid named geometries")
		var entry: Variant = (geometries as Dictionary)[geometry_name]
		if typeof(entry) != TYPE_DICTIONARY:
			return _fail("gate fixture has invalid named geometries")
		var geometry := entry as Dictionary
		if String(geometry.get("shape", "")) == "":
			return _fail("gate fixture has invalid named geometries")
		for field in ["majorRadius", "minorRadius", "height"]:
			if not _valid_fixture_number(geometry.get(field, null)) or float(geometry.get(field)) <= 0.0:
				return _fail("gate fixture has invalid named geometries")
		if not _valid_fixture_vector(geometry.get("offset", null)):
			return _fail("gate fixture has invalid named geometries")
	return true


func _valid_fixture_garrison_block(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("garrison fixture is missing its garrison module block")
	var block := value as Dictionary
	if not _exact_integer(block.get("containMax", null)) or int(block.get("containMax")) <= 0:
		return _fail("garrison fixture has an invalid containMax")
	return true


func _safe_object_property_value(value: Variant) -> bool:
	## Exactly the value shapes sage_map.py::_parse_property can emit
	## (bool / i32 / f32 / ascii / unicode strings); anything else fails closed.
	match typeof(value):
		TYPE_BOOL, TYPE_INT:
			return true
		TYPE_FLOAT:
			return _finite_number(value)
		TYPE_STRING:
			return String(value).length() <= MAX_OBJECT_PROPERTY_TEXT
	return false


func _castle_fixture_seed_disposition(record: Dictionary) -> String:
	## Lane L2b seed filter, mirrored clause-for-clause from the importer's
	## castle_fixtures.castle_fixture_seed_disposition and pinned to the same
	## oracle counts (Erebor seeds 587, Carn Dum 260; both suites assert).
	var type_name := String(record.get("typeName", "")).to_lower()
	for lair_type in CREEP_LAIR_TYPE_NAMES:
		# Creep-lair placements belong to the creep lane's routing; seeding
		# them here too would double-spawn every lair.
		if lair_type.to_lower() == type_name:
			return "creep-lair-owned"
	var kind_of := _array(record.get("kindOf", []))
	if kind_of.has("INERT"):
		# Indestructible scenery (Carn Dum's high-pass rocks, mine carts).
		return "inert-scenery"
	if kind_of.has("CAPTURABLE") or kind_of.has("CAPTUREFLAG"):
		# Capture-flag semantics belong to the capture lane.
		return "capturable-flag"
	return "seed"


func _cross_check_fixtures_against_objects() -> bool:
	## Every fixture row claims an objects.json row (typeName + index); verify
	## the claim against the cooked objects actually loaded. A fixtures
	## document describing objects this map does not place fails closed.
	for record_value in map_fixtures:
		var record: Dictionary = record_value
		var index := int(record.get("index", -1))
		if index < 0 or index >= _object_type_by_index.size() or _object_type_by_index[index] != String(record.get("typeName", "")):
			return _fail("castle fixture does not cross-match its cooked object")
	return true


func _derive_castle_fixture_placements() -> void:
	castle_fixture_placements.clear()
	castle_fixture_deferred.clear()
	for record_value in map_fixtures:
		var record: Dictionary = record_value
		var disposition := _castle_fixture_seed_disposition(record)
		if disposition != "seed":
			castle_fixture_deferred[disposition] = int(castle_fixture_deferred.get(disposition, 0)) + 1
			continue
		var local := source_to_local(_vector3(record.get("position", [])))
		var row := {
			"type_name": String(record.get("typeName", "")),
			"role": String(record.get("role", "")),
			"source_index": int(record.get("index", -1)),
			"position": Vector2(local.x, local.z),
			"elevation": local.y,
			"yaw": float(record.get("angle", 0.0)),
			"owner": String(record.get("originalOwner", "")),
			"maximum_health": float(record.get("maxHealth", 0.0)),
			# JSON null armor is the recorded SAGE passthrough (no authored
			# ArmorSet); the sim row carries "" for it.
			"armor": "" if record.get("armor") == null else String(record.get("armor")),
			"indestructible": bool(record.get("indestructible", false)),
		}
		if record.has("initialHealth"):
			row["initial_health"] = float(record.get("initialHealth"))
		castle_fixture_placements.append(row)


var _validation_cancel_check := Callable()


func _validation_cancelled() -> bool:
	if _validation_cancel_check.is_valid() and bool(_validation_cancel_check.call()):
		error = "validation-cancelled"
		return true
	return false


func _load_terrain(terrain: Dictionary, terrain_materials_relative: String) -> bool:
	var height_data := _dictionary(terrain.get("height", {}))
	var height_file := _dictionary(height_data.get("heightmap", {}))
	var passability := _dictionary(terrain.get("passability", {}))
	var blend := _dictionary(terrain.get("blend", {}))
	var grid_stats := _dictionary(blend.get("gridStats", {}))
	width = int(height_data.get("width", 0))
	height = int(height_data.get("height", 0))
	border_width = int(height_data.get("borderWidth", -1))
	horizontal_scale = float(height_data.get("horizontalScale", 0.0))
	vertical_scale = float(height_data.get("verticalScale", 0.0))
	raw_elevation_min = int(height_data.get("rawElevationMin", -1))
	raw_elevation_max = int(height_data.get("rawElevationMax", -1))
	impassable_count = int(grid_stats.get("impassable", -1))
	var blend_textures := _array(blend.get("textures", []))
	terrain_texture_count = blend_textures.size()
	if width <= 1 or height <= 1 or width * height > MAX_TERRAIN_CELLS:
		return _fail("invalid or unbounded cooked heightmap dimensions")
	if terrain_texture_count <= 0 or terrain_texture_count > MAX_TERRAIN_TEXTURES:
		return _fail("invalid or unbounded cooked terrain texture count")
	if not _finite_positive(horizontal_scale) or not _finite_positive(vertical_scale):
		return _fail("invalid cooked terrain scale")
	if String(height_file.get("encoding", "")) != "uint16" or String(height_file.get("endianness", "")) != "little" or String(height_file.get("order", "")) != "row-major-y-then-x":
		return _fail("unsupported cooked heightmap encoding")
	if String(passability.get("meaning", "")) != "one-is-impassable" or String(passability.get("bitOrder", "")) != "least-significant-bit-first" or not bool(passability.get("rowPadding", false)) or not bool(passability.get("sourceExact", false)):
		return _fail("unsupported cooked passability semantics")
	var extent_values := _array(height_data.get("playableWorldExtent", []))
	if border_width <= 0 or border_width * 2 >= mini(width, height) or extent_values.size() != 2:
		return _fail("invalid cooked playable border metadata")
	playable_world_extent = Vector2(float(extent_values[0]), float(extent_values[1]))
	var expected_extent := Vector2(float(width - border_width * 2), float(height - border_width * 2)) * horizontal_scale
	if not _finite_positive(playable_world_extent.x) or not _finite_positive(playable_world_extent.y) or not playable_world_extent.is_equal_approx(expected_extent):
		return _fail("playable world extent does not match the declared border")
	playable_grid_min = Vector2i(border_width, border_width)
	# SAGE's declared extent is measured from the inset sample to this inclusive
	# far sample: 20..395 and 20..333 for this cook.
	playable_grid_max = Vector2i(width - border_width, height - border_width)

	var height_path := _resolve(String(height_file.get("path", "")))
	var passability_path := _resolve(String(passability.get("path", "")))
	if height_path == "" or passability_path == "":
		return _fail("cooked terrain binaries are missing or unsafe")
	height_samples = _read_bytes(height_path, width * height * 2, "heightmap")
	passability_row_stride = int(passability.get("rowStrideBytes", 0))
	passability_bits = _read_bytes(passability_path, passability_row_stride * height, "passability")
	heightmap_bytes = height_samples.size()
	passability_bytes = passability_bits.size()
	if heightmap_bytes != width * height * 2:
		return _fail("heightmap byte count does not match its metadata")
	if passability_row_stride != (width + 7) / 8 or passability_bytes != passability_row_stride * height:
		return _fail("passability byte count does not match its metadata")
	if impassable_count < 0 or impassable_count > width * height:
		return _fail("invalid cooked impassability count")
	computed_raw_elevation_min = 0x7FFFFFFF
	computed_raw_elevation_max = -1
	computed_impassable_count = 0
	for grid_y in range(height):
		for grid_x in range(width):
			var raw_height := height_raw_at(grid_x, grid_y)
			computed_raw_elevation_min = mini(computed_raw_elevation_min, raw_height)
			computed_raw_elevation_max = maxi(computed_raw_elevation_max, raw_height)
			if is_impassable_at(grid_x, grid_y):
				computed_impassable_count += 1
	if computed_raw_elevation_min != raw_elevation_min or computed_raw_elevation_max != raw_elevation_max:
		return _fail("heightmap raw elevation range does not match its metadata")
	if computed_impassable_count != impassable_count:
		return _fail("passability popcount does not match its metadata")
	heightmap_sha256 = _sha256(height_samples)
	passability_sha256 = _sha256(passability_bits)
	if not _load_terrain_material_catalog(terrain_materials_relative, blend, blend_textures):
		return false
	if not _load_terrain_source_layers(terrain, blend):
		return false
	return true


func _load_terrain_material_catalog(relative: String, blend: Dictionary, blend_textures: Array) -> bool:
	if relative == "":
		return _fail("cooked map does not declare terrain materials")
	# Legacy cooks emit the catalog map-relative; newer cooks share one catalog
	# across maps at pack scope. Try the map first, then the pack — both stay
	# confined to the selected pack and every row is digest-validated below.
	var manifest_path := _resolve(relative)
	var manifest: Dictionary = {}
	if manifest_path != "":
		manifest = _read_document(relative, "terrain material")
	else:
		manifest_path = _resolve_pack_asset(relative)
		if manifest_path == "":
			return _fail("terrain material manifest escaped the selected map")
		manifest = _read_pack_document(relative, "terrain material")
	if manifest.is_empty():
		return false
	if String(manifest.get("schema", "")) != "openbfme.sage-terrain-materials" or int(manifest.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked terrain material schema")
	var materials := _array(manifest.get("materials", []))
	var texture_rows := _array(manifest.get("textures", []))
	# Two catalog shapes are supported. A map-scope catalog covers exactly the
	# map's texture table in authored order (Fords men-vslice). A pack-scope
	# shared catalog (five-maps pack) is a superset union across the pack's
	# maps; each map row is then matched by symbol against its own table.
	var exact_ordered := materials.size() == terrain_texture_count and texture_rows.size() == terrain_texture_count
	if exact_ordered:
		if int(manifest.get("symbolCount", -1)) != terrain_texture_count or int(manifest.get("textureCount", -1)) != terrain_texture_count:
			return _fail("terrain material manifest count does not match the terrain table")
		if blend_textures.size() != terrain_texture_count:
			return _fail("terrain material catalog is incomplete")
	else:
		# Two symbols may share one texture file (retail authors HelmsDeepBrick03
		# and ClifFornost02 onto the same bytes), so a deduplicating cook emits
		# fewer texture rows than materials. Older cooks emitted one row per
		# material; both shapes are validated below via the by-png lookup.
		if (
			int(manifest.get("symbolCount", -1)) != materials.size()
			or int(manifest.get("textureCount", -1)) != texture_rows.size()
			or texture_rows.size() < 1
			or texture_rows.size() > materials.size()
			or materials.size() < terrain_texture_count
			or materials.size() > MAX_SHARED_TERRAIN_MATERIALS
			or blend_textures.size() != terrain_texture_count
		):
			return _fail("terrain material catalog is incomplete")
	var symbol_indices: Dictionary = {}
	var texture_rows_by_png: Dictionary = {}
	if not exact_ordered:
		for material_index in range(materials.size()):
			var catalog_row := _dictionary(materials[material_index])
			var catalog_symbol := String(catalog_row.get("symbol", ""))
			if catalog_row.is_empty() or catalog_symbol == "" or String(catalog_row.get("definitionSymbol", "")) != catalog_symbol or int(catalog_row.get("tableIndex", -1)) != material_index or symbol_indices.has(catalog_symbol):
				return _fail("shared terrain material catalog is internally inconsistent")
			symbol_indices[catalog_symbol] = material_index
		for texture_value in texture_rows:
			var candidate_row := _dictionary(texture_value)
			var candidate_png := String(candidate_row.get("png", ""))
			if candidate_row.is_empty() or candidate_png == "":
				return _fail("shared terrain material catalog is internally inconsistent")
			if texture_rows_by_png.has(candidate_png):
				# A per-material cook repeats the row for a shared file; the
				# repeats must agree byte-for-byte or the catalog is lying.
				var existing := _dictionary(texture_rows_by_png[candidate_png])
				if String(existing.get("pngSha256", "")) != String(candidate_row.get("pngSha256", "")) \
						or int(existing.get("width", 0)) != int(candidate_row.get("width", 0)) \
						or int(existing.get("height", 0)) != int(candidate_row.get("height", 0)):
					return _fail("shared terrain material catalog is internally inconsistent")
				continue
			texture_rows_by_png[candidate_png] = candidate_row

	var material_root := manifest_path.get_base_dir()
	var expected_cell_start := 0
	var total_png_bytes := 0
	var seen_symbols: Dictionary = {}
	terrain_texture_array_dimension = 0
	for index in range(terrain_texture_count):
		var blend_row := _dictionary(blend_textures[index])
		var material_row := _dictionary(materials[index])
		var texture_row := _dictionary(texture_rows[index])
		var expected_table_index := index
		if not exact_ordered:
			var shared_index := int(symbol_indices.get(String(blend_row.get("name", "")), -1))
			if shared_index < 0:
				return _fail("shared terrain material catalog does not cover the map texture table")
			material_row = _dictionary(materials[shared_index])
			# Deduplicated texture lists no longer align index-wise with the
			# material table; the material's own png names its texture row, and
			# the rows-disagree check below still cross-validates the pair.
			texture_row = _dictionary(texture_rows_by_png.get(String(material_row.get("png", "")), {}))
			expected_table_index = shared_index
		var symbol := String(material_row.get("symbol", ""))
		var definition_symbol := String(material_row.get("definitionSymbol", ""))
		var blend_symbol := String(blend_row.get("name", ""))
		var table_index := int(material_row.get("tableIndex", -1))
		var cell_start := int(blend_row.get("cellStart", -1))
		var cell_count := int(blend_row.get("cellCount", -1))
		var cell_size := int(blend_row.get("cellSize", -1))
		if symbol == "" or definition_symbol != symbol or blend_symbol != symbol or table_index != expected_table_index or seen_symbols.has(symbol):
			return _fail("terrain material table order or symbol identity is invalid")
		if cell_size <= 0 or cell_size > 64 or cell_count != cell_size * cell_size or cell_start != expected_cell_start:
			return _fail("terrain material cell table is invalid")

		var png_relative := String(material_row.get("png", ""))
		var png_sha256 := String(material_row.get("pngSha256", "")).to_lower()
		var texture_png_relative := String(texture_row.get("png", ""))
		var texture_png_sha256 := String(texture_row.get("pngSha256", "")).to_lower()
		var image_width := int(material_row.get("width", 0))
		var image_height := int(material_row.get("height", 0))
		if png_relative == "" or png_relative.get_extension().to_lower() != "png" or png_sha256.length() != 64:
			return _fail("terrain material PNG declaration is invalid")
		if texture_png_relative != png_relative or texture_png_sha256 != png_sha256 or int(texture_row.get("width", 0)) != image_width or int(texture_row.get("height", 0)) != image_height:
			return _fail("terrain material manifest rows disagree")
		if image_width <= 0 or image_width > MAX_TERRAIN_TEXTURE_DIMENSION or image_height != image_width or image_width != cell_size * 64:
			return _fail("terrain material dimensions do not match the SAGE cell table")
		var png_path := _resolve_from(material_root, png_relative)
		if png_path == "":
			# Pack-scope catalogs keep their textures beside the manifest, above
			# the map directory; confinement to the selected pack still applies.
			png_path = _resolve_from_pack(material_root, png_relative)
		if png_path == "":
			return _fail("terrain material PNG escaped the selected map")
		var png_bytes := _read_bounded_bytes(png_path, MAX_TERRAIN_TEXTURE_BYTES, "terrain material PNG")
		if png_bytes.is_empty():
			return false
		if not _has_png_signature(png_bytes) or _sha256(png_bytes).to_lower() != png_sha256:
			return _fail("terrain material PNG failed signature or digest validation")
		total_png_bytes += png_bytes.size()
		if total_png_bytes > MAX_TERRAIN_TEXTURE_TOTAL_BYTES:
			return _fail("terrain material PNG payload exceeds the bounded runtime budget")

		seen_symbols[symbol] = true
		terrain_texture_array_dimension = maxi(terrain_texture_array_dimension, image_width)
		terrain_material_catalog.append({
			"symbol": symbol,
			"definition_symbol": definition_symbol,
			"source_texture": String(material_row.get("sourceTexture", "")),
			# Entries stay in map-local table order (identical to the shared
			# catalog's global index in exact mode) so consumers can index by
			# the map's own terrain tile table.
			"table_index": index,
			"png_relative": png_relative,
			"png_path": png_path,
			"png_sha256": png_sha256,
			"png_byte_length": png_bytes.size(),
			"width": image_width,
			"height": image_height,
			"cell_start": cell_start,
			"cell_count": cell_count,
			"cell_size": cell_size,
			"uv_divisor": cell_size * 2,
		})
		for _cell in range(cell_count):
			_terrain_cell_texture_indices.append(index)
		expected_cell_start += cell_count

	if expected_cell_start != int(blend.get("textureCellCount", -1)) or _terrain_cell_texture_indices.size() != expected_cell_start:
		return _fail("terrain material cell coverage does not match cooked metadata")
	terrain_texture_width = terrain_texture_array_dimension
	terrain_texture_height = terrain_texture_array_dimension
	return terrain_texture_array_dimension > 0


func _load_terrain_source_layers(terrain: Dictionary, blend: Dictionary) -> bool:
	var source_layers := _dictionary(terrain.get("sourceLayers", {}))
	if String(source_layers.get("schema", "")) != "openbfme.sage-terrain-source-layers" or int(source_layers.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked terrain source-layer schema")
	var cell_count := width * height
	if int(source_layers.get("gridWidth", -1)) != width or int(source_layers.get("gridHeight", -1)) != height or int(source_layers.get("cellCount", -1)) != cell_count:
		return _fail("terrain source-layer grid does not match the heightmap")
	var layer_descriptors := _dictionary(source_layers.get("layers", {}))
	var table_descriptors := _dictionary(source_layers.get("descriptionTables", {}))
	terrain_source_layer_sha256.clear()
	var tile_bytes := _read_terrain_grid_layer(layer_descriptors, "tileIndices", 2, "uint16", "terrain tile indices")
	var blend_bytes := _read_terrain_grid_layer(layer_descriptors, "blendCells", 4, "opaque-uint32", "terrain blend cells")
	var three_way_bytes := _read_terrain_grid_layer(layer_descriptors, "threeWayBlendCells", 4, "opaque-uint32", "terrain three-way blend cells")
	var cliff_bytes := _read_terrain_grid_layer(layer_descriptors, "cliffCells", 4, "opaque-uint32", "terrain cliff cells")
	var blend_description_count := int(blend.get("blendDescriptionCount", -1))
	var cliff_mapping_count := int(blend.get("cliffMappingCount", -1))
	# Maps without authored cliff mappings are valid: their table is empty and
	# every cliff cell reference must stay zero (checked below).
	if blend_description_count <= 0 or blend_description_count > cell_count or cliff_mapping_count < 0 or cliff_mapping_count > cell_count:
		return _fail("terrain description-table counts are invalid")
	var raw_cliff_count := int(blend.get("rawCliffCount", -1))
	var cliff_reserved_entry_valid := (
		raw_cliff_count == cliff_mapping_count + 1
		or (raw_cliff_count == 0 and cliff_mapping_count == 0)
	)
	# Blend descriptions always reserve source entry zero. Official Osgiliath's
	# entirely absent cliff table has neither mappings nor a reserved entry;
	# accept only that exact 0/0 cliff shape in addition to the normal invariant.
	if int(blend.get("rawBlendCount", -1)) != blend_description_count + 1 or not cliff_reserved_entry_valid:
		return _fail("terrain description-table reserved-entry counts are invalid")
	var blend_description_bytes := _read_terrain_description_table(table_descriptors, "blendDescriptions", blend_description_count, BLEND_DESCRIPTION_RECORD_BYTES, "terrain blend descriptions")
	var cliff_mapping_bytes := PackedByteArray()
	if cliff_mapping_count > 0:
		cliff_mapping_bytes = _read_terrain_description_table(table_descriptors, "cliffMappings", cliff_mapping_count, CLIFF_MAPPING_RECORD_BYTES, "terrain cliff mappings")
	if error != "" or tile_bytes.is_empty() or blend_bytes.is_empty() or three_way_bytes.is_empty() or cliff_bytes.is_empty() or blend_description_bytes.is_empty() or (cliff_mapping_count > 0 and cliff_mapping_bytes.is_empty()):
		return false

	terrain_tile_indices.resize(cell_count)
	terrain_tile_escape_count = 0
	var computed_max_tile := -1
	for index in range(cell_count):
		var tile_value := int(tile_bytes.decode_u16(index * 2))
		if _terrain_texture_index_for_tile_value_strict(tile_value) < 0:
			var on_non_rendered_fringe := index % width == width - 1 or index >= (height - 1) * width
			if not on_non_rendered_fringe:
				return _fail("terrain tile index escaped the material cell table on a rendered cell")
			# Preserve source-exact fringe values. Last-row/last-column samples own
			# no rendered quad, so presentation may resolve them through texture zero.
			terrain_tile_escape_count += 1
		terrain_tile_indices[index] = tile_value
		computed_max_tile = maxi(computed_max_tile, tile_value)
	if computed_max_tile != int(blend.get("maxTileValue", -1)):
		return _fail("terrain tile-index maximum does not match cooked metadata")

	for index in range(blend_description_count):
		var offset := index * BLEND_DESCRIPTION_RECORD_BYTES
		var secondary_tile := int(blend_description_bytes.decode_u32(offset))
		var direction := 0
		for direction_index in range(4):
			var direction_byte := int(blend_description_bytes[offset + 4 + direction_index])
			if direction_byte != 0 and direction_byte != 1:
				return _fail("terrain blend direction contains a non-boolean component")
			if direction_byte != 0:
				direction |= 1 << direction_index
		var source_flags := int(blend_description_bytes[offset + 8])
		var two_sided_byte := int(blend_description_bytes[offset + 9])
		var magic_one := int(blend_description_bytes.decode_u32(offset + 10))
		var magic_two := int(blend_description_bytes.decode_u32(offset + 14))
		var secondary_texture_index := _terrain_texture_index_for_tile_value_strict(secondary_tile)
		if secondary_texture_index < 0 or direction not in [1, 2, 4, 8] or (source_flags & ~3) != 0 or two_sided_byte not in [0, 1]:
			return _fail("terrain blend description contains an invalid source reference or flag")
		if magic_one not in [0xFFFFFFFF, 24] or magic_two != 0x7ADA0000:
			return _fail("terrain blend description magic does not match the supported SAGE record")
		terrain_blend_descriptions.append({
			"source_index": index + 1,
			"secondary_texture_tile": secondary_tile,
			"secondary_texture_index": secondary_texture_index,
			"blend_direction": direction,
			"source_flags": source_flags,
			"flipped": (source_flags & 1) != 0,
			"two_sided": two_sided_byte != 0,
			"magic_one": magic_one,
		})

	for index in range(cliff_mapping_count):
		var offset := index * CLIFF_MAPPING_RECORD_BYTES
		var texture_tile := int(cliff_mapping_bytes.decode_u32(offset))
		var bottom_left := Vector2(cliff_mapping_bytes.decode_float(offset + 4), cliff_mapping_bytes.decode_float(offset + 8))
		var bottom_right := Vector2(cliff_mapping_bytes.decode_float(offset + 12), cliff_mapping_bytes.decode_float(offset + 16))
		var top_right := Vector2(cliff_mapping_bytes.decode_float(offset + 20), cliff_mapping_bytes.decode_float(offset + 24))
		var top_left := Vector2(cliff_mapping_bytes.decode_float(offset + 28), cliff_mapping_bytes.decode_float(offset + 32))
		var texture_index := _terrain_texture_index_for_tile_value_strict(texture_tile)
		if texture_index < 0 or not _finite_vector2(bottom_left) or not _finite_vector2(bottom_right) or not _finite_vector2(top_right) or not _finite_vector2(top_left):
			return _fail("terrain cliff mapping contains an invalid source reference or coordinate")
		terrain_cliff_mappings.append({
			"source_index": index + 1,
			"texture_tile": texture_tile,
			"texture_index": texture_index,
			"bottom_left": bottom_left,
			"bottom_right": bottom_right,
			"top_right": top_right,
			"top_left": top_left,
			"unknown": int(cliff_mapping_bytes.decode_u16(offset + 36)),
		})

	terrain_blend_cells.resize(cell_count)
	terrain_three_way_blend_cells.resize(cell_count)
	terrain_cliff_cells.resize(cell_count)
	terrain_nonzero_blend_cell_count = 0
	terrain_nonzero_three_way_blend_cell_count = 0
	terrain_nonzero_cliff_cell_count = 0
	for index in range(cell_count):
		var blend_index := int(blend_bytes.decode_u32(index * 4))
		var three_way_index := int(three_way_bytes.decode_u32(index * 4))
		var cliff_index := int(cliff_bytes.decode_u32(index * 4))
		if blend_index < 0 or blend_index > blend_description_count or three_way_index < 0 or three_way_index > blend_description_count or cliff_index < 0 or cliff_index > cliff_mapping_count:
			return _fail("terrain cell references an out-of-range description")
		terrain_blend_cells[index] = blend_index
		terrain_three_way_blend_cells[index] = three_way_index
		terrain_cliff_cells[index] = cliff_index
		if blend_index != 0:
			terrain_nonzero_blend_cell_count += 1
		if three_way_index != 0:
			terrain_nonzero_three_way_blend_cell_count += 1
		if cliff_index != 0:
			terrain_nonzero_cliff_cell_count += 1
	if terrain_nonzero_blend_cell_count != int(blend.get("nonzeroBlendCells", -1)) or terrain_nonzero_three_way_blend_cell_count != int(blend.get("nonzeroThreeWayBlendCells", -1)) or terrain_nonzero_cliff_cell_count != int(blend.get("nonzeroCliffCells", -1)):
		return _fail("terrain source-layer nonzero counts do not match cooked metadata")
	return true


func _read_terrain_grid_layer(descriptors: Dictionary, key: String, cell_size: int, encoding: String, label: String) -> PackedByteArray:
	var descriptor := _dictionary(descriptors.get(key, {}))
	var expected_size := width * height * cell_size
	if int(descriptor.get("cellCount", -1)) != width * height or int(descriptor.get("cellSizeBytes", -1)) != cell_size or int(descriptor.get("byteLength", -1)) != expected_size:
		_fail("%s descriptor size does not match the terrain grid" % label)
		return PackedByteArray()
	if String(descriptor.get("encoding", "")) != encoding or String(descriptor.get("endianness", "")) != "little" or String(descriptor.get("order", "")) != "row-major-y-then-x" or not bool(descriptor.get("sourceExact", false)):
		_fail("%s descriptor encoding is unsupported" % label)
		return PackedByteArray()
	return _read_verified_terrain_bytes(descriptor, expected_size, key, label)


func _read_terrain_description_table(descriptors: Dictionary, key: String, record_count: int, record_size: int, label: String) -> PackedByteArray:
	var descriptor := _dictionary(descriptors.get(key, {}))
	var expected_size := record_count * record_size
	if int(descriptor.get("recordCount", -1)) != record_count or int(descriptor.get("recordSizeBytes", -1)) != record_size or int(descriptor.get("byteLength", -1)) != expected_size:
		_fail("%s descriptor size does not match its record table" % label)
		return PackedByteArray()
	if int(descriptor.get("sourceCount", -1)) != record_count + 1 or String(descriptor.get("encoding", "")) != "opaque-fixed-records" or not bool(descriptor.get("reservedZeroEntryExcluded", false)) or not bool(descriptor.get("sourceExact", false)):
		_fail("%s descriptor semantics are unsupported" % label)
		return PackedByteArray()
	return _read_verified_terrain_bytes(descriptor, expected_size, key, label)


func _read_verified_terrain_bytes(descriptor: Dictionary, expected_size: int, key: String, label: String) -> PackedByteArray:
	var declared_sha256 := String(descriptor.get("sha256", "")).to_lower()
	var path := _resolve(String(descriptor.get("path", "")))
	if path == "" or declared_sha256.length() != 64:
		_fail("%s path or digest is invalid" % label)
		return PackedByteArray()
	var bytes := _read_bytes(path, expected_size, label)
	if bytes.is_empty():
		return PackedByteArray()
	var actual_sha256 := _sha256(bytes).to_lower()
	if actual_sha256 != declared_sha256:
		_fail("%s digest does not match its descriptor" % label)
		return PackedByteArray()
	terrain_source_layer_sha256[key] = actual_sha256
	return bytes


func _load_waypoints(document: Dictionary) -> bool:
	var waypoints := _array(document.get("waypoints", []))
	var starts := _dictionary(document.get("playerStarts", {}))
	waypoint_count = waypoints.size()
	player_start_count = starts.size()
	if waypoint_count != int(document.get("count", -1)) or waypoint_count > MAX_WAYPOINTS:
		return _fail("cooked waypoint count does not match its metadata")
	local_named_waypoints.clear()
	for row_value in waypoints:
		var row := _dictionary(row_value)
		var position := _vector3(row.get("godotPosition", []))
		if row.is_empty() or position == Vector3.INF:
			return _fail("invalid cooked waypoint placement")
		var waypoint_name := String(row.get("name", ""))
		if waypoint_name != "":
			var local := source_to_local(position)
			local_named_waypoints[waypoint_name] = Vector2(local.x, local.z)
	for required_name in ["Player_1_Start", "Player_2_Start"]:
		var row := _dictionary(starts.get(required_name, {}))
		var position := _vector3(row.get("godotPosition", []))
		if row.is_empty() or position == Vector3.INF:
			return _fail("missing or invalid %s" % required_name)
		player_starts[required_name] = position
	# Retail maps author one start per supported player; the slice seeds a
	# two-player match from the first two starts, so extra authored starts are
	# valid source data (Rivendell 3, Mount Doom 4, Dagorlad 6, Mordor 8). Every
	# additional authored Player_N_Start is loaded so N-team matches can seed a
	# team per start; two-team matches ignore the extras (byte-identical config).
	for extra_index in range(3, MAX_WAYPOINTS + 1):
		var extra_name := "Player_%d_Start" % extra_index
		if not starts.has(extra_name):
			break
		var extra_row := _dictionary(starts.get(extra_name, {}))
		var extra_position := _vector3(extra_row.get("godotPosition", []))
		if extra_row.is_empty() or extra_position == Vector3.INF:
			return _fail("missing or invalid %s" % extra_name)
		player_starts[extra_name] = extra_position
	if player_start_count < 2:
		return _fail("cooked map declares fewer than two player starts")

	var player_one: Vector3 = player_starts["Player_1_Start"]
	var player_two: Vector3 = player_starts["Player_2_Start"]
	var one_horizontal := Vector2(player_one.x, player_one.z)
	var two_horizontal := Vector2(player_two.x, player_two.z)
	var separation := one_horizontal.distance_to(two_horizontal)
	if separation <= 1.0 or not _finite_positive(separation):
		return _fail("player starts cannot define a local battlefield transform")
	local_transform_origin = (one_horizontal + two_horizontal) * 0.5
	local_axis_x = (one_horizontal - two_horizontal).normalized()
	local_axis_z = Vector2(-local_axis_x.y, local_axis_x.x)
	local_transform_scale = LOCAL_START_SEPARATION / separation
	reference_elevation = (player_one.y + player_two.y) * 0.5
	local_player_starts["Player_1_Start"] = source_to_local(player_one)
	local_player_starts["Player_2_Start"] = source_to_local(player_two)
	# Transform every additional authored start into local space too, so N-team
	# matches can read each seat's anchor. Two-team matches never read these.
	for start_name in player_starts.keys():
		if start_name == "Player_1_Start" or start_name == "Player_2_Start":
			continue
		local_player_starts[start_name] = source_to_local(player_starts[start_name])
	return true


func _load_water(document: Dictionary) -> bool:
	var standing := _array(document.get("standingAreas", []))
	var rivers := _array(document.get("rivers", []))
	standing_water_count = standing.size()
	river_count = rivers.size()
	if standing_water_count > 128 or river_count > 128:
		return _fail("unbounded cooked water collection")
	var total_vertices := 0
	for area_value in standing:
		var area := _dictionary(area_value)
		var points := _array(area.get("godotPoints", []))
		if points.size() < 3 or points.size() > 256:
			return _fail("invalid standing-water polygon")
		var local_points := PackedVector3Array()
		for value in points:
			var source_point := _vector3(value)
			if source_point == Vector3.INF:
				return _fail("invalid standing-water vertex")
			local_points.append(source_to_local(source_point))
		standing_water_polygons.append(local_points)
		total_vertices += local_points.size()

	for river_value in rivers:
		var river := _dictionary(river_value)
		var sections := _array(river.get("crossSections", []))
		if sections.size() < 2 or sections.size() > 512:
			return _fail("invalid cooked river strip")
		var local_sections: Array = []
		for section_value in sections:
			var section := _dictionary(section_value)
			var edge_a := _vector3(section.get("godotV0", []))
			var edge_b := _vector3(section.get("godotV1", []))
			if edge_a == Vector3.INF or edge_b == Vector3.INF:
				return _fail("invalid cooked river cross-section")
			local_sections.append(PackedVector3Array([source_to_local(edge_a), source_to_local(edge_b)]))
		total_vertices += local_sections.size() * 2
		river_strips.append({
			"name": String(river.get("name", "")),
			"id": int(river.get("id", -1)),
			"sections": local_sections,
		})
	if total_vertices > MAX_WATER_VERTICES:
		return _fail("unbounded cooked water geometry")
	if not _derive_ford_gates():
		return false
	return true


func _derive_ford_gates() -> bool:
	ford_gates.clear()
	# Only maps whose runtime profile declares ford crossings (Fords of Isen II)
	# must have them; for every other map the absence of ford-named rivers is
	# ordinary source data, not a contract violation.
	var required_names: Array = _map_runtime_profile.get("required_ford_names", [])
	for ford_name_value in required_names:
		var ford_name := String(ford_name_value)
		var matching_river: Dictionary = {}
		for river in river_strips:
			if String(river.get("name", "")) == ford_name:
				matching_river = river
				break
		if matching_river.is_empty():
			return _fail("missing cooked %s water geometry" % ford_name)
		var sections: Array = matching_river["sections"]
		var middle_index := sections.size() / 2
		var middle: PackedVector3Array = sections[middle_index]
		var center := Vector3.ZERO
		var sample_count := 0
		for section_value in sections:
			var section: PackedVector3Array = section_value
			center += section[0] + section[1]
			sample_count += 2
		center /= float(sample_count)
		ford_gates.append({
			"name": ford_name,
			"source_river_id": int(matching_river.get("id", -1)),
			"source_section_index": middle_index,
			"edge_a": Vector2(middle[0].x, middle[0].z),
			"edge_b": Vector2(middle[1].x, middle[1].z),
			"center": Vector2(center.x, center.z),
			"elevation": (middle[0].y + middle[1].y) * 0.5,
		})
	return ford_gates.size() == required_names.size()


func _load_objects(document: Dictionary, road_document: Dictionary, binding_document: Dictionary, declared_road_summary: Dictionary) -> bool:
	var objects := _array(document.get("objects", []))
	object_count = objects.size()
	if object_count != int(document.get("count", -1)) or object_count > MAX_MAP_OBJECTS:
		return _fail("cooked map object count does not match its metadata")
	_object_type_by_index.clear()
	_object_type_by_index.resize(object_count)
	# Road wire objects are a separate layer only when the map declares a cooked
	# roads document. Without one (the five-maps cook does not convert roads),
	# every placement flows through object binding like any other source object.
	var roads_declared := not road_document.is_empty()
	var normalized_objects: Array[Dictionary] = []
	var source_roads: Dictionary = {}
	var source_type_counts: Dictionary = {}
	var source_indices: Dictionary = {}
	for object_value in objects:
		var object := _dictionary(object_value)
		var type_name := String(object.get("typeName", ""))
		var source_index := int(object.get("index", -1))
		var source_position := _vector3(object.get("godotPosition", []))
		var sage_position := _vector3(object.get("sagePosition", []))
		var yaw := float(object.get("godotYawRadians", NAN))
		var road_type_value: Variant = object.get("roadType", null)
		# Lane L2b: the authored properties bag (originalOwner, objectEnabled,
		# objectIndestructible, objectInitialHealth, objectTargetable, …) is on
		# disk in every cooked objects.json; it was silently dropped here. Keep
		# it, bounded to exactly what the SAGE property parser can emit.
		var properties_value: Variant = object.get("properties", {})
		if typeof(properties_value) != TYPE_DICTIONARY:
			return _fail("invalid cooked object placement properties")
		var properties: Dictionary = properties_value
		if properties.size() > MAX_OBJECT_PROPERTIES:
			return _fail("cooked object placement properties exceed their bound")
		for property_name in properties:
			if typeof(property_name) != TYPE_STRING or String(property_name).is_empty() or String(property_name).length() > MAX_OBJECT_PROPERTY_TEXT or not _safe_object_property_value(properties[property_name]):
				return _fail("invalid cooked object placement properties")
		if object.is_empty() or type_name == "" or type_name.length() > 128 or source_index < 0 or source_index >= object_count or source_indices.has(source_index) or source_position == Vector3.INF or sage_position == Vector3.INF or not _finite_number(yaw) or not _exact_integer(road_type_value):
			return _fail("invalid cooked object placement")
		var road_type := int(road_type_value)
		if road_type < 0 or road_type > 0xFFFFFFFF:
			return _fail("invalid cooked object road wire type")
		source_indices[source_index] = true
		if roads_declared and road_type != 0:
			source_roads[source_index] = {
				"road_id": type_name,
				"wire_type": road_type,
				"sage_position": sage_position,
				"godot_position": source_position,
			}
			continue
		source_type_counts[type_name] = int(source_type_counts.get(type_name, 0)) + 1
		normalized_objects.append({
			"source_type": type_name,
			"source_index": source_index,
			"source_position": source_position,
			"position": source_to_local(source_position),
			"source_yaw": yaw,
			"yaw": yaw,
			"scale": Vector3.ONE,
			"properties": properties,
		})
		_object_type_by_index[source_index] = type_name
	nonroad_object_count = normalized_objects.size()
	if roads_declared:
		if not _load_roads(road_document, source_roads, declared_road_summary):
			return false
	elif not declared_road_summary.is_empty():
		return _fail("cooked map declares a road summary without a roads document")
	if not _load_object_bindings(binding_document, source_type_counts):
		return false
	return _route_normalized_object_placements(normalized_objects)


func _route_normalized_object_placements(normalized_objects: Array[Dictionary]) -> bool:
	generic_prop_placements.clear()
	bound_prop_placements.clear()
	bound_structure_placements.clear()
	creep_lair_placements.clear()
	var vegetation: Array[Dictionary] = []
	var rocks: Array[Dictionary] = []
	var observed_bound_props := 0
	var observed_bound_structures := 0
	var observed_logical := 0
	var observed_unresolved := 0
	for placement_value in normalized_objects:
		var placement: Dictionary = placement_value
		var type_name := String(placement["source_type"])
		var binding: Dictionary = _object_binding_by_type.get(type_name, {})
		var status := String(binding.get("status", ""))
		if CREEP_LAIR_TYPE_NAMES.has(type_name):
			# Sim-facing lair record, independent of the visual binding route
			# below (bound lairs also keep their lifecycle-structure placement).
			var lair_local := Vector3(placement["position"])
			creep_lair_placements.append({
				"type_name": type_name,
				"source_index": int(placement["source_index"]),
				"position": Vector2(lair_local.x, lair_local.z),
				"yaw": float(placement["yaw"]),
				"binding_status": status if status != "" else "unresolved",
			})
		if status == "bound":
			placement["binding_status"] = "bound"
			placement["classification"] = String(binding.get("classification", ""))
			placement["glb_relative"] = String(binding.get("glb_relative", ""))
			placement["glb_path"] = String(binding.get("glb_path", ""))
			if String(binding.get("classification", "")) == "lifecycle-structure":
				placement["source_virtual_model"] = String(binding.get("source_virtual_model", ""))
				placement["object_id"] = String(binding.get("object_id", ""))
				bound_structure_placements.append(placement)
				observed_bound_structures += 1
			else:
				bound_prop_placements.append(placement)
				observed_bound_props += 1
			continue
		if status == "logical":
			observed_logical += 1
			continue
		if status != "unresolved":
			return _fail("cooked object placement has no explicit binding status")
		observed_unresolved += 1
		var lowered := type_name.to_lower()
		var kind := ""
		if lowered.contains("tree") or lowered.contains("shrub") or lowered.contains("bush"):
			kind = "vegetation"
		elif lowered.contains("rock") or lowered.contains("boulder"):
			kind = "rock"
		if kind == "":
			continue
		placement["kind"] = kind
		placement["binding_status"] = "unresolved"
		if kind == "vegetation":
			vegetation.append(placement)
		else:
			rocks.append(placement)
	if (
		observed_bound_props != bound_prop_placement_count
		or observed_bound_structures != bound_structure_placement_count
		or observed_logical != logical_prop_placement_count
		or observed_unresolved != unresolved_prop_placement_count
	):
		return _fail("object-binding placement totals disagree with cooked objects")
	generic_prop_placements.append_array(_even_sample(vegetation, 48))
	generic_prop_placements.append_array(_even_sample(rocks, 24))
	if generic_prop_placements.size() > MAX_GENERIC_PROPS:
		return _fail("unresolved source-placement preview exceeded its bound")
	for marker in generic_prop_placements:
		if (
			String(marker.get("binding_status", "")) != "unresolved"
			or bound_prop_type_ids.has(String(marker.get("source_type", "")))
			or bound_structure_type_ids.has(String(marker.get("source_type", "")))
		):
			return _fail("bound retail placement leaked into unresolved markers")
	return true


func _load_roads(document: Dictionary, source_roads: Dictionary, declared_summary: Dictionary = {}) -> bool:
	road_type_count = 0
	road_control_point_count = 0
	road_segment_count = 0
	road_unresolved_control_point_count = 0
	road_unique_endpoint_count = 0
	road_shared_node_count = 0
	road_curve_candidate_node_count = 0
	road_crossing_candidate_node_count = 0
	road_modifier_flags_or = 0
	road_type_ids.clear()
	road_control_points.clear()
	road_segments.clear()
	if (
		String(document.get("schema", "")) != "openbfme.sage-roads"
		or int(document.get("schemaVersion", -1)) != 0
		or String(document.get("coordinateTransform", "")) != "godot=(sage.x,sage.z,-sage.y)"
		or String(document.get("pairingPolicy", "")) != "source-order-exact-wire-2-then-4-same-road-id"
		or String(document.get("curveReconstruction", "")) != "not-attempted"
	):
		return _fail("invalid cooked roads contract")
	if (
		typeof(document.get("roadIds", null)) != TYPE_ARRAY
		or typeof(document.get("controlPoints", null)) != TYPE_ARRAY
		or typeof(document.get("segments", null)) != TYPE_ARRAY
		or typeof(document.get("unresolvedDiagnostics", null)) != TYPE_ARRAY
		or typeof(document.get("summary", null)) != TYPE_DICTIONARY
	):
		return _fail("invalid cooked roads inventory")
	var raw_road_ids := _array(document.get("roadIds", []))
	var control_points := _array(document.get("controlPoints", []))
	var segments := _array(document.get("segments", []))
	var diagnostics := _array(document.get("unresolvedDiagnostics", []))
	var summary := _dictionary(document.get("summary", {}))
	if declared_summary.is_empty() or declared_summary != summary:
		return _fail("map road summary disagrees with its roads document")
	if control_points.size() > MAX_MAP_OBJECTS or segments.size() > MAX_MAP_OBJECTS or diagnostics.size() > MAX_MAP_OBJECTS:
		return _fail("cooked roads inventory exceeds its runtime bound")
	if control_points.size() != source_roads.size():
		return _fail("cooked roads do not exactly cover source road control points")

	var parsed_road_ids: Array[String] = []
	for road_id_value in raw_road_ids:
		if typeof(road_id_value) != TYPE_STRING:
			return _fail("invalid cooked road ID")
		var road_id := String(road_id_value)
		if road_id == "" or road_id.length() > 128 or parsed_road_ids.has(road_id):
			return _fail("invalid or duplicate cooked road ID")
		parsed_road_ids.append(road_id)
	var sorted_road_ids: Array[String] = parsed_road_ids.duplicate()
	sorted_road_ids.sort()
	if parsed_road_ids != sorted_road_ids:
		return _fail("cooked road IDs are not deterministic")

	var observed_road_ids: Dictionary = {}
	var observed_source_indices: Dictionary = {}
	var previous_source_index := -1
	var normalized_points: Array[Dictionary] = []
	var observed_unresolved := 0
	for sequence in range(control_points.size()):
		var point := _dictionary(control_points[sequence])
		var sequence_value: Variant = point.get("sequence", null)
		var source_index_value: Variant = point.get("sourceIndex", null)
		var wire_type_value: Variant = point.get("wireType", null)
		var segment_index_value: Variant = point.get("segmentIndex", null)
		var road_id := String(point.get("roadId", ""))
		var sage_position := _vector3(point.get("sagePosition", []))
		var godot_position := _vector3(point.get("godotPosition", []))
		if point.is_empty() or not _exact_integer(sequence_value) or int(sequence_value) != sequence or not _exact_integer(source_index_value) or not _exact_integer(wire_type_value) or road_id == "" or not parsed_road_ids.has(road_id) or sage_position == Vector3.INF or godot_position == Vector3.INF:
			return _fail("invalid cooked road control point")
		var source_index := int(source_index_value)
		var wire_type := int(wire_type_value)
		if source_index <= previous_source_index or observed_source_indices.has(source_index) or not source_roads.has(source_index):
			return _fail("road control points do not preserve unique source order")
		var source: Dictionary = source_roads[source_index]
		if road_id != String(source.get("road_id", "")) or wire_type != int(source.get("wire_type", -1)) or sage_position != Vector3(source.get("sage_position", Vector3.INF)) or godot_position != Vector3(source.get("godot_position", Vector3.INF)):
			return _fail("road control point disagrees with its exact source object")
		var expected_role := "segment-start" if wire_type == 2 else ("segment-end" if wire_type == 4 else "unresolved")
		if String(point.get("role", "")) != expected_role:
			return _fail("road control point role disagrees with its wire type")
		var status := String(point.get("status", ""))
		if status == "unresolved":
			if typeof(segment_index_value) != TYPE_NIL:
				return _fail("unresolved road control point claims a segment")
			observed_unresolved += 1
		elif status == "paired":
			if not _exact_integer(segment_index_value) or int(segment_index_value) < 0:
				return _fail("paired road control point lacks a segment index")
		else:
			return _fail("road control point has an unknown status")
		previous_source_index = source_index
		observed_source_indices[source_index] = true
		observed_road_ids[road_id] = true
		normalized_points.append({
			"sequence": sequence,
			"source_index": source_index,
			"road_id": road_id,
			"wire_type": wire_type,
			"role": expected_role,
			"segment_index": int(segment_index_value) if _exact_integer(segment_index_value) else -1,
			"sage_position": sage_position,
			"godot_position": godot_position,
		})

	var observed_ids: Array[String] = []
	for road_id_value in observed_road_ids.keys():
		observed_ids.append(String(road_id_value))
	observed_ids.sort()
	if observed_ids != parsed_road_ids:
		return _fail("road ID inventory disagrees with its control points")
	var summary_status := String(summary.get("status", ""))
	var expected_status := "empty" if control_points.is_empty() else ("exact-paired-control-points" if observed_unresolved == 0 else "unresolved-control-points")
	if (
		summary_status != expected_status
		or int(summary.get("roadIdCount", -1)) != parsed_road_ids.size()
		or int(summary.get("controlPointCount", -1)) != control_points.size()
		or int(summary.get("pairedControlPointCount", -1)) != control_points.size() - observed_unresolved
		or int(summary.get("unresolvedControlPointCount", -1)) != observed_unresolved
		or int(summary.get("segmentCount", -1)) != segments.size()
		or int(summary.get("unresolvedDiagnosticCount", -1)) != diagnostics.size()
	):
		return _fail("cooked road summary disagrees with its exact records")
	if diagnostics.size() != observed_unresolved:
		return _fail("unresolved road diagnostics do not cover unresolved control points")
	if observed_unresolved != 0 or control_points.size() % 2 != 0 or segments.size() * 2 != control_points.size():
		# Incomplete cook (unpaired wires). Drop road presentation rather than
		# refusing the entire map — match still boots; roads stay absent.
		road_type_ids.clear()
		road_control_points.clear()
		road_segments.clear()
		road_type_count = 0
		road_control_point_count = 0
		road_segment_count = 0
		road_unresolved_control_point_count = observed_unresolved
		roads_path = ""
		road_materials_path = ""
		return true
	if not diagnostics.is_empty():
		return _fail("cooked roads are not exact 2-to-4 control-point pairs")

	for segment_index in range(segments.size()):
		var start: Dictionary = normalized_points[segment_index * 2]
		var finish: Dictionary = normalized_points[segment_index * 2 + 1]
		if int(start["wire_type"]) != 2 or int(finish["wire_type"]) != 4 or String(start["road_id"]) != String(finish["road_id"]) or int(start["segment_index"]) != segment_index or int(finish["segment_index"]) != segment_index:
			return _fail("cooked roads contain an unpaired or mismatched control-point sequence")
		var segment := _dictionary(segments[segment_index])
		var sage_start := _vector3(segment.get("sageStart", []))
		var sage_end := _vector3(segment.get("sageEnd", []))
		var godot_start := _vector3(segment.get("godotStart", []))
		var godot_end := _vector3(segment.get("godotEnd", []))
		if (
			int(segment.get("index", -1)) != segment_index
			or String(segment.get("roadId", "")) != String(start["road_id"])
			or int(segment.get("startSourceIndex", -1)) != int(start["source_index"])
			or int(segment.get("endSourceIndex", -1)) != int(finish["source_index"])
			or sage_start != Vector3(start["sage_position"])
			or sage_end != Vector3(finish["sage_position"])
			or godot_start != Vector3(start["godot_position"])
			or godot_end != Vector3(finish["godot_position"])
		):
			return _fail("cooked road segment does not preserve its exact source endpoints")
		road_segments.append({
			"index": segment_index,
			"road_id": String(start["road_id"]),
			"start_source_index": int(start["source_index"]),
			"end_source_index": int(finish["source_index"]),
			"sage_start": sage_start,
			"sage_end": sage_end,
			"godot_start": godot_start,
			"godot_end": godot_end,
			"start_wire_type": int(start["wire_type"]),
			"end_wire_type": int(finish["wire_type"]),
		})
		road_modifier_flags_or |= int(start["wire_type"]) & ~6
		road_modifier_flags_or |= int(finish["wire_type"]) & ~6

	road_type_ids.assign(parsed_road_ids)
	road_control_points.assign(normalized_points)
	road_type_count = road_type_ids.size()
	road_control_point_count = road_control_points.size()
	road_segment_count = road_segments.size()
	road_unresolved_control_point_count = observed_unresolved
	var endpoint_roads: Dictionary = {}
	for segment in road_segments:
		for endpoint_key in ["godot_start", "godot_end"]:
			var endpoint := Vector3(segment.get(endpoint_key, Vector3.INF))
			if not endpoint_roads.has(endpoint):
				endpoint_roads[endpoint] = []
			var endpoint_ids: Array = endpoint_roads[endpoint]
			endpoint_ids.append(String(segment.get("road_id", "")))
	road_unique_endpoint_count = endpoint_roads.size()
	for endpoint_ids_value in endpoint_roads.values():
		var endpoint_ids: Array = endpoint_ids_value
		if endpoint_ids.size() > 1:
			road_shared_node_count += 1
		var per_id: Dictionary = {}
		for road_id_value in endpoint_ids:
			var road_id := String(road_id_value)
			per_id[road_id] = int(per_id.get(road_id, 0)) + 1
		for same_type_count_value in per_id.values():
			var same_type_count := int(same_type_count_value)
			if same_type_count == 2:
				road_curve_candidate_node_count += 1
			elif same_type_count == 3 or same_type_count == 4:
				road_crossing_candidate_node_count += 1
			elif same_type_count > 4:
				return _fail("cooked road topology exceeds supported crossing degree")
	return true


func _load_road_materials(document: Dictionary) -> bool:
	road_material_count = 0
	road_material_catalog.clear()
	road_source_report_aggregate_sha256 = ""
	if (
		String(document.get("schema", "")) != "openbfme.sage-road-materials"
		or int(document.get("schemaVersion", -1)) != 0
		or typeof(document.get("roads", null)) != TYPE_ARRAY
	):
		return _fail("invalid cooked road-material contract")
	var rows := _array(document.get("roads", []))
	if rows.is_empty() or rows.size() > MAX_ROAD_MATERIALS or int(document.get("roadCount", -1)) != rows.size():
		return _fail("invalid cooked road-material inventory")
	if rows.size() != road_type_count or road_type_ids.size() != road_type_count:
		return _fail("road-material inventory does not cover the exact map road IDs")
	var source_aggregate := String(document.get("sourceReportAggregateSha256", "")).to_lower()
	if not _is_sha256(source_aggregate):
		return _fail("road-material source aggregate is invalid")

	var provenance := _read_pack_document(
		"provenance/manifest.json", "retail provenance", MAX_PROVENANCE_MANIFEST_BYTES
	)
	if provenance.is_empty():
		return false
	if String(provenance.get("contract", "")) != "openbfme.retail-import-provenance-v1" or typeof(provenance.get("bundle_files", null)) != TYPE_ARRAY:
		return _fail("invalid retail provenance contract for road materials")
	var bundle_rows := _array(provenance.get("bundle_files", []))
	if bundle_rows.is_empty() or bundle_rows.size() > MAX_PROVENANCE_BUNDLE_FILES:
		return _fail("invalid or unbounded retail provenance bundle inventory")
	var bundle_by_path: Dictionary = {}
	for bundle_value in bundle_rows:
		var bundle := _dictionary(bundle_value)
		var relative := String(bundle.get("path", "")).replace("\\", "/")
		var digest := String(bundle.get("sha256", "")).to_lower()
		var size_value: Variant = bundle.get("size", null)
		if bundle.is_empty() or not ModLoader.is_safe_relative_path(relative) or bundle_by_path.has(relative) or not _is_sha256(digest) or not _exact_integer(size_value) or int(size_value) < 0:
			return _fail("invalid retail provenance bundle-file record")
		bundle_by_path[relative] = {
			"sha256": digest,
			"size": int(size_value),
		}

	var parsed_ids: Array[String] = []
	var total_png_bytes := 0
	for row_value in rows:
		var row := _dictionary(row_value)
		var road_id := String(row.get("id", ""))
		var road_width := _parse_positive_decimal(row.get("RoadWidth", null), 1_000_000.0)
		var width_in_texture := _parse_positive_decimal(row.get("RoadWidthInTexture", null), 1.0)
		var texture_identifier := String(row.get("textureIdentifier", ""))
		var texture_relative := String(row.get("texturePng", "")).replace("\\", "/")
		var source_relative := String(row.get("sourceVirtualPath", "")).replace("\\", "/")
		var source_digest := String(row.get("sourceSha256", "")).to_lower()
		var source_length_value: Variant = row.get("sourceByteLength", null)
		if (
			row.is_empty()
			or road_id == ""
			or road_id.length() > 128
			or parsed_ids.has(road_id)
			or road_id != String(road_type_ids[parsed_ids.size()])
			or not _finite_positive(road_width)
			or not _finite_positive(width_in_texture)
			or texture_identifier == ""
			or texture_identifier.length() > 256
			or not texture_identifier.to_lower().ends_with(".tga")
			or not ModLoader.is_safe_relative_path(texture_relative)
			or texture_relative.get_extension().to_lower() != "png"
			or not ModLoader.is_safe_relative_path(source_relative)
			or source_relative.get_extension().to_lower() != "dds"
			or not _is_sha256(source_digest)
			or not _exact_integer(source_length_value)
			or int(source_length_value) <= 0
		):
			return _fail("invalid exact road-material record")
		var texture_path := _resolve_pack_asset(texture_relative)
		if texture_path == "" or not ModLoader.path_is_within(map_root, texture_path):
			return _fail("road-material PNG escaped the selected retail map")
		if not bundle_by_path.has(texture_relative):
			return _fail("road-material PNG is absent from retail provenance")
		var bundle: Dictionary = bundle_by_path[texture_relative]
		var png_bytes := _read_bounded_bytes(texture_path, MAX_ROAD_TEXTURE_BYTES, "road-material PNG")
		if png_bytes.is_empty():
			return false
		var png_digest := _sha256(png_bytes).to_lower()
		if not _has_png_signature(png_bytes) or png_bytes.size() != int(bundle.get("size", -1)) or png_digest != String(bundle.get("sha256", "")):
			return _fail("road-material PNG failed provenance signature or digest validation")
		var image := Image.new()
		if image.load_png_from_buffer(png_bytes) != OK or image.is_empty() or image.get_width() <= 0 or image.get_width() > MAX_ROAD_TEXTURE_DIMENSION or image.get_height() <= 0 or image.get_height() > MAX_ROAD_TEXTURE_DIMENSION:
			return _fail("road-material PNG could not be decoded within runtime bounds")
		total_png_bytes += png_bytes.size()
		if total_png_bytes > MAX_ROAD_TEXTURE_TOTAL_BYTES:
			return _fail("road-material PNG payload exceeds the bounded runtime budget")
		parsed_ids.append(road_id)
		road_material_catalog.append({
			"id": road_id,
			"road_width": road_width,
			"road_width_in_texture": width_in_texture,
			"road_width_authored": String(row.get("RoadWidth", "")),
			"road_width_in_texture_authored": String(row.get("RoadWidthInTexture", "")),
			"texture_identifier": texture_identifier,
			"texture_relative": texture_relative,
			"texture_path": texture_path,
			"texture_sha256": png_digest,
			"texture_byte_length": png_bytes.size(),
			"texture_width": image.get_width(),
			"texture_height": image.get_height(),
			"source_virtual_path": source_relative,
			"source_sha256": source_digest,
			"source_byte_length": int(source_length_value),
		})
	if parsed_ids != road_type_ids or road_material_catalog.size() != road_type_count:
		return _fail("road-material IDs do not exactly match the cooked map")
	road_material_count = road_material_catalog.size()
	road_source_report_aggregate_sha256 = source_aggregate
	return true


func _load_object_bindings(document: Dictionary, source_type_counts: Dictionary) -> bool:
	bound_prop_type_ids.clear()
	bound_structure_type_ids.clear()
	logical_prop_type_ids.clear()
	unresolved_prop_type_ids.clear()
	bound_prop_placement_count = 0
	bound_structure_placement_count = 0
	logical_prop_placement_count = 0
	unresolved_prop_placement_count = 0
	object_binding_record_count = 0
	object_binding_resolution_status = ""
	_object_binding_by_type.clear()
	if String(document.get("schema", "")) != "openbfme.sage-object-bindings" or int(document.get("schemaVersion", -1)) != 0 or String(document.get("matchPolicy", "")) != "explicit-exact-type-name-only":
		return _fail("invalid object-binding contract")
	var records := _array(document.get("records", []))
	var summary := _dictionary(document.get("summary", {}))
	if records.is_empty() or records.size() > MAX_OBJECT_BINDING_RECORDS or records.size() != source_type_counts.size() or summary.is_empty():
		return _fail("invalid or incomplete object-binding record table")
	var bound_types := 0
	var logical_types := 0
	var unresolved_types := 0
	var demoted_missing_glb := 0
	for record_value in records:
		var record := _dictionary(record_value)
		var type_name := String(record.get("typeName", ""))
		var status := String(record.get("status", ""))
		var classification := String(record.get("classification", ""))
		var match_method := String(record.get("matchMethod", ""))
		var placement_count := int(record.get("placementCount", -1))
		var glb_value: Variant = record.get("glb", "")
		var glb_relative := String(glb_value) if typeof(glb_value) == TYPE_STRING else ""
		var source_model_value: Variant = record.get("sourceVirtualModel", "")
		var source_virtual_model := String(source_model_value) if typeof(source_model_value) == TYPE_STRING else ""
		var object_id_value: Variant = record.get("objectId", "")
		var object_id := String(object_id_value) if typeof(object_id_value) == TYPE_STRING else ""
		if record.is_empty() or type_name == "" or type_name.length() > 128 or _object_binding_by_type.has(type_name) or not source_type_counts.has(type_name) or placement_count <= 0 or placement_count != int(source_type_counts[type_name]):
			return _fail("object-binding record does not exactly cover its source type")
		var normalized := {
			"type_name": type_name,
			"status": status,
			"classification": classification,
			"match_method": match_method,
			"placement_count": placement_count,
			"glb_relative": "",
			"glb_path": "",
			"source_virtual_model": "",
			"object_id": "",
		}
		match status:
			"bound":
				if match_method != "exact-type-name" or glb_relative == "" or glb_relative.get_extension().to_lower() != "glb":
					return _fail(
						"bound object record lacks an exact renderable GLB"
						if classification == "renderable"
						else "bound lifecycle structure record lacks an exact GLB binding"
					)
				var glb_path := _resolve_pack_asset(glb_relative)
				# Faction packs may ship map bindings whose prop GLBs live only in a
				# maps supplement (or a newer cook). Missing art must not brick the
				# whole skirmish map: demote that type to unresolved so match start
				# still works. Lifecycle structures without a GLB stay fail-closed.
				if glb_path == "" or not _validate_bound_glb(glb_path):
					if classification == "lifecycle-structure":
						return _fail("bound object GLB is missing or invalid")
					status = "unresolved"
					normalized["status"] = "unresolved"
					normalized["classification"] = "unknown"
					normalized["match_method"] = "none"
					unresolved_prop_type_ids.append(type_name)
					unresolved_prop_placement_count += placement_count
					unresolved_types += 1
					demoted_missing_glb += 1
					_object_binding_by_type[type_name] = normalized
					continue
				normalized["glb_relative"] = glb_relative
				normalized["glb_path"] = glb_path
				if classification == "renderable":
					if object_id != "":
						return _fail("renderable object record claims lifecycle structure fields")
					if source_virtual_model != "":
						if not _safe_source_virtual_model(source_virtual_model):
							return _fail("renderable object source model is unsafe")
						normalized["source_virtual_model"] = source_virtual_model
					bound_prop_type_ids.append(type_name)
					bound_prop_placement_count += placement_count
				elif classification == "lifecycle-structure":
					if not _safe_source_virtual_model(source_virtual_model) or not _safe_content_object_id(object_id):
						return _fail("lifecycle structure binding is unsafe or incomplete")
					normalized["source_virtual_model"] = source_virtual_model
					normalized["object_id"] = object_id
					bound_structure_type_ids.append(type_name)
					bound_structure_placement_count += placement_count
				else:
					return _fail("bound object record has an unsupported classification")
				bound_types += 1
			"logical":
				if match_method != "exact-type-name" or glb_relative != "" or classification == "" or classification == "renderable" or classification == "unknown":
					return _fail("logical object record has invalid presentation semantics")
				logical_prop_type_ids.append(type_name)
				logical_prop_placement_count += placement_count
				logical_types += 1
			"unresolved":
				if classification != "unknown" or match_method != "none" or glb_relative != "":
					return _fail("unresolved object record claims a presentation binding")
				unresolved_prop_type_ids.append(type_name)
				unresolved_prop_placement_count += placement_count
				unresolved_types += 1
			_:
				return _fail("object-binding record has an unknown status")
		_object_binding_by_type[type_name] = normalized
	bound_prop_type_ids.sort()
	bound_structure_type_ids.sort()
	logical_prop_type_ids.sort()
	unresolved_prop_type_ids.sort()
	object_binding_record_count = records.size()
	var resolved_types := bound_types + logical_types
	var bound_placement_count := bound_prop_placement_count + bound_structure_placement_count
	var resolved_placements := bound_placement_count + logical_prop_placement_count
	var all_placements := bound_placement_count + logical_prop_placement_count + unresolved_prop_placement_count
	var source_placement_count := 0
	for source_count_value in source_type_counts.values():
		source_placement_count += int(source_count_value)
	var expected_resolution_status := "complete" if unresolved_types == 0 else ("partial" if resolved_types > 0 else "unresolved")
	object_binding_resolution_status = String(summary.get("resolutionStatus", ""))
	# The five-maps cook labels a zero-resolved map "partial" where the runtime
	# formula derives "unresolved"; both labels describe the same, fully
	# count-validated record table, so the label check accepts either for that
	# case. Every count above is still matched exactly.
	var resolution_status_ok := object_binding_resolution_status == expected_resolution_status or (resolved_types == 0 and unresolved_types > 0 and object_binding_resolution_status == "partial")
	# When the runtime demotes missing prop GLBs, the cooked summary still
	# describes the pre-demotion table. Accept the live counters instead.
	if demoted_missing_glb > 0:
		object_binding_resolution_status = expected_resolution_status
		if all_placements != source_placement_count or int(summary.get("typeCount", -1)) != records.size():
			return _fail("object-binding summary disagrees with its exact records")
		return true
	if (
		int(summary.get("typeCount", -1)) != records.size()
		or int(summary.get("placementCount", -1)) != all_placements
		or int(summary.get("boundTypeCount", -1)) != bound_types
		or int(summary.get("boundPlacementCount", -1)) != bound_placement_count
		or int(summary.get("logicalTypeCount", -1)) != logical_types
		or int(summary.get("logicalPlacementCount", -1)) != logical_prop_placement_count
		or int(summary.get("resolvedTypeCount", -1)) != resolved_types
		or int(summary.get("resolvedPlacementCount", -1)) != resolved_placements
		or int(summary.get("unresolvedTypeCount", -1)) != unresolved_types
		or int(summary.get("unresolvedPlacementCount", -1)) != unresolved_prop_placement_count
		or all_placements != source_placement_count
		or not resolution_status_ok
	):
		return _fail("object-binding summary disagrees with its exact records")
	return true


func _safe_source_virtual_model(value: String) -> bool:
	var normalized := value.replace("\\", "/")
	return (
		value == normalized
		and normalized.begins_with("art/w3d/")
		and normalized.get_extension().to_lower() == "w3d"
		and ModLoader.is_safe_relative_path(normalized)
	)


func _safe_content_object_id(value: String) -> bool:
	if value.length() < 3 or value.length() > 128 or not value.begins_with("bfme2.object."):
		return false
	for index in range(value.length()):
		var codepoint := value.unicode_at(index)
		if not (codepoint >= 97 and codepoint <= 122) and not (codepoint >= 48 and codepoint <= 57) and codepoint not in [45, 46]:
			return false
	return not value.contains("..") and not value.ends_with(".") and not value.ends_with("-")


func _resolve_pack_asset(relative: String) -> String:
	## Resolve a pack-relative asset for map object bindings.
	## Prefer the map's own pack first; if the GLB is missing there (common when
	## a faction pack ships map.json+bindings but the prop GLBs live in a maps
	## supplement pack), search every mounted non-res pack root. Still fail
	## closed when no mounted pack contains the file.
	var resolved := ModLoader.resolve_pack_path(pack_root, relative)
	if resolved != "" and ModLoader.path_is_within(pack_root, resolved) and FileAccess.file_exists(resolved):
		return resolved
	# Reuse the already-mounted root set rather than re-scanning every pack on
	# each asset: list_pack_roots() re-reads and re-sorts all packs (and prints)
	# per call, so an asset-heavy map turned this into thousands of full rescans
	# and froze map loading. The mounted set is stable during a load.
	for root_value in ContentDB.pack_roots:
		var root := String(root_value)
		if root == "" or root == pack_root or root.begins_with("res://"):
			continue
		var candidate := ModLoader.resolve_pack_path(root, relative)
		if candidate != "" and ModLoader.path_is_within(root, candidate) and FileAccess.file_exists(candidate):
			return candidate
	return ""


func _validate_bound_glb(path: String) -> bool:
	if path == "" or path.get_extension().to_lower() != "glb" or not FileAccess.file_exists(path) or not _path_is_within_mounted_pack(path):
		return false
	var byte_count := _file_size(path)
	if byte_count < 20 or byte_count > MAX_BOUND_MODEL_BYTES:
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() != byte_count:
		if file != null:
			file.close()
		return false
	var header := file.get_buffer(12)
	file.close()
	return header.size() == 12 and int(header.decode_u32(0)) == 0x46546C67 and int(header.decode_u32(4)) == 2 and int(header.decode_u32(8)) == byte_count


func _path_is_within_mounted_pack(path: String) -> bool:
	if ModLoader.path_is_within(pack_root, path):
		return true
	# Cached mounted set, not a per-call re-scan (see _resolve_pack_asset).
	for root_value in ContentDB.pack_roots:
		var root := String(root_value)
		if root == "" or root == pack_root or root.begins_with("res://"):
			continue
		if ModLoader.path_is_within(root, path):
			return true
	return false


func _build_map_outline() -> void:
	var source_min := grid_to_source_xy(float(playable_grid_min.x), float(playable_grid_min.y))
	var source_max := grid_to_source_xy(float(playable_grid_max.x), float(playable_grid_max.y))
	var source_min_x := source_min.x
	var source_max_x := source_max.x
	var source_min_y := source_min.y
	var source_max_y := source_max.y
	map_outline = PackedVector2Array([
		_horizontal(source_to_local(Vector3(source_min_x, reference_elevation, -source_min_y))),
		_horizontal(source_to_local(Vector3(source_max_x, reference_elevation, -source_min_y))),
		_horizontal(source_to_local(Vector3(source_max_x, reference_elevation, -source_max_y))),
		_horizontal(source_to_local(Vector3(source_min_x, reference_elevation, -source_max_y))),
	])
	var minimum := map_outline[0]
	var maximum := map_outline[0]
	for point in map_outline:
		minimum = Vector2(minf(minimum.x, point.x), minf(minimum.y, point.y))
		maximum = Vector2(maxf(maximum.x, point.x), maxf(maximum.y, point.y))
	local_bounds = Rect2(minimum, maximum - minimum)


func _build_navigation() -> bool:
	navigation_ready = false
	_navigation_grid = null
	# The declared playable border is camera metadata. Retail maps may author
	# player starts just outside it (Rivendell's Player_2 sits two cells below
	# its border), and navigation must still reach those starts; the
	# navigation region is the declared playable rect expanded to cover every
	# stored player start. Fords authors both starts inside the border, so its
	# navigation region is exactly the declared rect.
	navigation_grid_min = playable_grid_min
	navigation_grid_max = playable_grid_max
	for local_start_value in local_player_starts.values():
		var local_start := Vector3(local_start_value)
		var start_cell := local_to_grid_cell(Vector2(local_start.x, local_start.z))
		navigation_grid_min = Vector2i(mini(navigation_grid_min.x, start_cell.x), mini(navigation_grid_min.y, start_cell.y))
		navigation_grid_max = Vector2i(maxi(navigation_grid_max.x, start_cell.x), maxi(navigation_grid_max.y, start_cell.y))
	navigation_grid_min = Vector2i(maxi(0, navigation_grid_min.x), maxi(0, navigation_grid_min.y))
	navigation_grid_max = Vector2i(mini(width - 1, navigation_grid_max.x), mini(height - 1, navigation_grid_max.y))
	# The cooked water mask only enforces navigation on maps whose runtime
	# profile declares it (Fords of Isen II's reviewed crossing rule). Other
	# maps navigate on the source-authored passability grid alone.
	var water_mask_blocks := bool(_map_runtime_profile.get("water_blocks_navigation", false))
	_water_cells.resize(width * height)
	_water_cells.fill(0)
	_ford_corridor_cells.clear()
	for ford_name in REQUIRED_FORD_NAMES:
		var empty_mask := PackedByteArray()
		empty_mask.resize(width * height)
		empty_mask.fill(0)
		_ford_corridor_cells[ford_name] = empty_mask

	for polygon_value in standing_water_polygons:
		if _validation_cancelled():
			return false
		_rasterize_local_polygon(_water_cells, polygon_value as PackedVector3Array)
	for river in river_strips:
		if _validation_cancelled():
			return false
		var sections: Array = river.get("sections", [])
		var river_name := String(river.get("name", ""))
		for section_index in range(sections.size() - 1):
			var first: PackedVector3Array = sections[section_index]
			var second: PackedVector3Array = sections[section_index + 1]
			var polygon := PackedVector3Array([first[0], second[0], second[1], first[1]])
			_rasterize_local_polygon(_water_cells, polygon)
			if _ford_corridor_cells.has(river_name):
				var ford_mask: PackedByteArray = _ford_corridor_cells[river_name]
				_rasterize_local_polygon(ford_mask, polygon)
				_ford_corridor_cells[river_name] = ford_mask

	for ford_name in REQUIRED_FORD_NAMES:
		if _validation_cancelled():
			return false
		var source_mask: PackedByteArray = _ford_corridor_cells[ford_name]
		# Continuous river edges can fall between 10-unit grid samples. A fixed
		# five-cell alignment margin reaches both banks; it only exempts cooked
		# water masking and never clears a source impassability bit.
		var expanded := _dilate_mask(source_mask, FORD_CORRIDOR_DILATION_CELLS)
		_ford_corridor_cells[ford_name] = expanded

	_navigation_grid = AStarGrid2D.new()
	_navigation_grid.region = Rect2i(navigation_grid_min, navigation_grid_max - navigation_grid_min + Vector2i.ONE)
	_navigation_grid.cell_size = Vector2.ONE
	_navigation_grid.diagonal_mode = AStarGrid2D.DIAGONAL_MODE_NEVER
	_navigation_grid.default_compute_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	_navigation_grid.default_estimate_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	_navigation_grid.update()
	navigation_walkable_count = 0
	navigation_water_blocked_count = 0
	navigation_ford_corridor_count = 0
	for grid_y in range(navigation_grid_min.y, navigation_grid_max.y + 1):
		if grid_y % 16 == 0 and _validation_cancelled():
			return false
		for grid_x in range(navigation_grid_min.x, navigation_grid_max.x + 1):
			var cell := Vector2i(grid_x, grid_y)
			var water_blocked := water_mask_blocks and is_water_cell(cell) and not is_ford_corridor_cell(cell)
			var blocked := is_impassable_at(grid_x, grid_y) or water_blocked
			if blocked:
				_navigation_grid.set_point_solid(cell, true)
				if water_blocked:
					navigation_water_blocked_count += 1
			else:
				navigation_walkable_count += 1
			if is_ford_corridor_cell(cell):
				navigation_ford_corridor_count += 1
	navigation_build_count += 1
	# Ford-corridor topology is a per-map contract (Fords of Isen II). Maps
	# without authored ford crossings only require a non-empty walkable grid;
	# their cooked water still blocks cells through the mask above.
	var requires_ford_crossings := not (_map_runtime_profile.get("required_ford_names", []) as Array).is_empty()
	navigation_ready = navigation_walkable_count > 0 and (not requires_ford_crossings or (navigation_water_blocked_count > 0 and navigation_ford_corridor_count > 0))
	if not navigation_ready:
		return _fail("cooked navigation topology could not be built")
	for known_cell_value in _map_runtime_profile.get("known_impassable_cells", []) as Array:
		if is_navigation_walkable(Vector2i(known_cell_value)):
			return _fail("known source-impassable ford cell became walkable")
	return true


func _rasterize_local_polygon(mask: PackedByteArray, polygon: PackedVector3Array) -> void:
	if polygon.size() < 3:
		return
	var grid_polygon := PackedVector2Array()
	var minimum := Vector2(INF, INF)
	var maximum := Vector2(-INF, -INF)
	for point in polygon:
		var grid_point := local_to_grid_float(Vector2(point.x, point.z))
		grid_polygon.append(grid_point)
		minimum = Vector2(minf(minimum.x, grid_point.x), minf(minimum.y, grid_point.y))
		maximum = Vector2(maxf(maximum.x, grid_point.x), maxf(maximum.y, grid_point.y))
	var minimum_cell := Vector2i(
		clampi(floori(minimum.x), navigation_grid_min.x, navigation_grid_max.x),
		clampi(floori(minimum.y), navigation_grid_min.y, navigation_grid_max.y)
	)
	var maximum_cell := Vector2i(
		clampi(ceili(maximum.x), navigation_grid_min.x, navigation_grid_max.x),
		clampi(ceili(maximum.y), navigation_grid_min.y, navigation_grid_max.y)
	)
	for grid_y in range(minimum_cell.y, maximum_cell.y + 1):
		if grid_y % 16 == 0 and _validation_cancelled():
			return
		for grid_x in range(minimum_cell.x, maximum_cell.x + 1):
			if Geometry2D.is_point_in_polygon(Vector2(grid_x, grid_y), grid_polygon):
				mask[grid_y * width + grid_x] = 1


func _dilate_mask(source: PackedByteArray, radius: int) -> PackedByteArray:
	var result := PackedByteArray()
	result.resize(width * height)
	result.fill(0)
	for grid_y in range(navigation_grid_min.y, navigation_grid_max.y + 1):
		if grid_y % 16 == 0 and _validation_cancelled():
			return result
		for grid_x in range(navigation_grid_min.x, navigation_grid_max.x + 1):
			if source[grid_y * width + grid_x] == 0:
				continue
			for expanded_y in range(maxi(navigation_grid_min.y, grid_y - radius), mini(navigation_grid_max.y, grid_y + radius) + 1):
				for expanded_x in range(maxi(navigation_grid_min.x, grid_x - radius), mini(navigation_grid_max.x, grid_x + radius) + 1):
					result[expanded_y * width + expanded_x] = 1
	return result


func query_route(from_local: Vector2, to_local: Vector2) -> Dictionary:
	route_query_count += 1
	if not navigation_ready:
		return {"valid": false, "reason": "navigation-unavailable", "points": [], "cells": []}
	if not is_local_inside_navigation(from_local) or not is_local_inside_navigation(to_local):
		return {"valid": false, "reason": "outside-playable-area", "points": [], "cells": []}
	var start_cell := local_to_grid_cell(from_local)
	var destination_cell := local_to_grid_cell(to_local)
	if not is_navigation_walkable(destination_cell):
		return {"valid": false, "reason": "blocked-destination", "points": [], "cells": []}
	if not is_navigation_walkable(start_cell):
		start_cell = _nearest_walkable_cell(start_cell, 12)
		if start_cell.x < 0:
			return {"valid": false, "reason": "blocked-origin", "points": [], "cells": []}
	var id_path: Array[Vector2i] = _navigation_grid.get_id_path(start_cell, destination_cell, false)
	if id_path.is_empty() or id_path.size() > MAX_ROUTE_CELLS:
		return {"valid": false, "reason": "no-bounded-route", "points": [], "cells": []}
	var points := _compress_route_points(id_path, to_local)
	return {
		"valid": true,
		"reason": "",
		"points": points,
		"cells": id_path,
		"ford_name": _ford_name_for_cells(id_path),
	}


func query_ford_probe(ford_name: String) -> Dictionary:
	if not REQUIRED_FORD_NAMES.has(ford_name):
		return {"valid": false, "reason": "unknown-ford", "points": [], "cells": []}
	for river in river_strips:
		if String(river.get("name", "")) != ford_name:
			continue
		var sections: Array = river.get("sections", [])
		if sections.size() < 2:
			break
		var grid_step_local := horizontal_scale * local_transform_scale
		# Begin at the middle cross-section, then nudge along the source strip to
		# the nearest section whose exact passability topology connects both banks.
		for section_index in _middle_out_indices(sections.size()):
			var section: PackedVector3Array = sections[section_index]
			var edge_a := Vector2(section[0].x, section[0].z)
			var edge_b := Vector2(section[1].x, section[1].z)
			var direction := edge_a.direction_to(edge_b)
			if direction.is_zero_approx():
				continue
			var start := _find_non_water_bank(edge_a, -direction, grid_step_local, 48)
			var finish := _find_non_water_bank(edge_b, direction, grid_step_local, 48)
			if start.x < 0 or finish.x < 0:
				continue
			var result := query_route(grid_to_local_horizontal(start), grid_to_local_horizontal(finish))
			if not bool(result.get("valid", false)) or String(result.get("ford_name", "")) != ford_name or not _route_contains_named_water(result.get("cells", []), ford_name):
				continue
			result["probe_ford_name"] = ford_name
			result["probe_section_index"] = section_index
			result["probe_bank_a"] = start
			result["probe_bank_b"] = finish
			result["probe_edge_a"] = edge_a
			result["probe_edge_b"] = edge_b
			return result
	return {"valid": false, "reason": "ford-probe-unavailable", "points": [], "cells": []}


func _middle_out_indices(size: int) -> Array[int]:
	var result: Array[int] = []
	var middle := (size - 1) / 2
	result.append(middle)
	for distance in range(1, size):
		var before := middle - distance
		var after := middle + distance
		if before >= 0:
			result.append(before)
		if after < size:
			result.append(after)
	return result


func _route_contains_named_water(cells_value: Variant, ford_name: String) -> bool:
	if typeof(cells_value) != TYPE_ARRAY:
		return false
	for cell_value in cells_value as Array:
		var cell := Vector2i(cell_value)
		if is_water_cell(cell) and is_named_ford_corridor_cell(cell, ford_name):
			return true
	return false


func _find_non_water_bank(edge: Vector2, outward: Vector2, grid_step_local: float, maximum_steps: int) -> Vector2i:
	for step in range(1, maximum_steps + 1):
		var candidate := local_to_grid_cell(edge + outward * grid_step_local * float(step))
		if not is_grid_inside_navigation(candidate):
			break
		if is_navigation_walkable(candidate) and not is_water_cell(candidate):
			return candidate
	return Vector2i(-1, -1)


func _compress_route_points(cells: Array[Vector2i], exact_destination: Vector2) -> Array[Vector2]:
	var points: Array[Vector2] = []
	if cells.size() > 1:
		var previous_direction := cells[1] - cells[0]
		for index in range(1, cells.size()):
			var next_direction := previous_direction
			if index + 1 < cells.size():
				next_direction = cells[index + 1] - cells[index]
			if index == cells.size() - 1 or next_direction != previous_direction:
				points.append(grid_to_local_horizontal(cells[index]))
			previous_direction = next_direction
	if points.is_empty() or not points[-1].is_equal_approx(exact_destination):
		points.append(exact_destination)
	return points


func _ford_name_for_cells(cells: Array[Vector2i]) -> String:
	var best_name := ""
	var best_count := 0
	for ford_name in REQUIRED_FORD_NAMES:
		var count := 0
		for cell in cells:
			if is_named_ford_corridor_cell(cell, ford_name):
				count += 1
		if count > best_count:
			best_name = ford_name
			best_count = count
	return best_name


func _nearest_walkable_cell(origin: Vector2i, maximum_radius: int) -> Vector2i:
	if is_navigation_walkable(origin):
		return origin
	for radius in range(1, maximum_radius + 1):
		for offset_y in range(-radius, radius + 1):
			var offset_x := radius - absi(offset_y)
			var candidates: Array[Vector2i] = [Vector2i(origin.x - offset_x, origin.y + offset_y)]
			if offset_x != 0:
				candidates.append(Vector2i(origin.x + offset_x, origin.y + offset_y))
			for candidate in candidates:
				if is_navigation_walkable(candidate):
					return candidate
	return Vector2i(-1, -1)


func is_local_inside_playable(local_position: Vector2) -> bool:
	var grid := local_to_grid_float(local_position)
	return grid.x >= float(playable_grid_min.x) - 0.001 and grid.x <= float(playable_grid_max.x) + 0.001 and grid.y >= float(playable_grid_min.y) - 0.001 and grid.y <= float(playable_grid_max.y) + 0.001


func clamp_local_to_playable(local_position: Vector2) -> Vector2:
	## Project a local-space look-at onto the authored playable quad (the SAGE
	## playable rect / map_outline), not the local AABB of that quad. Interior
	## points are returned unchanged so the round-trip cannot shrink a legal
	## focus. Used by the tactical-camera scroll clamp.
	if not ready or local_transform_scale <= 0.0:
		return local_position
	var source_h := local_to_source_horizontal(local_position)
	var sage_x := source_h.x
	var sage_y := -source_h.y
	var source_min := grid_to_source_xy(float(playable_grid_min.x), float(playable_grid_min.y))
	var source_max := grid_to_source_xy(float(playable_grid_max.x), float(playable_grid_max.y))
	var min_x := minf(source_min.x, source_max.x)
	var max_x := maxf(source_min.x, source_max.x)
	var min_y := minf(source_min.y, source_max.y)
	var max_y := maxf(source_min.y, source_max.y)
	var clamped_x := clampf(sage_x, min_x, max_x)
	var clamped_y := clampf(sage_y, min_y, max_y)
	if is_equal_approx(clamped_x, sage_x) and is_equal_approx(clamped_y, sage_y):
		return local_position
	var local := source_to_local(Vector3(clamped_x, reference_elevation, -clamped_y))
	return Vector2(local.x, local.z)


func is_local_inside_navigation(local_position: Vector2) -> bool:
	## The navigation region extends the declared playable border to cover
	## player starts authored just outside it; routing follows this region.
	var grid := local_to_grid_float(local_position)
	return grid.x >= float(navigation_grid_min.x) - 0.001 and grid.x <= float(navigation_grid_max.x) + 0.001 and grid.y >= float(navigation_grid_min.y) - 0.001 and grid.y <= float(navigation_grid_max.y) + 0.001


## SAGE anchors world XY at the INNER (playable) map corner, so heightmap grid
## index (border_width, border_width) - not (0, 0) - is world (0, 0), and the
## border ring occupies negative world coordinates. Retail:
##   OpenSAGE HeightMap.ConvertWorldCoordinates: p / HorizontalScale + BorderWidth
##   OpenSAGE HeightMap.GetPosition: (grid - BorderWidth) * HorizontalScale
## Every cooked record other than the terrain grid (objects, waypoints, water,
## roads) is already stored in that world frame - the importer resolved each
## object's ground Z through the border-shifted sample in
## importer/openbfme_importer/sage_map.py `_HeightMap.sample_world_z`. Skipping
## the shift here displaced the entire ground surface by border_width cells
## under every placement it was supposed to carry (fortress walls floating over
## the valley on Amon Sul Fortress, border 40 = 400 source units).
func grid_to_source_xy(grid_x: float, grid_y: float) -> Vector2:
	var border := float(border_width)
	return Vector2((grid_x - border) * horizontal_scale, (grid_y - border) * horizontal_scale)


func source_xy_to_grid(source_x: float, source_y: float) -> Vector2:
	if horizontal_scale <= 0.0:
		return Vector2.ZERO
	var border := float(border_width)
	return Vector2(source_x / horizontal_scale + border, source_y / horizontal_scale + border)


func local_to_grid_float(local_position: Vector2) -> Vector2:
	# local_to_source_horizontal returns cooked Godot horizontal space, which is
	# (sage.x, -sage.y); the grid runs along +sage.y.
	var source := local_to_source_horizontal(local_position)
	return source_xy_to_grid(source.x, -source.y)


func local_to_grid_cell(local_position: Vector2) -> Vector2i:
	var grid := local_to_grid_float(local_position)
	return Vector2i(roundi(grid.x), roundi(grid.y))


func grid_to_local_horizontal(cell: Vector2i) -> Vector2:
	var source := grid_to_source_xy(float(cell.x), float(cell.y))
	var local := source_to_local(Vector3(source.x, reference_elevation, -source.y))
	return Vector2(local.x, local.z)


func is_grid_inside_playable(cell: Vector2i) -> bool:
	return cell.x >= playable_grid_min.x and cell.x <= playable_grid_max.x and cell.y >= playable_grid_min.y and cell.y <= playable_grid_max.y


func is_grid_inside_navigation(cell: Vector2i) -> bool:
	return cell.x >= navigation_grid_min.x and cell.x <= navigation_grid_max.x and cell.y >= navigation_grid_min.y and cell.y <= navigation_grid_max.y


func is_navigation_walkable(cell: Vector2i) -> bool:
	return navigation_ready and is_grid_inside_navigation(cell) and not _navigation_grid.is_point_solid(cell)


func is_water_cell(cell: Vector2i) -> bool:
	return is_grid_inside_navigation(cell) and _water_cells[cell.y * width + cell.x] != 0


func is_ford_corridor_cell(cell: Vector2i) -> bool:
	for ford_name in REQUIRED_FORD_NAMES:
		if is_named_ford_corridor_cell(cell, ford_name):
			return true
	return false


func is_named_ford_corridor_cell(cell: Vector2i, ford_name: String) -> bool:
	if not is_grid_inside_navigation(cell) or not _ford_corridor_cells.has(ford_name):
		return false
	var mask: PackedByteArray = _ford_corridor_cells[ford_name]
	return mask[cell.y * width + cell.x] != 0


func source_to_local(source_position: Vector3) -> Vector3:
	var delta := Vector2(source_position.x, source_position.z) - local_transform_origin
	return Vector3(
		delta.dot(local_axis_x) * local_transform_scale,
		(source_position.y - reference_elevation) * local_transform_scale,
		delta.dot(local_axis_z) * local_transform_scale
	)


func local_to_source_horizontal(local_position: Vector2) -> Vector2:
	if local_transform_scale <= 0.0:
		return Vector2.ZERO
	return local_transform_origin + local_axis_x * (local_position.x / local_transform_scale) + local_axis_z * (local_position.y / local_transform_scale)


func terrain_local_at(grid_x: int, grid_y: int) -> Vector3:
	var safe_x := clampi(grid_x, 0, width - 1)
	var safe_y := clampi(grid_y, 0, height - 1)
	var horizontal := grid_to_source_xy(float(safe_x), float(safe_y))
	var source := Vector3(
		horizontal.x,
		float(height_raw_at(safe_x, safe_y)) * vertical_scale,
		-horizontal.y
	)
	return source_to_local(source)


func terrain_tile_index_at(grid_x: int, grid_y: int) -> int:
	var index := _terrain_cell_index(grid_x, grid_y)
	return int(terrain_tile_indices[index]) if index >= 0 and index < terrain_tile_indices.size() else -1


func terrain_base_texture_index_at(grid_x: int, grid_y: int) -> int:
	return terrain_texture_index_for_tile_value(terrain_tile_index_at(grid_x, grid_y))


func terrain_primary_blend_index_at(grid_x: int, grid_y: int) -> int:
	var index := _terrain_cell_index(grid_x, grid_y)
	return int(terrain_blend_cells[index]) if index >= 0 and index < terrain_blend_cells.size() else -1


func terrain_three_way_blend_index_at(grid_x: int, grid_y: int) -> int:
	var index := _terrain_cell_index(grid_x, grid_y)
	return int(terrain_three_way_blend_cells[index]) if index >= 0 and index < terrain_three_way_blend_cells.size() else -1


func terrain_cliff_mapping_index_at(grid_x: int, grid_y: int) -> int:
	var index := _terrain_cell_index(grid_x, grid_y)
	return int(terrain_cliff_cells[index]) if index >= 0 and index < terrain_cliff_cells.size() else -1


func terrain_texture_index_for_tile_value(tile_value: int) -> int:
	var texture_index := _terrain_texture_index_for_tile_value_strict(tile_value)
	if texture_index < 0 and tile_value >= 0:
		# Load policy permits this only for non-quad-owning last-row/last-column
		# base samples. Preserve their source values but render texture zero.
		return 0
	return texture_index


func _terrain_texture_index_for_tile_value_strict(tile_value: int) -> int:
	if tile_value < 0:
		return -1
	var source_cell := tile_value >> 2
	if source_cell < 0 or source_cell >= _terrain_cell_texture_indices.size():
		return -1
	return int(_terrain_cell_texture_indices[source_cell])


func terrain_tile_orientation_at(grid_x: int, grid_y: int) -> int:
	var tile_value := terrain_tile_index_at(grid_x, grid_y)
	return tile_value & 3 if tile_value >= 0 else -1


func _terrain_cell_index(grid_x: int, grid_y: int) -> int:
	if grid_x < 0 or grid_x >= width or grid_y < 0 or grid_y >= height:
		return -1
	return grid_y * width + grid_x


func height_raw_at(grid_x: int, grid_y: int) -> int:
	if grid_x < 0 or grid_x >= width or grid_y < 0 or grid_y >= height:
		return 0
	var offset := (grid_y * width + grid_x) * 2
	if offset + 1 >= height_samples.size():
		return 0
	return int(height_samples[offset]) | (int(height_samples[offset + 1]) << 8)


func is_impassable_at(grid_x: int, grid_y: int) -> bool:
	if grid_x < 0 or grid_x >= width or grid_y < 0 or grid_y >= height:
		return true
	var offset := grid_y * passability_row_stride + grid_x / 8
	if offset < 0 or offset >= passability_bits.size():
		return true
	return (int(passability_bits[offset]) & (1 << (grid_x % 8))) != 0


## Bilinear terrain elevation in SAGE world space, transcribed from the retail
## engine's HeightMap.GetHeight(float, float) (OpenSAGE Terrain/HeightMap.cs).
## This is the exact sample the importer used to resolve every authored object's
## ground Z, so a placement authored flush to the ground (Z offset 0) lands on
## the surface this returns. Nearest-vertex sampling was wrong by up to a full
## cell of relief even once the grid origin was corrected.
func source_ground_elevation(source_horizontal: Vector2) -> float:
	if width <= 0 or height <= 0:
		return 0.0
	# source_horizontal is cooked Godot horizontal space: (sage.x, -sage.y).
	var grid := source_xy_to_grid(source_horizontal.x, -source_horizontal.y)
	var grid_x := clampf(grid.x, 0.0, float(width - 1))
	var grid_y := clampf(grid.y, 0.0, float(height - 1))
	var x0 := int(floor(grid_x))
	var y0 := int(floor(grid_y))
	var x1 := mini(x0 + 1, width - 1)
	var y1 := mini(y0 + 1, height - 1)
	var fx := grid_x - float(x0)
	var fy := grid_y - float(y0)
	var low := float(height_raw_at(x0, y0)) * (1.0 - fx) + float(height_raw_at(x1, y0)) * fx
	var high := float(height_raw_at(x0, y1)) * (1.0 - fx) + float(height_raw_at(x1, y1)) * fx
	return (low * (1.0 - fy) + high * fy) * vertical_scale


func local_ground_height(local_position: Vector2) -> float:
	var elevation := source_ground_elevation(local_to_source_horizontal(local_position))
	return (elevation - reference_elevation) * local_transform_scale


func simulation_configuration() -> Dictionary:
	var player_one: Vector3 = local_player_starts.get("Player_1_Start", Vector3(38.0, 0.0, 0.0))
	var player_two: Vector3 = local_player_starts.get("Player_2_Start", Vector3(-38.0, 0.0, 0.0))
	var spawn_positions := {}
	spawn_positions[1] = _walkable_spawn(Vector2(player_two.x, player_two.z - 4.5))
	spawn_positions[2] = _walkable_spawn(Vector2(player_two.x, player_two.z + 4.5))
	spawn_positions[101] = _walkable_spawn(Vector2(player_one.x, player_one.z - 4.5))
	spawn_positions[102] = _walkable_spawn(Vector2(player_one.x, player_one.z + 4.5))
	var player_one_horizontal := Vector2(player_one.x, player_one.z)
	var player_two_horizontal := Vector2(player_two.x, player_two.z)
	# Team 0 intentionally uses source Player_2, team 1 uses Player_1 (existing
	# spawn mapping). Teams >=2 seat at Player_3, Player_4, ... in order. Every
	# base/rally anchor is snapped to the cooked navigation mask before it enters
	# deterministic simulation. Two-team matches carry no extra seats, so this
	# whole block is empty for them and the 2-team config is byte-identical.
	var team_centers := [player_two_horizontal, player_one_horizontal]
	var home_layout := {
		0: _home_layout_for(player_two_horizontal, player_one_horizontal),
		1: _home_layout_for(player_one_horizontal, player_two_horizontal),
	}
	var team_start_centers := {}
	# Script START_POSITION_IS is authored with 1-based Player_N_Start values,
	# while Player::getMpStartIndex() stores the corresponding zero-based
	# multiplayer start index. Keep that internal value beside each roster team.
	var team_start_indices := {
		0: 1, # default human team owns authored Player_2_Start
		1: 0, # default enemy team owns authored Player_1_Start
	}
	var extra_seat := 3
	while local_player_starts.has("Player_%d_Start" % extra_seat):
		var seat_local: Vector3 = local_player_starts["Player_%d_Start" % extra_seat]
		var seat_horizontal := _walkable_spawn(Vector2(seat_local.x, seat_local.z))
		var team := extra_seat - 1
		team_centers.append(seat_horizontal)
		team_start_centers[team] = seat_horizontal
		team_start_indices[team] = extra_seat - 1
		extra_seat += 1
	# Give each extra seat a base layout aimed at the roster centroid.
	if not team_start_centers.is_empty():
		var centroid := Vector2.ZERO
		for center in team_centers:
			centroid += center
		centroid /= float(team_centers.size())
		for team_key in team_start_centers.keys():
			var seat: Vector2 = team_start_centers[team_key]
			home_layout[team_key] = _home_layout_for(seat, centroid)
	return {
		"source_map_configured": ready,
		"route_provider": self,
		"playable_outline": map_outline.duplicate(),
		"spawn_positions": spawn_positions,
		"player_starts": {
			"Player_1_Start": player_one_horizontal,
			"Player_2_Start": player_two_horizontal,
		},
		"home_layout": home_layout,
		"team_start_centers": team_start_centers,
		"team_start_indices": team_start_indices,
		"script_waypoints": local_named_waypoints.duplicate(true),
		"ford_gates": _simulation_ford_gates(),
		# Authored PlyrCreeps lairs. The simulation only seeds them when its
		# opt-in creep rule is enabled; carrying them here is inert otherwise.
		"creep_lair_placements": creep_lair_placements.duplicate(true),
		# Lane L2b castle fixtures in sim-local space. Inert unless the sim's
		# opt-in "enable_castle_fixtures" rule is enabled; the deferred tally
		# names every admitted-but-unseeded placement by reason.
		"castle_fixture_placements": castle_fixture_placements.duplicate(true),
		"castle_fixture_deferred": castle_fixture_deferred.duplicate(true),
	}


func _simulation_ford_gates() -> Array:
	## RetailSliceSim's map-configuration contract accepts exactly three ford
	## gate rows; they surface only in the deterministic state snapshot, never
	## in routing. Maps with three authored ford crossings (Fords of Isen II)
	## pass them through unchanged. Maps without authored fords receive three
	## inert, explicitly-named sentinel gates so the sim contract holds without
	## inventing water crossings the source did not author.
	if ford_gates.size() == 3:
		return ford_gates.duplicate(true)
	var sentinels: Array[Dictionary] = []
	for index in range(3):
		sentinels.append({
			"name": "no-authored-ford-crossing-%d" % (index + 1),
			"source_river_id": -1,
			"source_section_index": -1,
			"edge_a": Vector2.ZERO,
			"edge_b": Vector2.ZERO,
			"center": Vector2.ZERO,
			"elevation": 0.0,
		})
	return sentinels


func _home_layout_for(home: Vector2, opponent: Vector2) -> Dictionary:
	var inward := home.direction_to(opponent)
	if inward.length_squared() < 0.01:
		inward = Vector2.RIGHT
	var outward := -inward
	var side := Vector2(-inward.y, inward.x)
	return {
		"fortress": _walkable_spawn(home + outward * 9.0),
		"farm": _walkable_spawn(home + side * 11.0 + outward * 2.0),
		"barracks": _walkable_spawn(home - side * 11.0 + outward),
		"archery_range": _walkable_spawn(home + side * 18.0 - outward * 3.0),
		"stable": _walkable_spawn(home - side * 18.0 - outward * 3.0),
		"rally": _walkable_spawn(home + inward * 9.0),
	}


func resolve_walkable_position(candidate: Vector2) -> Vector2:
	return _walkable_spawn(candidate)


func _walkable_spawn(candidate: Vector2) -> Vector2:
	var cell := local_to_grid_cell(candidate)
	if is_navigation_walkable(cell):
		return candidate
	var nearest := _nearest_walkable_cell(cell, WALKABLE_SNAP_RADIUS_CELLS)
	return grid_to_local_horizontal(nearest) if nearest.x >= 0 else candidate


func _read_document(relative: String, label: String) -> Dictionary:
	var path := _resolve(relative)
	if path == "" or not FileAccess.file_exists(path):
		_fail("missing or unsafe %s document" % label)
		return {}
	var byte_count := _file_size(path)
	if byte_count <= 0 or byte_count > MAX_DOCUMENT_BYTES:
		_fail("invalid or unbounded %s document" % label)
		return {}
	var value: Variant = ModLoader._read_json(path)
	if typeof(value) != TYPE_DICTIONARY:
		_fail("invalid %s document" % label)
		return {}
	return value as Dictionary


func _read_pack_document(relative: String, label: String, max_bytes: int = MAX_DOCUMENT_BYTES) -> Dictionary:
	var path := _resolve_pack_asset(relative)
	if path == "" or not FileAccess.file_exists(path):
		_fail("missing or unsafe %s document" % label)
		return {}
	var byte_count := _file_size(path)
	if byte_count <= 0 or byte_count > max_bytes:
		_fail("invalid or unbounded %s document (%d bytes, bound %d)" % [label, byte_count, max_bytes])
		return {}
	var value: Variant = ModLoader._read_json(path)
	if typeof(value) != TYPE_DICTIONARY:
		_fail("invalid %s document" % label)
		return {}
	provenance_manifest_path = path if relative == "provenance/manifest.json" else provenance_manifest_path
	return value as Dictionary


func _read_bytes(path: String, expected_size: int, label: String) -> PackedByteArray:
	if expected_size <= 0 or expected_size > MAX_TERRAIN_BINARY_BYTES:
		_fail("invalid %s size" % label)
		return PackedByteArray()
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() != expected_size:
		if file != null:
			file.close()
		_fail("%s byte count does not match its metadata" % label)
		return PackedByteArray()
	var bytes := file.get_buffer(expected_size)
	file.close()
	return bytes


func _read_bounded_bytes(path: String, maximum_size: int, label: String) -> PackedByteArray:
	var byte_count := _file_size(path)
	if byte_count <= 0 or byte_count > maximum_size:
		_fail("invalid or unbounded %s size" % label)
		return PackedByteArray()
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() != byte_count:
		if file != null:
			file.close()
		_fail("%s could not be opened at its validated path" % label)
		return PackedByteArray()
	var bytes := file.get_buffer(byte_count)
	file.close()
	if bytes.size() != byte_count:
		_fail("%s read was truncated" % label)
		return PackedByteArray()
	return bytes


func _resolve(relative: String) -> String:
	if relative == "":
		return ""
	var resolved := ModLoader.resolve_pack_path(map_root, relative)
	if resolved == "" or not ModLoader.path_is_within(map_root, resolved) or not ModLoader.path_is_within(pack_root, resolved):
		return ""
	return resolved


func _resolve_from(base_root: String, relative: String) -> String:
	if base_root == "" or relative == "":
		return ""
	var resolved := ModLoader.resolve_pack_path(base_root, relative)
	if resolved == "" or not ModLoader.path_is_within(base_root, resolved) or not ModLoader.path_is_within(map_root, resolved) or not ModLoader.path_is_within(pack_root, resolved):
		return ""
	return resolved


func _resolve_from_pack(base_root: String, relative: String) -> String:
	## Like _resolve_from but confined to the pack rather than the map directory,
	## for pack-scope shared assets (the five-maps terrain material catalog).
	if base_root == "" or relative == "":
		return ""
	var resolved := ModLoader.resolve_pack_path(base_root, relative)
	if resolved == "" or not ModLoader.path_is_within(base_root, resolved) or not ModLoader.path_is_within(pack_root, resolved):
		return ""
	return resolved


func _file_size(path: String) -> int:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return -1
	var size := file.get_length()
	file.close()
	return size


func _sha256(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	if context.start(HashingContext.HASH_SHA256) != OK:
		return ""
	if context.update(bytes) != OK:
		return ""
	return context.finish().hex_encode()


func _has_png_signature(bytes: PackedByteArray) -> bool:
	return bytes.size() >= 8 and int(bytes[0]) == 137 and int(bytes[1]) == 80 and int(bytes[2]) == 78 and int(bytes[3]) == 71 and int(bytes[4]) == 13 and int(bytes[5]) == 10 and int(bytes[6]) == 26 and int(bytes[7]) == 10


func _parse_positive_decimal(value: Variant, maximum: float) -> float:
	if typeof(value) != TYPE_STRING or not _finite_positive(maximum):
		return NAN
	var text := String(value)
	if text == "" or text != text.strip_edges() or text.ends_with("."):
		return NAN
	var decimal_count := 0
	var digit_count := 0
	for index in range(text.length()):
		var code := text.unicode_at(index)
		if code == 46:
			decimal_count += 1
			if decimal_count > 1:
				return NAN
		elif code >= 48 and code <= 57:
			digit_count += 1
		else:
			return NAN
	if digit_count == 0 or text.begins_with(".") or (text.length() > 1 and text.begins_with("0") and not text.begins_with("0.")):
		return NAN
	var parsed := text.to_float()
	return parsed if _finite_positive(parsed) and parsed <= maximum else NAN


func _is_sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for index in range(value.length()):
		var code := value.unicode_at(index)
		if not ((code >= 48 and code <= 57) or (code >= 97 and code <= 102)):
			return false
	return true


func _vector3(value: Variant) -> Vector3:
	var values := _array(value)
	if values.size() != 3:
		return Vector3.INF
	var result := Vector3(float(values[0]), float(values[1]), float(values[2]))
	if not _finite_number(result.x) or not _finite_number(result.y) or not _finite_number(result.z):
		return Vector3.INF
	return result


func _even_sample(candidates: Array[Dictionary], limit: int) -> Array[Dictionary]:
	if candidates.size() <= limit:
		return candidates.duplicate(true)
	var result: Array[Dictionary] = []
	for index in range(limit):
		var source_index := int(floor(float(index) * float(candidates.size()) / float(limit)))
		result.append(candidates[source_index].duplicate(true))
	return result


func _horizontal(value: Vector3) -> Vector2:
	return Vector2(value.x, value.z)


func _finite_positive(value: float) -> bool:
	return _finite_number(value) and value > 0.0


func _finite_number(value: float) -> bool:
	return not is_nan(value) and not is_inf(value)


func _exact_integer(value: Variant) -> bool:
	if typeof(value) == TYPE_INT:
		return true
	if typeof(value) != TYPE_FLOAT:
		return false
	var numeric := float(value)
	return _finite_number(numeric) and numeric == floor(numeric) and absf(numeric) <= 9007199254740991.0


func _finite_vector2(value: Vector2) -> bool:
	return _finite_number(value.x) and _finite_number(value.y)


func _dictionary(value: Variant) -> Dictionary:
	return value as Dictionary if typeof(value) == TYPE_DICTIONARY else {}


func _array(value: Variant) -> Array:
	return value as Array if typeof(value) == TYPE_ARRAY else []


func _reset() -> void:
	ready = false
	error = ""
	map_id = ""
	source_virtual_path = ""
	source_sha256 = ""
	source_bytes = 0
	castle_gameplay_blockers.clear()
	castle_required_capabilities.clear()
	map_fixtures.clear()
	fixtures_omitted.clear()
	castle_fixture_placements.clear()
	castle_fixture_deferred.clear()
	_object_type_by_index.clear()
	_map_runtime_profile = DEFAULT_MAP_RUNTIME_PROFILE
	height_samples = PackedByteArray()
	passability_bits = PackedByteArray()
	terrain_texture_count = 0
	terrain_texture_width = 0
	terrain_texture_height = 0
	terrain_texture_array_dimension = 0
	terrain_material_catalog.clear()
	terrain_tile_indices = PackedInt32Array()
	terrain_tile_escape_count = 0
	terrain_blend_cells = PackedInt32Array()
	terrain_three_way_blend_cells = PackedInt32Array()
	terrain_cliff_cells = PackedInt32Array()
	terrain_blend_descriptions.clear()
	terrain_cliff_mappings.clear()
	terrain_nonzero_blend_cell_count = 0
	terrain_nonzero_three_way_blend_cell_count = 0
	terrain_nonzero_cliff_cell_count = 0
	terrain_source_layer_sha256.clear()
	_terrain_cell_texture_indices = PackedInt32Array()
	player_starts.clear()
	local_player_starts.clear()
	standing_water_polygons.clear()
	river_strips.clear()
	ford_gates.clear()
	generic_prop_placements.clear()
	bound_prop_placements.clear()
	bound_structure_placements.clear()
	creep_lair_placements.clear()
	bound_prop_type_ids.clear()
	bound_structure_type_ids.clear()
	logical_prop_type_ids.clear()
	unresolved_prop_type_ids.clear()
	bound_prop_placement_count = 0
	bound_structure_placement_count = 0
	logical_prop_placement_count = 0
	unresolved_prop_placement_count = 0
	nonroad_object_count = 0
	road_type_count = 0
	road_control_point_count = 0
	road_segment_count = 0
	road_unresolved_control_point_count = 0
	road_unique_endpoint_count = 0
	road_shared_node_count = 0
	road_curve_candidate_node_count = 0
	road_crossing_candidate_node_count = 0
	road_modifier_flags_or = 0
	road_type_ids.clear()
	road_control_points.clear()
	road_segments.clear()
	roads_path = ""
	road_material_count = 0
	road_material_catalog.clear()
	road_materials_path = ""
	road_source_report_aggregate_sha256 = ""
	provenance_manifest_path = ""
	object_binding_record_count = 0
	object_binding_resolution_status = ""
	object_bindings_path = ""
	_object_binding_by_type.clear()
	map_outline = PackedVector2Array()
	navigation_grid_min = Vector2i.ZERO
	navigation_grid_max = Vector2i.ZERO
	navigation_ready = false
	navigation_walkable_count = 0
	navigation_water_blocked_count = 0
	navigation_ford_corridor_count = 0
	navigation_build_count = 0
	route_query_count = 0
	_navigation_grid = null
	_water_cells = PackedByteArray()
	_ford_corridor_cells.clear()


func _roads_document_is_empty_network(roads: Dictionary, declared_summary: Dictionary, objects: Dictionary) -> bool:
	## True only for the exact schema-valid empty-road contract. A missing file,
	## malformed inventory, contradictory summary, or source road control point
	## must remain a hard failure rather than being erased as an optional layer.
	if (
		roads.is_empty()
		or String(roads.get("schema", "")) != "openbfme.sage-roads"
		or int(roads.get("schemaVersion", -1)) != 0
		or String(roads.get("coordinateTransform", "")) != "godot=(sage.x,sage.z,-sage.y)"
		or String(roads.get("pairingPolicy", "")) != "source-order-exact-wire-2-then-4-same-road-id"
		or String(roads.get("curveReconstruction", "")) != "not-attempted"
		or typeof(roads.get("roadIds", null)) != TYPE_ARRAY
		or typeof(roads.get("controlPoints", null)) != TYPE_ARRAY
		or typeof(roads.get("segments", null)) != TYPE_ARRAY
		or typeof(roads.get("unresolvedDiagnostics", null)) != TYPE_ARRAY
		or typeof(roads.get("summary", null)) != TYPE_DICTIONARY
	):
		return false
	var summary := roads.get("summary", {}) as Dictionary
	var exact_empty := (
		not declared_summary.is_empty()
		and declared_summary == summary
		and String(summary.get("status", "")) == "empty"
		and int(summary.get("roadIdCount", -1)) == 0
		and int(summary.get("controlPointCount", -1)) == 0
		and int(summary.get("pairedControlPointCount", -1)) == 0
		and int(summary.get("unresolvedControlPointCount", -1)) == 0
		and int(summary.get("segmentCount", -1)) == 0
		and int(summary.get("unresolvedDiagnosticCount", -1)) == 0
		and (roads.get("roadIds", []) as Array).is_empty()
		and (roads.get("controlPoints", []) as Array).is_empty()
		and (roads.get("segments", []) as Array).is_empty()
		and (roads.get("unresolvedDiagnostics", []) as Array).is_empty()
	)
	if not exact_empty or typeof(objects.get("objects", null)) != TYPE_ARRAY:
		return false
	# The roads inventory is derived from nonzero object roadType wires. Refuse
	# a contradictory "empty" inventory instead of routing those wires as props.
	for object_value in objects.get("objects", []) as Array:
		if typeof(object_value) != TYPE_DICTIONARY:
			return false
		var road_type: Variant = (object_value as Dictionary).get("roadType", null)
		if not _exact_integer(road_type) or int(road_type) != 0:
			return false
	return true


func _fail(message: String) -> bool:
	if error == "":
		error = message
	ready = false
	return false
