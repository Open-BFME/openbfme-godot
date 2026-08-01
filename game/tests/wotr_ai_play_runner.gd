extends SceneTree

## PLAY THE CAMPAIGN, IN A WINDOW, WITH THE OPPONENT TAKING ITS TURNS.
##
## `wotr_ai_runner.gd` proves the board changed. It cannot show whether the TURN
## CYCLE FEELS LIKE A GAME - whether pressing END TURN hands the war to somebody
## who does something and hands it back. That needs the real shell, the real
## strategic screen, and a window a person can watch.
##
## So this runner does exactly what a player does: it instantiates
## `scenes/boot.tscn`, navigates the shell to War of the Ring through its own
## public `show_page("wotr")` (the same route the menu's click takes), and then
## presses END TURN, letting frames run between presses so the map redraws and
## the change is VISIBLE. It prints a transcript of every turn as it happens, so
## the run is legible even from the console.
##
## ------------------------------------------------------------------------------
## WHERE THE OPPONENT'S TURN HAPPENS - AND WHY THIS FILE USED TO CRY WOLF
## ------------------------------------------------------------------------------
##
## This runner was written when nothing drove the opponent, so it alternated:
## human turn through `screen.end_turn()`, AI turn through `session.run_ai_turns()`
## itself, and it decided the run had proved something by scanning its own
## transcript for the words "marched", "took" or "attacked".
##
## `wotr_screen.end_turn()` NOW RUNS THE OPPONENT ITSELF (`run_opponent_turns()`,
## on the last line of it) - which is the feature this runner existed to ask for.
## The consequence is that when control returns from `end_turn()` the turn has
## ALREADY come back to the human, `session.active_seat_is_ai()` is false every
## time this file looks, the AI branch never runs, and nothing ever writes those
## three words into the transcript. So the runner printed "BROKEN: no AI turn
## moved anything" over a transcript whose own seat counts showed the opponent
## going from 9 regions to 22. A runner that fails on working software is worse
## than no runner: it teaches the reader to ignore it.
##
## WHAT IT OBSERVES NOW is the thing that is actually true rather than the thing
## that used to be:
##
##   * the screen's own `_ai_reports` - the reports `run_opponent_turns()` just
##     received - are read after every END TURN and narrated into the transcript,
##     so the opponent's turns appear in the account again;
##   * every seat's REGION COUNT is measured before and after each press, so the
##     board's own movement is evidence independent of any wording. A war where
##     the transcript is silent but a seat gained four regions is a working war
##     with a narration bug; the reverse is a real failure. Both are now told
##     apart instead of being one string search.
##
## IT STILL EDITS NOTHING ON THE SCREEN. It calls the public methods
## (`end_turn`, `refresh`, `available_map_ids`, `session`) and READS one private
## field, which is a deliberate, narrow coupling to the one thing the screen
## computes and does not otherwise expose. If that field is ever renamed the
## region-count evidence still carries the run.
##
## THE WINDOW IS MEANT TO BE SEEN. It opens at `--at <x>,<y>` (default 80,80) and
## is NOT no-focus: the point of this run is that somebody watches it.
##
## Usage:
##   Godot_v4.7 --path game --script res://tests/wotr_ai_play_runner.gd -- \
##       [--turns 12] [--at 80,80] [--frames 45]
## with the OPENBFME_LIVING_* settings pointing at the document and bundles.

const StateScript = preload("res://src/wotr/wotr_state.gd")
const AiScript = preload("res://src/wotr/wotr_ai.gd")

const SHELL_SCENE_PATH := "res://scenes/boot.tscn"
## The shell defers its boot settings and its skirmish sweep over the first idle
## frames; navigating into the middle of that opens a shell still assembling.
const SHELL_SETTLE_FRAMES := 14

var _menu: Node = null
var _screen: Control = null
var _settled := 0
var _mounted := false
var _turns_wanted := 12
var _frames_between := 45
var _wait := 0
var _turns_taken := 0
var _transcript: Array[String] = []
var _broken: Array[String] = []
var _done := false
## True once any report - the screen's or this file's - actually narrated a turn.
var _opponent_narrated := false
## True once a seat OTHER than the one on turn gained or lost regions, which can
## only happen inside somebody else's turn. Evidence off the authoritative state
## rather than off any string.
var _someone_else_moved := false
## Regions nobody held when the campaign opened, against the final count.
var _neutral_at_start := -1


func _initialize() -> void:
	_turns_wanted = int(_argument("--turns", "12"))
	_frames_between = int(_argument("--frames", "45"))
	var shell := load(SHELL_SCENE_PATH)
	if shell == null:
		push_error("[play] the shell scene did not load: %s" % SHELL_SCENE_PATH)
		quit(1)
		return
	root.add_child(shell.instantiate())
	_menu = _find_menu(root)
	_place_the_window_where_it_can_be_watched()
	print("[play] shell mounted; navigating to War of the Ring in %d frame(s)." % SHELL_SETTLE_FRAMES)


func _process(_delta: float) -> bool:
	if _done:
		return true
	if not _mounted:
		_settled += 1
		if _settled < SHELL_SETTLE_FRAMES:
			return false
		_mount_through_the_menu()
		if _screen == null:
			return _finish()
		_prepare_the_campaign()
		return false
	if _wait > 0:
		_wait -= 1
		return false
	if _turns_taken >= _turns_wanted:
		return _finish()
	_take_one_turn()
	_wait = _frames_between
	return false


# --- the loop ------------------------------------------------------------------

## ONE TURN, BY WHOEVER'S IT IS. This is the whole claim under test: that the
## cycle closes. A human turn goes through the screen's own END TURN - which runs
## the opponent before it returns - and control comes back.
func _take_one_turn() -> void:
	var session = _screen.get("session")
	if session == null or session.state == null:
		_broken.append("the shell mounted the screen with no session")
		_done = true
		return
	var state: StateScript = session.state

	# A DECIDED WAR STOPS. Turning after a victory would be playing a war that is
	# over, and would read as the loop being broken rather than finished.
	var standing: Dictionary = state.evaluate_victory()
	if bool(standing.get("ok", false)) \
			and int(standing.get("victorious_team", StateScript.NEUTRAL)) != StateScript.NEUTRAL:
		_say("the war is decided: team %d has won after %d turn(s)." % [
			int(standing.get("victorious_team", -1)), _turns_taken])
		_done = true
		return

	var seat := state.active_player()
	_turns_taken += 1
	# THE BOARD BEFORE THE PRESS. Measured here rather than inferred from any
	# narration, because this is the evidence that survives a wording change.
	var before := _region_counts(state)
	if session.active_seat_is_ai():
		# A SEAT THE SCREEN DID NOT PLAY FOR US. This is not the normal path any
		# more - `end_turn()` runs every consecutive AI seat itself - but a campaign
		# seated entirely by the GAME SETUP screen can hand this loop an AI seat on
		# its very first turn, before any END TURN has been pressed.
		var maps: Array = _screen.get("available_map_ids") as Array
		var reports: Array[Dictionary] = session.run_ai_turns(maps, 4)
		if reports.is_empty():
			_broken.append("the opponent was on turn and returned no report at all")
			_say("turn %d: seat %d is an AI seat and DID NOTHING - the loop is broken." % [
				_turns_taken, seat])
		_narrate(reports, "")
	else:
		_say("turn %d: seat %d is yours; pressing END TURN." % [_turns_taken, seat])
		_screen.call("end_turn")
		# AND THE OPPONENT'S TURNS CAME BACK INSIDE THAT CALL. `end_turn()` finishes
		# with `run_opponent_turns()`, which leaves every report it received on the
		# screen. Reading them here is what puts the opponent back into this
		# transcript; before this the account showed only the human's presses and
		# the runner then complained that the opponent never moved.
		var carried: Array = _screen.get("_ai_reports") as Array
		if carried == null:
			carried = []
		var opponent: Array[Dictionary] = []
		for entry in carried:
			opponent.append(entry as Dictionary)
		if opponent.is_empty():
			_say("turn %d:   END TURN handed play back with no opponent turn taken." % _turns_taken)
		_narrate(opponent, "the screen ran ")

	# THE SCREEN IS ASKED TO REDRAW rather than assumed to have. The whole reason
	# this runner exists is that a turn nobody can see is the bug.
	if _screen.has_method("refresh"):
		_screen.call("refresh")
	# WHAT THE PRESS ACTUALLY DID TO THE BOARD, seat by seat, in the transcript.
	# This is the line that would have told the reader, at a glance, that the old
	# "no AI turn moved anything" verdict was false.
	state = session.state
	var after := _region_counts(state)
	var deltas: Array[String] = []
	for index in range(after.size()):
		var was := int(before[index]) if index < before.size() else 0
		if int(after[index]) != was:
			deltas.append("seat %d %d -> %d" % [index, was, int(after[index])])
			if index != seat:
				# A SEAT THAT IS NOT THE ONE WHOSE TURN IT WAS GAINED OR LOST GROUND,
				# which can only have happened inside somebody else's turn. That is the
				# opponent acting, proved off the authoritative state.
				_someone_else_moved = true
	if not deltas.is_empty():
		_say("turn %d:   board: %s (neutral %d)" % [
			_turns_taken, ", ".join(deltas), _neutral_count(state)])


## Every report's narrative, refusals and score line, in the transcript.
func _narrate(reports: Array[Dictionary], prefix: String) -> void:
	for report in reports:
		var seat := int(report.get("seat", StateScript.NEUTRAL))
		for line in report.get("narrative", PackedStringArray()) as PackedStringArray:
			_say("turn %d: %sseat %d: %s" % [_turns_taken, prefix, seat, String(line)])
			_opponent_narrated = true
		for refusal in report.get("refusals", PackedStringArray()) as PackedStringArray:
			_say("turn %d:   (refused: %s)" % [_turns_taken, String(refusal)])
		var attack := report.get("attack", {}) as Dictionary
		if not attack.is_empty():
			_say("turn %d:   scored %d = retail %d + project %d; %s" % [
				_turns_taken, int(attack.get("score", 0)),
				int(attack.get("retail_score", 0)), int(attack.get("project_score", 0)),
				str(attack.get("reasons", PackedStringArray()))])


## Regions held by each seat, by index. The authoritative state's own count.
func _region_counts(state: StateScript) -> PackedInt32Array:
	var counts := PackedInt32Array()
	for index in range(state.players.size()):
		counts.append(state.regions_owned_by(index).size())
	return counts


## Regions nobody holds - the number the owner's original complaint was about.
func _neutral_count(state: StateScript) -> int:
	var neutral := 0
	for region_id in state.world.region_ids:
		if state.owner_of(String(region_id)) == StateScript.NEUTRAL:
			neutral += 1
	return neutral


## Load the opponent's retail weights and make sure a seat is actually an AI.
##
## The shell already seats seat 0 human and seat 1 AI, but a campaign started
## from the GAME SETUP screen can seat differently, and a run where every seat is
## human would prove nothing about the opponent while looking like a success.
func _prepare_the_campaign() -> void:
	var session = _screen.get("session")
	if session == null or session.state == null:
		_broken.append("the shell mounted the screen with no session")
		return
	var template: Dictionary = session.load_ai_template(_pack_roots())
	_say("retail AI template: %s" % (
		String(template.get("path", "")) if bool(template.get("ok", false))
		else "NONE - %s" % String(template.get("reason", ""))))

	var state: StateScript = session.state
	var ai_seats: Array[int] = []
	for index in range(state.players.size()):
		if String((state.players[index] as Dictionary).get("controller", "")) == StateScript.CONTROLLER_AI:
			ai_seats.append(index)
	if ai_seats.is_empty():
		_broken.append("this campaign seated no AI player, so there was no opponent to watch")
	_reseat_if_the_ai_cannot_fight(session)
	state = session.state
	# THE NUMBER THE OWNER'S COMPLAINT WAS ABOUT - 49 of 52 regions still neutral
	# at turn 14 - recorded at the start so the final line can be compared to it.
	_neutral_at_start = _neutral_count(state)
	_say("seats: %s" % _seat_line(state))
	_say("scenario '%s', %d region(s), AI seat(s) %s" % [
		session.scenario_name, session.world.region_ids.size(), str(ai_seats)])


## RE-SEAT WHEN THE OPPONENT'S ARMIES CANNOT FIGHT, and say so loudly.
##
## WHAT THIS COMMENT USED TO CLAIM WAS WRONG. It said the shell seats the Dwarves
## and that "NO Dwarven roster is covered by the auto-resolve bindings bundle this
## checkout ships" - a content coverage hole. It was not one. Two real bugs were
## wearing that costume, and both are fixed:
##
##   1. `main_menu.gd` called `session.load_auto_resolve()` AFTER
##      `session.begin()`, and `begin()` is what builds `state.roster_units`. Every
##      army in the first session was placed fielding nothing, whatever its
##      faction.
##   2. `livingworldbuildableunits.inc` declares `DainPlayerArmy`'s entry as
##      `DainPlayerArmy` - a row pointing at itself - which shadowed the
##      `DwarvenDain` row carrying the units. `wotr_world.gd` refuses
##      self-referential roster rows now and records each in `world.data_defects`.
##
## With both fixed every seatable template fields a complete roster and this
## function does not fire. It is KEPT because the question is still worth asking
## before a twelve-turn run: a checkout with no auto-resolve bundles would seat a
## mute opponent, and this loop would then spend its whole run proving nothing.
## The seating is redone through `session.begin()` - the same door the menu uses -
## onto the first template pair whose AI seat fields units, and the swap is
## printed. A run that silently played a different campaign than the menu opened
## would be worthless.
func _reseat_if_the_ai_cannot_fight(session) -> void:
	if _ai_seat_can_fight(session):
		return
	var path := String(session.document_path)
	var handle := FileAccess.open(path, FileAccess.READ)
	if handle == null:
		_broken.append("the AI seat's armies field no auto-resolve units and the document could not be re-read from %s" % path)
		return
	var parsed: Variant = JSON.parse_string(handle.get_as_text())
	handle.close()
	if not (parsed is Dictionary):
		_broken.append("the AI seat's armies field no auto-resolve units and %s no longer parses" % path)
		return
	var document := parsed as Dictionary
	var names: Array[String] = []
	for key in session.world.player_templates.keys():
		if int((session.world.player_templates[key] as Dictionary).get("starting_world_cp", -1)) > 0:
			names.append(String(key))
	names.sort()
	var campaign := String(session.world.campaign_name)
	var scenario := String(session.scenario_name)
	var was := String((session.state.players[1] as Dictionary).get("template", ""))
	for step in range(names.size()):
		var human := names[step]
		var ai := names[(step + 1) % names.size()]
		if not session.begin(document, campaign, scenario, [
			{"template": human, "team": 1, "controller": StateScript.CONTROLLER_HUMAN},
			{"template": ai, "team": 2, "controller": StateScript.CONTROLLER_AI},
		]):
			continue
		if _ai_seat_can_fight(session):
			_say(("RE-SEATED: the menu's AI seat (%s) was placed fielding no auto-resolve "
				+ "units, so this run seats %s (human) against %s (AI) instead. Look at the "
				+ "ORDER of load_auto_resolve() and begin() in main_menu.gd and at "
				+ "world.data_defects before believing this is a missing bundle - it has "
				+ "twice been neither.") % [was, human, ai])
			return
	_broken.append(
		"no seating in this document gives the AI armies the auto-resolve bindings cover, "
		+ "so no AI attack can resolve at all")


## True when the AI seat holds armies and every one of them fields auto-resolve
## units - the precondition for an AI attack to resolve rather than refuse.
func _ai_seat_can_fight(session) -> bool:
	var found := false
	for key in session.state.armies.keys():
		var army := session.state.armies[key] as Dictionary
		if int(army.get("owner", -1)) != 1:
			continue
		if (army.get("units", []) as Array).is_empty():
			return false
		found = true
	return found


func _seat_line(state: StateScript) -> String:
	var parts: Array[String] = []
	for index in range(state.players.size()):
		var seat := state.players[index] as Dictionary
		parts.append("%d=%s(%s, %d region(s))" % [
			index, String(seat.get("template", "")), String(seat.get("controller", "")),
			state.regions_owned_by(index).size()])
	return ", ".join(parts)


func _pack_roots() -> Array:
	if _screen != null and _screen.get("pack_roots") != null:
		return _screen.get("pack_roots") as Array
	return []


# --- mounting ------------------------------------------------------------------

func _mount_through_the_menu() -> void:
	_mounted = true
	if _menu == null or not _menu.has_method("show_page"):
		_broken.append("the shell did not instantiate, so nothing could be mounted")
		return
	if not bool(_menu.call("show_page", "wotr")):
		var reason := String(_menu.call("wotr_unavailable_reason")) \
			if _menu.has_method("wotr_unavailable_reason") else "<no reason given>"
		_broken.append("the shell refused to open War of the Ring: %s" % reason)
		return
	_screen = _menu.get("wotr_screen") as Control
	if _screen == null:
		_broken.append("the shell reported War of the Ring open but owns no strategic screen")


func _find_menu(node: Node) -> Node:
	if node.has_method("show_page") and node.has_method("get_current_page"):
		return node
	for child in node.get_children():
		var found := _find_menu(child)
		if found != null:
			return found
	return null


## THE WINDOW IS FOR WATCHING. It is deliberately NOT created no-focus and NOT
## placed off-screen: an earlier rule put runs in the taskbar where the owner
## could not see them, and being in the loop is the point of this one.
func _place_the_window_where_it_can_be_watched() -> void:
	var at := Vector2i(80, 80)
	var override := _argument("--at", "")
	var parts := override.split(",", false)
	if parts.size() == 2 and parts[0].is_valid_int() and parts[1].is_valid_int():
		at = Vector2i(int(parts[0]), int(parts[1]))
	DisplayServer.window_set_position(at)
	print("[play] window at %s - watch it." % str(DisplayServer.window_get_position()))


# --- reporting -----------------------------------------------------------------

func _say(line: String) -> void:
	_transcript.append(line)
	print("[play] %s" % line)


## A RUN THAT PROVED NOTHING FAILS - but it now fails on what the BOARD did, not
## on which words appeared in its own transcript.
##
## THE OLD TEST WAS A STRING SEARCH over lines this file wrote itself, looking for
## "marched", "took " or "attacked". It went false the moment `wotr_screen.gd`
## started running the opponent inside `end_turn()`, because this loop then never
## entered its own AI branch and never wrote any of those words - so a campaign
## whose seat counts went 9 -> 22 regions in the same transcript was reported
## BROKEN. The lesson is in the fix: assert on the authoritative state, and let
## the narration be narration.
func _finish() -> bool:
	_done = true
	var session = _screen.get("session") if _screen != null else null
	if session != null and session.state != null:
		_say("final: %s" % _seat_line(session.state))
		_say("final: %d region(s) still held by nobody (started at %d)." % [
			_neutral_count(session.state), _neutral_at_start])
	print("\n[play] ---- transcript ----")
	for line in _transcript:
		print("[play] %s" % line)
	# THE OPPONENT ACTED, off the state: a seat that was not the one on turn
	# changed how much ground it held. Nothing but another seat's turn can do that.
	if not _someone_else_moved:
		_broken.append(
			("no seat other than the one on turn gained or lost a region in %d turn(s) - "
			+ "the opponent held its ground every time") % _turns_taken)
	# AND IT WAS REPORTED. A war that moves without ever narrating a turn is a
	# different failure from a war that does not move, and the two are now told
	# apart instead of sharing one verdict.
	if not _opponent_narrated:
		_broken.append(
			("the board changed but no opponent turn was ever narrated in %d turn(s) - "
			+ "either the screen stopped keeping its reports or this runner stopped "
			+ "reading them") % _turns_taken)
	if _broken.is_empty():
		print("[play] OK: %d turn(s) played, the opponent acted and was reported, control came back each time." % _turns_taken)
		quit(0)
		return true
	for complaint in _broken:
		printerr("[play] BROKEN: %s" % complaint)
	quit(1)
	return true


func _argument(name: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size()):
		if String(args[index]) == name and index + 1 < args.size():
			return String(args[index + 1])
	return fallback
