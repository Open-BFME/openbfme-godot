## ONLINE16-FIX (Sol follow-up of P16): the device bridge's game message. An absent "map_start_count" is the legacy eight-slot
## message; present, it is the extended tag's capacity and must be 9 .. 16 — an explicit 8 is refused like 7 or 17.
## No generated or retail bytes are written. Requires the two retail installations.
extends SceneTree

var failures := 0

func check(ok: bool, what: String) -> void:
	print("PASS " if ok else "FAIL ", what)
	if not ok:
		failures += 1

func message(slot_count: int) -> Dictionary:
	var slots: Array = []
	for i in slot_count:
		slots.append({"state": 6 if i < 2 else 1, "name": "Player %d" % (i + 1), "accepted": true,
			"color": i, "start_pos": -1, "player_template": 3, "team": -1})
	return {"mode": "skirmish", "map": "maps/map mp evendim/map mp evendim.map", "map_crc": 0,
		"map_size": 0, "seed": 9001, "starting_cash": 2000, "slots": slots}

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("ONLINE16_FIX_BRIDGE SKIP: retail installations required")
		quit(77)
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail()
	check(mounted.get("ok", false), "mount retail")
	var world: Node = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	check(world.setup(fs).get("ok", false), "world setup")
	if failures:
		quit(1)
		return
	var legacy := message(8)
	var prepared: Dictionary = world.prepare_new_game(legacy)
	check(prepared.get("ok", false), "the legacy message (no map_start_count) is accepted: %s" % str(prepared.get("errors", [])))
	var explicit8 := message(8)
	explicit8.map_start_count = 8
	prepared = world.prepare_new_game(explicit8)
	check(not prepared.get("ok", true), "an explicit map_start_count 8 is refused")
	check(str(prepared.get("errors", [])).contains("map_start_count 8"), "the refusal names the value: %s" % str(prepared.get("errors", [])))
	var wide8 := message(16)
	wide8.map_start_count = 8
	check(not world.prepare_new_game(wide8).get("ok", true), "sixteen slots with an explicit 8 are refused")
	print("ONLINE16_FIX_BRIDGE ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
