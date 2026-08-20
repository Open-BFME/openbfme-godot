extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 3

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim = _fixture_sim()
	var site := Vector2(0.5, 0.0)
	var foundation := _fixture_by_role(sim, "foundation")
	var wall := _fixture_by_role(sim, "wall")
	var exempted_ids := []
	for structure_id in sim.structure_ids():
		if int(structure_id) < Sim.CASTLE_FIXTURE_FIRST_ID:
			continue
		var fixture: Dictionary = sim.structure(structure_id)
		if sim._authored_site_foundation_fixture_contains(fixture, site):
			exempted_ids.append(int(structure_id))
	_check(
		"exactly_one_fixture_exempted",
		exempted_ids.size() == 1 and exempted_ids[0] == int(foundation.get("id", 0)),
		"exempted=%s foundation=%s" % [str(exempted_ids), str(foundation)]
	)
	_check(
		"foundation_footprint_contains_site",
		sim._authored_site_foundation_fixture_contains(foundation, site),
		"site=%s foundation=%s" % [str(site), str(foundation)]
	)
	_check(
		"neighbouring_wall_fixture_not_exempted",
		not sim._authored_site_foundation_fixture_contains(wall, site),
		"site=%s wall=%s" % [str(site), str(wall)]
	)
	_finish()


func _fixture_sim():
	# Sealed synthetic placements follow the map-fixture seeding path used by
	# castle_wall_defense_runner. Both footprints contain `site`; role identity
	# must make only the foundation/build-plot fixture exempt.
	var sim = Sim.new()
	var fixture_placements := [
		{
			"type_name": "SyntheticCastleFoundation",
			"role": "foundation",
			"source_index": 1,
			"position": Vector2.ZERO,
			"elevation": 0.0,
			"yaw": 0.0,
			"owner": "Player_1",
			"maximum_health": 1000.0,
			"armor": "CastleWallArmor",
			"indestructible": false,
		},
		{
			"type_name": "SyntheticCastleWall",
			"role": "wall",
			"source_index": 2,
			"position": Vector2(1.25, 0.0),
			"elevation": 0.0,
			"yaw": 0.0,
			"owner": "Player_1",
			"maximum_health": 1000.0,
			"armor": "CastleWallArmor",
			"indestructible": false,
		},
	]
	sim.setup({}, {
		"enable_base_loop": true,
		"enable_castle_fixtures": true,
		"source_map_transform_scale": 0.1,
	})
	sim._castle_fixture_placements = fixture_placements
	sim._seed_castle_fixtures()
	for structure_id in sim.structure_ids():
		if int(structure_id) >= Sim.CASTLE_FIXTURE_FIRST_ID:
			# Synthetic equivalent of compiled geometry: radius 10 source units
			# becomes 1.0 sim unit at the sealed map transform above.
			(sim.structures[structure_id] as Dictionary)["footprint_radius_source"] = 10.0
	return sim


func _fixture_by_role(sim, role: String) -> Dictionary:
	for structure_id in sim.structure_ids():
		var row: Dictionary = sim.structure(structure_id)
		if String(row.get("castle_fixture_role", "")) == role:
			return row
	return {}


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("CASTLE_FOUNDATION_EXEMPTION PASS " + label)
	else:
		failed += 1
		push_error("CASTLE_FOUNDATION_EXEMPTION FAIL %s%s" % [label, (" (%s)" % detail) if detail != "" else ""])


func _finish() -> void:
	print("CASTLE_FOUNDATION_EXEMPTION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED else 1)
