## Lane SPELL-2 (review r1, fix 1): the spell book screens do not outlive the game. A store is opened with a pending purchase, a bought power is put in
## targeting, the map is loaded again (the game is replaced): the store must be closed, the targeting cancelled, and a store opened again belongs to the
## new game's player (its points, nothing pending).
##
##   godot --headless --path godot --script res://tests/spellbook_reload_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failed := false


func _check(cond: bool, what: String) -> void:
	print(("  ok   " if cond else "  FAIL ") + what)
	if not cond:
		_failed = true


func _local(rep: Dictionary) -> int:
	for i in rep.players.size():
		if String(rep.players[i]).contains("'Player_1'"):
			return i
	return -1


func _initialize() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SPELLBOOK RELOAD SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		quit(77)
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		print("SPELLBOOK RELOAD FAIL: the retail mount failed")
		quit(1)
		return
	var w: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(w)
	if not w.setup(fs).ok:
		print("SPELLBOOK RELOAD FAIL: GameWorld.setup")
		quit(1)
		return
	var opts := {"seed": 123, "slots": [{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0}]}
	var first: Dictionary = w.load_map("map mp evendim", opts)
	_check(first.ok, "the first game loads")
	var pi := _local(first)
	_check(pi >= 0, "the local player is found")
	# the spell book's requirement (SPELL_BOOK_REQUIREMENTS_FILTER: a COMMANDCENTER): a load_map game has no starting fortress
	_check(w.create_object("MenFortressCitadel", pi, 1200.0, 1200.0, 0.0) > 0, "a citadel for the spell book's requirement")
	# buy the first available science through the store, run the purchase, then put its power in targeting
	_check(w.spell_store_open(pi), "the store opens")
	var bought := ""
	for b in w.get_spell_store().buttons:
		if b.state == "available" and bought.is_empty():
			_check(w.spell_store_click(b.index), "a click makes " + b.science + " pending")
			bought = b.science
	_check(w.spell_store_close() == 1, "the close sends the purchase")
	for i in 10:
		w.advance(0.2)
	var bar: Dictionary = w.get_spell_bar(pi)
	var target := -1
	for b in bar.buttons:
		if b.owned and b.needs_position and target < 0:
			target = b.index
	_check(target >= 0, "the bought power is on the cast bar")
	_check(w.spell_bar_press(pi, target), "its press starts the targeting")
	_check(w.get_spell_bar(pi).targeting, "the bar is targeting")
	# a second store open across the replacement, with a pending click
	_check(w.spell_store_open(pi), "the store opens again")
	for b in w.get_spell_store().buttons:
		if b.state == "available":
			w.spell_store_click(b.index)
			break
	# the game is replaced
	opts.seed = 124
	var second: Dictionary = w.load_map("map mp evendim", opts)
	_check(second.ok, "the second game loads")
	var stale: Dictionary = w.get_spell_store()
	_check(not stale.open, "the store of the old game is closed")
	_check(w.spell_store_close() == 0, "closing it sends nothing into the new game")
	var pi2 := _local(second)
	var bar2: Dictionary = w.get_spell_bar(pi2)
	_check(not bar2.targeting, "the targeting of the old game is cancelled")
	var owned := 0
	for b in bar2.buttons:
		owned += 1 if b.owned else 0
	_check(owned == 0, "nothing of the old game's purchases is owned")
	_check(w.spell_store_open(pi2), "a store opens against the new player")
	var fresh: Dictionary = w.get_spell_store()
	_check(fresh.pending.is_empty(), "nothing is pending")
	_check(fresh.points == 5, "the new player's rank-1 points (5)")
	print("SPELLBOOK RELOAD " + ("FAIL" if _failed else "PASS"))
	quit(1 if _failed else 0)
