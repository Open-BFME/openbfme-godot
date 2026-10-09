## Lane QA-1: the scripted human of the QA runs (game.gd --qa). It plays the local side of a live skirmish through the player's own input paths only: clicks on
## the world and on the Palantir's command buttons (InGameHudNode.inject_mouse_*: the HUD's input, the same one the mouse feeds), retail's hotkeys
## (inject_key with DirectInput codes: Q = SELECT_ALL), the spell book and the spell store movies. It reads the game only to decide what a player would do
## next (the command bar it sees, its objects, the money) and to judge the effect of each action.
##
## What goes wrong is printed as one line per issue and kind:  QA ISSUE <kind> | <subject> | <detail>
## and the run ends with:  QA SUMMARY <json>  (counters, the buttons pressed and their effects, the stops the live game hit, the live errors).
## tools/qa/qa_matrix.py runs the matrix of factions x maps x AI levels and ranks the issues (workspace/rebuild/QA-1.md).
##
## Lane QA-2 adds invariants watched on every player's units through the whole game (played or idle), the feedback items' symptoms measured on the
## logic's state (workspace/rebuild/QA-2.md): a unit standing still with the MOVING model condition for 3 probes (the "treadmill", community feedback
## G1: the run animation plays in place) and a unit the AI calls moving that has not moved for 30 s of game time (stuck: lost infantry, F4 / G2).
extends Node

const DIK_Q := 0x10
const DIK_ESCAPE := 0x01
const DIK_A := 0x1E

# commands the player never presses in a QA run: they undo work (sell, cancel), leave a structure, or only change a mode a later click would use
const SKIP_COMMANDS := ["SELL", "CANCEL_UNIT_BUILD", "CANCEL_UPGRADE", "DOZER_CONSTRUCT_CANCEL", "FOUNDATION_CONSTRUCT_CANCEL", "EVACUATE", "EVACUATE_CONTESTED",
	"EXIT_CONTAINER", "CREW_EVACUATE", "BEACON_DELETE", "PLACE_BEACON", "PUSH_VISIBLE_COMMAND_RANGE", "POP_VISIBLE_COMMAND_RANGE", "CANCEL_NEIGHBORHOOD",
	"CASTLE_PACK", "STOP", "WAYPOINTS", "NONE", "ONE_RING"]
# commands whose press reaches the logic at once (a GameMessage in the command list)
const DIRECT_COMMANDS := ["UNIT_BUILD", "SECONDARY_UNIT_BUILD", "PLAYER_UPGRADE", "OBJECT_UPGRADE", "CASTLE_UPGRADE", "PURCHASE_SCIENCE", "TOGGLE_STANCE",
	"SET_STANCE", "HORDE_TOGGLE_FORMATION", "HORDE_SET_FORMATION", "TOGGLE_WEAPON", "TOGGLE_WEAPONSET", "SWITCH_WEAPON", "START_SELF_REPAIR", "REVIVE",
	"FOUNDATION_CONSTRUCT", "CASTLE_UNPACK", "CASTLE_UNPACK_EXPLICIT_OBJECT", "BLOODTHIRSTY", "OPEN_GATE", "CLOSE_GATE", "TOGGLE_GATE", "SPECIAL_POWER_TOGGLE",
	"AUTOCAST_WEAPON"]

var _game: Node
var _world: Node
var _hud: Node
var _local := ""
var _screens := ""
var _tag := "qa"
var _end_frame := 0
var _stalls := 0

var _issues := {}          # kind|subject -> {kind, subject, detail, count, frame}
var _counts := {}          # counter -> n
var _buttons := {}         # button name -> {command, presses, effects, template, upgrade, power, sets: {}}
var _sets_seen := {}       # template -> command set name
var _builders := {}        # template -> true (a DOZER_CONSTRUCT button in its set)
var _placed := {}          # structure template -> placements ordered
var _queued := {}          # unit template -> {frame, from}
var _produced := {}        # unit template -> true (an object of it appeared)
var _upgrades_ordered := {}
var _builder_site := {}   # builder id -> the id of the site it was last given
var _sites := {}           # structure id -> {template, frame, builder}: a placed site that must finish
var _base := Vector2()
var _enemy_bases: Array = []
var _shots_taken := 0
var _shot_sets := {}       # templates whose selected command bar was photographed
var _next_shot_frame := 0
var _known_ids := {}
var _attack_waves := 0
var _spells_cast := 0
var _cash := 0             # --qa-cash: a test hook's one grant at the start (outside the input paths; logged)
var _victory := false
var _defeat := false
# lane QA-2: the motion invariants (_probe_motion)
const MOTION_PROBE_FRAMES := 10       # logic frames between two probes
const MOTION_STILL := 0.25            # a unit that moved less than this between two probes stands still
const TREADMILL_PROBES := 3           # still with MOVING in this many probes in a row: the run animation plays in place
const STUCK_FRAMES := 150             # 30 s of game time: the AI says moving, the unit stands still
const MOTION_DETAIL_CHECKS := 120     # get_object calls per probe at most (the still units, round robin)
var _motion_players := []
var _motion_last := {}     # id -> [frame, position]
var _treadmill := {}       # id -> probes in a row standing still with MOVING
var _stuck_since := {}     # id -> the frame since which the AI says moving while the unit stands still
var _stuck_reported := {}
var _next_motion_probe := 0
var _motion_structures := {}  # player -> its structures at the last probe
var _stuck_samples := []      # the first 40 stuck units: where, which state, the nearest own structure


## the owner's structure nearest to a unit (template and distance), for the stuck samples
func _nearest_structure(o: Dictionary) -> String:
	var best := ""
	var best_d := 1.0e20
	for s in _motion_structures.get(o.player, []):
		var d := Vector2(s.x, s.y).distance_to(Vector2(o.x, o.y))
		if d < best_d:
			best_d = d
			best = s.template
	return "%s at %.0f" % [best, best_d] if not best.is_empty() else ""


func setup(game: Node, world: Node, hud: Node, local_name: String, options: Dictionary) -> void:
	_game = game
	_world = world
	_hud = hud
	_local = local_name
	_screens = options.get("screens", "")
	_tag = options.get("tag", "qa")
	_end_frame = int(options.get("minutes", 20.0) * 60.0 * 5.0)
	_cash = int(options.get("cash", 0))


# ---- reporting ----------------------------------------------------------------------------------------------------------------------------------------

func issue(kind: String, subject: String, detail: String) -> void:
	var key := kind + "|" + subject
	if _issues.has(key):
		_issues[key].count += 1
		return
	_issues[key] = {"kind": kind, "subject": subject, "detail": detail, "count": 1, "frame": _world.get_frame()}
	print("QA ISSUE %s | %s | %s (logic frame %d)" % [kind, subject, detail, _world.get_frame()])


func count(name: String, n := 1) -> void:
	_counts[name] = _counts.get(name, 0) + n


func _shot(label: String) -> void:
	if _screens.is_empty() or DisplayServer.get_name() == "headless":
		return
	DirAccess.make_dir_recursive_absolute(_screens)
	var image := get_viewport().get_texture().get_image()
	var path := "%s/qa1-%s-%s.png" % [_screens, _tag, label]
	image.save_png(path)
	_shots_taken += 1
	print("QA screenshot ", path)


# ---- waiting ---------------------------------------------------------------------------------------------------------------------------------------------

## at least n render frames and n / 60 s: the HUD's camera and input run on 30 client frames a second, a headless window renders hundreds a second
func _frames(n: int) -> void:
	var until := Time.get_ticks_msec() + int(n * 1000.0 / 60.0)
	for i in n:
		await get_tree().process_frame
	while Time.get_ticks_msec() < until:
		await get_tree().process_frame


## waits until the logic ran `n` more frames; a logic that does not move for 20 s of real time is a stall (reported once per stall)
func _wait_logic(n: int, stop_on_end := true) -> void:
	var target: int = _world.get_frame() + n
	var last: int = _world.get_frame()
	var since := Time.get_ticks_msec()
	while _world.get_frame() < target:
		await get_tree().process_frame
		if not is_instance_valid(_world) or (stop_on_end and _game_over()):
			return
		var f: int = _world.get_frame()
		if f != last:
			last = f
			since = Time.get_ticks_msec()
		elif Time.get_ticks_msec() - since > 20000 and not _world.is_paused():
			_stalls += 1
			issue("stall", "logic", "the logic frame stayed at %d for 20 s of real time" % f)
			return


func _game_over() -> bool:
	return _victory or _defeat


# ---- lane QA-2: the motion invariants --------------------------------------------------------------------------------------------------------------------

func _process(_delta: float) -> void:
	if is_instance_valid(_world) and _world.get_frame() > 0:
		_probe_motion()


## every MOTION_PROBE_FRAMES logic frames: which units stand still, and of those whether the logic still shows them running (MOVING) or moving (the AI)
func _probe_motion() -> void:
	var f: int = _world.get_frame()
	if f < _next_motion_probe:
		return
	_next_motion_probe = f + MOTION_PROBE_FRAMES
	if _motion_players.is_empty():
		for p in _world.get_economy().get("players", []):
			if p.playable:
				_motion_players.append(p.name)
	var still := []
	_motion_structures = {}
	for n in _motion_players:
		for o in _world.get_player_objects(n):
			if o.structure:
				if not _motion_structures.has(n):
					_motion_structures[n] = []
				_motion_structures[n].append(o)
				continue
			o["player"] = n
			var pos := Vector2(o.x, o.y)
			var last = _motion_last.get(o.id)
			_motion_last[o.id] = [f, pos]
			if last == null or f - int(last[0]) > 3 * MOTION_PROBE_FRAMES:
				continue
			if pos.distance_to(last[1]) < MOTION_STILL:
				still.append(o)
			else:
				_treadmill.erase(o.id)
				_stuck_since.erase(o.id)
	count("motion_probes")
	count("motion_still_samples", still.size())
	var checks := 0
	for o in still:
		# the units already in a streak first, then the others until the per-probe budget is spent
		if checks >= MOTION_DETAIL_CHECKS and not _treadmill.has(o.id) and not _stuck_since.has(o.id):
			continue
		checks += 1
		var full: Dictionary = _world.get_object(o.id)
		if not full.get("ok", false) or not full.get("members", []).is_empty():
			continue # gone, or a horde (its members carry the drawn bodies)
		var conditions: Array = full.get("conditions", [])
		if conditions.has("MOVING"):
			var n: int = _treadmill.get(o.id, 0) + 1
			_treadmill[o.id] = n
			if n == TREADMILL_PROBES:
				count("treadmill_units")
				# which AI state leaves the flag on (idle = a stale MOVING; a move state = blocked while walking; attacking)
				count("treadmill_ai_state_%d%s" % [int(full.get("ai_state", -1)), "_attacking" if conditions.has("ATTACKING") else ""])
				issue("treadmill", o.template, "stands still for %d logic frames with MOVING set (conditions %s, AI state %d, AI moving %s)" % [
					(TREADMILL_PROBES) * MOTION_PROBE_FRAMES, str(conditions), int(full.get("ai_state", -1)), str(full.get("moving", false))])
		else:
			_treadmill.erase(o.id)
		if full.get("moving", false):
			if not _stuck_since.has(o.id):
				_stuck_since[o.id] = f
			elif f - int(_stuck_since[o.id]) >= STUCK_FRAMES and not _stuck_reported.has(o.id):
				_stuck_reported[o.id] = true
				count("stuck_units")
				count("stuck_ai_state_%d" % int(full.get("ai_state", -1)))
				var near := _nearest_structure(o)
				if _stuck_samples.size() < 40:
					_stuck_samples.append({"frame": f, "player": o.player, "template": o.template, "x": snappedf(o.x, 1.0), "y": snappedf(o.y, 1.0),
						"ai_state": int(full.get("ai_state", -1)), "conditions": conditions, "near": near})
					print("QA stuck sample: %s" % JSON.stringify(_stuck_samples[-1]))
				issue("stuck_moving", o.template, "the AI says moving, the unit stood still at (%.0f, %.0f) for %d logic frames (AI state %d, conditions %s)" % [
					o.x, o.y, f - int(_stuck_since[o.id]), int(full.get("ai_state", -1)), str(conditions)])
		else:
			_stuck_since.erase(o.id)


# ---- input: the player's hands ------------------------------------------------------------------------------------------------------------------------

## the camera goes there (View::lookAt, as the radar click) and the player waits until the view settled on it (the drawn camera eases between client frames)
func _look(p: Vector2) -> void:
	_hud.camera_look_at(p)
	var centre: Vector2 = get_viewport().get_visible_rect().size * 0.5
	var until := Time.get_ticks_msec() + 2000
	await _frames(2)
	while Time.get_ticks_msec() < until:
		var px: Vector2 = _hud.world_to_pixel(p)
		if px.distance_to(centre) < 40.0:
			break
		await get_tree().process_frame
	await _frames(1)


func _click_pixel(px: Vector2, button := 1) -> void:
	_hud.inject_mouse_move(px)
	await _frames(2)
	# a quick click (Mouse.ini DragToleranceMS 150: a slow window must not turn it into a drag)
	_hud.inject_mouse_button(button, true, px, false)
	await get_tree().process_frame
	_hud.inject_mouse_button(button, false, px, false)
	await _frames(3)


func _click_world(p: Vector2, button := 1) -> void:
	await _click_pixel(_hud.world_to_pixel(p), button)


func _key(dik: int) -> void:
	_hud.inject_key(dik, true)
	await _frames(2)
	_hud.inject_key(dik, false)
	await _frames(3)


## the player's click on an object: a right click on open ground first drops the selection (BFME's default mouse: with a builder selected a click on our
## building would be a context order), then the camera goes there and a left click lands where the object is drawn now; true when the HUD selected it
## (or, for a horde member or a castle's shell, the object the HUD selects for it: a selected object of ours close to the click)
func _select(o: Dictionary) -> bool:
	if not _hud.get_selection().is_empty():
		await _deselect()
	await _look(Vector2(o.x, o.y))
	var first := ""
	# the centre of where it is drawn, then (as a player clicking again on another part of it) points around it
	# (the camera looks north: +y offsets are the parts of a building higher on the screen, above whatever stands in front of it)
	for offset in [Vector2(), Vector2(0, 15), Vector2(0, 30), Vector2(18, 25), Vector2(-18, 25), Vector2(0, 45), Vector2(25, 0), Vector2(-25, 0), Vector2(0, -12)]:
		var at := Vector2(o.x, o.y)
		# a horde draws nothing itself: the player clicks one of its soldiers
		var target_id: int = o.id
		var members: Array = _world.get_object(o.id).get("members", [])
		if not members.is_empty():
			target_id = int(members[0])
		var pose: Dictionary = _world.get_render_pose(target_id)
		if pose.get("ok", false):
			at = Vector2(pose.x, pose.y)
		at += offset
		var px: Vector2 = _hud.world_to_pixel(at)
		_hud.inject_mouse_move(px)
		await _frames(1)
		_hud.inject_mouse_button(1, true, px, false)
		_hud.inject_mouse_button(1, false, px, false)
		await _frames(4)
		var sel: Array = _hud.get_selection()
		var container := int(_world.get_object(o.id).get("contained_by", 0))
		var ok := false
		for s in sel:
			var so: Dictionary = _world.get_object(s)
			# the object, the horde of a member, or the object standing on the same spot that the HUD selects for a castle's shell
			if int(s) == int(o.id) or int(s) == container or (not members.is_empty() and int(s) == int(o.id)) or (so.get("ok", false) and so.owner == _local and Vector2(so.x, so.y).distance_to(Vector2(o.x, o.y)) < 8.0):
				ok = true
		if ok:
			if not first.is_empty():
				issue("select_needed_retry", o.template, first)
			return true
		if first.is_empty():
			var got := []
			for s in sel:
				var so2: Dictionary = _world.get_object(s)
				got.append("%s at (%.0f, %.0f)" % [so2.get("template", "?"), so2.get("x", 0.0), so2.get("y", 0.0)])
			first = "a left click on the middle of %s at (%.0f, %.0f) (pixel %s) selected %s" % [o.template, at.x, at.y, str(px), str(got)]
		if not _hud.get_selection().is_empty():
			await _deselect()
	issue("select_failed", o.template, first + "; clicks around it did not select it either")
	return false


## BFME's default mouse (alternate mouse off): a right click drops the selection
func _deselect() -> void:
	var p: Vector2 = _hud.world_to_pixel(_hud.get_camera().get("position", _base))
	# a right click in the middle of the screen; if the selection holds on, that is a finding
	await _click_pixel(p, 2)
	if not _hud.get_selection().is_empty():
		issue("deselect_failed", "right click", "a right click on the ground left %d objects selected" % _hud.get_selection().size())


## a click on the Palantir position that shows the button (AptPalantir::syncFrames: the movie's own button)
func _press(b: Dictionary) -> bool:
	var w: Dictionary = {}
	for attempt in 10:
		w = _hud.find_button_window(b.frame)
		if w.get("found", false):
			break
		await _frames(3) # the movie rebuilds its buttons after a selection change
	if not w.get("found", false):
		issue("button_window_missing", b.name, "the Palantir has no clip %s for the button (command %s) after 30 frames" % [b.frame, b.command])
		return false
	await _click_pixel(Vector2(w.x, w.y))
	return true


func _own_objects() -> Array:
	return _world.get_player_objects(_local)


# ---- the game -------------------------------------------------------------------------------------------------------------------------------------------

func play() -> Dictionary:
	var start_frame: int = _world.get_frame()
	_end_frame += start_frame
	for o in _own_objects():
		if o.commandcenter:
			_base = Vector2(o.x, o.y)
	if _base == Vector2():
		var objs := _own_objects()
		if not objs.is_empty():
			_base = Vector2(objs[0].x, objs[0].y)
		issue("no_command_center", _local, "the local player starts without a COMMANDCENTER object")
	var eco: Dictionary = _world.get_economy()
	if _cash > 0:
		for p in eco.get("players", []):
			if p.name == _local:
				_world.give_money(int(p.index), _cash)
				print("QA TEST HOOK: %s was given %d (--qa-cash: the scripted army grows fast enough to win in a short run)" % [_local, _cash])
	for p in eco.get("players", []):
		if p.playable and p.name != _local:
			var objs: Array = _world.get_player_objects(p.name)
			for o in objs:
				if o.commandcenter:
					_enemy_bases.append({"player": p.name, "pos": Vector2(o.x, o.y)})
					break
	print("QA play: %s at (%.0f, %.0f), %d enemy bases, until logic frame %d" % [_local, _base.x, _base.y, _enemy_bases.size(), _end_frame])
	await _look(_base)
	_shot("start")
	if OS.get_environment("QA_DEBUG") == "2":
		await _probe_deselect()
	var round := 0
	while _world.get_frame() < _end_frame and not _game_over():
		round += 1
		await _survey()
		await _economy()
		await _production()
		await _spells()
		await _abilities()
		await _army()
		await _check_production()
		_check_end()
		if _world.get_frame() >= _next_shot_frame:
			_next_shot_frame = _world.get_frame() + 5 * 90
			await _look(_base)
			_shot("f%05d" % _world.get_frame())
		await _wait_logic(15)
		if _stalls >= 3:
			break
	return summary()


func _check_end() -> void:
	var v: Dictionary = _world.get_victory_report()
	if v.get("local_victorious", false):
		_victory = true
	if v.get("local_defeated", false):
		_defeat = true


# every object the player owns, once: which command set each template shows (selecting one of each kind)
func _survey() -> void:
	var seen_templates := {}
	for o in _own_objects():
		if _known_ids.has(o.id):
			continue
		_known_ids[o.id] = true
		count("objects_seen")
		if not _produced.has(o.template):
			_produced[o.template] = true
		if _sets_seen.has(o.template) or seen_templates.has(o.template):
			continue
		seen_templates[o.template] = true
		var full: Dictionary = _world.get_object(o.id)
		if full.get("under_construction", false) or int(full.get("contained_by", 0)) != 0 or not full.has("health"):
			continue # a member inside its horde, a projectile (no body)
		if not await _select(o):
			_sets_seen[o.template] = ""
			continue
		await _frames(3)
		var st: Dictionary = _hud.get_state()
		var set_name: String = st.get("control_bar", {}).get("command_set", "")
		_sets_seen[o.template] = set_name
		var buttons: Array = _hud.get_command_buttons()
		for b in buttons:
			_note_button(b, set_name)
			if b.command == "DOZER_CONSTRUCT":
				_builders[o.template] = true
		if set_name.is_empty() and not o.structure:
			issue("no_command_set", o.template, "selected, but the command bar shows no command set")
		print("QA survey %s: set %s, %d buttons" % [o.template, set_name, buttons.size()])
		if OS.get_environment("QA_DEBUG") == "1":
			var sel_t := []
			for sid in _hud.get_selection():
				sel_t.append(_world.get_object(sid).get("template", "?"))
			print("QA debug clicked id %d %s at %s px %s camera %s; selected templates %s" % [o.id, o.template, Vector2(o.x, o.y), _hud.world_to_pixel(Vector2(o.x, o.y)), JSON.stringify(_hud.get_camera().get("position", "")), sel_t])
			print("QA debug log tail ", _hud.get_message_log().slice(-4), " pixel_to_world ", _hud.pixel_to_world(_hud.world_to_pixel(Vector2(o.x, o.y))))
			print("QA debug selection ", _hud.get_selection(), " logic ", _hud.get_logic_selection(), " state ", JSON.stringify(st))


func _note_button(b: Dictionary, set_name: String) -> void:
	if not _buttons.has(b.name):
		_buttons[b.name] = {"command": b.command, "presses": 0, "effects": 0, "template": b.template, "upgrade": b.upgrade, "power": b.power, "sets": {}, "states": {}}
	_buttons[b.name].sets[set_name] = true
	_buttons[b.name].states[str(b.state)] = true


func _local_index() -> int:
	for p in _world.get_economy().get("players", []):
		if p.name == _local:
			return int(p.index)
	return -1


func _money() -> int:
	for p in _world.get_economy().get("players", []):
		if p.name == _local:
			return int(p.money)
	return 0


# ---- economy: the builders put up every structure their command set offers ----------------------------------------------------------------------------

## a builder that still has an unfinished site of its own is busy (its AI reports idle while it works: a new order would leave the site)
func _builder_busy(id: int) -> bool:
	var site_id: int = _builder_site.get(id, 0)
	if site_id == 0:
		return false
	var site: Dictionary = _world.get_object(site_id)
	if site.get("ok", false) and site.get("under_construction", false):
		return true
	_builder_site.erase(id)
	return false


## a site nobody works on any more (no progress for 30 s): a free builder is selected and the player clicks the site (MSG_RESUME_CONSTRUCTION)
func _resume_stalled() -> bool:
	for id in _sites.keys():
		var site: Dictionary = _sites[id]
		var o: Dictionary = _world.get_object(id)
		if not o.get("ok", false) or not o.get("under_construction", false):
			continue
		var pct: float = o.get("construction_percent", 0.0)
		if pct != site.get("last_pct", -1.0):
			site["last_pct"] = pct
			site["last_change"] = _world.get_frame()
			continue
		if _world.get_frame() - site.get("last_change", _world.get_frame()) < 5 * 30:
			continue
		for b in _own_objects():
			# a free builder, or the one this site was given (it stands idle beside it: it gets the same site again)
			if not _builders.has(b.template) or (_builder_busy(b.id) and int(_builder_site.get(b.id, 0)) != int(id)):
				continue
			if not await _select(b):
				continue
			await _look(Vector2(o.x, o.y))
			var before: int = _hud.get_message_log().size()
			await _click_world(Vector2(o.x, o.y))
			var log: PackedStringArray = _hud.get_message_log()
			var sent := false
			for i in range(before, log.size()):
				sent = sent or log[i].begins_with("MSG_RESUME_CONSTRUCTION")
			if sent:
				count("sites_resumed")
				_builder_site[b.id] = id
				site["last_change"] = _world.get_frame()
				print("QA resume %s (%.0f%%) with %s" % [site.template, pct, b.template])
			else:
				issue("resume_not_sent", site.template, "a builder selected, a click on our %s at %.0f%% sent %s, not MSG_RESUME_CONSTRUCTION" % [site.template, pct,
					str(log.slice(before))])
			return true
	return false


func _economy() -> void:
	if await _resume_stalled():
		return
	var builder: Dictionary = {}
	var tried := 0
	for o in _own_objects():
		if _builders.has(o.template) and not _builder_busy(o.id):
			var full: Dictionary = _world.get_object(o.id)
			# one hidden behind a building is skipped for the next
			if full.get("ok", false) and int(full.get("contained_by", 0)) == 0:
				tried += 1
				if await _select(o):
					builder = o
					break
				if tried >= 3:
					break
	if builder.is_empty():
		return
	var buttons: Array = _hud.get_command_buttons()
	for b in buttons:
		_note_button(b, _sets_seen.get(builder.template, ""))
	# the next structure: the first button of the builder (every retail faction's economy building) alternates with every other structure once in the
	# bar's order, then more economy buildings up to six
	var economy: Dictionary = {}
	var others: Array = []
	for b in buttons:
		if b.command != "DOZER_CONSTRUCT" or b.template.is_empty() or int(b.state) == 2:
			continue
		if economy.is_empty():
			economy = b
		else:
			others.append(b)
	var choice: Dictionary = {}
	var econ_count: int = _placed.get(economy.get("template", ""), 0)
	var other_count := 0
	for b in others:
		other_count += _placed.get(b.template, 0)
	if not economy.is_empty() and econ_count < mini(6, 1 + other_count):
		choice = economy
	else:
		for b in others:
			if not _placed.has(b.template):
				choice = b
				break
	if choice.is_empty() and not economy.is_empty() and econ_count < 6:
		choice = economy
	if choice.is_empty():
		return
	if int(choice.state) == 3:
		count("build_waiting_for_money")
		return
	await _place(choice, builder)


func _place(b: Dictionary, builder: Dictionary) -> void:
	var before_log: int = _hud.get_message_log().size()
	if not await _press(b):
		return
	_buttons[b.name].presses += 1
	await _frames(4)
	var pl: Dictionary = _hud.get_placement()
	if not pl.get("placing", false):
		issue("build_button_no_placement", b.name, "pressing the %s button (state %d) did not start the placement of %s" % [b.command, b.state, b.template])
		return
	# a ring search for a legal site around the base: the ghost is moved there and the HUD's own verdict is read
	var site := Vector2()
	var found := false
	var ghost_seen := false
	var n: int = _placed.get(b.template, 0) + _placed.size()
	for ring in range(2, 10):
		for k in 10:
			var a := TAU * float(k + n * 3) / 10.0
			var p := _base + Vector2(cos(a), sin(a)) * 70.0 * ring
			await _look(p)
			_hud.inject_mouse_move(_hud.world_to_pixel(p))
			await _frames(2)
			pl = _hud.get_placement()
			if pl.get("has_ghost", false):
				ghost_seen = true
			if pl.get("placing", false) and int(pl.legal) == 0:
				site = p
				found = true
				break
		if found:
			break
	if not found:
		issue("no_legal_site", b.template, "no legal site within 630 of the base for %s (last legal code %s)" % [b.template, str(pl.get("legal", "?"))])
		await _click_pixel(_hud.world_to_pixel(_base), 2) # a right click leaves the placement
		await _key(DIK_ESCAPE)
		return
	if not ghost_seen:
		issue("placement_no_ghost", b.template, "the placement of %s never showed a ghost model over the terrain" % b.template)
	await _click_pixel(_hud.world_to_pixel(site))
	await _frames(4)
	var log: PackedStringArray = _hud.get_message_log()
	if log.size() <= before_log:
		issue("placement_no_message", b.template, "a click on a legal site sent no construct message")
		return
	_placed[b.template] = _placed.get(b.template, 0) + 1
	_buttons[b.name].effects += 1
	count("structures_ordered")
	print("QA build %s at (%.0f, %.0f) by %s: %s" % [b.template, site.x, site.y, builder.template, log[log.size() - 1]])
	# did a site appear?
	var appeared := false
	for i in 30:
		await _wait_logic(2)
		for o in _own_objects():
			if o.template == b.template and Vector2(o.x, o.y).distance_to(site) < 80.0:
				appeared = true
				if not _sites.has(o.id):
					_sites[o.id] = {"template": b.template, "frame": _world.get_frame(), "builder": builder.template}
					_builder_site[int(builder.id)] = int(o.id)
				break
		if appeared:
			break
	if not appeared:
		issue("structure_not_started", b.template, "60 logic frames after the order no %s stands near the site" % b.template)


# ---- production: every structure's buttons (units, upgrades, heroes, powers) ---------------------------------------------------------------------------

func _production() -> void:
	var by_template := {}
	for o in _own_objects():
		if not o.structure or by_template.has(o.template):
			continue
		var full: Dictionary = _world.get_object(o.id)
		if full.get("under_construction", false):
			continue
		by_template[o.template] = o
	for t in by_template.keys():
		if _game_over():
			return
		var o: Dictionary = by_template[t]
		if not await _select(o):
			continue
		var buttons: Array = _hud.get_command_buttons()
		var set_name: String = _hud.get_state().get("control_bar", {}).get("command_set", "")
		if not _sets_seen.has(o.template) or not _shot_sets.has(o.template):
			_shot_sets[o.template] = true
			await _frames(20) # the Palantir's buttons and their images settle
			_shot("sel-" + o.template)
		_sets_seen[o.template] = set_name
		var pressed := 0
		for b in buttons:
			_note_button(b, set_name)
			if b.command in SKIP_COMMANDS or b.command == "DOZER_CONSTRUCT":
				continue
			if int(b.state) != 1:
				continue
			# a unit: one at a time per button; an upgrade: once
			if b.command == "UNIT_BUILD" or b.command == "SECONDARY_UNIT_BUILD":
				if int(b.queued) > 0 or _money() < max(int(b.cost), 0):
					continue # one at a time per button: the army grows as fast as the money allows
				if _builders.has(b.template):
					var builders := 0
					for x in _own_objects():
						builders += 1 if x.template == b.template else 0
					if builders >= 3:
						continue
			elif b.command in ["PLAYER_UPGRADE", "OBJECT_UPGRADE", "CASTLE_UPGRADE"]:
				if _upgrades_ordered.has(b.name):
					continue
			elif b.command == "SET_RALLY_POINT":
				if _buttons[b.name].presses > 0:
					continue
			# the bar as it is now (a press before this one spent money or filled a queue)
			var now_b: Dictionary = {}
			for nb in _hud.get_command_buttons():
				if nb.name == b.name:
					now_b = nb
			if now_b.is_empty() or int(now_b.state) != 1:
				continue
			await _press_and_judge(now_b, o)
			pressed += 1
			if pressed >= 3:
				break
			# the bar may have changed (a press refreshes it)
			await _frames(2)


func _press_and_judge(b: Dictionary, source: Dictionary) -> void:
	var before_log: int = _hud.get_message_log().size()
	var before_unported: Dictionary = _hud.get_report().get("unported_presses", {}).duplicate()
	if not await _press(b):
		return
	_buttons[b.name].presses += 1
	count("presses")
	await _frames(4)
	var log: PackedStringArray = _hud.get_message_log()
	var st: Dictionary = _hud.get_state()
	var unported: Dictionary = _hud.get_report().get("unported_presses", {})
	for k in unported.keys():
		if unported[k] != before_unported.get(k, 0):
			issue("button_unported", b.name, "the %s button of %s is not ported (ControlBar: %s)" % [b.command, source.template, k])
	if log.size() > before_log:
		_buttons[b.name].effects += 1
		if b.command == "UNIT_BUILD" or b.command == "SECONDARY_UNIT_BUILD":
			_queued[b.template] = {"frame": _world.get_frame(), "from": source.template, "button": b.name, "producer": int(source.id)}
			count("units_queued")
		elif b.command in ["PLAYER_UPGRADE", "OBJECT_UPGRADE", "CASTLE_UPGRADE"]:
			_upgrades_ordered[b.name] = {"frame": _world.get_frame(), "upgrade": b.upgrade}
			count("upgrades_ordered")
		return
	# a press that waits for a target (a power, rally point, attack-move): a click on the ground near the enemy finishes it
	if st.get("gui_command", false):
		var target := _base + Vector2(120, 60)
		if not _enemy_bases.is_empty():
			target = _base.lerp(_enemy_bases[0].pos, 0.35)
		await _look(target)
		await _click_world(target)
		await _frames(4)
		# lane QA-2: a power that wants an object (Capture Building: a structure of another player) gets a click on the nearest other structure next
		var other := {}
		if _hud.get_message_log().size() <= before_log and _hud.get_state().get("gui_command", false):
			other = _nearest_other_structure(Vector2(source.x, source.y))
			if not other.is_empty():
				await _look(Vector2(other.x, other.y))
				await _click_world(Vector2(other.x, other.y))
				await _frames(4)
		if _hud.get_message_log().size() > before_log:
			_buttons[b.name].effects += 1
			count("targeted_presses")
		else:
			issue("target_click_no_message", b.name, "the %s button (%s) asked for a target; the click on the ground%s sent nothing" % [b.command, b.power,
				(" and on the %s of %s" % [other.template, other.player]) if not other.is_empty() else ""])
			await _key(DIK_ESCAPE)
		await _look(Vector2(source.x, source.y))
		return
	if b.command == "SPECIAL_POWER" or b.command == "SPECIAL_POWER_FROM_COMMAND_CENTER":
		issue("ability_no_effect", b.name, "a ready %s button of %s (%s) neither sent a message nor asked for a target (ui messages %s)" % [b.command, source.template,
			b.power, str(st.get("messages", []).slice(-2))])
	elif b.command in DIRECT_COMMANDS:
		issue("button_no_effect", b.name, "an enabled %s button of %s (%s) sent no message (selection %s, ui messages %s)" % [b.command, source.template,
			b.template if not b.template.is_empty() else b.upgrade, str(_hud.get_selection()), str(st.get("messages", []).slice(-2))])


## lane QA-2: the structure of another player (an enemy, a neutral or civilian building) nearest to a point
func _nearest_other_structure(p: Vector2) -> Dictionary:
	var best := {}
	var best_d := 1.0e20
	for pl in _world.get_economy().get("players", []):
		if pl.name == _local:
			continue
		for o in _world.get_player_objects(pl.name):
			var d := Vector2(o.x, o.y).distance_to(p)
			if o.structure and d < best_d:
				best_d = d
				best = o
				best["player"] = pl.name
	return best


## units ordered long ago that never appeared; upgrades ordered that never completed; sites that never finished
func _check_production() -> void:
	var now: int = _world.get_frame()
	for id in _sites.keys():
		var site: Dictionary = _sites[id]
		var o: Dictionary = _world.get_object(id)
		if not o.get("ok", false):
			if site.get("seen_rising", false):
				issue("site_vanished", site.template, "our %s placed at frame %d by %s disappeared while it was rising (last %.0f%%, health %.0f)" % [site.template,
					site.frame, site.builder, site.get("last_pct", -1.0), site.get("last_health", -1.0)])
			_sites.erase(id) # destroyed (or sold)
			continue
		site["seen_rising"] = o.get("under_construction", false)
		site["last_health"] = o.get("health", -1.0)
		if not o.get("under_construction", false):
			count("structures_finished")
			_sites.erase(id)
			continue
		if now - site.frame > 5 * 150 and not site.get("reported", false):
			site["reported"] = true
			issue("construction_stalled", site.template, "placed by %s at frame %d, still %.0f%% built 150 s later (health %.0f / %.0f)" % [site.builder, site.frame,
				o.get("construction_percent", -1.0), o.get("health", -1.0), o.get("max_health", -1.0)])
	for t in _queued.keys():
		var q: Dictionary = _queued[t]
		if not _produced.has(t) and now - q.frame > 5 * 240:
			var prod: Dictionary = _world.get_production(_local_index(), q.get("producer", 0))
			# lane QA-2: a queue that waits because the player is at its command point limit is retail's rule (ProductionUpdate RW 0x8A1E02), and a
			# producer that is gone took its queue with it: neither is a stall
			var cp := ""
			var cp_available := 1.0e9
			for p in _world.get_economy().get("players", []):
				if p.name == _local:
					cp_available = float(p.cp_available)
					cp = "command points %s / %s" % [str(p.cp_used), str(p.cp_limit)]
			var kind := "unit_never_produced"
			if not prod.get("ok", false):
				kind = "unit_producer_gone"
			elif cp_available <= 0.0:
				kind = "unit_waits_command_points"
			issue(kind, t, "ordered from %s at frame %d (button %s), not on the map 240 s later; %s; the producer's queue now: %s" % [q.from, q.frame,
				q.button, cp, JSON.stringify(prod).substr(0, 400)])


# ---- abilities: every unit kind's (heroes included) powers and toggles, each kind once every three minutes -------------------------------------------------

const ABILITY_COMMANDS := ["SPECIAL_POWER", "SPECIAL_POWER_TOGGLE", "TOGGLE_WEAPON", "TOGGLE_WEAPONSET", "SWITCH_WEAPON", "HORDE_TOGGLE_FORMATION",
	"HORDE_SET_FORMATION", "TOGGLE_STANCE", "SET_STANCE", "FIRE_WEAPON", "BLOODTHIRSTY", "AUTOCAST_WEAPON"]
var _ability_due := {}     # template -> logic frame of its next turn

func _abilities() -> void:
	var seen := {}
	for o in _own_objects():
		if o.structure or _builders.has(o.template) or seen.has(o.template) or _world.get_frame() < _ability_due.get(o.template, 0):
			continue
		var full: Dictionary = _world.get_object(o.id)
		if not full.get("has_ai", false) or int(full.get("contained_by", 0)) != 0 or not full.has("health"):
			continue
		seen[o.template] = true
		_ability_due[o.template] = _world.get_frame() + 5 * 180
		if not await _select(o):
			continue
		var set_name: String = _hud.get_state().get("control_bar", {}).get("command_set", "")
		var pressed := 0
		for b in _hud.get_command_buttons():
			_note_button(b, set_name)
			if not b.command in ABILITY_COMMANDS or int(b.state) != 1:
				continue
			await _press_and_judge(b, o)
			count("abilities_pressed")
			pressed += 1
			if pressed >= 4:
				break
		if seen.size() >= 3:
			break # a few kinds per round


# ---- spells: the spell store buys a power, the spell book casts the ready ones ---------------------------------------------------------------------------

func _spells() -> void:
	var sb: Dictionary = _hud.get_spellbook_state()
	if not sb.get("ok", false):
		return
	# the store: buy the first power the movie offers
	if _hud.open_spell_store():
		await _frames(6)
		sb = _hud.get_spellbook_state()
		var store: Dictionary = sb.get("store", {})
		var states: Array = store.get("states", [])
		for i in states.size():
			if states[i] == "_active": # AptSpellStore state names RW 0xC50BF8: _active is a power the points can buy now
				if _hud.spell_store_click(i):
					count("spells_bought")
					print("QA spell store: bought button %d (%s)" % [i, store.get("sciences", [])[i] if i < store.get("sciences", []).size() else "?"])
				break
		_hud.close_spell_store()
		await _frames(4)
	sb = _hud.get_spellbook_state()
	var slots: Array = sb.get("slots", [])
	for k in slots.size():
		if slots[k] != "_up":
			continue
		var target := _base.lerp(_enemy_bases[0].pos, 0.5) if not _enemy_bases.is_empty() else _base + Vector2(100, 0)
		# the army's position is a better target for buffs; the enemy base for damage: the power decides (both are legal clicks)
		await _look(target)
		var before: int = _hud.get_message_log().size()
		_hud.press_spell_slot(k)
		await _frames(3)
		var st: Dictionary = _hud.get_spellbook_state()
		if st.get("targeting", false):
			await _click_world(target)
			await _frames(3)
		# the spell bar sends its messages to the command list itself (InGameHud::send, not the HUD's translators): a cast shows as the slot recharging
		var ok: bool = _hud.get_message_log().size() > before
		for w in 20:
			if ok:
				break
			await _wait_logic(1)
			var after: Array = _hud.get_spellbook_state().get("slots", [])
			ok = k < after.size() and after[k] != "_up"
		var power: String = st.get("target_power", "slot %d" % k)
		if ok:
			_spells_cast += 1
			count("spells_cast")
			print("QA spell cast slot %d %s" % [k, power])
		else:
			issue("spell_no_message", power, "the ready spell book slot %d (%s) sent no message" % [k, power])
			await _key(DIK_ESCAPE)
		break


# ---- the army: Q selects every unit, a click on the enemy's command centre sends them ------------------------------------------------------------------

func _army() -> void:
	var units := 0
	for o in _own_objects():
		if not o.structure and not _builders.has(o.template):
			var full: Dictionary = _world.get_object(o.id)
			if full.get("has_ai", false) and int(full.get("contained_by", 0)) == 0 and full.get("speed", 0.0) >= 0.0 and not full.get("conditions", []).has("UNSELECTABLE"):
				units += 1
	var due: bool = _world.get_frame() > 5 * 60 * 6 and units >= 6
	due = due or (_world.get_frame() > 5 * 60 * 12 and units >= 1)
	if not due or _enemy_bases.is_empty():
		return
	if _world.get_frame() < _counts.get("next_attack_frame", 0):
		return
	_counts["next_attack_frame"] = _world.get_frame() + 5 * 45
	# the target: the nearest enemy command centre (a fortress keep's death takes the whole fortress: KeepDeathKillsEverything), else the nearest structure
	var target := {}
	var best := 1.0e20
	for pass_cc in [true, false]:
		for e in _enemy_bases:
			for o in _world.get_player_objects(e.player):
				if o.structure and (o.commandcenter or not pass_cc):
					var d := Vector2(o.x, o.y).distance_squared_to(_base)
					if d < best:
						best = d
						target = o
		if not target.is_empty():
			break
	if target.is_empty():
		return
	await _look(_base)
	await _key(DIK_Q)
	await _frames(3)
	var sel: Array = _hud.get_selection()
	if sel.is_empty():
		issue("select_all_empty", "Q", "SELECT_ALL (Q) selected nothing while the player has %d units" % units)
		return
	await _look(Vector2(target.x, target.y))
	var before: int = _hud.get_message_log().size()
	# attack-move (CommandMap TOGGLE_ATTACKMOVE = A, then a click): the army fights its way to the target, also when the target is in the fog
	await _key(DIK_A)
	await _click_world(Vector2(target.x, target.y))
	await _frames(3)
	var log: PackedStringArray = _hud.get_message_log()
	var attack_move := false
	for i in range(before, log.size()):
		attack_move = attack_move or log[i].begins_with("MSG_DO_ATTACKMOVETO") or log[i].begins_with("MSG_DO_ATTACK_OBJECT")
	if log.size() <= before:
		issue("attack_no_message", target.template, "%d selected units, A and a click at the enemy %s sent no order" % [sel.size(), target.template])
	elif not attack_move:
		issue("attack_move_not_sent", "A", "A and a click at the enemy %s sent %s, not an attack-move" % [target.template, str(log.slice(before))])
	else:
		_attack_waves += 1
		count("attack_orders")
		var enemy_structures := 0
		for e in _enemy_bases:
			for eo in _world.get_player_objects(e.player):
				enemy_structures += 1 if eo.structure else 0
		var th: Dictionary = _world.get_object(target.id)
		print("QA attack wave %d: %d units at %s (health %.0f / %.0f), enemy structures %d: %s" % [_attack_waves, sel.size(), target.template, th.get("health", -1.0),
			th.get("max_health", -1.0), enemy_structures, log[log.size() - 1]])
		if _attack_waves <= 3 or _attack_waves % 5 == 0:
			await _frames(30)
			_shot("attack%d" % _attack_waves)
	await _deselect()


func _probe_deselect() -> void:
	var o: Dictionary = {}
	for x in _own_objects():
		if x.commandcenter:
			o = x
	await _select(o)
	await _frames(10)
	print("QA probe buttons ", JSON.stringify(_hud.get_command_buttons()))
	for path in ["CommandButtons.0", "_level1.CommandButtons.0", "CommandButtons.0.content", "CommandButtons.1", "SideCommandBar.ButtonSet.Button0.Button"]:
		print("QA probe find ", path, " ", JSON.stringify(_hud.find_button_window(path)))
	print("QA probe tree ", _hud.dump_tree(3))
	print("QA probe selected ", _hud.get_selection())
	var px: Vector2 = _hud.world_to_pixel(_base + Vector2(150, -150))
	_hud.inject_mouse_move(px)
	await _frames(2)
	_hud.inject_mouse_button(2, true, px, false)
	_hud.inject_mouse_button(2, false, px, false)
	await _frames(3)
	print("QA probe after same-frame right click: ", _hud.get_selection(), " state ", JSON.stringify(_hud.get_state().get("cursor")), " log ", _hud.get_message_log().slice(-3))
	await _select(o)
	_hud.inject_mouse_button(2, true, px, false)
	await _frames(1)
	_hud.inject_mouse_button(2, false, px, false)
	await _frames(3)
	print("QA probe after one-frame right click: ", _hud.get_selection())


# ---- an AI game watched (--qa-idle) --------------------------------------------------------------------------------------------------------------------

## the local side does nothing: the camera follows the fighting (the computer unit farthest from its own base), a state line per game minute, screenshots;
## until a single alliance remains or the time is up
func watch() -> Dictionary:
	_end_frame += _world.get_frame()
	for o in _own_objects():
		if o.commandcenter:
			_base = Vector2(o.x, o.y)
	var bases := {}
	var names := []
	for p in _world.get_economy().get("players", []):
		if p.playable:
			names.append(p.name)
			for o in _world.get_player_objects(p.name):
				if o.commandcenter:
					bases[p.name] = Vector2(o.x, o.y)
	print("QA watch bases: %s" % str(bases))
	var next_line := 0
	var lines_printed := 0
	var tactics_line_for := -1
	var peak_objects := 0
	while _world.get_frame() < _end_frame:
		await _wait_logic(25, false)
		if _stalls >= 3:
			break
		var v: Dictionary = _world.get_victory_report()
		_check_end()
		var far := Vector2()
		var far_d := -1.0
		var parts := []
		for n in names:
			var objs: Array = _world.get_player_objects(n)
			var structures := 0
			for o in objs:
				if o.structure:
					structures += 1
				elif bases.has(n) and n != _local:
					var d: float = Vector2(o.x, o.y).distance_to(bases[n])
					if d > far_d:
						far_d = d
						far = Vector2(o.x, o.y)
			parts.append("%s %d/%d" % [n, objs.size(), structures])
		peak_objects = maxi(peak_objects, _world.get_object_count())
		if far_d > 0.0:
			await _look(far)
		if _world.get_frame() >= next_line:
			next_line = _world.get_frame() + 5 * 60
			print("QA watch frame %d: objects %d | %s | defeated %s" % [_world.get_frame(), _world.get_object_count(), ", ".join(parts), str(v.get("defeated", []))])
			lines_printed += 1
		if _world.get_frame() >= _next_shot_frame:
			_next_shot_frame = _world.get_frame() + 5 * 120
			_shot("w%05d" % _world.get_frame())
		if lines_printed % 3 == 1 and tactics_line_for != lines_printed:
			tactics_line_for = lines_printed
			# every third state line: what the computers' tactics are doing (SkirmishAI tactics: kind, started, target)
			var tactics := []
			for t in _world.get_ai_report():
				var teams_txt := []
				for tm in t.get("teams", []):
					var at_txt := ""
					if not tm.get("members", []).is_empty():
						var m0: Dictionary = _world.get_object(int(tm.members[0]))
						at_txt = " first at (%.0f, %.0f) %s" % [m0.get("x", 0.0), m0.get("y", 0.0), "moving" if m0.get("moving", false) else "still"]
					teams_txt.append("%d members%s idle %d%s" % [tm.get("members", []).size(), " handed" if tm.get("handed_over", false) else "", int(tm.get("idle_frames", 0)), at_txt])
				tactics.append("P%d %s%s step %s/%s%s teams [%s]" % [int(t.get("player", -1)), t.get("kind", "?"), " started" if t.get("started", false) else "",
					str(t.get("step", "?")), str(t.get("step2", "?")), (" at (%.0f, %.0f)" % [t.target.x, t.target.y]) if t.get("has_target", false) else "",
					"; ".join(teams_txt)])
			print("QA watch tactics frame %d: %s" % [_world.get_frame(), ", ".join(tactics)])
		if v.get("single_alliance", false):
			print("QA watch: a single alliance remains at frame %d" % _world.get_frame())
			break
	count("peak_objects", peak_objects)
	return summary()


# ---- the summary ----------------------------------------------------------------------------------------------------------------------------------------

func summary() -> Dictionary:
	_check_end()
	var live: Dictionary = _world.get_live_report() if _world.has_method("get_live_report") else {}
	var hud_report: Dictionary = _hud.get_report()
	var game_audio: Dictionary = _world.get_audio_report()
	var fx: Dictionary = _world.get_fx_report()
	var never := []
	var dead := []
	for name in _buttons.keys():
		var b: Dictionary = _buttons[name]
		if b.presses > 0 and b.effects == 0:
			dead.append(name)
		elif b.presses == 0:
			never.append(name)
	var ups: Dictionary = {}
	for k in _upgrades_ordered.keys():
		ups[k] = _upgrades_ordered[k].upgrade
	var s := {
		"local": _local,
		"frame": _world.get_frame(),
		"victory": _victory,
		"defeat": _defeat,
		"counts": _counts,
		"stalls": _stalls,
		"structures_placed": _placed,
		"units_queued": _queued.keys(),
		"units_seen": _produced.keys(),
		"upgrades_ordered": ups,
		"buttons_pressed_without_effect": dead,
		"buttons_never_pressed": never,
		"command_sets": _sets_seen,
		"issues": _issues.values(),
		"live_errors": live.get("errors", []),
		"live_stops": live.get("stops", []),
		"stop_hits": live.get("stop_hits", {}),
		"unported_modules": live.get("unported_modules", {}),
		"hud_errors": hud_report.get("errors", []),
		"hud_unported_presses": hud_report.get("unported_presses", {}),
		"screenshots": _shots_taken,
		"stuck_samples": _stuck_samples,
		"game_audio": {"calls_without_audio": game_audio.get("calls_without_audio", 0), "calls_without_eva": game_audio.get("calls_without_eva", 0),
			"unit_voices_without_handler": game_audio.get("unit_voices_without_handler", 0), "ambient_unknown_event": game_audio.get("ambient_unknown_event", 0),
			"music_error": game_audio.get("music_error", ""), "music_track_failures": game_audio.get("music_track_failures", 0), "music_unported": game_audio.get("music_unported", {}),
			"large_group_events": game_audio.get("large_group_events", 0), "large_group_started": game_audio.get("large_group_started", 0),
			"footstep_modules": game_audio.get("footstep_modules", 0), "footsteps_played": game_audio.get("footsteps_played", 0)},
		"fx": {"setup_errors": fx.get("setup_errors", []), "missing_fx_lists": fx.get("missing_fx_lists", []), "unresolved_particle_systems": fx.get("unresolved_particle_systems", []),
			"missing_bones": fx.get("missing_bones", []), "bone_misses": fx.get("bone_misses", 0), "skipped": fx.get("skipped", {})},
	}
	return s
