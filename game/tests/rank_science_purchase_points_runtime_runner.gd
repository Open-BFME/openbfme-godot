extends SceneTree
## field:rank.SciencePurchasePointsGranted

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

func _initialize() -> void:
	var oracle_path:=ProjectSettings.globalize_path("res://../.private/retail-work/editions/rotwk/cache/effective-assets/data/ini/rank.ini")
	var oracle:=FileAccess.get_file_as_string(oracle_path)
	var retail_receipts_ok:=oracle.contains("SciencePurchasePointsGranted\t= 5") and oracle.contains("SciencePurchasePointsGranted\t= PLAYER_PURCHASE_POINTS_GRANTED")
	var sim = Sim.new()
	var configured: bool = sim.configure_player_rank_science_grants([
		{"rank": 1, "sciencePurchasePointsGranted": {"authored": "5", "value": 5, "sourceIni": "data/ini/rank.ini", "line": 13}},
		{"rank": 2, "sciencePurchasePointsGranted": {"authored": "PLAYER_PURCHASE_POINTS_GRANTED", "value": 1, "sourceIni": "data/ini/rank.ini", "line": 19}},
	])
	sim.team_power_points[0] = 0
	var first: Dictionary = sim.advance_player_rank(0, 1)
	var second: Dictionary = sim.advance_player_rank(0, 2)
	var replay: Dictionary = sim.advance_player_rank(0, 2)
	var ok := retail_receipts_ok and configured and bool(first.get("ok", false)) and bool(second.get("ok", false)) and bool(replay.get("ok", false)) and sim.power_points(0) == 6 and int(replay.get("granted", -1)) == 0
	print("RANK_SCIENCE_PURCHASE_POINTS_RESULT passed=%d failed=%d points=%d" % [1 if ok else 0, 0 if ok else 1, sim.power_points(0)])
	quit(0 if ok else 1)
