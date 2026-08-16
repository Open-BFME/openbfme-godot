extends SceneTree
## field:rank.SciencePurchasePointsGranted
## field:rank.SkillPointsNeededDefault
##
## The RotWK Rank ladder pays spell points on promotion. Retail oracle:
## data/ini/rank.ini Rank 1 grants 5 at threshold 0, every later rank grants
## PLAYER_PURCHASE_POINTS_GRANTED (gamedata.ini = 1) at
## #MULTIPLY( PLAYER_SKILL_POINTS_DELTA_DEFAULT n ) skill points (60 * n).

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const ORACLE_ROOT := "res://../.private/retail-work/editions/rotwk/cache/effective-assets/data/ini/"
const WATCHDOG_FRAMES := 600

var _frames := 0


func _initialize() -> void:
	_run()


func _process(_delta: float) -> bool:
	# A silent abort inside _run() would otherwise idle here forever and look
	# like a hang instead of a failure.
	_frames += 1
	if _frames > WATCHDOG_FRAMES:
		_report(false, "watchdog: the runner aborted before reporting", 0)
	return false


func _oracle(name: String) -> String:
	return FileAccess.get_file_as_string(ProjectSettings.globalize_path(ORACLE_ROOT + name))


func _run() -> void:
	var rank_ini := _oracle("rank.ini")
	var gamedata_ini := _oracle("gamedata.ini")
	if not rank_ini.contains("SciencePurchasePointsGranted\t= 5"):
		_report(false, "RotWK rank.ini lacks the Rank 1 grant", 0)
		return
	if not rank_ini.contains("SciencePurchasePointsGranted\t= PLAYER_PURCHASE_POINTS_GRANTED"):
		_report(false, "RotWK rank.ini lacks the constant grant", 0)
		return
	if not rank_ini.contains("#MULTIPLY( PLAYER_SKILL_POINTS_DELTA_DEFAULT 1 )"):
		_report(false, "RotWK rank.ini lacks the authored rank threshold", 0)
		return
	if not gamedata_ini.contains("PLAYER_PURCHASE_POINTS_GRANTED"):
		_report(false, "RotWK gamedata.ini lacks PLAYER_PURCHASE_POINTS_GRANTED", 0)
		return
	if not gamedata_ini.contains("PLAYER_SKILL_POINTS_DELTA_DEFAULT"):
		_report(false, "RotWK gamedata.ini lacks PLAYER_SKILL_POINTS_DELTA_DEFAULT", 0)
		return

	var ladder: Array = [
		_ladder_row(1, "5", 5, "0", 0, 11, 13),
		_ladder_row(2, "PLAYER_PURCHASE_POINTS_GRANTED", 1, "#MULTIPLY( PLAYER_SKILL_POINTS_DELTA_DEFAULT 1 )", 60, 17, 19),
		_ladder_row(3, "PLAYER_PURCHASE_POINTS_GRANTED", 1, "#MULTIPLY( PLAYER_SKILL_POINTS_DELTA_DEFAULT 2 )", 120, 23, 25),
	]

	var failures: Array[String] = []

	# 1. An unconfigured ladder refuses instead of granting anything.
	var bare = Sim.new()
	var refused: Dictionary = bare.advance_player_rank(0, 1)
	if String(refused.get("reason", "")) != "rank-ladder-unavailable":
		failures.append("unconfigured ladder did not refuse: %s" % str(refused))

	# 2. A descending threshold is rejected whole.
	var broken = Sim.new()
	var descending: Array = [
		_ladder_row(1, "5", 5, "60", 60, 11, 13),
		_ladder_row(2, "1", 1, "30", 30, 17, 19),
	]
	if bool(broken.configure_player_rank_science_grants(descending)):
		failures.append("descending ladder was accepted")
	elif not String(broken.player_rank_ladder_error()).contains("SkillPointsNeededDefault"):
		failures.append("descending ladder error is unclear: %s" % broken.player_rank_ladder_error())

	# 3. Explicit promotion pays every crossed rank exactly once.
	var sim = Sim.new()
	if not bool(sim.configure_player_rank_science_grants(ladder)):
		_report(false, "ladder refused: %s" % sim.player_rank_ladder_error(), 0)
		return
	sim.team_power_points[0] = 0
	var first: Dictionary = sim.advance_player_rank(0, 1)
	var second: Dictionary = sim.advance_player_rank(0, 2)
	var replay: Dictionary = sim.advance_player_rank(0, 2)
	if int(first.get("granted", -1)) != 5:
		failures.append("rank 1 granted %s, expected 5" % str(first.get("granted")))
	if int(second.get("granted", -1)) != 1:
		failures.append("rank 2 granted %s, expected 1" % str(second.get("granted")))
	if not bool(replay.get("ok", false)) or int(replay.get("granted", -1)) != 0:
		failures.append("replaying rank 2 was not idempotent: %s" % str(replay))
	if sim.power_points(0) != 6:
		failures.append("points after ranks 1-2 = %d, expected 6" % sim.power_points(0))
	if sim.player_rank(0) != 2:
		failures.append("player rank = %d, expected 2" % sim.player_rank(0))
	var unknown: Dictionary = sim.advance_player_rank(0, 9)
	if String(unknown.get("reason", "")) != "unknown-rank":
		failures.append("a rank outside the ladder was accepted: %s" % str(unknown))

	# 4. Skill points cross the authored thresholds and pay the same grants.
	var ladder_sim = Sim.new()
	if not bool(ladder_sim.configure_player_rank_science_grants(ladder)):
		_report(false, "ladder refused: %s" % ladder_sim.player_rank_ladder_error(), 0)
		return
	ladder_sim.team_power_points[0] = 0
	var start: Dictionary = ladder_sim.award_player_skill_points(0, 0)
	if int(start.get("rank", -1)) != 1 or ladder_sim.power_points(0) != 5:
		failures.append("threshold 0 did not seat rank 1 with 5 points: %s" % str(start))
	var below: Dictionary = ladder_sim.award_player_skill_points(0, 59)
	if int(below.get("rank", -1)) != 1 or ladder_sim.power_points(0) != 5:
		failures.append("59 skill points promoted early: %s" % str(below))
	var crossed: Dictionary = ladder_sim.award_player_skill_points(0, 1)
	if int(crossed.get("rank", -1)) != 2 or ladder_sim.power_points(0) != 6:
		failures.append("threshold 60 did not promote to rank 2: %s" % str(crossed))
	var jumped: Dictionary = ladder_sim.award_player_skill_points(0, 60)
	if int(jumped.get("rank", -1)) != 3 or ladder_sim.power_points(0) != 7:
		failures.append("threshold 120 did not promote to rank 3: %s" % str(jumped))
	if ladder_sim.player_skill_points(0) != 120:
		failures.append("banked skill points = %d, expected 120" % ladder_sim.player_skill_points(0))

	_report(failures.is_empty(), "; ".join(failures) if not failures.is_empty() else "ranks 1-3 granted 5+1+1", failures.size())


func _ladder_row(
	rank: int,
	granted_authored: String,
	granted: int,
	threshold_authored: String,
	threshold: int,
	threshold_line: int,
	granted_line: int
) -> Dictionary:
	return {
		"rank": rank,
		"sciencePurchasePointsGranted": {
			"authored": granted_authored,
			"value": granted,
			"sourceIni": "data/ini/rank.ini",
			"line": granted_line,
		},
		"skillPointsNeededDefault": {
			"authored": threshold_authored,
			"value": threshold,
			"sourceIni": "data/ini/rank.ini",
			"line": threshold_line,
		},
	}


func _report(ok: bool, detail: String, failed: int) -> void:
	print("RANK_SCIENCE_PURCHASE_POINTS_RESULT passed=%d failed=%d detail=%s" % [
		1 if ok else 0, failed if not ok and failed > 0 else (0 if ok else 1), detail,
	])
	quit(0 if ok else 1)
