## OpenBFME lane WOTR-1: the War of the Ring strategic map (RotWK's "Living World").
##
## game.gd hands over the new-game message the strategic lobby posted (Skirmish.apt in strategic mode, NewGameMessage.living_world_campaign): the lobby's
## random choices are resolved by the logic (GameWorld.prepare_new_game, retail's order), the campaign starts in LivingWorldGame (the C++ logic:
## GameLogic/LivingWorld/LivingWorldLogic.h) and this node draws Middle-earth from the retail data: LivingWorldMapInfo's MapObject (LivingMap.w3d), the
## region fills of LivingWorldRegionEffects' RegionObject (LMR_Fill) tinted with the owners' MultiplayerColor LivingWorldColor, the army banners and the
## building models of the LivingWorldArmyIcon / LivingWorldBuildingIcon blocks at the regions' HeroArmySpot / GarrisonArmySpot / BuildingSpot.
## A battle the human fights is a real skirmish game on the region's MapName (game.gd's own start path) with the armies created at the players' starts;
## its result (the surviving army members and the winner) goes back to the logic and the map returns.
##
## STOPS (docs/STOPS.md): S-1334 the strategic HUD is StrategicHUD.apt with parts of its sub-movies not ported; S-1335 the camera is not
## livingworld.txt's camera model; S-1329 the battle start and its end; S-1336 the strategic messages
## are simplified; region effects (flare-ups, unified territories), sounds, the eye tower and the clouds are not drawn.
extends Node3D
const L10n := preload("res://scripts/l10n/l10n.gd") # WOTR2 milestone 8: OpenBFME's own text (docs/L10N.md)

const PHASE_ORDERS := 0
const PHASE_BATTLES := 2
const PHASE_OWNERSHIP := 4 # the retreat phase (WOTR2 milestone 6)
const SLOT_PLAYER := 6

var game: Node                   # game.gd (shell, world, fs, audio)
var lw: RefCounted               # LivingWorldGame
var data := {}                   # LivingWorldGame.setup(): scenarios, regions, icons, colours ...
var regions: Array = []          # the scenario's region campaign (static)
var region_campaign := ""        # its name (WOTR2 milestone 5: the region help reads the shell's copy, RW 0x9737F9)
var state := {}                  # LivingWorldGame.get_strategic_state()
var local_player := 0
var lobby_message := {}
var slot_of_player: Array = []   # the lobby slot each strategic player came from
var active := false
const SESSION_VERSION := 3       # WOTR2 milestone 3: the save's session (save_map); 2 (milestone 6): a battle's save adds "battle" and "strategic_hash"; 3 (milestone 8 step 4): a LAN war's save adds "lan" (retail's mode-6 header)

var map_root: Node3D
var fill_by_region := {}         # region index -> MeshInstance3D (LMR_Fill, the pick bodies)
var region_fx: Node3D            # wotr_region_fx.gd
var map_bounds := AABB()
var army_nodes := {}             # army id -> Node3D
var army_targets := {}           # army id -> its place on the map (Vector3; the node may still be easing there)
var army_lines: Node3D           # WOTR-2: the move lines (LivingWorldMapInfo ArmyLine*)
var army_line_texture: Texture2D
var building_nodes: Array = []
var camera: Camera3D
var cam_target := Vector3.ZERO
var cam_dist := 2400.0
var cam_pitch := deg_to_rad(65.0) # livingworld.txt's second camera row: pitch 65 (the owner's RotWK screenshots open on the whole map at that angle)
var map_extent := Rect2()
var builder: RefCounted
var map_instancer: Node3D        # WOTR-2: LivingMap through the W3DInstancer
var model_cache := {}

var selected_region := -1
var selected_army := -1
var apt_army_panel := {}        # WOTR2 milestone 4: the army panel dictionary last sent (its members' keys in panel order)
var apt_army_selected := []     # the HUD's member selection (panel order) and the leader icon flag, from the last sync
var apt_army_leader := false
var hud: CanvasLayer
var last_pending_battle := -1   # WOTR2 milestone 4: the battle the camera last went to
var last_event_count := 0
var apt_hud := false              # WOTR-2: StrategicHUD.apt is up
var apt_hud_parts := 0
var apt_prompt_battle := -1      # the battle the APT prompt shows
var hud_requests_seen := 0       # the HUD requests handled so far (the scripted run waits on it)
var radial_buildings := []      # WOTR-2: the buildings of the open build radial menu, by button
var radial_region := -1
var radial_kind := ""            # WOTR-2: "build" (a free plot's buildings) or "recruit" (a building's ArmyToSpawn entries)
var radial_slot := -1            # the recruit radial's building plot
var radial_armies := []           # the recruit radial's ArmyToSpawn index per button (canBuildUnit's list)
var selected_building := -1      # WOTR-2: the selected finished building's plot (its build queue panel and recruit radial), -1 none
var apt_queue_key := []           # the build queue panel last sent to the HUD
var apt_army_key := []            # WOTR-2: the army details panel last sent to the HUD
var details_army := -1            # WOTR-2: the army the tray shows (an army selection: the armies page's click; a region click clears it)
var apt_radial_request = null    # the radial request for the next HUD sync ({} hides it)
var selected_plot := -1         # WOTR-2: the structures page's selected build plot
var radial_build_plot := -1     # WOTR2 milestone 4: the build radial's plot (its buttons build there)
var plot_from_page := false     # WOTR2 milestone 4: the plot was picked on the structures page (BuildPlotSubselection: the palantir's build buttons)
var army_from_page := false     # the army was picked on the region's armies page (its subselection: CancelArmyMoveButton only)
var selected_struct := -1       # an own building selected (its plot): the palantir's DestroyBuilding / CancelBuildingConstruction (slot 5)
var apt_command_key := ""       # the palantir buttons last sent
var war_end_at := -1            # WOTR2 milestone 4: the end splash's deadline (msec), -1 not shown
var hero_portraits := {}          # WOTR-2: hero template -> its ButtonImage (the armies page)
var apt_region_panel_key := []    # WOTR-2: the region panel last sent to the HUD ([region, owner])
var details_hidden := false      # WOTR-2: the selection details tray is closed (the palantir's magnifier, RW 0x9DB97E)
var notice_queue: Array = []     # WOTR2 milestone 5: the local player's strategic notices (LWL + 0x154: FIFO, one at a time, not saved)
var notice_up := false           # the current notice is on screen (+ 0x25 until the box's _OnHidden)
var notice_question := ""        # WOTR2 milestone 8 (step 4): the notice on screen asks Yes / No: "save" (the LAN war's save request) or "leave" (leave for now?)
var net_save_name := ""          # the pending save request's name (the Yes / No box shows it)
var net_leave_vote_open := false # the leave-for-now vote was open in the last frame (its end without the war's end: "overruled")
var net_saves: Array = []        # the LAN war saves this peer wrote ({path, md5, hash, frame}: the scripted run reads them)
var conflict_pending := {}       # WOTR2 milestone 5: the battle statistics to show (StrategicConflictResults), {} none
var banner_opened := false        # WOTR2 milestone 7: the battle's reinforcement banners (BannerUI.apt) were made
var banner_armies: Array = []     # the army of each banner slot
var banner_arrivals_seen := 0
var logic_after_results := false # the logic waits for the battle statistics to close (the resolver's ConflictResultsState, RW 0x9DF550)
var net := false                  # WOTR2 milestone 8 (step 2b): a LAN war: the orders go to the lockstep, the logic runs on its frames (lw.net_tick)
var net_online := false
var net_phase := 0
var net_resolved := {}
var net_start_stack := PackedStringArray()
var capture_armed := false # WOTR2 milestone 8 (step 3): this battle's BattleResultCapture is armed (an Exit waits for it only then)
var exit_waiting := false # WOTR2 milestone 8 (step 3): an Exit after the battle's victory waits for T_end (game.gd _quit_menu_exit)
var net_war_ended := ""   # the reason the last LAN war ended in a battle (the scripted run reads it)
var net_battle_abort := "" # (Sol review wotr2m8s3) why a LAN war's battle ended the war (a player left / dropped, or this client left it), "" none # WOTR2 milestone 8: a LAN war's shell stack at its start (the score screen's Continue shows it again)            # the battles seen resolved (index -> true) this turn: their statistics are queued when the lockstep resolves them

var conflict_up := false         # the screen is up (the resolver's ConflictResultsState until it is inactive)
var checklist_key := ""         # WOTR2 milestone 6: the checklist rows last sent
var swapper_armies: Array = []   # WOTR2 milestone 5: the unit swapper's top / bottom armies ([] closed)
var swapper_buttons_key := ""    # the button states last sent
var strategic_seconds := 0.0    # WOTR2 milestone 5: the score keeper's wall clock (+ 0x6C: outside the real-time battles, real time)
var hover_target := ""           # WOTR2 milestone 5: the map rollover's object ("army:<id>", "building:<region>:<slot>", "region:<r>", "" none)
var hover_start := 0             # its creation time (timeGetTime, RW 0x97E652)
var hover_shown := false         # its help is on the help box

# the real-time battle
var battle := {}                 # LivingWorldGame.choose_rts()
var battle_ids := {}             # "army:member" -> Array of object ids
var battle_spawned := {}         # WOTR2 milestone 6: object id -> [army id, spawn id] of every object the start placed for an army entry
var battle_stage := ""           # "", "loading", "fighting", "ending"
var battle_result := {}
var battles_fought := 0
var battle_bases: Array = []      # each battle player's start (SAGE x, y)
var battle_slot_players: Array = [] # the battle's player names by slot (the start report's slot_players)

# scripted run (--wotr): game.gd's video / test flow
var scripted := false
var log_prefix := "WOTR"


func init(game_node: Node) -> void:
	game = game_node
	name = "WarOfTheRing"


func is_active() -> bool:
	return active


func in_battle() -> bool:
	return battle_stage != ""


func _say(text: String) -> void:
	print(log_prefix, " ", text)


func _text(label: String) -> String:
	if label.is_empty():
		return ""
	var t: Dictionary = game._shell.fetch_text(label)
	return t.text if t.get("found", false) else label


# ---- start ---------------------------------------------------------------------------------------------------------------------------------

## The strategic lobby's message (living_world_campaign set): resolve the lobby, start the campaign, show the map. False on a failure (reported).
func begin_campaign(message: Dictionary, network := false) -> bool:
	lobby_message = message.duplicate(true)
	if lw == null:
		lw = ClassDB.instantiate("LivingWorldGame")
		data = lw.setup(game._world)
		if not data.get("ok", false):
			game._fail("War of the Ring: " + str(data.get("error", "")))
			return false
	# the lobby's random faction / colour / start choices: the logic resolves them in retail's order (SkirmishRandom, seeded from the game seed)
	var prep: Dictionary = game._world.prepare_new_game(message)
	if not prep.get("ok", false):
		game._fail("War of the Ring: the lobby could not be resolved: " + "\n".join(prep.get("errors", [])))
		return false
	var resolved: Dictionary = prep.resolved
	var faction_names: PackedStringArray = data.faction_names
	var setup := {"campaign": message.living_world_campaign, "seed": int(message.seed), "slots": []}
	# WOTR-2: a slot's start position is the index of the region the player picked on the setup's map preview (the scenario's region campaign, definition
	# order: RW 0x60E8DB(startPos))
	var campaign_regions: Array = []
	for sc in data.scenarios:
		if sc.name == message.living_world_campaign:
			for rc in data.region_campaigns:
				if rc.name == sc.get("region_campaign", ""):
					campaign_regions = rc.regions
	slot_of_player.clear()
	local_player = -1
	for i in range(resolved.slots.size()):
		var s: Dictionary = resolved.slots[i]
		if s.state < 2:
			continue
		var pt: int = int(s.player_template)
		if pt < 0 or pt >= faction_names.size():
			game._fail("War of the Ring: slot %d has no faction (%d)" % [i, pt])
			return false
		if s.state == SLOT_PLAYER and local_player < 0:
			local_player = setup.slots.size()
		var start_region := ""
		var sp: int = int(s.get("start_pos", -1))
		if sp >= 0 and sp < campaign_regions.size():
			start_region = str(campaign_regions[sp].name)
		# LivingWorldCampaign::CreatePlayers RW 0x932EB8 (the same slot loop for single player and a network game): the handicap level is -(slot + 0x20)
		setup.slots.append({"name": s.name, "faction": faction_names[pt], "human": s.state == SLOT_PLAYER, "ai": s.state, "team": s.team, "color": s.color,
			"start_region": start_region, "handicap": -int(s.get("handicap", 0))})
		slot_of_player.append(i)
	var started: Dictionary = lw.start(setup)
	if not started.ok:
		game._fail("War of the Ring: " + started.error)
		return false
	# WOTR2 milestone 8 (step 4): a resumed LAN war (the START's resume branch RW 0x779C1F .. 0x779D10): seat i takes the saved war's player i; every peer
	# loads its own copy (found by the save's identity) and the war's generator starts again from 0
	var resume := str(message.get("resume_save", ""))
	if network and not resume.is_empty():
		var why := _resume_lan_war(resume, int(message.get("resume_hash", 0)), message)
		if not why.is_empty():
			game._fail("War of the Ring (LAN): the saved war could not be resumed: " + why)
			return false
	if local_player < 0:
		local_player = 0
	net = network
	net_online = network and game._world.net_is_online()
	net_phase = 0
	net_resolved = {}
	net_start_stack = game._shell.shell_stack() if network else PackedStringArray()
	if network:
		# WOTR2 milestone 8 (step 2b): every peer built the same war from the START (CreatePlayers RW 0x932EB8 in slot order); the strategic map rides the
		# lockstep on the game socket; the local player is the local lobby slot's
		var nb: Dictionary = game._world.lan_strategic_begin(lw, slot_of_player, game._wotr_net_options())
		if not nb.get("ok", false):
			game._fail("War of the Ring (LAN): " + str(nb.get("error", "")))
			return false
		local_player = int(nb.local_player)
		_say("LAN war: local player %d, phase timer %d s" % [local_player, int(nb.timer)])
	lw.set_local_player(local_player) # WOTR2 milestone 8: the battle votes' sender on the strategic command stream
	_say("campaign %s started: %d players, local player %d" % [setup.campaign, setup.slots.size(), local_player])
	for p in setup.slots:
		_say("  player %s: %s team %d colour %d %s" % [p.name, p.faction, p.team, p.color, "human" if p.human else "AI"])
	var scenario := {}
	for sc in data.scenarios:
		if sc.name == setup.campaign:
			scenario = sc
	regions = []
	region_campaign = str(scenario.get("region_campaign", ""))
	for rc in data.region_campaigns:
		if rc.name == scenario.get("region_campaign", ""):
			regions = rc.regions
	_enter_map()
	return true


## WOTR2 milestone 3: the War of the Ring map's save (save_flow.gd's Save on the War of the Ring page): the strategic game (LivingWorldGame.save_state)
## and the session its battles start from (the lobby's game, each player's lobby slot, the local player, the battle count that seeds the next battle)
## -> GameWorld.save_wotr_map's result. Retail saves the map in mode 4 (GameState RW 0x6DCFDF: TheLivingWorldLogic's selection locked, single player);
## a fought battle in mode 3 (not ported: refused by the battle's save, GameWorld.save_game)
func save_map(path: String, description: String) -> Dictionary:
	if not active or in_battle():
		return {"ok": false, "error": "the War of the Ring map is not up"}
	if net:
		return {"ok": false, "error": "a LAN War of the Ring is saved when the other players agree: the quit menu's Save asks them (RW 0x629750)"} # WOTR2 milestone 8 (Sol wotr2m8s2b; step 4: the request, ask_lan_save)
	if logic_after_results:
		# the logic waits for the battle statistics to close (its phase moves on then): a save now would load in a phase nothing advances
		return {"ok": false, "error": "close the battle statistics first"}
	var s: Dictionary = lw.save_state()
	if not s.get("ok", false):
		return s
	var session := var_to_bytes({"version": SESSION_VERSION, "lobby": lobby_message, "slots": slot_of_player, "local": local_player,
		"battles": battles_fought})
	var r: Dictionary = game._world.save_wotr_map(path, {"description": description, "scenario": s.campaign, "state": s.state,
		"state_hash": s.state_hash, "session": session})
	if r.get("ok", false):
		_say("saved turn %d (state hash %d, %d bytes) to %s" % [int(s.turn), int(s.state_hash), int(r.bytes), path.get_file()])
	return r


## WOTR2 milestone 8 (step 4): this player's LAN war save of the identity `md5` in the Save folder (the session's "lan" header, retail's mode-6 header
## RW 0x6DEFD0; RotWK looks the copy up by the MD5, RW 0x81869B): {path, save, session}, {} when there is none
func find_lan_save(md5: String) -> Dictionary:
	var dir: String = game._save_flow.save_dir()
	if dir.is_empty():
		return {}
	for file in DirAccess.get_files_at(dir):
		if not file.ends_with(".obfmesave"):
			continue
		var path := dir.path_join(file)
		var save: Dictionary = game._world.read_wotr_save(path)
		if not save.get("ok", false):
			continue
		var session = bytes_to_var(save.get("session", PackedByteArray()))
		if lan_session_error(session).is_empty() and str(session.lan.md5) == md5:
			return {"path": path, "save": save, "session": session}
	return {}


## WOTR2 milestone 8 (step 4, Sol wotr2m8s4 #5): a LAN war save's session as the lobby reads it (the resume, the Load Game, a joiner's check): "" when
## it is whole, else why not. The session's version is 3 or later, its lobby slots and rules are there, every saved player names a lobby slot, and its
## "lan" header holds the MD5 (32 lower-case hex digits), the asker, the frame and one {name, human, defeated} per saved player
func lan_session_error(session) -> String:
	if typeof(session) != TYPE_DICTIONARY or typeof(session.get("version")) != TYPE_INT or int(session.version) < 3 or int(session.version) > SESSION_VERSION \
			or typeof(session.get("lobby")) != TYPE_DICTIONARY or typeof(session.get("slots")) != TYPE_ARRAY or typeof(session.get("lan")) != TYPE_DICTIONARY:
		return "is no saved LAN War of the Ring"
	var lobby: Dictionary = session.lobby
	if typeof(lobby.get("slots")) != TYPE_ARRAY or typeof(lobby.get("rules", [])) != TYPE_ARRAY:
		return "its lobby is damaged"
	for r in lobby.get("rules", []):
		if typeof(r) != TYPE_INT:
			return "its lobby rules are damaged"
	for ls in lobby.slots:
		if typeof(ls) != TYPE_DICTIONARY:
			return "its lobby slots are damaged"
	for i in session.slots:
		if typeof(i) != TYPE_INT or int(i) < 0 or int(i) >= lobby.slots.size():
			return "its players name a lobby slot that is not there"
	var lan: Dictionary = session.lan
	var md5 = lan.get("md5")
	if typeof(md5) != TYPE_STRING or str(md5).length() != 32 or not str(md5).is_valid_hex_number() or str(md5) != str(md5).to_lower() \
			or typeof(lan.get("asker")) != TYPE_INT or typeof(lan.get("frame")) != TYPE_INT or typeof(lan.get("players")) != TYPE_ARRAY \
			or lan.players.size() != session.slots.size():
		return "its LAN header is damaged"
	for p in lan.players:
		if typeof(p) != TYPE_DICTIONARY or typeof(p.get("name")) != TYPE_STRING or typeof(p.get("human")) != TYPE_BOOL or typeof(p.get("defeated")) != TYPE_BOOL:
			return "its saved players are damaged"
	return ""


## WOTR2 milestone 8 (step 4): the LAN lobby's check of a resumed war (LANAPI resumeCheck, sent to the host as this player's answer): "" when this
## player's copy is there and loads with the saved state hash, else why not (the host names the player and tells every PC)
func resume_check(md5: String, state_hash: int) -> String:
	if lw == null:
		lw = ClassDB.instantiate("LivingWorldGame")
		data = lw.setup(game._world)
		if not data.get("ok", false):
			return "the War of the Ring data did not load: " + str(data.get("error", ""))
	var found := find_lan_save(md5)
	if found.is_empty():
		return "has no copy of this saved war"
	var rules: Array = found.session.lobby.get("rules", [])
	var why: String = lw.check_lan_state(found.save.state, state_hash, found.session.slots.size(), int(rules[6]) if rules.size() > 8 else 0,
		int(rules[8]) if rules.size() > 8 else 0)
	return "" if why.is_empty() else "its copy of the saved war differs: " + why


## WOTR2 milestone 8 (step 4): the resumed war's load on this peer (after lw.start built the START's war): this player's copy, its state hash checked
## with the START's (the network clock included), its battle count; "" when it loaded
func _resume_lan_war(md5: String, state_hash: int, message: Dictionary) -> String:
	var found := find_lan_save(md5)
	if found.is_empty():
		return "this player has no copy of the saved war " + md5
	var session: Dictionary = found.session
	if session.slots.size() != slot_of_player.size():
		return "the saved war has %d players, the START seats %d" % [session.slots.size(), slot_of_player.size()]
	var rules: Array = message.get("rules", [])
	var loaded: Dictionary = lw.load_lan_state(found.save.state, state_hash, session.slots.size(), int(rules[6]) if rules.size() > 8 else 0,
		int(rules[8]) if rules.size() > 8 else 0)
	if not loaded.get("ok", false):
		return str(loaded.get("error", ""))
	battles_fought = int(session.get("battles", 0))
	print("GAME WOTR NET resumed the saved war %s from %s (state hash %d)" % [md5, found.path.get_file(), state_hash])
	return ""


## WOTR2 milestone 8 (step 4): RW 0x6B5E3E(1) > RW 0x6B5E3E(0): a human of the network war has left or was removed (the quit menu's Save is disabled then)
func war_humans_gone() -> bool:
	for p in state.get("players", []):
		if bool(p.get("human", false)) and bool(p.get("defeated", false)):
			return true
	return false


## WOTR2 milestone 8 (step 4): the answer of a Yes / No notice: the save request ("save": kind 5 Yes / kind 2 No) or the leave-for-now vote ("leave": MSG 0x6B9)
func _answer_question(kind: String, yes: bool) -> void:
	var r: Dictionary = lw.net_save_answer(yes) if kind == "save" else lw.net_leave_vote(yes)
	print("GAME WOTR NET answer %s: %s (%s)" % [kind, "yes" if yes else "no", "sent" if r.ok else str(r.error)])


## WOTR2 milestone 8 (step 4): the local player asks the others to save the LAN war (the quit menu's Save on the map: RW 0x81682C sends kind 1 with the
## name); with no other human it saves at once (the logic orders it)
func ask_lan_save(name: String) -> Dictionary:
	if not net or not active or in_battle():
		return {"ok": false, "error": "the LAN War of the Ring map is not up"}
	var r: Dictionary = lw.net_save_ask(name)
	print("GAME WOTR NET save asked: %s (%s)" % [name, "sent" if r.ok else str(r.error)])
	return r


## WOTR2 milestone 8 (step 4): a save the logic ordered at this frame: this peer writes its own copy (RW 0x6DEF4E, mode 6) under the asker's name, with
## retail's mode-6 header in the session (the lobby's slots, rules and scenario, the MD5 that identifies the save: RW 0x6DEFD0)
func _write_lan_save(order: Dictionary) -> void:
	var s: Dictionary = lw.lan_save_state()
	if not s.get("ok", false):
		push_error("WotR: the LAN war could not be saved: " + str(s.get("error", "")))
		_say("the war could not be saved: " + str(s.get("error", "")))
		return
	var name := str(order.name)
	if not name.is_valid_filename(): # the logic refuses such a name already (LivingWorldLogic::saveNameValid): never a path
		push_error("WotR: the LAN war's save name %s is not a file name: not saved" % JSON.stringify(name))
		_say("the war could not be saved: the save name is not a file name")
		return
	var session := var_to_bytes({"version": SESSION_VERSION, "lobby": lobby_message, "slots": slot_of_player, "local": local_player, "battles": battles_fought,
		"lan": {"md5": str(s.md5), "asker": int(order.asker), "frame": int(order.frame), "players": state.players.map(func(p): return {"name": str(p.name),
			"human": bool(p.human), "defeated": bool(p.defeated)})}})
	var path: String = game._save_flow.save_dir().path_join(name + ".obfmesave")
	var r: Dictionary = game._world.save_wotr_map(path, {"description": name, "scenario": s.campaign, "state": s.state, "state_hash": s.state_hash,
		"session": session, "lan": true})
	print("GAME WOTR NET saved %s: frame %d, md5 %s, hash %d (%s)" % [name, int(order.frame), str(s.md5), int(s.state_hash), "ok" if r.get("ok", false) else str(r.get("error", ""))])
	if r.get("ok", false):
		net_saves.append({"path": path, "md5": str(s.md5), "hash": int(s.state_hash), "frame": int(order.frame)})
	else:
		_say("the war could not be saved: " + str(r.get("error", "")))


## WOTR2 milestone 6 (spec Q): a save inside the battle being fought (retail's mode 3: GameState RW 0x6DCFDF, the strategic map left in a single player war):
## the battle's RTS game (GameWorld.save_wotr_battle: log and snapshot at F), the strategic game as it stood when the battle started (RTS_REQUESTED) and the
## session with the battle's host state. Only while it is fought: the start has not placed the armies yet while it loads, and its result is taken once
## it ends [port decision, stricter than retail: S-1332]
func save_battle(path: String, description: String) -> Dictionary:
	if battle_stage != "fighting":
		return {"ok": false, "error": "the War of the Ring battle can be saved only while it is fought (it is %s)" % ("loading" if battle_stage == "loading" else "ending")}
	var s: Dictionary = lw.save_state()
	if not s.get("ok", false):
		return s
	var session := var_to_bytes({"version": SESSION_VERSION, "lobby": lobby_message, "slots": slot_of_player, "local": local_player,
		"battles": battles_fought, "strategic_hash": int(s.state_hash),
		"battle": {"setup": battle, "ids": battle_ids, "spawned": battle_spawned, "slot_players": battle_slot_players, "bases": battle_bases}})
	var r: Dictionary = game._world.save_wotr_battle(path, {"description": description, "scenario": s.campaign, "state": s.state, "session": session})
	if r.get("ok", false):
		_say("saved the battle at frame %d (RTS hash %d, strategic hash %d, %d bytes) to %s" % [int(r.frame), int(r.hash), int(s.state_hash), int(r.bytes),
			path.get_file()])
	return r


## WOTR2 milestone 6: a battle's save, part one (spec Q 4, retail's order: the strategic game first): the strategic game loaded without a reseed (retail
## calls InitRandom only after a map save, RW 0x6DEA25), the session and the battle's host state; the map stays hidden. The host then loads the battle's
## RTS game (save_flow.gd); on_battle_started resumes the battle instead of reading the start report. "" when loaded, else why not
func begin_battle_from_save(save: Dictionary) -> String:
	var why := check_save(save)
	if not why.is_empty():
		return why
	var session = bytes_to_var(save.get("session", PackedByteArray()))
	var loaded: Dictionary = lw.load_state(save.state, int(session.strategic_hash), session.slots.size(), true)
	if not loaded.get("ok", false):
		return str(loaded.get("error", ""))
	var b: Dictionary = session.battle
	var st: Dictionary = lw.get_strategic_state()
	var idx: int = int(b.setup.battle)
	if idx < 0 or idx >= st.battles.size() or int(st.battles[idx].resolution) != 1:
		return "the War of the Ring battle save's battle %d is not the one fought in real time" % idx
	_enter_loaded(save, session, session.lobby, false)
	battle = b.setup.duplicate(true)
	battle_ids = b.ids.duplicate(true)
	battle_spawned = b.spawned.duplicate(true)
	battle_slot_players = b.slot_players.duplicate()
	battle_bases = b.bases.duplicate()
	battle_result = {}
	banner_opened = false
	banner_armies = []
	banner_arrivals_seen = 0
	battle_stage = "resuming"
	visible = false
	if hud != null:
		hud.visible = false
	if apt_hud:
		game._shell.strategic_hud({"kind": "close"})
		apt_hud = false
		apt_hud_parts = 0
		apt_prompt_battle = -1
	active = false
	_set_pick_bodies(false)
	_say("loaded the battle %d in %s: %d army objects, local player %d" % [idx, regions[int(battle.region)].name if int(battle.region) < regions.size() else "?",
		battle_spawned.size(), local_player])
	return ""


## WOTR2 milestone 6: a battle's save that failed to load after its strategic game did: the war is left (the host returns to the menu)
func abandon_battle() -> void:
	battle = {}
	battle_stage = ""
	battle_ids.clear()
	battle_spawned.clear()
	battle_result = {}
	active = false
	visible = false


## WOTR2 milestone 3: a War of the Ring map save (GameWorld.read_wotr_save's result) in place of any campaign: the strategic game, its session, then the
## map as after the setup. "" when the map is up, else why not (nothing changed when the save is refused before its game loads)
func begin_from_save(save: Dictionary) -> String:
	if net:
		return "a LAN War of the Ring game cannot load a save" # WOTR2 milestone 8 (Sol wotr2m8s2b): RotWK RW 0x921F83 disables Load in a network game
	var why := check_save(save)
	if not why.is_empty():
		return why
	var session = bytes_to_var(save.get("session", PackedByteArray()))
	var lobby: Dictionary = session.lobby
	# the running campaign (if any) is replaced only when the save loads, its player count the session's
	var loaded: Dictionary = lw.load_state(save.state, int(save.state_hash), session.slots.size())
	if not loaded.get("ok", false):
		return str(loaded.get("error", ""))
	return _enter_loaded(save, session, lobby)


## WOTR2 milestone 3 (review r2): everything begin_from_save checks, nothing changed (a load from a running RTS game asks first): "" when the save
## would load
func check_save(save: Dictionary) -> String:
	var session = bytes_to_var(save.get("session", PackedByteArray())) # no objects (bytes_to_var refuses them)
	var point: int = int(save.get("save_point", 2))
	if typeof(session) != TYPE_DICTIONARY or int(session.get("version", 0)) < 1 or int(session.get("version", 0)) > SESSION_VERSION \
			or typeof(session.get("lobby")) != TYPE_DICTIONARY \
			or typeof(session.get("slots")) != TYPE_ARRAY or typeof(session.get("local")) != TYPE_INT or typeof(session.get("battles")) != TYPE_INT:
		return "the War of the Ring save's session is damaged"
	if session.has("lan"):
		return "a LAN War of the Ring save is resumed from the LAN lobby's Load Game" # (Sol wotr2m8s4 #1) never loaded as a single player war
	var lobby: Dictionary = session.lobby
	if typeof(lobby.get("slots")) != TYPE_ARRAY:
		return "the War of the Ring save's session has no lobby slots"
	for i in session.slots:
		if typeof(i) != TYPE_INT or int(i) < 0 or int(i) >= lobby.slots.size() or typeof(lobby.slots[int(i)]) != TYPE_DICTIONARY:
			return "the War of the Ring save's session names a lobby slot that is not there"
	if lw == null:
		lw = ClassDB.instantiate("LivingWorldGame")
		data = lw.setup(game._world)
		if not data.get("ok", false):
			var why := str(data.get("error", ""))
			lw = null
			return "War of the Ring: " + why
	if int(session.local) < 0 or int(session.local) >= session.slots.size():
		return "the War of the Ring save's session names no local player"
	if point == 3:
		# WOTR2 milestone 6: a battle's save: the battle being fought (its setup, army objects, slot names, starts) and the strategic hash (the header's is the
		# battle's RTS hash)
		var b = session.get("battle")
		if int(session.version) < 2 or typeof(session.get("strategic_hash")) != TYPE_INT or typeof(b) != TYPE_DICTIONARY \
				or typeof(b.get("setup")) != TYPE_DICTIONARY or typeof(b.get("ids")) != TYPE_DICTIONARY or typeof(b.get("spawned")) != TYPE_DICTIONARY \
				or typeof(b.get("slot_players")) != TYPE_ARRAY or typeof(b.get("bases")) != TYPE_ARRAY:
			return "the War of the Ring battle save's session holds no battle"
		var checked_battle: Dictionary = lw.check_state(save.state, int(session.strategic_hash), session.slots.size(), true)
		if not checked_battle.get("ok", false):
			return str(checked_battle.get("error", ""))
		var setup: Dictionary = b.setup
		if typeof(setup.get("battle")) != TYPE_INT or typeof(setup.get("players")) != TYPE_ARRAY or typeof(setup.get("spawns")) != TYPE_ARRAY \
				or typeof(setup.get("player_starts")) != TYPE_ARRAY or typeof(setup.get("region")) != TYPE_INT:
			return "the War of the Ring battle save's battle setup is damaged"
		# review wotr2m6 (Sol): every battle player has its tactical name and is a player of the war; every object tag names an army of the battle
		var players: Array = setup.players
		if players.is_empty() or b.slot_players.size() < players.size() or setup.player_starts.size() != players.size():
			return "the War of the Ring battle save's battle players do not match its slots"
		for i in range(players.size()):
			if typeof(players[i]) != TYPE_INT or int(players[i]) < 0 or int(players[i]) >= session.slots.size() \
					or typeof(b.slot_players[i]) != TYPE_STRING or str(b.slot_players[i]).is_empty():
				return "the War of the Ring battle save's battle player %d is damaged" % i
		var armies := {}
		for ps in setup.player_starts:
			if typeof(ps) != TYPE_DICTIONARY or not players.has(int(ps.get("player", -1))) or typeof(ps.get("garrison")) != TYPE_INT:
				return "the War of the Ring battle save's player starts are damaged"
			armies[int(ps.garrison)] = true
		for sp in setup.spawns:
			if typeof(sp) != TYPE_DICTIONARY or typeof(sp.get("army")) != TYPE_INT:
				return "the War of the Ring battle save's army members are damaged"
			armies[int(sp.army)] = true
		for id in b.spawned.keys():
			var tag = b.spawned[id]
			if typeof(id) != TYPE_INT or typeof(tag) != TYPE_ARRAY or tag.size() != 2 or typeof(tag[0]) != TYPE_INT or not armies.has(int(tag[0])) \
					or typeof(tag[1]) != TYPE_STRING:
				return "the War of the Ring battle save's army objects are damaged"
		return ""
	var checked: Dictionary = lw.check_state(save.state, int(save.state_hash), session.slots.size())
	return "" if checked.get("ok", false) else str(checked.get("error", ""))


func _enter_loaded(save: Dictionary, session: Dictionary, lobby: Dictionary, show_map := true) -> String:
	battle = {} # a War of the Ring battle the load left (save_flow tore its game down) is over
	battle_stage = ""
	battle_ids.clear()
	battle_spawned.clear()
	battle_result = {}
	logic_after_results = false
	state = lw.get_strategic_state()
	lobby_message = lobby.duplicate(true)
	slot_of_player = session.slots.duplicate()
	local_player = int(session.local)
	lw.set_local_player(local_player)
	battles_fought = int(session.battles)
	var scenario := {}
	for sc in data.scenarios:
		if sc.name == str(save.scenario):
			scenario = sc
	regions = []
	region_campaign = str(scenario.get("region_campaign", ""))
	for rc in data.region_campaigns:
		if rc.name == scenario.get("region_campaign", ""):
			regions = rc.regions
	selected_region = -1
	selected_army = -1
	selected_building = -1
	details_army = -1
	last_event_count = 0
	_say("loaded '%s': %s turn %d, %d players, local player %d" % [str(save.get("description", "")), str(save.scenario), int(state.turn),
		state.players.size(), local_player])
	if show_map:
		_enter_map()
	return ""


## the quit menu's Exit on the War of the Ring map (AptQuitMenu's dtor RW 0x921904: the selection locked -> TheLivingWorldLogic's message 0x6B8 MSG_LW_PLAYER_SURRENDERED with the local player id RW 0x6B3557 (BFME2 0x2B2B66):
## the strategic game ends and the shell comes back): the map and its HUD go, the main menu shows
## WOTR2 milestone 4: the end of the war for this client (EndWar RW 0x6B943A): the war is decided, or the local player was defeated (the per-client test of
## RW 0x6BA994). ShowEndGameSplashScreen (RW 0x6B694D): ":VictoryDefeat" = APT:EndVictorious / APT:EndDefeat, GuiFX.apt's ShowEndGame (the good / evil splash,
## Gui_VictoryScreen / Gui_DefeatScreen, the cheer on a victory). After 60 LivingWorldLogic updates (12 s at 5 updates a second: RW 0x6B94C6) HideEndGame and
## the map is left. Not ported (S-1331): the TimeLine score screen of the War of the Ring (mode 6, AptTimeLine::CollectPlayerData RW 0x92723E: the
## scenario's DisplayVictoriousText / DisplayDefeatedText): the game goes back to the shell
func _check_war_end() -> void:
	if war_end_at >= 0 or state.is_empty():
		return
	var me: Dictionary = state.players[local_player]
	if not bool(state.get("finished", false)) and not bool(me.defeated):
		return
	var victory := not bool(me.defeated)
	var evil := bool(data.get("faction_evil", {}).get(str(me.faction), false))
	game._shell.end_game_screen({"kind": "preload"})
	var res: Dictionary = game._shell.end_game_screen({"kind": "show_end_game", "text": "APT:EndVictorious" if victory else "APT:EndDefeat", "evil": evil,
		"sound": "Gui_VictoryScreen" if victory else "Gui_DefeatScreen", "cheer": ("Gui_VictoryCheerEvil" if evil else "Gui_VictoryCheerGood") if victory else ""})
	_say("the war is over for %s: %s (%s)" % [str(me.name), "victory" if victory else "defeat", JSON.stringify(res)])
	war_end_at = Time.get_ticks_msec() + 12000


func _war_end_tick() -> void:
	if war_end_at >= 0 and Time.get_ticks_msec() >= war_end_at:
		war_end_at = -2
		game._shell.end_game_screen({"kind": "hide_end_game"})
		leave_to_shell()


func leave_to_shell() -> void:
	if net:
		lw.net_leave() # WOTR2 milestone 8: the local player's surrender goes out before the leave (StrategicSession::leave); the session is detached
		net = false
		# (Sol wotr2m8s2b) the LAN game is over: the game socket and start go and the room is left, so the next game is not a network game
		game._world.lan_strategic_end()
		game._net_state = ""
		game._restore_switched_profile(true)
	if apt_hud:
		game._shell.strategic_hud({"kind": "close"})
		apt_hud = false
		apt_hud_parts = 0
		apt_prompt_battle = -1
	visible = false
	if hud != null:
		hud.visible = false
	_set_pick_bodies(false)
	active = false
	_say("left the War of the Ring map")
	if net_start_stack.is_empty():
		game._start_stack = PackedStringArray(["MainMenu.apt"]) # INFERENCE [S-1331]: retail's CONTINUE pops back to the strategic lobby (spec P 3)
	else:
		# WOTR2 milestone 8: a LAN war's Continue shows the screens it started from again (the LAN lobby): RotWK pops the score screen off the shell stack it
		# kept through the game (Shell::pop RW 0x75DB34; TheShell + 0x9C, RW 0x925798), as after a LAN battle game
		game._start_stack = net_start_stack
		net_start_stack = PackedStringArray()
	game._state = game.State.SCORE
	# WOTR2 milestone 5: the score screen (clearGameData -> TimeLine.apt type 6, RW 0x927898); its CONTINUE leaves through game._score_continue
	if not game._shell.set_score_screen_from_living_world(lw, game._world, local_player, int(strategic_seconds)):
		_say("the War of the Ring score screen has no data")
		game._score_continue()
		return
	if not game._shell.shell_push("TimeLine.apt"):
		_say("the shell refused TimeLine.apt: " + str(game._shell.get_shell_report().errors))
		game._score_continue()
		return
	game._audio.stop_music(false)
	game._audio.play_misc("ScoreScreenMusic")
	game._install_graph_overlay()


## WOTR2 milestone 8 (step 2b): the local player has an army in the battle or owns its region (its prompt is this client's)
func _in_battle(b: Dictionary) -> bool:
	if int(b.get("attacker", -1)) == local_player or int(b.get("defender", -1)) == local_player:
		return true
	for id in b.get("armies", []):
		var a := _army(int(id))
		if not a.is_empty() and int(a.owner) == local_player:
			return true
	return false


## WOTR2 milestone 8 (step 2b): a LAN war's lockstep: the strategic frames run here at the logic rate; refused local orders are reported, a battle the
## lockstep auto-resolved shows its statistics to its players (without holding the logic: the other peers run on), a dropped or quitting network ends
## the war for this client
func _net_tick() -> void:
	if battle_stage == "loading":
		return # both peers have left strategic orders; wait for the new relay ticket before parking the map
	var t: Dictionary = lw.net_tick()
	for e in t.get("executed", []):
		if int(e.sender) == local_player and not bool(e.ok):
			_say("order refused: %s: %s" % [str(e.name), str(e.error)])
		if str(e.name) == "MSG_LW_PLAYER_SURRENDERED":
			print("GAME WOTR NET surrender: player %d at frame %d (%s)" % [int(e.sender), int(e.frame), "ok" if e.ok else str(e.error)])
	for err in t.get("errors", []):
		push_warning("War of the Ring (LAN): " + str(err))
	# WOTR2 milestone 8 (step 4): the save request's name, the saves ordered in this frame (every peer writes its own copy of the frame's state, RW
	# 0x6DEF4E), the leave-for-now vote's end
	net_save_name = str(t.get("save_name", ""))
	for o in t.get("save_orders", []):
		_write_lan_save(o)
	if bool(t.get("war_end_agreed", false)) and active:
		net_war_ended = "every player left the war for now (it is saved)"
		print("GAME WOTR NET war ended: " + net_war_ended)
		_say(net_war_ended)
		leave_to_shell()
		return
	var vote_open := bool(t.get("leave_vote_open", false))
	if net_leave_vote_open and not vote_open:
		notice_queue.append({"title": "APT:SaveGameProgress", "text": "APT:MultiplayerExitOverruled", "args": []}) # one No: the war goes on
		print("GAME WOTR NET leave vote: overruled")
	net_leave_vote_open = vote_open
	# (Sol wotr2m8s2b2) a client the others dropped while its own lockstep stalled runs no more frames: its network's quit comes first
	var nq: Dictionary = lw.net_status()
	if bool(nq.get("quit_requested", false)):
		print("GAME WOTR NET left: " + str(nq.get("quit_reason", "")))
		_say("the network game ended: " + str(nq.get("quit_reason", "")))
		leave_to_shell()
		return
	if int(t.get("frames", 0)) == 0:
		return
	# WOTR2 milestone 8 (step 3): the vote decided a real-time battle: every peer launches it (StartRealTimeBattle RW 0x6B95BB, RW 0x6497EC)
	var bs: Dictionary = lw.take_battle_setup()
	if bs.get("ok", false):
		_refresh()
		if bs.get("resolved", false):
			_say("the enemy retreated before the battle")
		else:
			_launch_battle(bs)
		return
	var turn_before: int = int(state.get("turn", -1))
	_refresh()
	if int(state.turn) != turn_before:
		net_resolved = {}
		# the turn's first frame: every peer has this state on this frame (the process test compares the lines)
		print("GAME WOTR NET turn %d frame %d hash %d" % [int(state.turn), int(lw.net_status().get("frame", -1)), int(t.get("hash", 0))])
	for i in range(state.battles.size()):
		var b: Dictionary = state.battles[i]
		if net_resolved.has(i) or int(b.get("resolution", 0)) != 2:
			continue
		net_resolved[i] = true
		if bool(b.get("results_counted", false)) and _in_battle(b):
			_queue_conflict_results(b)


func _enter_map() -> void:
	# the shell goes (the strategic HUD replaces it), the map is shown
	while game._shell.shell_stack().size() > 0:
		game._shell.shell_pop()
		game._shell.tick(0.033)
	# WOTR-2: the shell's pictures go as at any game's start (main's game.gd _shell_leave_for_game, CAMP-2: the front-end background and the shell
	# backdrop); without it the menu backdrop covered the strategic map until the first battle
	game._shell_leave_for_game()
	active = true
	if map_root == null:
		_build_map()
	visible = true
	# WOTR-2: LivingWorldMapInfo's sun and accent lights as the W3D shaders' light set (a fought battle's map set its own)
	var lit: Dictionary = lw.apply_map_lighting()
	if not lit.get("ok", false):
		push_error("WotR: " + str(lit.get("error", "")))
	_set_pick_bodies(true)
	if hud != null:
		hud.visible = true
	_open_apt_hud()
	camera.current = true
	game._state = game.State.STRATEGIC
	_refresh()
	if battle.is_empty() and not state.is_empty():
		focus_region(int(state.players[local_player].capital))
	if game._audio != null:
		var pt := _player_template(local_player)
		if not pt.is_empty() and not str(pt.get("music", "")).is_empty():
			game._audio.play_music(pt.music) # LivingWorldPlayerTemplate Music


## WOTR-2: StrategicHUD.apt (the retail HUD movie and its sub-movies: AptStrategicHUD) over the map. WOTR2 milestone 4: it is the only interface (the
## provisional Godot panel is retired): a HUD that does not open is an error (no fallback)
func _open_apt_hud() -> void:
	# a new HUD (e.g. after a fought battle) has no panels yet: send the current selection's again (Sol review wotr2m2)
	apt_region_panel_key = []
	apt_queue_key = []
	apt_army_key = []
	var r: Dictionary = game._shell.strategic_hud({"kind": "open"})
	apt_hud = r.get("open", false)
	if not apt_hud:
		push_error("StrategicHUD.apt did not open: %s" % r.get("error", "not opened"))
		_say("StrategicHUD.apt: %s" % r.get("error", "not opened"))
	else:
		_say("StrategicHUD.apt open at level %d" % r.level)


func _sync_apt_hud() -> void:
	if not apt_hud or state.is_empty():
		return
	var me: Dictionary = state.players[local_player]
	# the stats (RW 0x97E280's reads of the LivingWorldPlayer: LivingWorldLogic::hudStats): the bonus lines, the resource multiplier, the power points
	var bonus: Array = me.get("hud_bonus", [0, 0, 0])
	var request := {"kind": "sync", "stats": {"treasury": me.treasury, "income": me.income, "resource_multiplier": float(me.get("resource_multiplier", 1.0)), "power_points": int(me.get("power_points", 0)), "bonus": bonus}, "turn": int(state.turn), "phase": int(state.phase)}
	if net:
		# WOTR2 milestone 8: the phase timer (RW 0x97EFB2) and STRATEGICHUD:WaitMessage (RW 0x8385A3: the local player is done, another live human is not;
		# also while another player's battle is decided); the message stays off while a notice or the statistics are up
		var ns: Dictionary = lw.net_status()
		request["time_remaining"] = int(ns.get("timer", -1))
		var pend: int = int(state.get("pending_battle", -1))
		var others_battle: bool = pend >= 0 and not _in_battle(state.battles[pend])
		request["wait_message"] = (bool(ns.get("waiting", false)) or others_battle) and _notices_idle() and not conflict_up
	# WOTR-2: the region details panel in the retail tray (StrategicDetailsRegion.swf): the selected region's map and its seven bonus lines (command
	# points, attack, defense, experience, resource, power points, treasury income); a region click opens the territory page
	# the armies page: the local player's armies with members in the region (at most 5, RegionDetailsArmiesPage WB 0x1630220): a hero's ButtonImage or
	# the garrison portrait, the faction icon, the name (the hero's, else GarrisonDisplayNameTag), command points against the player's limit
	# (INFERENCE S-1334: the caller of ShowCommandPoints was not read)
	var panel_armies := []
	if selected_region >= 0:
		for a in state.armies:
			if int(a.region) != selected_region or int(a.owner) != local_player or a.members.is_empty() or panel_armies.size() >= 5:
				continue
			var hero := str(a.get("hero", ""))
			var portrait := ""
			var army_name := ""
			if hero != "":
				if not hero_portraits.has(hero):
					hero_portraits[hero] = str(lw.template_field(game._world, hero, "ButtonImage"))
				portrait = hero_portraits[hero]
				army_name = _text(str(lw.template_field(game._world, hero, "DisplayName")))
			else:
				portrait = str(a.get("garrison_portrait", ""))
				army_name = _text(str(a.get("garrison_name_tag", "")))
			panel_armies.append({"id": int(a.id), "portrait": portrait, "type_image": str(a.get("type_image", "")), "name": army_name,
				"cp": int(a.cp), "max_cp": int(me.cp_limit)})
	# the structures page: one slot per build plot (the region's BuildingSpots), the building in it (its ConstructButtonImage and Type, turns left)
	var panel_plots := []
	var plot_name := ""
	if selected_region >= 0:
		var spots: Array = regions[selected_region].get("building_spots", [])
		for i in range(spots.size()):
			var plot := {"built": false}
			for b in state.regions[selected_region].buildings:
				if int(b.slot) == i:
					var bt := _building_template(str(b.template))
					plot = {"built": true, "portrait": str(bt.get("button_image", "")), "type": int(bt.get("type", 0)), "turns_left": int(b.turns_left)}
					if i == selected_plot:
						plot_name = _text(str(bt.get("display_name", "")))
			panel_plots.append(plot)
	var panel_key := [selected_region, int(state.regions[selected_region].owner) if selected_region >= 0 else -1, selected_army, JSON.stringify(panel_armies),
		JSON.stringify(panel_plots), selected_plot]
	# WOTR-2: a selected army of the local player shows its details in the tray (PlanningPhaseHeroArmySelection / PlanningPhaseGarrisonSelection:
	# StrategicDetailsArmyRetinue.swf) in place of the region panel
	var army_panel := {}
	if details_army >= 0:
		var own := false
		for a in state.armies:
			if int(a.id) == details_army:
				own = int(a.owner) == local_player
		if own:
			army_panel = lw.army_panel(game._world, details_army)
	apt_army_panel = army_panel
	var army_key := [details_army, JSON.stringify(army_panel)]
	if army_key != apt_army_key:
		apt_army_key = army_key
		request["army_panel"] = army_panel
		if army_panel.is_empty():
			apt_region_panel_key = [] # the region panel takes the tray back
	if panel_key != apt_region_panel_key and selected_building < 0 and army_panel.is_empty():
		apt_region_panel_key = panel_key
		if selected_region < 0:
			request["region_panel"] = {}
		else:
			var reg: Dictionary = regions[selected_region]
			request["region_panel"] = {"map": str(reg.get("map_name", "")), "owned": int(state.regions[selected_region].owner) == local_player, "page": 0,
				"armies": panel_armies, "selected_army": selected_army, "plots": panel_plots, "selected_plot": selected_plot, "selected_plot_name": plot_name,
				"fortress": bool(reg.get("auto_fort", false)), "fortress_portrait": str(reg.get("fortress_portrait", "")),
				"bonus": [int(reg.get("army_bonus", 0)), int(reg.get("attack_bonus", 0)), int(reg.get("defense_bonus", 0)), int(reg.get("experience_bonus", 0)),
					int(reg.get("resource_bonus", 0)), int(reg.get("legendary_bonus", 0)), int(reg.get("fertile_bonus", 0))]}
	# WOTR-2: a selected finished building with a SpawnArmy nugget shows its build queue in the tray (PlanningPhaseBuildingSelection::DetailsPanel,
	# StrategicDetailsBuildQueue.swf) in place of the region panel; deselected, the region panel comes back
	var queue_panel := _build_queue_panel()
	var queue_key := [selected_region, selected_building, JSON.stringify(queue_panel)]
	if queue_key != apt_queue_key:
		apt_queue_key = queue_key
		request["build_queue"] = queue_panel
	if apt_radial_request != null:
		request["radial"] = apt_radial_request
		apt_radial_request = null
	# the battle prompt (StrategicBattlePrompt.swf) for the battle the local player decides
	# WOTR2 milestone 5: the notice pump (RW 0x6B74D5): the front notice when none is up; the phase-done gate (RW 0x6B6ACC) holds the battle prompt
	if not notice_up and not notice_queue.is_empty():
		request["notice"] = notice_queue.pop_front()
		notice_up = true
		notice_question = str(request["notice"].get("question_kind", ""))
	if not conflict_pending.is_empty() and not conflict_up:
		request["conflict_results"] = conflict_pending
		conflict_pending = {}
		conflict_up = true
	var pending: int = int(state.pending_battle) if battle_stage == "" and _notices_idle() and not request.has("notice") else -1
	if pending != apt_prompt_battle:
		apt_prompt_battle = pending
		if pending >= 0:
			var opts: int = lw.resolution_options(pending, local_player) # LivingWorldBattle::GetResolutionOptions
			var pb: Dictionary = state.battles[pending]
			var enemy := int(pb.defender) if int(pb.attacker) == local_player else int(pb.attacker)
			if net and not _in_battle(pb):
				request["battle_prompt"] = {} # a LAN war: another player's battle (the wait message shows instead)
			else:
				request["battle_prompt"] = {"region": _region_name(int(pb.region)), "auto_resolve": (opts & 2) != 0, "real_time": (opts & 8) != 0,
					"retreat": (opts & 4) != 0, "ally": state.players[local_player].name, "enemy": state.players[enemy].name if enemy >= 0 else ""}
		else:
			request["battle_prompt"] = {}
	# WOTR2 milestone 5: the palantir's region stats tray for the selection's region (RegionDisplay RW 0x9DAD60: a region, an army's, a building's)
	var stats_region := selected_region
	if selected_army >= 0:
		var sa := _army(selected_army)
		stats_region = int(sa.region) if not sa.is_empty() else -1
	request["region_stats"] = lw.region_stats(local_player, stats_region) if stats_region >= 0 else {}
	var rows := _checklist_rows()
	var rows_key := JSON.stringify(rows)
	if rows_key != checklist_key:
		checklist_key = rows_key
		request["checklist"] = rows
	var buttons := _command_buttons()
	var command_key := JSON.stringify(buttons)
	if command_key != apt_command_key:
		apt_command_key = command_key
		request["command_buttons"] = buttons
	var r: Dictionary = game._shell.strategic_hud(request)
	apt_army_selected = r.get("army_selected", [])
	apt_army_leader = bool(r.get("army_leader_selected", false))
	if notice_up and not bool(r.get("notice_shown", false)):
		notice_up = false # the box's _OnHidden (RW 0x900B6D)
		if not notice_question.is_empty():
			_answer_question(notice_question, int(r.get("notice_answer", -1)) == 1)
			notice_question = ""
	conflict_up = bool(r.get("conflict_results_active", false))
	if logic_after_results and conflict_pending.is_empty() and not conflict_up:
		call_deferred("_run_logic_after_results")
	_sync_unit_swapper(r)
	for req in r.get("requests", []):
		_say("StrategicHUD request %s" % req)
		hud_requests_seen += 1
		if req == "end_turn":
			end_turn()
		elif str(req).begins_with("checklist:retreat:"):
			# the PlanRetreat row's click: the army selected as the retreat planner (and the camera on it)
			var id := int(str(req).get_slice(":", 2))
			if not _army(id).is_empty():
				select_army(id)
		elif str(req).begins_with("swapper_") and swapper_armies.size() == 2:
			_swapper_request(str(req), r.get("swapper_selected", [[], []]))
		elif req == "battle_autoresolve" and apt_prompt_battle >= 0:
			choose_battle(false)
		elif req == "battle_realtime" and apt_prompt_battle >= 0:
			choose_battle(true)
		elif str(req).begins_with("select_plot:"):
			# WOTR-2: the structures page's slot click (RegionDetailsStructuresPage: the plot selected); an empty plot of the local player's region in
			# the orders phase opens the build radial menu (BuildPlotRadialMenu::AppendButtons: the buildings the player may build there); WOTR2
			# milestone 4: an empty plot is also BuildPlotSubselection (the palantir's build buttons), a built one a building subselection (slot 5)
			selected_plot = int(str(req).get_slice(":", 1))
			plot_from_page = true
			selected_struct = selected_plot if _building_in(selected_region, selected_plot) >= 0 else -1
			_open_build_radial(selected_region, selected_plot)
			_refresh_panels()
		elif str(req).begins_with("command:"):
			_command(str(req).substr(8))
		elif str(req).begins_with("radial:"):
			var i := int(str(req).get_slice(":", 1))
			if radial_kind == "recruit":
				# QueueUnitButton (WB 0x16485F0 ..): MSG_LW_QUEUE_UNIT for the button's ArmyToSpawn; the ring stays while the building is selected
				if i >= 0 and i < radial_armies.size() and radial_region >= 0 and radial_slot >= 0:
					order_recruit(radial_region, radial_slot, int(radial_armies[i]))
					_update_recruit_radial()
			else:
				if i >= 0 and i < radial_buildings.size() and radial_region >= 0:
					order_build(radial_region, radial_buildings[i], radial_build_plot) # BuildBuildingButton: MSG_LW_BUILD_BUILDING in the plot (0x6AA)
				_close_build_radial()
		elif str(req).begins_with("bq_dequeue:") and selected_building >= 0:
			# the build queue slot's right click (MSG_LW_DEQUEUE_UNIT_AT_INDEX): the entry leaves the queue, its cost paid back (RW 0x8FF6F8)
			var qi := int(str(req).get_slice(":", 1))
			var res: Dictionary = lw.order_dequeue_at(local_player, selected_region, selected_building, qi)
			_say("dequeue %d in %s: %s" % [qi, regions[selected_region].name, "ok" if res.ok else res.error])
			_refresh()
			_update_recruit_radial()
		elif req == "army_back" and details_army >= 0:
			# the army details panel's back button (DetailsPanel::SelectRegion WB 0x15FB4B0 / 0x15FBDE0): back to the army's region
			details_army = -1
			_refresh_panels()
		elif req == "bq_back" and selected_building >= 0:
			# the build queue panel's back button (DetailsPanel::SelectRegion WB 0x15FC7A0): back to the region selection with this plot
			selected_plot = selected_building
			_deselect_building()
			_refresh_panels()
		elif str(req).begins_with("select_army:"):
			# WOTR-2: the armies page's slot click (RegionDetailsArmiesPage: the left click selects the army, WB 0x162FCB0)
			selected_army = int(str(req).get_slice(":", 1))
			details_army = selected_army # the army selection: its details in the tray
			army_from_page = true
			_refresh_panels()
		elif req == "battle_retreat" and apt_prompt_battle >= 0:
			var res: Dictionary = lw.choose_retreat(apt_prompt_battle, local_player)
			_say("retreat from battle %d: %s" % [apt_prompt_battle, "ok" if res.ok else res.error])
			_run_logic()
	# WOTR-2: the retail selection details tray (StrategicDetailsTray, opened and closed by the HUD and the palantir's magnifier) holds the details
	details_hidden = int(r.get("details_tray_state", -1)) in [0, 3]
	var parts: Dictionary = r.get("parts", {})
	if parts.size() != apt_hud_parts:
		apt_hud_parts = parts.size()
		_say("StrategicHUD parts %s" % JSON.stringify(parts))


func _player_template(p: int) -> Dictionary:
	if state.is_empty() or p < 0 or p >= state.players.size():
		return {}
	for t in data.player_templates:
		if t.name == state.players[p].template:
			return t
	return {}


# ---- the map ------------------------------------------------------------------------------------------------------------------------------

func _w3d(x: float, y: float, z: float = 0.0) -> Vector3:
	return Vector3(x, z, -y) # W3D (right-handed, Z up) -> Godot (Y up), as GodotW3DModelBuilder's W3DSpace


func _model(model_name: String) -> Node3D:
	if model_name.is_empty():
		return null
	var n: Node3D = builder.build_model(game._fs, model_name)
	var rep: Dictionary = builder.get_report()
	if n == null or not rep.get("errors", []).is_empty():
		_say("model %s: errors %s skipped %s" % [model_name, JSON.stringify(rep.get("errors", [])), JSON.stringify(rep.get("skipped", []))])
	return n


func _build_map() -> void:
	builder = ClassDB.instantiate("W3DModelBuilder")
	map_root = Node3D.new()
	map_root.name = "LivingWorldMap"
	add_child(map_root)
	var mi: Dictionary = data.map_info
	# WOTR-2: LivingMap draws through the W3DInstancer (the full W3D material path: vertex materials, the second texture stage, the UV mappers
	# that move the coast, smoke and border clouds, WaterShader.FX's colour; the W3DModelBuilder drew every surface as a plain textured material,
	# which lit the water and the border cloud pink)
	map_instancer = ClassDB.instantiate("W3DInstancer")
	map_root.add_child(map_instancer)
	var su: Dictionary = map_instancer.setup(game._fs)
	if not su.ok:
		_say("W3DInstancer setup: %s" % JSON.stringify(su.errors))
	var living_model: int = map_instancer.add_model(mi.map_object)
	if living_model >= 0:
		var rep: Dictionary = map_instancer.get_model_report(living_model)
		if not rep.errors.is_empty():
			_say("model %s: errors %s" % [mi.map_object, JSON.stringify(rep.errors)])
		var living_instance: int = map_instancer.add_instance(living_model, Transform3D.IDENTITY)
		# [S-1335] the IMPASSABLE overlays of LivingMap (a shading pass over the mountains in retail) are hidden
		var hidden := PackedStringArray()
		var cloud_border := str(mi.get("cloud_border_sub_object", "")).to_upper()
		for so in rep.sub_objects:
			var sub := str(so.name).to_upper()
			if sub.contains("IMPASSABLE"):
				hidden.append(str(so.name))
			# [S-1335] MapInfo's CloudBorderSubObject ("bordercloud"): the retail client's use of it was not found; drawn plainly it is an opaque
			# light cloud sheet over everything outside the map, where the base game shows black: hidden
			elif not cloud_border.is_empty() and sub.get_slice(".", 1) == cloud_border:
				hidden.append(str(so.name))
		# WOTR-2 [S-1335, presentation]: TextLayerSubObject ("text plane": black, the alpha holds the territory names and the territory lines) is drawn
		# faint in retail (the owner's RotWK 2.01 screenshots: the letters darken the ground to about 80%); retail's fade rule was not found: here a
		# second instance of LivingMap that shows only that layer, at opacity 0.2
		var text_layer := str(mi.get("text_layer_sub_object", "")).to_upper()
		var text_hidden := PackedStringArray()
		for so in rep.sub_objects:
			var sub := str(so.name).to_upper()
			if not text_layer.is_empty() and sub.get_slice(".", 1) == text_layer:
				hidden.append(str(so.name))
			else:
				text_hidden.append(str(so.name))
		map_instancer.set_instance_hidden_subobjects(living_instance, hidden)
		if text_hidden.size() < rep.sub_objects.size():
			var text_instance: int = map_instancer.add_instance(living_model, Transform3D.IDENTITY)
			map_instancer.set_instance_hidden_subobjects(text_instance, text_hidden)
			map_instancer.set_instance_opacity(text_instance, 0.2)
	# WOTR-2: the region effects of LivingWorldRegionEffects (wotr_region_fx.gd: LMR_Fill, LMR_Border, LMR_Highlight, LMR_Edge with their retail blends)
	var fx: Dictionary = data.region_effects[0] if data.region_effects.size() > 0 else {}
	region_fx = preload("res://scripts/wotr_region_fx.gd").new()
	map_root.add_child(region_fx)
	region_fx.build(game._fs, regions, fx)
	_bind_region_fills(region_fx.fill_meshes())
	map_extent = Rect2(mi.center.x - mi.extent.x * 0.5, mi.center.y - mi.extent.y * 0.5, mi.extent.x, mi.extent.y)
	cam_target = _w3d(mi.center.x, mi.center.y)
	cam_dist = 3600.0
	if map_bounds.size != Vector3.ZERO:
		# WOTR-2: the map's width in view (retail's opening view, the owner's screenshots): the distance at which the regions' width fills the window
		var vs := get_viewport().get_visible_rect().size
		var half_h := atan(tan(deg_to_rad(45.0) * 0.5) * vs.x / maxf(1.0, vs.y))
		cam_target = map_bounds.get_center()
		cam_target.y = 0.0
		cam_dist = (map_bounds.size.x * 0.5) / tan(half_h) * 1.02
	camera = Camera3D.new()
	camera.fov = 45
	camera.near = 5.0
	camera.far = 40000.0
	add_child(camera)
	_update_camera()
	_build_hud()


## LMR_Fill's sub-objects are named after the regions' SubObject ("LMR_FILL.AMON_SUL"; Godot turns '.' into '_'): each gets a pick body (the retail
## region pick-boxes) and an override material for the owner's colour.
func _bind_region_fills(fill_meshes: Dictionary) -> void:
	fill_by_region = fill_meshes
	map_bounds = AABB()
	var first := true
	for i in fill_by_region.keys():
		var mesh: MeshInstance3D = fill_by_region[i]
		var body := StaticBody3D.new()
		body.set_meta("region", i)
		var shape := CollisionShape3D.new()
		shape.shape = mesh.mesh.create_trimesh_shape()
		body.add_child(shape)
		mesh.add_child(body)
		var ab: AABB = mesh.global_transform * mesh.get_aabb()
		map_bounds = ab if first else map_bounds.merge(ab)
		first = false
	var missing: Array = []
	for i in range(regions.size()):
		if not fill_by_region.has(i):
			missing.append(regions[i].name)
	if missing.size() > 0:
		_say("regions without a fill sub-object: " + ", ".join(missing))


func _set_pick_bodies(on: bool) -> void:
	for mesh in fill_by_region.values():
		for b in mesh.get_children():
			if b is StaticBody3D:
				b.collision_layer = 1 if on else 0


func _owner_color(p: int) -> Color:
	if p < 0 or state.is_empty():
		return Color(0, 0, 0, 0)
	var ci: int = int(state.players[p].color)
	var colors: Array = data.colors
	if ci < 0 or ci >= colors.size():
		ci = p % maxi(1, colors.size())
	var rgb: int = int(colors[ci].living_world)
	if rgb == 0:
		rgb = int(colors[ci].rgb)
	return Color8((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)


func _ground(x: float, y: float) -> float:
	var space := get_world_3d().direct_space_state
	var q := PhysicsRayQueryParameters3D.create(_w3d(x, y, 4000.0), _w3d(x, y, -4000.0))
	var hit := space.intersect_ray(q)
	return hit.position.y if not hit.is_empty() else 0.0


func _update_camera() -> void:
	if camera == null:
		return
	_scale_banners()
	var off := Vector3(0, sin(cam_pitch), cos(cam_pitch)) * cam_dist
	camera.position = cam_target + off
	camera.look_at(cam_target, Vector3.UP)


func _process(delta: float) -> void:
	if net and battle_stage == "fighting" and net_battle_abort.is_empty() and game._world.lan_war_battle_departed():
		# retail: a player leaving during a battle (0x448 then 0x6B8) or dropped ends the war for every peer (QuitWar RW 0x6B94C6 -> DoExitMap): every
		# peer sees the departure at one frame of the battle's lockstep and leaves the battle and the war. (Sol review wotr2m8s3r3) before a waiting
		# Exit's own leave; net_finish checks again on the drained logic whatever the exit
		exit_waiting = false
		net_battle_abort = "a player left the battle"
		print("GAME WOTR NET war ended: " + net_battle_abort)
		game._exit_to_score_screen()
		return
	if net and exit_waiting and battle_stage == "fighting" and lw.battle_capture_done():
		exit_waiting = false
		print("GAME WOTR NET exit: the battle's end frame ran, leaving")
		game._quit_menu_exit_now()
		return
	if not active or battle_stage != "":
		return
	if net:
		_net_tick()
	if war_end_at == -1:
		strategic_seconds += delta # stopped by the war's end (RW 0x8F3D59)
	_check_war_end() # WOTR2 milestone 4: the end splash, then the map is left
	_war_end_tick()
	if apt_hud and Engine.get_process_frames() % 3 == 0:
		_hover_tick()
	if apt_hud and Engine.get_process_frames() % 6 == 0:
		_sync_apt_hud() # the HUD's button presses
	if region_fx != null:
		region_fx.update_gamma() # WOTR-2: the camera's gamma-space composite comes with the map's W3DInstancer (RENDER-3)
	var pan := Vector3.ZERO
	if Input.is_key_pressed(KEY_LEFT) or Input.is_key_pressed(KEY_A):
		pan.x -= 1
	if Input.is_key_pressed(KEY_RIGHT) or Input.is_key_pressed(KEY_D):
		pan.x += 1
	if Input.is_key_pressed(KEY_UP) or Input.is_key_pressed(KEY_W):
		pan.z -= 1
	if Input.is_key_pressed(KEY_DOWN) or Input.is_key_pressed(KEY_S):
		pan.z += 1
	if pan != Vector3.ZERO:
		cam_target += pan.normalized() * delta * cam_dist * 0.6
		_clamp_camera()
		_update_camera()


func _clamp_camera() -> void:
	cam_target.x = clampf(cam_target.x, map_extent.position.x, map_extent.end.x)
	cam_target.z = clampf(cam_target.z, -map_extent.end.y, -map_extent.position.y)
	cam_dist = clampf(cam_dist, 500.0, 6000.0)


func _unhandled_input(event: InputEvent) -> void:
	if not active or battle_stage != "" or game._quit_open: # WOTR2 milestone 3: the quit menu is over the map
		return
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			cam_dist *= 0.9
			_clamp_camera()
			_update_camera()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			cam_dist *= 1.1
			_clamp_camera()
			_update_camera()
		elif event.button_index == MOUSE_BUTTON_LEFT or event.button_index == MOUSE_BUTTON_RIGHT:
			# WOTR-2: a click on the strategic HUD (an Apt button under the cursor: its buttons, the tray's and the panels' backgrounds) is the HUD's,
			# not the map's [inference S-1334: retail's Apt input consumes it; the rule of what blocks was not read beyond the buttons]
			if apt_hud and String(game._shell.hit_test_stage(game._shell.window_to_stage(event.position))) != "":
				return
			_click(event.position, event.button_index == MOUSE_BUTTON_RIGHT)


func pick_region(screen: Vector2) -> int:
	var from := camera.project_ray_origin(screen)
	var to := from + camera.project_ray_normal(screen) * 50000.0
	var hit := get_world_3d().direct_space_state.intersect_ray(PhysicsRayQueryParameters3D.create(from, to))
	if hit.is_empty() or not hit.collider.has_meta("region"):
		return -1
	return int(hit.collider.get_meta("region"))


func region_screen_position(r: int) -> Vector2:
	var c: Vector2 = _region_center(r)
	return camera.unproject_position(_w3d(c.x, c.y, _ground(c.x, c.y)))


func _region_center(r: int) -> Vector2:
	var reg: Dictionary = regions[r]
	if reg.get("custom_center", false) or reg.hero_spots.is_empty():
		return reg.center
	return reg.hero_spots[0]


## WOTR2 milestone 4: the local player's army whose icon is within `radius` pixels of `screen` (the nearest), -1 none. Retail picks the army icons before
## the regions (RW 0x97FBB7, the classifier RW 0x6C0A19) and only the local player's own armies are selectable (RW 0x6B6657) [inference S-1334: the
## icon's pick box approximated by a radius around the icon]
func _army_near(screen: Vector2, radius: float, exclude: int = -1, any_owner: bool = false) -> int:
	var best := -1
	var best_d := radius
	for a in state.armies:
		if (int(a.owner) != local_player and not any_owner) or int(a.id) == exclude or not army_nodes.has(a.id):
			continue
		var node: Node3D = army_nodes[a.id]
		if not camera.is_position_in_frustum(node.global_position):
			continue
		var d := camera.unproject_position(node.global_position).distance_to(screen)
		if d <= best_d:
			best_d = d
			best = int(a.id)
	return best


func _army(id: int) -> Dictionary:
	for a in state.armies:
		if int(a.id) == id:
			return a
	return {}


## WOTR2 milestone 4: an army selection (PlanningPhaseHeroArmySelection / PlanningPhaseGarrisonSelection): its details in the tray
func select_army(id: int) -> void:
	var a := _army(id)
	if a.is_empty():
		return
	if int(a.region) != selected_region:
		_close_build_radial()
	_deselect_building()
	selected_region = int(a.region)
	selected_plot = -1
	selected_army = id
	details_army = id
	army_from_page = false
	selected_struct = -1
	plot_from_page = false
	_refresh_panels()


## WOTR2 milestone 4: the right click with an army selected (RW 0x9E1532 / 0x9F8958): a hero army whose leader icon is selected (or whose panel is not
## up) moves to the region under the mouse (MSG_LW_ARMY_MOVE_REQUEST 0x6A8; a garrison is never ordered to move by the UI); otherwise the selected
## members move to the army under the mouse or to the player's garrison in the region under it (MSG_LW_SWAP_ARMY_MEMBERS 0x6AF with an empty second
## list), refused by CanMoveArmyMembers (retail then opens the unit swapper, StrategicArmyUnitSwapper.swf: not ported, S-1334)
func _order_click(screen: Vector2) -> void:
	var a := _army(selected_army)
	if a.is_empty() or swapper_armies.size() == 2: # while the swapper is up the member translator does nothing (RW 0x9F9376's +0x40)
		return
	var panel_up := details_army == selected_army and not apt_army_panel.is_empty()
	if str(a.get("hero", "")) != "" and (apt_army_leader or not panel_up):
		var r := pick_region(screen)
		if r >= 0:
			order_move(selected_army, r)
		return
	var keys := []
	var members: Array = apt_army_panel.get("members", [])
	for i in range(min(members.size(), apt_army_selected.size())):
		if bool(apt_army_selected[i]):
			keys.append(int(members[i].get("key", 0)))
	if keys.is_empty():
		return
	keys.sort() # the message lists the entry ids ascending (RW 0xA08962)
	# WOTR2 milestone 5 (spec O A.1): the destination is any other army's icon (an enemy's fails the gate below: no fallback to the region), else the
	# region's garrison of the player
	var target := _army_near(screen, 30.0, selected_army, true)
	if target < 0:
		var r := pick_region(screen)
		for o in state.armies:
			if r >= 0 and int(o.region) == r and int(o.owner) == local_player and str(o.get("hero", "")) == "" and int(o.id) != selected_army:
				target = int(o.id)
				break
	if target < 0 or not lw.can_move_army_members_no_room(selected_army, keys, target): # MoveTo: CanMoveArmyMembersNoRoom RW 0x6B7938
		return
	# MoveArmyMembers RW 0x9F9376: without Ctrl a move that fits is sent at once; with Ctrl, or when only the command points refuse it, the swapper opens
	if not Input.is_key_pressed(KEY_CTRL) and lw.can_swap_army_members(selected_army, keys, target, []):
		var res: Dictionary = lw.swap_army_members(selected_army, keys, target, [])
		_say("move members %s of army %d to army %d: %s" % [str(keys), selected_army, target, "ok" if res.ok else res.error])
		_refresh()
		_refresh_panels()
		return
	open_unit_swapper(selected_army, keys, target)


## WOTR2 milestone 5: the army unit swapper's slot dictionary (InitMovieClipSlot RW 0xA08736)
func _swapper_slot(id: int, selected: Array) -> Dictionary:
	var a := _army(id)
	return {"army": id, "region_name": _region_name(int(a.region)), "army_name": _army_display_name(id), "cp": int(a.cp), "max_cp": int(a.get("max_cp", 0)),
		"panel": lw.army_panel(game._world, id), "selected": selected}


## StrategicInGameUI::GetDisplayName RW 0x9D8AA5: a garrison's name label, else "%s's Army" (STRATEGICHUD:HeroArmyName) with the hero's name
func _army_display_name(id: int) -> String:
	var a := _army(id)
	var hero := str(a.get("hero", ""))
	if hero == "":
		return _text(str(a.get("garrison_name_tag", "")))
	var hn := _text(str(lw.template_field(game._world, hero, "DisplayName")))
	var fmt: Dictionary = game._shell.fetch_text("STRATEGICHUD:HeroArmyName")
	return str(fmt.text).replace("%s", hn) if fmt.get("found", false) else hn


## WOTR2 milestone 6: the checklist's rows (spec T 3): in the retreat phase one required PlanRetreat row per local army in the retreat list, in list
## order (CreateChecklistItems RW 0x9813EE): "Battle lost in %s. Retreat %s from %s.", ticked once the army has a path or is gone (RW 0x9E169F)
func _checklist_rows() -> Array:
	var rows := []
	if state.is_empty() or int(state.phase) != PHASE_OWNERSHIP:
		return rows
	for id in state.get("retreat_list", []):
		var a := _army(int(id))
		if not a.is_empty() and int(a.owner) != local_player:
			continue
		var region: String = str(regions[int(a.region)].display_name) if not a.is_empty() else ""
		rows.append({"key": "retreat:%d" % int(id), "label": "STRATEGICHUD:PlanRetreatChecklistItem",
			"args": [{"label": region}, {"text": _army_display_name(int(id)) if not a.is_empty() else ""}, {"label": region}],
			"importance": 1, "checked": a.is_empty() or int(a.get("destination", -1)) >= 0})
	return rows


## WOTR2 milestone 5: the swapper each HUD sync (the dialog update RW 0xA0915A): closed at once when an army is gone or the phase left 0 (the selection
## object owns it), else its three buttons from the panels' selections (CanMoveUI RW 0xA08D0C / CanSwapUI RW 0xA08D90: the moving sets non-empty)
func _sync_unit_swapper(r: Dictionary) -> void:
	if swapper_armies.size() != 2:
		return
	if not bool(r.get("swapper_open", false)):
		swapper_armies = []
		_refresh_panels()
		return
	var top := int(swapper_armies[0])
	var bottom := int(swapper_armies[1])
	if _army(top).is_empty() or _army(bottom).is_empty() or int(state.phase) != PHASE_ORDERS or selected_army != top:
		game._shell.strategic_hud({"kind": "sync", "unit_swapper": {"close": true}})
		swapper_armies = []
		return
	var sel: Array = r.get("swapper_selected", [[], []])
	var a: Array = sel[0] if sel.size() > 0 else []
	var b: Array = sel[1] if sel.size() > 1 else []
	var buttons := [not a.is_empty() and lw.can_swap_army_members(top, a, bottom, []), not b.is_empty() and lw.can_swap_army_members(bottom, b, top, []),
		not a.is_empty() and not b.is_empty() and lw.can_swap_army_members(top, a, bottom, b)]
	var key := str(buttons)
	if key != swapper_buttons_key:
		swapper_buttons_key = key
		game._shell.strategic_hud({"kind": "sync", "unit_swapper": {"buttons": buttons}})


func _swapper_request(req: String, sel: Array) -> void:
	var top := int(swapper_armies[0])
	var bottom := int(swapper_armies[1])
	var a: Array = sel[0] if sel.size() > 0 else []
	var b: Array = sel[1] if sel.size() > 1 else []
	var res := {}
	if req == "swapper_move_down" and not a.is_empty(): # MSG_LW_SWAP_ARMY_MEMBERS 0x6AF, one per press; the swapper stays open
		res = lw.swap_army_members(top, a, bottom, [])
	elif req == "swapper_move_up" and not b.is_empty():
		res = lw.swap_army_members(bottom, b, top, [])
	elif req == "swapper_swap" and not a.is_empty() and not b.is_empty():
		res = lw.swap_army_members(top, a, bottom, b)
	elif req == "swapper_closed":
		swapper_armies = []
		_refresh_panels()
		return
	if res.is_empty():
		return
	_say("unit swapper %s: %s" % [req, "ok" if res.ok else res.error])
	_refresh()
	if res.ok:
		game._shell.strategic_hud({"kind": "sync", "unit_swapper": {"update": {"top": _swapper_slot(top, a), "bottom": _swapper_slot(bottom, b)}}})
		swapper_buttons_key = ""
	_refresh_panels()


func open_unit_swapper(top: int, keys: Array, bottom: int) -> void:
	if not apt_hud:
		return
	var r: Dictionary = game._shell.strategic_hud({"kind": "sync", "unit_swapper": {"open": {"top": _swapper_slot(top, keys), "bottom": _swapper_slot(bottom, [])}}})
	if bool(r.get("swapper_opened", false)):
		swapper_armies = [top, bottom]
		swapper_buttons_key = ""
	_say("unit swapper for armies %d and %d: %s" % [top, bottom, "open" if r.get("swapper_opened", false) else "refused"])


func _click(screen: Vector2, order: bool) -> void:
	if not state.is_empty() and int(state.phase) == PHASE_OWNERSHIP:
		_retreat_click(screen, order)
		return
	if order:
		if selected_army >= 0:
			_order_click(screen)
		return
	var army := _army_near(screen, 30.0)
	if army >= 0:
		select_army(army)
		return
	var r := pick_region(screen)
	if r < 0:
		return
	select_region(r)
	# WOTR-2: a click on a finished building of the local player that recruits (a SpawnArmy nugget) selects it (PlanningPhaseBuildingSelection
	# WB 0x15FC870: its queue panel, the recruit radial); a click on a free build plot opens the build radial menu around it
	# (PlanningPhaseBuildPlotSelection -> BuildPlotRadialMenu) [inference S-1334: retail picks the icon's own box; here within 30 pixels of the plot]
	if not order:
		var building := _recruiting_building_near(r, screen, 30.0)
		if building >= 0:
			_select_building(r, building)
			selected_struct = building
			_refresh_panels()
			return
		var other := _own_building_near(r, screen, 30.0)
		if other >= 0:
			selected_struct = other # PlanningPhaseBuildingSelection (RW 0x97F868): the palantir's slot 5
			_refresh_panels()
			return
		_deselect_building()
		var plot := _free_plot_near(r, screen, 30.0)
		if plot >= 0:
			selected_plot = plot
			_open_build_radial(r, plot)
			_refresh_panels()


## WOTR-2: the free build plot of region `r` (the local player's) whose screen position is within `radius` pixels of `screen`, -1 none
## WOTR2 milestone 5: the map rollover help (StrategicInGameUI CommonBehavior, spec O B): over the HUD or off the map the rollover goes (RW 0x97E88B); army
## icons of every owner first, then building icons, then a build plot (which keeps the rollover as it is), then the region (RW 0x9DB18B); a new object
## restarts the clock, and its help shows once more than 999 ms passed (RW 0x97E6DD), never refreshed while shown; a null help is tried again
func _hover_tick() -> void:
	_update_highlights()
	if state.is_empty() or regions.is_empty():
		return
	var mouse := get_viewport().get_mouse_position()
	var target := ""
	var inside := Rect2(Vector2.ZERO, get_viewport().get_visible_rect().size).has_point(mouse)
	if inside and String(game._shell.hit_test_stage(game._shell.window_to_stage(mouse))) == "":
		var a := _army_near(mouse, 30.0, -1, true)
		if a >= 0:
			target = "army:%d" % a
		else:
			var b := _building_near(mouse, 20.0)
			if not b.is_empty():
				target = "building:%d:%d" % [int(b[0]), int(b[1])]
			else:
				var r := pick_region(mouse)
				if r >= 0 and _free_plot_near(r, mouse, 20.0, true) >= 0:
					target = hover_target # the plot icon keeps the current rollover (picker slot 13: a no-op)
				elif r >= 0:
					target = "region:%d" % r
	if target != hover_target:
		if hover_shown:
			game._shell.strategic_hud({"kind": "sync", "map_help": {}}) # the old rollover's release (RW 0x97E675)
		hover_shown = false
		hover_target = target
		hover_start = Time.get_ticks_msec()
		return
	if target == "" or hover_shown or Time.get_ticks_msec() - hover_start <= 999:
		return
	var help := _hover_help(target)
	if help.is_empty():
		return # RW 0x97E6DD: a null help is retried on the next update
	game._shell.strategic_hud({"kind": "sync", "map_help": help})
	hover_shown = true


func _hover_help(target: String) -> Dictionary:
	var parts := target.split(":")
	if parts[0] == "region":
		return {"kind": "region", "region_campaign": region_campaign, "region": int(parts[1])}
	if parts[0] == "army":
		var h: Dictionary = lw.army_help(game._world, int(parts[1]))
		if h.is_empty():
			return {}
		h["kind"] = "army"
		return h
	var r := int(parts[1])
	var slot := int(parts[2])
	var bi := _building_in(r, slot)
	if bi < 0:
		return {}
	var owner := int(state.regions[r].owner)
	var b: Dictionary = state.regions[r].buildings[bi]
	var allied := owner >= 0 and (owner == local_player or int(state.players[owner].team) == int(state.players[local_player].team))
	if not allied and int(b.turns_left) > 0:
		return {} # INFERENCE [S-1334]: a foreign building's + 0x20 == -1 (no help) read as one under construction
	var bh: Dictionary = lw.building_help(str(b.template))
	if bh.is_empty():
		return {}
	bh["kind"] = "building"
	return bh


## the building icon (any owner) within `radius` pixels of `screen`: [region, slot], [] none
func _building_near(screen: Vector2, radius: float) -> Array:
	var best := []
	var best_d := radius
	for r in range(state.regions.size()):
		for b in state.regions[r].buildings:
			var p := building_screen_position(r, int(b.slot))
			if p.x < 0.0:
				continue
			var d := p.distance_to(screen)
			if d <= best_d:
				best_d = d
				best = [r, int(b.slot)]
	return best


func _free_plot_near(r: int, screen: Vector2, radius: float, any_owner: bool = false) -> int:
	if int(state.regions[r].owner) != local_player and not any_owner:
		return -1
	var spots: Array = regions[r].get("building_spots", [])
	for i in range(spots.size()):
		var taken := false
		for b in state.regions[r].buildings:
			taken = taken or int(b.slot) == i
		if taken:
			continue
		var p: Vector2 = spots[i]
		if camera.unproject_position(_w3d(p.x, p.y, _ground(p.x, p.y))).distance_to(screen) <= radius:
			return i
	return -1


## WOTR-2: the plot of the local player's finished building in region `r` that recruits (LivingWorldGame.recruitable_in) whose screen position is within
## `radius` pixels of `screen`, -1 none
func _recruiting_building_near(r: int, screen: Vector2, radius: float) -> int:
	if int(state.regions[r].owner) != local_player:
		return -1
	for rec in lw.recruitable_in(local_player, r):
		if building_screen_position(r, int(rec.slot)).distance_to(screen) <= radius:
			return int(rec.slot)
	return -1


## WOTR-2: the window position of build plot `slot` of region `r`, Vector2(-1, -1) none
func building_screen_position(r: int, slot: int) -> Vector2:
	var spots: Array = regions[r].get("building_spots", [])
	if slot < 0 or slot >= spots.size():
		return Vector2(-1, -1)
	var p: Vector2 = spots[slot]
	return camera.unproject_position(_w3d(p.x, p.y, _ground(p.x, p.y)))


## WOTR-2: region `r`'s recruiting building in plot `slot` (LivingWorldGame.recruitable_in's entry), {} none
func _recruitable(r: int, slot: int) -> Dictionary:
	if r < 0:
		return {}
	for rec in lw.recruitable_in(local_player, r):
		if int(rec.slot) == slot:
			return rec
	return {}


## WOTR-2: select the finished building in plot `slot` of region `r` (PlanningPhaseBuildingSelection WB 0x15FC870): the recruit radial with one
## QueueUnitButton per ArmyToSpawn (WB 0x1647160), the build queue panel in the tray
func _select_building(r: int, slot: int) -> void:
	_close_build_radial()
	selected_building = slot
	selected_plot = slot
	var rec := _recruitable(r, slot)
	if int(state.phase) != PHASE_ORDERS or rec.is_empty() or rec.armies.is_empty():
		return
	radial_kind = "recruit"
	radial_region = r
	radial_slot = slot
	radial_armies = []
	for a in rec.armies:
		radial_armies.append(int(a.index))
	apt_radial_request = {"center": building_screen_position(r, slot), "buttons": _recruit_buttons(rec, r, slot)}


func _deselect_building() -> void:
	if selected_building < 0:
		return
	selected_building = -1
	apt_region_panel_key = [] # the region panel takes the tray back
	_close_build_radial()


## the recruit radial's buttons: each ArmyToSpawn's ConstructButtonImage, its queued count (ShowProductionCount) and whether it can be queued
## (QueueUnitButton: canQueueUnit RW-twin WB 0x1334DE0; LivingWorldGame.can_recruit)
func _recruit_buttons(rec: Dictionary, r: int, slot: int) -> Array:
	var counts := {}
	for o in state.regions[r].production:
		if int(o.slot) == slot:
			counts[int(o.army_index)] = int(counts.get(int(o.army_index), 0)) + 1
	var buttons := []
	for a in rec.armies:
		var idx := int(a.index)
		buttons.append({"image": str(a.image), "enabled": bool(lw.can_recruit(local_player, r, slot, idx)), "count": int(counts.get(idx, 0))})
	return buttons


func _update_recruit_radial() -> void:
	if radial_kind != "recruit" or radial_region < 0:
		return
	var rec := _recruitable(radial_region, radial_slot)
	if rec.is_empty():
		_close_build_radial()
		return
	apt_radial_request = {"update": true, "center": building_screen_position(radial_region, radial_slot), "buttons": _recruit_buttons(rec, radial_region, radial_slot)}


## WOTR-2: the selected building's build queue panel (BuildQueueDetailsPanel WB 0x1644080): the queue of its SpawnArmy nugget, index 0 the unit in
## production (its turns left), the others their BuildTime (GetBuildTimeAtIndex); each entry's ConstructButtonImage, the first army entry's
## AutoResolveUnitType and count, its title and the first entry's CommandPoints. {} none
func _build_queue_panel() -> Dictionary:
	if selected_region < 0 or selected_building < 0:
		return {}
	var rec := _recruitable(selected_region, selected_building)
	if rec.is_empty():
		return {}
	var by_index := {}
	for a in rec.armies:
		by_index[int(a.index)] = a
	var queue := []
	for o in state.regions[selected_region].production:
		if int(o.slot) != selected_building:
			continue
		var a: Dictionary = by_index.get(int(o.army_index), {})
		queue.append({"portrait": str(a.get("image", "")), "unit_type": int(a.get("unit_type", -1)), "quantity": int(a.get("first_quantity", 0)),
			"turns": int(o.turns_left), "name": _text(str(a.get("title", ""))), "cp": int(a.get("first_cp", 0))})
	return {"building": _building_display(str(rec.building)), "queue": queue}


## WOTR-2: the window position of region `r`'s first free build plot (the scripted run's click), Vector2(-1, -1) none
func free_plot_screen_position(r: int) -> Vector2:
	var spots: Array = regions[r].get("building_spots", [])
	for i in range(spots.size()):
		var taken := false
		for b in state.regions[r].buildings:
			taken = taken or int(b.slot) == i
		if not taken:
			var p: Vector2 = spots[i]
			return camera.unproject_position(_w3d(p.x, p.y, _ground(p.x, p.y)))
	return Vector2(-1, -1)


## WOTR-2: the build radial menu (StrategicHUD::RadialMenu, BuildPlotRadialMenu) around build plot `plot` of region `r`: one button per building the
## local player may build there (LivingWorldGame.buildable_in), its ConstructButtonImage; nothing when the plot holds a building or the region is not
## the local player's, outside the orders phase
func _open_build_radial(r: int, plot: int) -> void:
	_close_build_radial()
	if r < 0 or plot < 0 or int(state.phase) != PHASE_ORDERS or int(state.regions[r].owner) != local_player:
		return
	for b in state.regions[r].buildings:
		if int(b.slot) == plot:
			return
	var spots: Array = regions[r].get("building_spots", [])
	if plot >= spots.size():
		return
	var names: Array = lw.buildable_in(local_player, r)
	if names.is_empty():
		return
	var buttons := []
	for bname in names:
		buttons.append({"image": str(_building_template(str(bname)).get("button_image", "")), "enabled": true})
	var p: Vector2 = spots[plot]
	var at := camera.unproject_position(_w3d(p.x, p.y, _ground(p.x, p.y)))
	radial_buildings = names.duplicate()
	radial_region = r
	radial_build_plot = plot
	radial_kind = "build"
	apt_radial_request = {"center": at, "buttons": buttons}


func _close_build_radial() -> void:
	if radial_region < 0 and radial_buildings.is_empty():
		return
	radial_buildings = []
	radial_region = -1
	radial_kind = ""
	radial_slot = -1
	radial_armies = []
	apt_radial_request = {}


func select_region(r: int) -> void:
	if r != selected_region:
		_close_build_radial()
		selected_building = -1
	selected_region = r
	selected_plot = -1
	details_army = -1 # a region click is a region selection (the tray shows the region; RW 0x97F999 selects no army)
	selected_army = -1
	selected_struct = -1
	plot_from_page = false
	_refresh_panels()


## WOTR2 milestone 6: the retreat phase's map (StrategicInGameUI PlanRetreatsPhaseBehavior, spec S 3): a left click picks an own hero army that must
## retreat (IsArmySelectable RW 0x6B6657), a right click sends it to the region under the mouse (MSG_LW_ARMY_MOVE_REQUEST 0x6A8, RW 0x9E1532; the logic
## refuses an illegal region silently)
func _retreat_click(screen: Vector2, order: bool) -> void:
	if order:
		if selected_army >= 0 and retreats_pending().has(selected_army):
			var r := pick_region(screen)
			if r >= 0:
				order_move(selected_army, r)
		return
	var army := _army_near(screen, 30.0)
	if army >= 0 and retreats_pending().has(army):
		select_army(army)


## the local player's hero armies in the retreat list (the retreat checklist's items)
func retreats_pending() -> Array:
	var out := []
	for id in state.get("retreat_list", []):
		var a := _army(int(id))
		if not a.is_empty() and int(a.owner) == local_player and str(a.get("hero", "")) != "":
			out.append(int(id))
	return out


func order_move(army: int, region: int) -> bool:
	var res: Dictionary = lw.order_move(local_player, army, region)
	_say("order move army %d -> %s: %s" % [army, regions[region].name, "ok" if res.ok else res.error])
	_refresh()
	return res.ok


# ---- the state on the map -----------------------------------------------------------------------------------------------------------------

func _refresh() -> void:
	var old_positions := {}
	for id in army_nodes.keys():
		old_positions[id] = army_nodes[id].position
	state = lw.get_strategic_state()
	_take_notices()
	# FilledOwnershipEffect in the owner's colour, HomeRegionHighlight on each player's capital, RegionSelectionEffect on the selected region
	region_fx.clear_regions()
	var none := Color(0, 0, 0, 0)
	var capitals := {}
	for p in range(state.players.size()):
		if not state.players[p].defeated and int(state.players[p].capital) >= 0:
			capitals[int(state.players[p].capital)] = p
	for i in range(state.regions.size()):
		var owner: int = int(state.regions[i].owner)
		var fill := none
		var ring := none
		if owner >= 0 and state.regions[i].enabled:
			fill = _owner_color(owner)
			if capitals.get(i, -1) == owner:
				ring = fill
		var edge := _owner_color(local_player) if i == selected_region else none
		region_fx.set_region(i, fill, ring, edge)
	_place_armies(old_positions)
	_place_buildings()
	_refresh_panels()


func _icon(kind: int, icon_name: String) -> Dictionary:
	for t in data.icons:
		if int(t.kind) == kind and str(t.name).to_lower() == icon_name.to_lower():
			return t
	return {}


## The visible objects of an icon template: the base models and (WOTR2 milestone 7) the selection / hilight helpers, hidden until their army, building or
## plot is selected / under the mouse (_update_highlights; the icon object predicate RW 0x5C4B26: HideWhenUnselected / HideWhenUnhilighted); the
## move-order, rally and production helpers are not drawn (S-1335); sub-objects filter the model's meshes.
func _icon_node(icon: Dictionary, size_bit: int, under_construction: bool) -> Node3D:
	var root := Node3D.new()
	for o in icon.get("objects", []):
		if o.show_only_after_move_order or o.display_at_rally_point or o.hide_when_not_producing:
			continue
		if under_construction and o.hide_when_under_construction:
			continue
		if not under_construction and o.hide_when_not_under_construction:
			continue
		if int(o.visible_army_sizes) != 0 and size_bit != 0 and (int(o.visible_army_sizes) & size_bit) == 0:
			continue
		var m := _model(o.model)
		if m == null:
			continue
		var subs: PackedStringArray = o.sub_objects
		if subs.size() > 0:
			for mesh in m.find_children("*", "MeshInstance3D", true, false):
				var keep := false
				for sname in subs:
					if str(mesh.name).to_upper().ends_with("_" + str(sname).to_upper()):
						keep = true
				mesh.visible = keep
		m.position.y = float(o.z_offset)
		m.scale = Vector3.ONE * float(o.scale)
		m.rotation.y = deg_to_rad(float(o.orient_angle))
		if o.hide_when_unselected:
			m.set_meta("lw_hide_unselected", true)
			m.visible = false
		if o.hide_when_unhilighted:
			m.set_meta("lw_hide_unhilighted", true)
			m.visible = false
		root.add_child(m)
	return root


## WOTR2 milestone 7: the selection and rollover helpers of the map's icons (the predicate RW 0x5C4B26 per object: hidden while its icon is not selected /
## not hilighted)
func _update_highlights() -> void:
	for id in army_nodes.keys():
		_set_highlight(army_nodes[id], int(id) == selected_army, hover_target == "army:%d" % int(id))
	for n in building_nodes:
		if not n.has_meta("region"):
			continue
		var r: int = int(n.get_meta("region"))
		var slot: int = int(n.get_meta("slot"))
		var sel: bool = r == selected_region and (slot == selected_building or slot == selected_plot or slot == selected_struct)
		_set_highlight(n, sel, hover_target == "building:%d:%d" % [r, slot])


func _set_highlight(node: Node3D, selected: bool, hilighted: bool) -> void:
	if node == null or not is_instance_valid(node):
		return
	for c in node.get_children():
		if c.has_meta("lw_hide_unselected") or c.has_meta("lw_hide_unhilighted"):
			c.visible = (not c.has_meta("lw_hide_unselected") or selected) and (not c.has_meta("lw_hide_unhilighted") or hilighted)


func _army_icon_name(a: Dictionary) -> String:
	if not str(a.icon).is_empty():
		return a.icon
	return str(_player_template(int(a.owner)).get("default_army_icon", ""))


func _place_armies(old_positions: Dictionary) -> void:
	var seen := {}
	var per_region := {}
	for a in state.armies:
		seen[a.id] = true
		var r: int = int(a.region)
		var n: int = per_region.get(r, 0)
		per_region[r] = n + 1
		var reg: Dictionary = regions[r]
		var spots: Array = reg.garrison_spots if a.garrison and reg.garrison_spots.size() > 0 else reg.hero_spots
		var p: Vector2 = spots[n % spots.size()] if spots.size() > 0 else reg.center
		if spots.size() > 0 and n >= spots.size():
			p += Vector2(30, 0) * float(n / spots.size())
		var target := _w3d(p.x, p.y, _ground(p.x, p.y) + 2.0)
		army_targets[a.id] = target
		var node: Node3D = army_nodes.get(a.id)
		if node == null:
			var size_bit := 1 if a.members.size() <= 1 else (2 if a.cp < 120 else 4)
			node = _icon_node(_icon(0, _army_icon_name(a)), size_bit, false)
			if node.get_child_count() == 0:
				node = _fallback_marker(_owner_color(int(a.owner)))
			var banner := _banner_node(int(a.owner))
			if banner != null:
				node.add_child(banner)
			node.name = "Army%d" % a.id
			map_root.add_child(node)
			army_nodes[a.id] = node
			node.position = target
		elif old_positions.has(a.id) and old_positions[a.id].distance_to(target) > 1.0:
			var tw := create_tween()
			tw.tween_property(node, "position", target, 1.2).set_trans(Tween.TRANS_SINE)
		else:
			node.position = target
	for id in army_nodes.keys():
		if not seen.has(id):
			army_nodes[id].queue_free()
			army_nodes.erase(id)
			army_targets.erase(id)
	_place_army_lines()


## WOTR-2: a line from each army with a destination to that region, as retail's army lines (LivingWorldMapInfo: NumPointsPerArmyLine points along it,
## ArmyLineHeightBias over the ground, ArmyLineWidth wide, ArmyLineTextureName across it; ArmyLineColorAttacking into a region an enemy holds,
## ArmyLineColorAllied into one the owner or an ally holds, ArmyLineColorNeutral into an unowned one). [presentation, S-1335] the line drawer of
## retail was not read: here a straight ribbon over the ground, added to the map (EXLaser is a grey gradient across, black at the edges), ending in an
## arrowhead of four widths (the owner's RotWK 2.01 screenshots show one)
func _place_army_lines() -> void:
	if army_lines != null:
		army_lines.queue_free()
		army_lines = null
	var mi: Dictionary = data.map_info
	var width := float(mi.get("army_line_width", 6.0))
	var bias := float(mi.get("army_line_height_bias", 3.0))
	var points := maxi(2, int(mi.get("army_line_points", 100)))
	if army_line_texture == null and str(mi.get("army_line_texture", "")) != "":
		army_line_texture = ClassDB.instantiate("W3DModelBuilder").load_texture(game._fs, str(mi.army_line_texture))
		if army_line_texture == null:
			_say("army line texture %s did not load" % mi.army_line_texture)
	for a in state.armies:
		var dest := int(a.destination)
		if dest < 0 or dest == int(a.region) or dest >= state.regions.size() or not army_targets.has(a.id):
			continue
		var owner := int(state.regions[dest].owner)
		var color: Color = mi.get("army_line_neutral", Color8(240, 180, 38))
		if owner >= 0:
			var mine := int(a.owner)
			var allied := owner == mine or (int(state.players[owner].team) >= 0 and int(state.players[owner].team) == int(state.players[mine].team))
			color = mi.get("army_line_allied", color) if allied else mi.get("army_line_attacking", Color8(255, 0, 0))
		var t: Vector3 = army_targets[a.id]
		var from := Vector2(t.x, -t.z) # Godot (x, z) -> SAGE (x, y)
		var to := _region_center(dest)
		if army_lines == null:
			army_lines = Node3D.new()
			army_lines.name = "ArmyLines"
			map_root.add_child(army_lines)
		army_lines.add_child(_army_line_mesh(from, to, width, bias, points, color))


func _army_line_mesh(from: Vector2, to: Vector2, width: float, bias: float, points: int, color: Color) -> MeshInstance3D:
	var dir := (to - from).normalized()
	var side := Vector2(-dir.y, dir.x) * width * 0.5
	var head := minf(width * 4.0, from.distance_to(to) * 0.5)
	var end := to - dir * head
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var prev_l := Vector3.ZERO
	var prev_r := Vector3.ZERO
	for i in range(points):
		var k := float(i) / float(points - 1)
		var c := from.lerp(end, k)
		var h := _ground(c.x, c.y) + bias
		var l := _w3d(c.x + side.x, c.y + side.y, h)
		var r := _w3d(c.x - side.x, c.y - side.y, h)
		if i > 0:
			for v in [[prev_l, 0.0], [prev_r, 1.0], [l, 0.0], [prev_r, 1.0], [r, 1.0], [l, 0.0]]:
				st.set_uv(Vector2(v[1], k))
				st.add_vertex(v[0])
		prev_l = l
		prev_r = r
	# the arrowhead: a triangle from four widths across to the destination
	var hb := _ground(end.x, end.y) + bias
	var ht := _ground(to.x, to.y) + bias
	for v in [[_w3d(end.x + side.x * 4.0, end.y + side.y * 4.0, hb), 0.5], [_w3d(end.x - side.x * 4.0, end.y - side.y * 4.0, hb), 0.5], [_w3d(to.x, to.y, ht), 0.5]]:
		st.set_uv(Vector2(v[1], 1.0))
		st.add_vertex(v[0])
	var mesh := MeshInstance3D.new()
	mesh.mesh = st.commit()
	mesh.material_override = region_fx.overlay_material("blend_add, depth_test_disabled", Color(0, 0, 0), Color(0, 0, 0), color, 1.0, army_line_texture, 5)
	return mesh


## WOTR-2: the army's banner (LivingWorldMapInfo MenBanner .. AngmarBanner by the owner's side, NeutralBanner otherwise) BannerHeight above the army,
## leaning BannerTiltAngle; it grows with the camera's distance up to BannerMaxScale (BannerScaleSpeed: retail eases it; the rule of the growth was
## not read: here linear in the distance past the opening view's half) [presentation, S-1335]
func _banner_node(owner: int) -> Node3D:
	var mi: Dictionary = data.map_info
	var pt := _player_template(owner)
	var side := str(pt.get("faction", "")).trim_prefix("Faction")
	var banners: Dictionary = mi.get("banners", {})
	var model_name := str(banners.get(side, banners.get("Neutral", "")))
	var m := _model(model_name)
	if m == null:
		return null
	m.name = "Banner"
	m.position.y = float(mi.get("banner_height", 90.0))
	m.rotation.x = -deg_to_rad(float(mi.get("banner_tilt_angle", 0.0)))
	return m


func _scale_banners() -> void:
	var max_scale := float(data.map_info.get("banner_max_scale", 2.0))
	var k := clampf(cam_dist / 1800.0, 1.0, maxf(1.0, max_scale))
	for node in army_nodes.values():
		var b: Node3D = node.get_node_or_null("Banner")
		if b != null:
			b.scale = Vector3.ONE * k


func _fallback_marker(c: Color) -> Node3D:
	# an army whose icon template is unknown: a plain marker in the owner's colour (reported once)
	var mesh := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = 6
	cyl.bottom_radius = 6
	cyl.height = 40
	mesh.mesh = cyl
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	mesh.material_override = m
	mesh.position.y = 20
	var root := Node3D.new()
	root.add_child(mesh)
	return root


func _building_icon(template_name: String) -> String:
	for b in data.buildings:
		if b.name == template_name:
			return b.icon
	return ""


func _place_buildings() -> void:
	for n in building_nodes:
		n.queue_free()
	building_nodes.clear()
	for i in range(state.regions.size()):
		var rs: Dictionary = state.regions[i]
		var spots: Array = regions[i].building_spots
		var used := {}
		for b in rs.buildings:
			if spots.is_empty():
				continue
			var p: Vector2 = spots[int(b.slot) % spots.size()]
			var node := _icon_node(_icon(1, _building_icon(b.template)), 0, int(b.turns_left) > 0)
			node.position = _w3d(p.x, p.y, _ground(p.x, p.y))
			node.set_meta("region", i)
			node.set_meta("slot", int(b.slot))
			map_root.add_child(node)
			building_nodes.append(node)
			used[int(b.slot)] = true
		# WOTR2 milestone 7: LivingWorldBuildPlot's icon (its ctor: the region owner's LivingWorldPlayerTemplate BuildPlotIconName -> CreateIcon, BFME2
		# LivingWorldBuildPlot.cpp; LivingWorldBuildPlotIcon ctor 0x59E10F) on every plot of an owned region; a plot with a building shows the building
		var owner := int(rs.owner)
		var plot_icon := str(_player_template(owner).get("build_plot_icon", "")) if owner >= 0 else ""
		if plot_icon.is_empty():
			continue
		var icon := _icon(2, plot_icon)
		if icon.is_empty():
			continue
		for slot in range(spots.size()):
			if used.has(slot):
				continue
			var p: Vector2 = spots[slot]
			var node := _icon_node(icon, 0, false)
			node.position = _w3d(p.x, p.y, _ground(p.x, p.y))
			node.set_meta("region", i)
			node.set_meta("slot", slot)
			map_root.add_child(node)
			building_nodes.append(node)


# ---- the map's overlay layer ------------------------------------------------------------------------------------------------------------------

## The layer the map's own overlays sit in (the HUD itself is the shell's Apt movie). WOTR2 milestone 4: the provisional panel is retired
func _build_hud() -> void:
	hud = CanvasLayer.new()
	hud.layer = 20
	add_child(hud)


## WOTR2 milestone 5: the logic's notices for the local player become queued boxes (spec M 5): the labels and their arguments, formatted by the HUD
func _take_notices() -> void:
	for n in lw.take_notices():
		var kind := int(n.kind)
		if kind == 1: # RemovePlayer's observers (RW 0x6BAEB6): every client but the defeated one's (whose camera flies home: not ported, S-1334)
			var d := int(n.defeated)
			if d == local_player or bool(state.get("finished", false)):
				continue
			var ally := int(state.players[d].team) == int(state.players[local_player].team) # RW 0x6E1940
			notice_queue.append({"title": "LW:AllyDefeatedTitle" if ally else "LW:EnemyDefeatedTitle",
				"text": "LW:AllyDefeatedText" if ally else "LW:EnemyDefeatedText", "args": [{"text": str(state.players[d].name)}]})
		elif kind == 3 or kind == 4:
			# WOTR2 milestone 8: a LAN war's real-time vote auto-resolved instead (OpenBFME notices, every player: no game text for them)
			var where := str(regions[int(n.region)].display_name) if int(n.region) >= 0 and int(n.region) < regions.size() else ""
			var shown := _text(where) if not where.is_empty() else ""
			if kind == 3: # [S-1342]
				notice_queue.append({"raw_title": L10n.t("game.wotr.rts_demoted.title"), "raw_text": L10n.t("game.wotr.rts_demoted.text", {"region": shown})})
			else:
				notice_queue.append({"raw_title": L10n.t("game.wotr.map_missing.title"), "raw_text": L10n.t("game.wotr.map_missing.text", {"region": shown})})
		elif int(n.player) != local_player:
			continue
		elif kind == 0: # PrepareRetreats RW 0x6BABA8: the hero's display name, empty without one
			notice_queue.append({"title": "LW:HeroKilledTitle", "text": "LW:HeroKilledText", "args": [{"label": _hero_label(str(n.hero))}]})
		elif kind == 5: # WOTR2 milestone 8 (step 4): another player asks to save the LAN war (RW 0x629750 kind 1: "APT:ConfirmMultiplayerSave", Yes / No)
			notice_queue.append({"title": "APT:SaveGameMultiplayerConfirmationTitle", "text": "APT:ConfirmMultiplayerSave", "args": [{"text": net_save_name}],
				"question": true, "question_kind": "save"})
		elif kind == 6: # the request was refused (kind 2: AptSaveLoad::MultiplayerSaveGameDenied RW 0x816715)
			notice_queue.append({"title": "APT:SaveGameMultiplayerConfirmationTitle", "text": "APT:MultiplayerSaveDenied", "args": []})
		elif kind == 7: # saved: leave the war for now? (RW 0x8173EF: "APT:MultiplayerGameSaved"; RW 0x8160B3: MSG 0x6B9 with the answer)
			notice_queue.append({"title": "APT:SaveGameProgress", "text": "APT:MultiplayerGameSaved", "args": [], "question": true, "question_kind": "leave"})
		elif kind == 2: # CheckHeadToHeadBattles RW 0x6BD97E: one box, an item per army
			var items := []
			for e in n.repelled:
				var r := int(e.region)
				if r < 0 or r >= regions.size():
					continue
				items.append({"label": "LW:AttackRepelledItemText", "args": [{"label": _hero_label(str(e.hero))}, {"label": str(regions[r].display_name)}]})
			notice_queue.append({"title": "LW:AttackRepelledTitle", "text": "LW:AttackRepelledSummaryText", "args": [], "items": items})
	if bool(state.get("finished", false)) and (not notice_queue.is_empty() or notice_up):
		notice_queue.clear() # EndWar RW 0x6B943A: the queue and the notice on screen go
		notice_up = false
		if apt_hud:
			game._shell.strategic_hud({"kind": "sync", "notice_clear": true})


func _hero_label(hero: String) -> String:
	return "" if hero.is_empty() else str(lw.template_field(game._world, hero, "DisplayName"))


## the notices' gate (RW 0x6B6ACC): nothing queued and nothing on screen; WOTR2 milestone 5: the battle statistics hold the resolver too
func _notices_idle() -> bool:
	return notice_queue.is_empty() and not notice_up and conflict_pending.is_empty() and not conflict_up and not logic_after_results


## WOTR2 milestone 5: the StrategicConflictResults rows of an autoresolved battle (RW 0x9FEB0E: the winning side's players first, then the other sides)
func _queue_conflict_results(b: Dictionary) -> void:
	if not bool(b.get("results_counted", false)):
		return
	var sides: Array = b.get("sides", [])
	var w := int(b.get("winning_side", -1))
	var order := []
	if w >= 0 and w < sides.size():
		order.append(w)
	for i in range(sides.size()):
		if i != w:
			order.append(i)
	var rows := []
	for si in order:
		for e in sides[si]:
			var p := int(e.player)
			var c := _owner_color(p)
			rows.append({"name": str(state.players[p].name), "destroyed": int(e.destroyed), "heroes_lost": int(e.heroes_lost), "lost": int(e.lost),
				"veterancy": int(e.veterancy), "faction_image": str(state.players[p].get("faction_icon", "")),
				"color": (c.r8 << 16) | (c.g8 << 8) | c.b8})
	conflict_pending = {"num_winners": sides[w].size() if w >= 0 and w < sides.size() else 0, "rows": rows}


func _region_name(r: int) -> String:
	if r < 0 or r >= regions.size():
		return "?"
	return _text(regions[r].display_name)


func _refresh_panels() -> void:
	if hud == null or state.is_empty():
		return
	_sync_apt_hud()
	var events: Array = state.events
	if events.size() != last_event_count:
		for e in events.slice(last_event_count):
			_say("event turn %d: %s" % [e.turn, e.text])
		last_event_count = events.size()
	var pending: int = int(state.pending_battle)
	if pending >= 0 and pending != last_pending_battle:
		focus_region(int(state.battles[pending].region)) # the camera goes to the battle the local player decides (the prompt in the HUD)
	last_pending_battle = pending


## WOTR-2: the LivingWorldBuilding block of a building template name (setup's buildings), {} when none
func _building_template(template_name: String) -> Dictionary:
	for b in data.buildings:
		if b.name == template_name:
			return b
	return {}


func _building_display(template_name: String) -> String:
	for b in data.buildings:
		if b.name == template_name:
			var t := _text(b.display_name)
			return t if not t.is_empty() else template_name
	return template_name


## WOTR2 milestone 4: the plot index of region r's building in `slot`'s index into state.regions[r].buildings, -1 none
func _building_in(r: int, slot: int) -> int:
	if r < 0:
		return -1
	var bs: Array = state.regions[r].buildings
	for i in range(bs.size()):
		if int(bs[i].slot) == slot:
			return i
	return -1


## WOTR2 milestone 4: the plot of the local player's building (finished or not) in region r within `radius` pixels of `screen`, -1 none
func _own_building_near(r: int, screen: Vector2, radius: float) -> int:
	if int(state.regions[r].owner) != local_player:
		return -1
	for b in state.regions[r].buildings:
		if building_screen_position(r, int(b.slot)).distance_to(screen) <= radius:
			return int(b.slot)
	return -1


## WOTR2 milestone 4: the army details panel's selected members (their entry keys, ascending: the member set is a std::set<int>)
func _selected_member_keys() -> Array:
	var keys := []
	var members: Array = apt_army_panel.get("members", [])
	for i in range(min(members.size(), apt_army_selected.size())):
		if bool(apt_army_selected[i]):
			keys.append(int(members[i].get("key", 0)))
	keys.sort()
	return keys


func _member_home(key: int) -> int:
	for m in apt_army_panel.get("members", []):
		if int(m.get("key", 0)) == key:
			return int(m.get("home", 0))
	return 0


func _member_marked(key: int) -> bool:
	for m in apt_army_panel.get("members", []):
		if int(m.get("key", 0)) == key:
			return bool(m.get("upgrade_marked", false))
	return false


## WOTR2 milestone 4: the palantir command UI's slots 1..5 for the selection (StrategicInGameUI's selection objects, spec H section 2) with their
## enabled tests (section 3), recomputed every sync as retail does every HUD update
func _command_buttons() -> Array:
	var none := {}
	if int(state.phase) != 0 or bool(state.get("finished", false)):
		return []
	if selected_army >= 0 and details_army == selected_army:
		var a := _army(selected_army)
		if a.is_empty() or int(a.owner) != local_player:
			return []
		var moving := int(a.get("destination", -1)) >= 0
		if army_from_page:
			return [{"id": "CancelArmyMoveButton", "enabled": moving}] # RW 0x9FBFA0
		var keys := _selected_member_keys()
		var cancel := moving if keys.is_empty() else true # CanCancelMove RW 0xA092D3
		for k in keys:
			var home := _member_home(int(k))
			cancel = cancel and home != 0 and lw.can_move_army_member(selected_army, int(k), home)
		var disband := not keys.is_empty() # ShouldEnableButton RW 0xA09964
		for k in keys:
			disband = disband and lw.can_disband_army_member(selected_army, int(k))
		# UpgradeUnitButton (RW 0xA0979A, milestone 5): a selected member marked (CanCancelAnyUpgrades RW 0xA09619) or one the region's armories can take
		# (CanUpgradeAny RW 0xA0958B)
		var upgrade := false
		for k in keys:
			upgrade = upgrade or _member_marked(int(k)) or lw.can_upgrade_army_member(selected_army, int(k))
		return [{"id": "CancelArmyMemberMoveButton", "enabled": cancel}, {"id": "UpgradeUnitButton", "enabled": upgrade}, none, none,
			{"id": "DisbandArmyMemberButton", "enabled": disband}]
	if selected_region >= 0 and int(state.regions[selected_region].owner) == local_player:
		if selected_struct >= 0:
			var bi := _building_in(selected_region, selected_struct)
			if bi >= 0:
				var b: Dictionary = state.regions[selected_region].buildings[bi]
				var id := "CancelBuildingConstructionButton" if int(b.turns_left) > 0 else "DestroyBuildingButton" # RW 0x9FBC1E
				return [none, none, none, none, {"id": id, "enabled": true}]
		if plot_from_page and selected_plot >= 0 and _building_in(selected_region, selected_plot) < 0:
			var out := []
			for bname in lw.buildable_in(local_player, selected_region):
				var t := _building_template(str(bname))
				var cost := int(t.get("cost", 0))
				out.append({"id": "BuildBuildingButton:" + str(bname), "image": str(t.get("button_image", "")), "title": str(t.get("button_title", "")),
					"help": str(t.get("button_help", "")), "enabled": cost <= int(state.players[local_player].treasury) or cost == 0})
				if out.size() >= 5:
					break
			return out
	return []


## WOTR2 milestone 4: a palantir button's left click (its OnLeftClicked: the LivingWorld message, spec H sections 2.1 / 3)
func _command(id: String) -> void:
	if id == "CancelArmyMoveButton" or (id == "CancelArmyMemberMoveButton" and _selected_member_keys().is_empty()):
		var res: Dictionary = lw.cancel_move(local_player, selected_army) # MSG_LW_CANCEL_ARMY_MOVE 0x6A9
		_say("cancel move of army %d: %s" % [selected_army, "ok" if res.ok else res.error])
	elif id == "CancelArmyMemberMoveButton":
		for k in _selected_member_keys():
			if _member_home(int(k)) != 0:
				lw.cancel_army_member_move(selected_army, int(k)) # MSG_LW_CANCEL_ARMY_MEMBER_MOVE 0x6B0
	elif id == "UpgradeUnitButton":
		# OnLeftClicked RW 0xA097D1: mark what the armories can take (SendUpgradeMessages RW 0xA0967D, 0x6BF; the logic counts the armories' room),
		# else cancel the marks (SendCancelUpgradeMessages RW 0xA09732, 0x6C0); the member set in ascending key order
		var keys: Array = _selected_member_keys()
		keys.sort()
		var any := false
		for k in keys:
			any = any or lw.can_upgrade_army_member(selected_army, int(k))
		for k in keys:
			if any and lw.can_upgrade_army_member(selected_army, int(k)):
				var res: Dictionary = lw.order_upgrade_army_member(local_player, selected_army, int(k))
				_say("upgrade member %d of army %d: %s" % [int(k), selected_army, "ok" if res.ok else res.error])
			elif not any and _member_marked(int(k)):
				lw.cancel_upgrade_army_member(local_player, selected_army, int(k))
				_say("cancel the upgrade of member %d of army %d" % [int(k), selected_army])
	elif id == "DisbandArmyMemberButton":
		for k in _selected_member_keys():
			var res: Dictionary = lw.disband_army_member(local_player, selected_army, int(k)) # MSG_LW_DISBAND_ARMY_MEMBER 0x6B1
			_say("disband member %d of army %d: %s" % [int(k), selected_army, "ok" if res.ok else res.error])
	elif id == "DestroyBuildingButton" or id == "CancelBuildingConstructionButton":
		var res: Dictionary = lw.order_destroy_building(local_player, selected_region, selected_struct) # MSG_LW_DESTROY_BUILDING 0x6AB
		_say("destroy the building in plot %d of %s: %s" % [selected_struct, regions[selected_region].name, "ok" if res.ok else res.error])
		if res.ok:
			selected_struct = -1
			_deselect_building()
	elif id.begins_with("BuildBuildingButton:"):
		var res: Dictionary = lw.order_build(local_player, selected_region, id.substr(20), selected_plot) # MSG_LW_BUILD_BUILDING 0x6AA in the plot
		_say("order build %s in plot %d of %s: %s" % [id.substr(20), selected_plot, regions[selected_region].name, "ok" if res.ok else res.error])
		_close_build_radial()
	_refresh()
	_refresh_panels()


func order_build(region: int, building: String, plot: int = -1) -> bool:
	var res: Dictionary = lw.order_build(local_player, region, building, plot)
	_say("order build %s in %s: %s" % [building, regions[region].name, "ok" if res.ok else res.error])
	_refresh()
	return res.ok


func order_recruit(region: int, slot: int, index: int) -> bool:
	var res: Dictionary = lw.order_recruit(local_player, region, slot, index)
	_say("order recruit in %s: %s" % [regions[region].name, "ok" if res.ok else res.error])
	_refresh()
	return res.ok


## End Turn: the orders phase ends for the local player; the logic runs the other phases (the AI's orders, the moves, the battles it can resolve)
## until the next orders phase or a battle the player must decide.
func end_turn() -> void:
	if not state.is_empty() and int(state.phase) == PHASE_OWNERSHIP:
		# WOTR2 milestone 6: END PHASE in the retreat phase (MSG_LW_DONE_PHASE 0x6A6), refused while a retreat has no destination (RW 0x6B6ACC)
		if not _notices_idle() or not lw.can_end_phase(local_player):
			# ManualPhaseEnder::OnEndPhase RW 0x9DCEBB: the list opens and the incomplete rows flash (their Eva WorldMustRetreat: not ported)
			var r: Dictionary = game._shell.strategic_hud({"kind": "sync", "checklist_flag": true}) if apt_hud else {}
			_say("END PHASE: an army still needs a retreat destination (flagged %s)" % str(r.get("checklist_flagged", [])))
			return
		lw.end_phase(local_player)
		_run_logic()
		return
	if state.is_empty() or int(state.phase) != PHASE_ORDERS or not _notices_idle(): # the phase-done gate (RW 0x6B6ACC)
		return
	lw.end_turn(local_player)
	_run_logic()


## the battle statistics closed: the resolver's next step (RW 0x9DF550 -> the end state, resolver + 0x1C slot 1)
func _run_logic_after_results() -> void:
	if not logic_after_results:
		return
	logic_after_results = false
	_run_logic()


func _run_logic() -> void:
	if net:
		_refresh() # a LAN war: the order is on its way; the logic runs on the lockstep's frames (_net_tick)
		return
	lw.update()
	_refresh()
	_say("turn %d phase %s hash %d" % [state.turn, state.phase_name, state.hash])


func choose_battle(fight: bool) -> void:
	var pending: int = int(state.get("pending_battle", -1))
	if pending < 0:
		return
	if net:
		# WOTR2 milestone 8: the vote goes to every peer (MSG_LW_SET_CONFLICT_RESOLUTION_METHOD); the lockstep decides once every participant voted
		# (RW 0x6BE9A5: real time only when every live human of the war is in the battle) and _net_tick launches a real-time battle on every peer
		var vote: Dictionary = lw.choose_rts(pending) if fight else lw.choose_auto_resolve(pending)
		_say("%s vote for battle %d: %s" % ["real-time" if fight else "auto-resolve", pending, "sent" if vote.ok else vote.error])
		return
	if not fight:
		var res: Dictionary = lw.choose_auto_resolve(pending)
		_say("auto-resolve battle %d: %s" % [pending, "ok" if res.ok else res.error])
		if res.ok:
			_queue_conflict_results(lw.get_strategic_state().battles[pending]) # read before the next turn clears the battles
		if not conflict_pending.is_empty():
			# the resolver goes on (the next battle's prompt, or the phase's end) only when its ConflictResultsState sees the screen closed (RW 0x9DF550,
			# then the resolver's callback + 0x1C slot 1): the logic waits, so nothing of the next phases (a retreat notice) opens over the statistics
			logic_after_results = true
			_refresh()
			return
		_run_logic()
		return
	var setup: Dictionary = lw.choose_rts(pending)
	if not setup.ok:
		_say("the battle cannot be fought: " + setup.error)
		return
	if setup.get("resolved", false):
		_say("the enemy retreated before the battle")  # LivingWorldAI::GetBattleChoice (RW 0x9001FB)
		_run_logic()
		return
	_launch_battle(setup)


# ---- the real-time battle ----------------------------------------------------------------------------------------------------------------

## WOTR2 milestone 9: the lobby issues a fresh relay game for each real-time battle and map return. Fetch and check it before releasing the current
## transport; no LAN fallback and no change to StrategicSession's command or save formats (RW 0x6B95BB, BFME2 AptOnlineCustomMatchCtor.cpp).
func request_online_phase(slots: Array) -> String:
	if not net_online or game._online_http == null:
		return "the online war has no active lobby transport"
	var phase := net_phase + 1
	var requested: Dictionary = game._shell.online_phase_request(phase, slots)
	if not requested.get("ok", false):
		return str(requested.get("error", "the lobby refused the phase request"))
	var deadline := Time.get_ticks_msec() + 30000
	while Time.get_ticks_msec() < deadline:
		await game.get_tree().process_frame
		var status: Dictionary = game._shell.online_phase_status()
		if int(status.get("phase", -1)) != phase or bool(status.get("pending", false)):
			continue
		if bool(status.get("waiting", false)):
			await game.get_tree().create_timer(2.0).timeout
			requested = game._shell.online_phase_request(phase, slots)
			if not requested.get("ok", false):
				return str(requested.get("error", "the lobby refused the phase vote retry"))
			continue
		var error := str(status.get("error", ""))
		if not error.is_empty():
			return "the lobby refused phase %d: %s" % [phase, error]
		var start := str(status.get("start", ""))
		if start.is_empty():
			return "the lobby returned no phase %d START" % phase
		var staged: Dictionary = game._world.online_war_phase(start, phase)
		return "" if staged.get("ok", false) else str(staged.get("error", "the phase START was refused"))
	return "the lobby did not agree on phase %d within 30 seconds" % phase


func online_battle_phase_seats(players: Array) -> Array:
	var seats: Array = []
	for player in players:
		var p: int = int(player)
		if bool(state.players[p].human):
			seats.append(int(slot_of_player[p]))
	return seats


func _map_key(map_name: String) -> String:
	var lower := map_name.to_lower()
	return "maps/%s/%s.map" % [lower, lower]


## The battle as a game on the region's map.  RotWK starts a War of the Ring battle in its own game state: no starting bases, the region owner (start
## position 1, slot 0 here) gets the region's buildings on Player_1_BuildPlot_<n> (RW 0x629F34); the armies' members are placed at their players' starts
## (NewGameMessage.living_world_battle, GameNetwork/GameInfo.h). WOTR2 milestone 6: no lobby cash (playerStartMoney 0); each slot's cash (StartingCashRTS or
## StartingCashRTSWithFort, plus ExtraStartResources), its garrison and its bonus units come from the logic's player starts [S-1329: reinforcements]
func _launch_battle(setup: Dictionary) -> void:
	battle = setup
	battle_ids.clear()
	battle_spawned.clear()
	battle_bases.clear()
	battle_result = {}
	battle_stage = "loading"
	battles_fought += 1
	# the region's owner first: it is RW 0x629F34's Player_1
	var players: Array = setup.players.duplicate()
	var owner: int = int(state.regions[int(setup.region)].owner)
	if owner >= 0 and players.has(owner):
		players.erase(owner)
		players.push_front(owner)
	battle.players = players
	var msg: Dictionary = lobby_message.duplicate(true)
	msg.map = _map_key(setup.map)
	msg.living_world_campaign = ""
	msg.starting_cash = 0 # Player::initFromDict RW 0x6B1213 deposits the battle cash on top (slot_starts)
	# a LAN war's battle: the lobby seed itself on every peer (RW 0x6497EC's InitGameLogicRandom(game + 0x50); Sol review wotr2m8s3: battles_fought is
	# this node's count across campaigns, not the war's); single player keeps one seed per battle
	msg.seed = int(lobby_message.seed) if net else int(lobby_message.seed) + battles_fought
	var slots: Array = []
	for i in range(8):
		slots.append({"state": 1, "name": "", "accepted": false, "has_map": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	var faction_names: PackedStringArray = data.faction_names
	var members: Array = []
	# WOTR-2 (review r1): strategic allies fight on one side: the battle's team of a player is its strategic team (the lobby team, or its own,
	# LivingWorldLogic's 100 + index), numbered 0, 1, ... in the order the battle's players first show it
	var tactical_team := {}
	for i in range(players.size()):
		var t: int = int(state.players[int(players[i])].team)
		if not tactical_team.has(t):
			tactical_team[t] = tactical_team.size()
	for i in range(players.size()):
		var p: Dictionary = state.players[int(players[i])]
		var lobby_slot: Dictionary = lobby_message.slots[slot_of_player[int(players[i])]]
		slots[i] = {"state": SLOT_PLAYER if p.human else maxi(2, int(lobby_slot.state)), "name": p.name, "accepted": true, "has_map": true,
			"color": int(p.color), "start_pos": i, "player_template": faction_names.find(p.faction), "team": int(tactical_team[int(p.team)])}
		for sp in setup.spawns:
			if int(sp.player) == int(players[i]):
				members.append({"slot": i, "army": int(sp.army), "member": int(sp.member), "template": sp.template, "quantity": int(sp.quantity),
					"experience": float(sp.experience), "upgrades": sp.upgrades, "spawn_id": str(sp.spawn_id)})
	msg.slots = slots
	var slot_starts: Array = []
	for ps in setup.get("player_starts", []):
		var slot: int = players.find(int(ps.player))
		if slot >= 0:
			slot_starts.append({"slot": slot, "cash": int(ps.cash), "garrison": int(ps.garrison), "bonus_units": ps.bonus_units, "heroes": ps.get("heroes", []),
				"skill_points": float(ps.get("skill_points", 0.0)), "bonus_points": int(ps.get("bonus_points", 0)), "sciences": ps.get("sciences", []),
				"upgrades": ps.get("upgrades", [])})
	var army_starts: Array = []
	for a in setup.get("army_starts", []):
		var aslot: int = players.find(int(a.player))
		if aslot >= 0:
			army_starts.append({"slot": aslot, "army": int(a.army), "state": int(a.state), "arrival_frame": int(a.arrival_frame), "spawn_points": a.spawn_points})
	msg.living_world_battle = {"armies": army_starts, "owner_slot": players.find(owner) if owner >= 0 else -1, "owner_buildings": setup.buildings,
		"owner_building_ids": setup.get("building_ids", []), "members": members, "slot_starts": slot_starts}
	_say("battle %d in %s on %s: players %s (owner slot %d, %d buildings), %d army members" % [int(setup.battle), regions[int(setup.region)].name, msg.map,
		str(players), msg.living_world_battle.owner_slot, setup.buildings.size(), members.size()])
	visible = false
	hud.visible = false
	if apt_hud:
		game._shell.strategic_hud({"kind": "close"}) # the battle's HUD (Palantir.apt) takes over
		apt_hud = false
		apt_hud_parts = 0
		apt_prompt_battle = -1
	active = false
	_set_pick_bodies(false) # the battle's own picking must not hit the hidden map
	game._state = game.State.MENU
	if net:
		# WOTR2 milestone 8 (step 3): every peer built this start from the war state (RW 0x6497EC: no host message); the strategic session parks, the
		# battle plays over the war's socket as a LAN game (its load barrier is retail's 0x6BA)
		var lobby_slots: Array = []
		for i in range(players.size()):
			lobby_slots.append(int(slot_of_player[int(players[i])]))
		if net_online:
			var phase_error: String = await request_online_phase(online_battle_phase_seats(players))
			if not phase_error.is_empty():
				game._fail("War of the Ring (online) battle: " + phase_error)
				return
		lw.net_park()
		var lb: Dictionary = game._world.lan_war_battle(msg, lobby_slots)
		if not lb.get("ok", false):
			game._fail("War of the Ring battle: " + str(lb.get("error", "")))
			return
		if net_online:
			net_phase += 1
		game._net_state = "loading"
		print("GAME WOTR NET battle %d: local battle slot %d, players %s" % [int(setup.battle), int(lb.local_slot), str(players)])
	game._begin_game(msg)


## game.gd after the battle game is up (_enter_game): the objects the start placed for each army member (the start report's starting objects)
func on_battle_started(rep: Dictionary) -> void:
	if battle_stage == "resuming":
		# WOTR2 milestone 6: a battle's save: the restored game's start report is not the battle's start; the session gave the army objects
		# (milestone 7: the army tags come back with the RTS snapshot, obfme:GameLogic version 3)
		battle_stage = "fighting"
		_say("battle resumed from its save: army members %s" % JSON.stringify(battle_ids))
		return
	if battle_stage != "loading":
		return
	battle_stage = "fighting"
	var start: Dictionary = rep.get("start", {})
	battle_slot_players = start.get("slot_players", [])
	if net:
		# WOTR2 milestone 8 (step 3): every peer reads the battle's result at the end of logic frame T_end (the victory event's frame + the end-game
		# timer), whatever frame it leaves on (BattleResultCapture)
		var armed: Dictionary = lw.arm_battle_capture(game._world, _battle_seats())
		print("GAME WOTR NET battle result capture armed: ", JSON.stringify(armed))
		capture_armed = armed.get("ok", false)
		if not capture_armed:
			push_error("WotR: the LAN battle's result capture could not be armed: " + str(armed.get("error", "")))
	var players: Array = battle.players
	var sums: Array = []
	for i in range(players.size()):
		sums.append([Vector2.ZERO, 0])
	for o in start.get("starting_objects", []):
		var army: int = int(o.get("army", -1))
		if army < 0 or int(o.get("member", -1)) < 0:
			continue # a structure, or a starting unit (its garrison's: memberIndex -1)
		var key := "%d:%d" % [army, int(o.member)]
		if not battle_ids.has(key):
			battle_ids[key] = []
		battle_ids[key].append(int(o.id))
		for sp in battle.spawns:
			if int(sp.army) == army and int(sp.member) == int(o.member):
				battle_spawned[int(o.id)] = [army, str(sp.spawn_id)]
				break
		for sp in battle.spawns:
			if int(sp.army) == army:
				var idx := players.find(int(sp.player))
				if idx >= 0:
					sums[idx][0] += Vector2(o.x, o.y)
					sums[idx][1] += 1
				break
	for s in sums:
		battle_bases.append(s[0] / s[1] if s[1] > 0 else Vector2.ZERO)
	for e in start.get("errors", []):
		_say("battle start error: " + str(e))
	for e in start.get("notes", []):
		_say("battle start: " + str(e))
	_say("battle started: army members %s, player positions %s" % [JSON.stringify(battle_ids), str(battle_bases)])


## Every frame of the battle (game.gd's end tick). WOTR2 milestone 6 (spec R 4.2): the battle ends only through the human side's Multiplayer_Human
## VICTORY / DEFEAT and the end-game timer (25 frames, then 0x7D9 / 0x7ED: game.gd's clear_game_data request leaves the game); nothing here ends it
func battle_tick() -> void:
	if battle_stage == "fighting":
		_banner_tick()


## WOTR2 milestone 7 (step 5): TheBannerUI in a War of the Ring battle: on the movie's init a banner per local reinforcement army (at most 4: CreateBanner
## RW 0x616AF0, its BannerType from the army's banner name, else BannerMen), each update its state / progress / timer (RW 0x6164D7), its button's call-in
## (MSG_CALL_IN_REINFORCEMENTS), and for each local army called in the message LW:ReinforcementsArrived (ArmyPlacer RW 0x782214; the radar event is not
## ported, S-1329)
func _banner_tick() -> void:
	var rf: Dictionary = lw.battle_reinforcements(game._world)
	if not banner_opened:
		banner_opened = true
		banner_armies = []
		banner_arrivals_seen = rf.get("arrivals", []).size() # a resumed battle's earlier call-ins were shown already
		var banners: Array = []
		var types: Dictionary = data.get("banner_types", {})
		for r in rf.get("armies", []):
			if not r.local or int(r.state) == 4 or banner_armies.size() >= 4:
				continue
			var name := str(_army(int(r.army)).get("banner", ""))
			if not types.has(name):
				name = "BannerMen"
			var bt: Dictionary = types.get(name, {})
			banners.append({"army": int(r.army), "flag": str(bt.get("FlagObj", "")), "glow": str(bt.get("GlowObj", ""))})
			banner_armies.append(int(r.army))
		if not banners.is_empty():
			var opened: Dictionary = game._shell.banner_ui({"kind": "open", "banners": banners})
			_say("reinforcement banners for armies %s: %s" % [str(banner_armies), "open" if opened.get("open", false) else str(opened.get("error", ""))])
	if not banner_armies.is_empty():
		var rows: Array = []
		for r in rf.get("armies", []):
			var slot: int = banner_armies.find(int(r.army))
			if slot >= 0:
				var total: int = int(r.arrival_frame)
				var progress: float = 1.0 if total <= 0 else minf(1.0, float(r.elapsed_frames) / float(total)) # RW 0x80FED2 (a display value)
				rows.append({"slot": slot, "state": int(r.state), "progress": progress, "remaining": int(r.remaining_frames)})
		var b: Dictionary = game._shell.banner_ui({"kind": "sync", "rows": rows})
		for army in b.get("call_ins", []):
			var res: Dictionary = lw.call_in_reinforcement(game._world, int(army))
			_say("the banner calls in army %d: %s" % [int(army), "ok" if res.ok else str(res.get("error", ""))])
	var arrivals: Array = rf.get("arrivals", [])
	while banner_arrivals_seen < arrivals.size():
		var army: int = int(arrivals[banner_arrivals_seen])
		banner_arrivals_seen += 1
		for r in rf.get("armies", []):
			if int(r.army) == army and r.local:
				game._show_end_message({"text": "LW:ReinforcementsArrived", "name": ""})
		_say("reinforcement army %d is on the field" % army)


## the scripted run (and a player's banner click goes the same way): every ready local reinforcement is called in
func call_in_ready_reinforcements() -> int:
	var called := 0
	for r in lw.battle_reinforcements(game._world).get("armies", []):
		if r.local and int(r.state) == 3:
			lw.call_in_reinforcement(game._world, int(r.army))
			called += 1
	return called


func _close_banners() -> void:
	if banner_opened and not banner_armies.is_empty():
		game._shell.banner_ui({"kind": "close"})
	banner_opened = false
	banner_armies = []
	banner_arrivals_seen = 0


## The players of the battle in side order (the logic's player starts) with their tactical names and garrisons
func _battle_seats() -> Array:
	var seats: Array = []
	var players: Array = battle.players
	for ps in battle.get("player_starts", []):
		var idx: int = players.find(int(ps.player))
		var pname: String = battle_slot_players[idx] if idx >= 0 and idx < battle_slot_players.size() else ""
		seats.append({"player": int(ps.player), "name": pname, "garrison": int(ps.garrison)})
	return seats


## GameLogic::LivingWorldTacticalBattleComplete RW 0x626662: the survivors (ArmySummarySystem::Save RW 0x811E1F) and the victor (RW 0x6265D2), read while the
## battle's objects still exist. `left`: the quit menu left the battle before its end (S-1329 inference: the leaver has no survivors)
func _collect_battle_result(left: bool) -> void:
	battle_result = lw.collect_battle_result(game._world, battle_spawned, _battle_seats(), left)
	if not battle_result.get("collected", false):
		_say("the battle result could not be read: " + str(battle_result.get("error", "")))
	_say("battle over%s: winner %d, %d survivors" % [" (left)" if left else "", int(battle_result.get("winner", -1)), battle_result.get("survivors", []).size()])


## game.gd's _exit_to_score_screen before the game is cleared: the end-game timer ran out (the battle is decided) or the quit menu left it
func before_battle_left() -> void:
	_close_banners()
	if battle_stage == "fighting":
		var es: Dictionary = game._world.get_end_game_state()
		var timer_ran: bool = int(es.get("end_timer", -1)) == 0
		if not timer_ran:
			_say("the battle is left before its end")
		if net:
			# WOTR2 milestone 8 (step 3): the result every peer read at the end of frame T_end (BattleResultCapture); it counts only when this client's
			# leave comes at or after T_end, the battle's end for every peer (on_battle_left: clear_game_data's war_left_before_end); a leave before it
			# ends the war with no result (the capture has not run then)
			exit_waiting = false
			battle_result = lw.captured_battle_result()
			_say("battle over at T_end (frame %d): winner %d, %d survivors%s" % [int(battle_result.get("frame", -1)), int(battle_result.get("winner", -1)),
				battle_result.get("survivors", []).size(), "" if battle_result.get("collected", false) else " (" + str(battle_result.get("error", "")) + ")"])
		else:
			_collect_battle_result(not timer_ran)
		battle_stage = "ending"


## game.gd's _exit_to_score_screen after clear_game_data: the result goes to the logic and the map returns
func on_battle_left(report: Dictionary) -> void:
	# (Sol review wotr2m8s3r2) this client's own leave before T_end (the victory event's frame + the end-game timer: the frame the others judge its
	# departure by, GameWorld.lan_war_battle_departed) ends the war for every peer; a leave at or after it is the battle's normal end
	if net and net_battle_abort.is_empty() and bool(report.get("war_left_before_end", false)):
		net_battle_abort = "this player left the battle"
		print("GAME WOTR NET war ended: " + net_battle_abort)
	elif net and net_battle_abort.is_empty() and bool(report.get("war_other_departed_before_end", false)):
		# (Sol review wotr2m8s3r3) a departure before T_end that the presentation had not shown when this client left (GameWorld.net_finish's final check)
		net_battle_abort = "a player left the battle"
		print("GAME WOTR NET war ended: " + net_battle_abort + " (seen at the leave)")
	if net and not net_battle_abort.is_empty():
		# the war is over for every peer (QuitWar): no result is applied, the network game ends, the score screen of the war (Continue: the LAN lobby)
		_say("the LAN war ended: " + net_battle_abort)
		net_war_ended = net_battle_abort
		net_battle_abort = ""
		battle_stage = ""
		battle = {}
		battle_ids.clear()
		battle_spawned.clear()
		battle_result = {}
		game._world.lan_war_battle_end()
		game._world.lan_strategic_end()
		game._net_state = ""
		game._restore_switched_profile(true)
		net = false
		active = true # leave_to_shell's score screen of the war
		leave_to_shell()
		return
	var idx: int = int(battle.get("battle", -1))
	var res: Dictionary = lw.apply_battle_result(idx, battle_result)
	_say("battle %d result applied: %s" % [idx, "ok" if res.ok else res.error])
	battle_stage = ""
	battle = {}
	if game._audio != null:
		game._audio.stop_music(false)
	if not res.ok:
		# review wotr2m6 (Sol): a result the war refuses would leave the battle in real time for ever (no prompt, phase 2 stuck): the war ends here, with
		# the reason, as a refused load does (no fallback)
		push_error("WotR: the battle's result was refused: " + str(res.error))
		battle_ids.clear()
		battle_spawned.clear()
		battle_result = {}
		game._start_stack = PackedStringArray(["MainMenu.apt"])
		game._state = game.State.SCORE
		game._score_continue()
		return
	if net:
		# WOTR2 milestone 8 (step 3): back on the map: the war's START again and a new strategic session (its load barrier is retail's 0x6BB); the
		# frame numbering starts again (RW 0x625FC2's new network session), the war's clock goes on
		if net_online:
			var map_phase: Dictionary = game._world.online_war_map_begin()
			if not map_phase.get("ok", false):
				game._fail("War of the Ring (online): back on the map: " + str(map_phase.get("error", "")))
				return
			net_phase += 1
		else:
			game._world.lan_war_battle_end()
		var nb: Dictionary = game._world.lan_strategic_begin(lw, slot_of_player, game._wotr_net_options())
		if not nb.get("ok", false):
			game._fail("War of the Ring (LAN): back on the map: " + str(nb.get("error", "")))
			return
		game._net_state = "playing"
		print("GAME WOTR NET back on the map: battle %d applied, war hash %d" % [idx, int(lw.get_state_hash())])
	_enter_map()
	_run_logic()


# ---- the scripted run (--wotr) ----------------------------------------------------------------------------------------------------------

func _region_index(region_name: String) -> int:
	for i in range(regions.size()):
		if str(regions[i].name).to_lower() == region_name.to_lower():
			return i
	return -1


## Orders for the local player: every hero army takes one step along the shortest connection path (breadth-first, enabled regions) toward the nearest
## enemy capital or enemy garrison (the test / video flow)
func scripted_orders(humans_only := false) -> void:
	# the enemies' capitals and their garrisons (armies that stay put): a march there meets a defender (`humans_only`: the human players' only)
	var targets := {}
	for p in range(state.players.size()):
		if p != local_player and not state.players[p].defeated and int(state.players[p].capital) >= 0 and (not humans_only or bool(state.players[p].human)):
			targets[int(state.players[p].capital)] = true
	for a in state.armies:
		if int(a.owner) != local_player and a.garrison and (not humans_only or bool(state.players[int(a.owner)].human)):
			targets[int(a.region)] = true
	for a in state.armies:
		if int(a.owner) != local_player or a.garrison:
			continue
		var start: int = int(a.region)
		var prev := {start: -1}
		var queue: Array = [start]
		var found := -1
		while not queue.is_empty() and found < 0:
			var r: int = queue.pop_front()
			for c in regions[r].connections:
				var to := _region_index(str(c))
				if to < 0 or prev.has(to) or not state.regions[to].enabled:
					continue
				prev[to] = r
				if targets.has(to):
					found = to
					break
				queue.append(to)
		if found < 0:
			continue
		var step := found
		while int(prev[step]) != start:
			step = int(prev[step])
		lw.order_move(local_player, int(a.id), step)
		if selected_region != start:
			select_region(start)
			focus_region(start)
	_refresh()


## The scripted run's battle: the local player's army marches on the enemy's start (attack move, a player command) and the camera follows it
func scripted_battle_orders() -> void:
	if battle_bases.size() < 2:
		return
	var players: Array = battle.players
	var mine: Array = []
	for sp in battle.spawns:
		if int(sp.player) == local_player:
			mine.append_array(battle_ids.get("%d:%d" % [sp.army, sp.member], []))
	var local_index := players.find(local_player)
	if local_index < 0 or mine.is_empty():
		return
	var target: Vector2 = battle_bases[1 - local_index] if local_index <= 1 else battle_bases[0]
	var r: Dictionary = game._world.order_move(mine, target.x, target.y, {"type": "attack"})
	_say("scripted battle: %d units attack-move to (%.0f, %.0f): %s" % [mine.size(), target.x, target.y, str(r.get("ok", false))])


func battle_army_center() -> Vector2:
	var sum := Vector2.ZERO
	var n := 0
	for key in battle_ids.keys():
		for id in lw.battle_alive(game._world, battle_ids[key]):
			var o: Dictionary = game._world.get_object(id)
			if o.has("x") and str(o.get("owner", "")) == str(game._local_name):
				sum += Vector2(o.x, o.y)
				n += 1
	return sum / n if n > 0 else Vector2.ZERO


## The camera eases to a region (the map's own camera, S-1335)
func focus_region(r: int) -> void:
	if r < 0 or r >= regions.size() or camera == null:
		return
	var c := _region_center(r)
	var target := _w3d(c.x, c.y)
	_say("camera to %s (%.0f, %.0f) from %s" % [regions[r].name, c.x, c.y, str(cam_target)])
	var tw := create_tween()
	tw.tween_method(func(t: Vector3):
		cam_target = t
		_update_camera(), cam_target, target, 1.0).set_trans(Tween.TRANS_SINE)


## The scripted run's overview: the camera pulls back over the map's centre (the user's zoom-out limit, 6000) and returns after `seconds`
## (the video shows what lies outside the map: the border clouds, then black)
func overview(seconds: float) -> void:
	if camera == null:
		return
	var back_target := cam_target
	var back_dist := cam_dist
	var mi: Dictionary = data.map_info
	var center := _w3d(mi.center.x, mi.center.y)
	var tw := create_tween()
	tw.tween_method(func(t: float):
		cam_target = back_target.lerp(center, t)
		cam_dist = lerpf(back_dist, 6000.0, t)
		_update_camera(), 0.0, 1.0, 1.5).set_trans(Tween.TRANS_SINE)
	tw.tween_interval(seconds)
	tw.tween_method(func(t: float):
		cam_target = center.lerp(back_target, t)
		cam_dist = lerpf(6000.0, back_dist, t)
		_update_camera(), 0.0, 1.0, 1.5).set_trans(Tween.TRANS_SINE)
