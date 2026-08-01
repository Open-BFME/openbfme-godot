extends SceneTree
const SliceScript = preload("res://src/retail_slice/retail_vertical_slice.gd")
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

func _initialize():
	call_deferred("_run")

func _run():
	var slice = SliceScript.new()
	# Minimal setup matching runner? Use full slice scene path if needed
	# Instead call normalization after configuring sim
	var sim = SimScript.new()
	sim.configure_team_roster([
		{"team": 0, "faction": "men", "is_ai": false, "start_index": 0},
		{"team": 1, "faction": "men", "is_ai": true, "start_index": 1},
	])
	sim.setup({}, {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"unit_rules": {},
	})
	print("desc0=", sim.team_descriptor(0))
	print("start_index in live=", sim._team_descriptors[0].get("start_index", "MISSING"))
	# Attach slice simulation for normalize
	slice.simulation = sim
	var document = {
		"schema": "openbfme.map-scripts",
		"schemaVersion": 1,
		"world": {
			"available": true,
			"players": [
				{"index": 0, "name": ""},
				{"index": 1, "name": "PlyrCreeps"},
				{"index": 2, "name": "SkirmishMen"},
				{"index": 3, "name": "Player_1"},
			],
			"namedObjects": [],
			"teams": [
				{"index": 0, "name": "teamPlayer_1", "owner": "PlyrCreeps", "objectCount": 0, "namedMembers": [], "units": []},
				{"index": 1, "name": "Player Strike", "owner": "Player_1", "objectCount": 0, "namedMembers": [], "units": []},
			],
		},
		"scripts": [{
			"playerIndex": 3,
			"payload": {"name": "v1-fixture", "isActive": true, "records": []},
		}],
	}
	var n = slice._normalized_map_scripts_document(document)
	print("ok=", n.get("ok"), " reason=", n.get("reason", ""))
	print("players=", n.get("players", {}))
	if n.get("ok"):
		for t in n.get("teams", []):
			print("team=", t.get("name"), " owner_player=", t.get("owner_player"), " owner_team=", t.get("owner_team"), " default=", t.get("default"))
	quit(0)
