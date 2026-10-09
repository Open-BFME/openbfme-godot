## HUD viewer / test harness (lane HUD-1): starts a retail map as a LIVE game directly (no menu, no loading screen: lane START-1 owns that flow), creates the in-game HUD node
## (the Palantir movie, the input stack) on it and lets you play with the real mouse and keyboard, or runs a scripted scenario of synthetic input and takes screenshots.
##
##   godot --path godot res://scenes/hud_viewer.tscn -- [options]
##
## Mouse and keys are the game's: left click selects / drags a box, the right click orders (--alternate-mouse) or deselects, Ctrl+digit makes a control group, the digit
## selects it, S stops. The camera is the retail tactical camera (lane CAM-1): arrow keys scroll, the wheel zooms between the retail height limits, the right button dragged
## scrolls, the middle button dragged rotates (a middle click resets), numpad 4 / 6 rotate, 8 / 2 zoom, the screen edge scrolls in fullscreen (or with --edge-scroll).
##
## Options (after `--`):
##   --map=<name>            the map (default "map mp fall back 4p")
##   --faction=<Name>        the local player's faction (default FactionMen); the opponent is FactionMordor
##   --spawn=<Template>:<dx>,<dy>   (repeatable) an object for the local player near the first free spot (SAGE offsets)
##   --enemy=<Template>:<dx>,<dy>   (repeatable) an object for the opponent (Player_2) near the first free spot (lane COMBAT-1: the combat scenario)
##   --alternate-mouse       the right click orders (default: the left click orders)
##   --edge-scroll           scroll when the pointer touches the window edge (retail does it in fullscreen only)
##   --scenario=<name>       a scripted run (see _run_scenario): horde2 (lane HORDE-2: our horde charges the enemy horde, <prefix>-select / -contact / -melee / -aftermath), camera (lane CAM-1: the retail camera tour), streaks_paused (the arrow ribbons face a camera turned while paused), select_building, select_horde, drag_box, radar, rally, combat (box-select our horde, click the enemy horde, follow the fight),
##                           combat_building (lane COMBAT-2: the same against the enemy's BUILDING, the first --enemy: it is damaged, collapses into rubble and the victory report follows)
##   --no-audio / --build=<Template>   (lane AUDIO-2) run without the audio manager / the unit the audio scenario builds at the second --spawn
##   --screenshot=<path>     save the window after the scenario (or 30 frames) and quit
##   --shots=<dir>           the scenario saves its named screenshots there
##   --prefix=<name> / --timescale=<f>   (scenario projectiles) the screenshot name prefix (default proj1) and the logic speed (default 0.25); --air=<n> projectiles in the air for the volley shot (default 6)
##   --bow-shot              (scenario projectiles, lane RENDER-2) first a close view of our shooters, saved as <prefix>-bow.png when arrows have just left their launch bones
##   --report                print the HUD's report (stops, errors, notes) before quitting
##   --logic-thread=on|off   (lane SMOOTH-1, S-810) the logic on its worker thread (default) or on the main thread
##   --scenario=smooth_bench (lane SMOOTH-1) the frame-time benchmark and motion probe: every --spawn object marches, turns (a second move order at 90 degrees) and then
##                           attack-moves into every --enemy object (which attack-moves back); per segment it prints SMOOTH PERF (render frame times, the C++ timers of the
##                           frame, Godot's process and render CPU / GPU times) and SMOOTH MOTION (how evenly 8 horde members move: render frames without motion while their logic
##                           moves, the spread of their per-frame steps). --bench-seconds=<s> per segment (default 8); --interp=on|off|both (default on; both runs each segment stepped, then
##                           interpolated, captioned, for the before / after video)
##   --scenario=smooth2      (lane SMOOTH-2) the horde motion probe: the --spawn hordes march, turn 90 degrees, charge the --enemy hordes (attack-move both ways), fight and
##                           re-form (both sides walk away); the camera follows the drawn centre of our members. Per segment SMOOTH2 prints the drawn motion of every
##                           member (velocity reversals per member-second, the largest step of one render frame against the median moving step, facing turns) and
##                           the frame pacing (render frame times, the worker's held presentations). --bench-seconds=<s> per segment (default 8), --caption=<text>,
##                           --s2-csv=<path> (every member's drawn pose per render frame), --s2-height=<h> the camera height (default 240), --s2-follow=<n> follow the n-th --spawn only, --s2-offset-y=<d> shift the followed point
##   --scenario=move2        (lane MOVE-2) the slot-distance test's march: every --m2-horde=<Template> side by side at --m2-from=x,y, each sent on its own to
##                           --m2-to=x,y (default: the test's route on fall back 4p), the camera following from --s2-height; MOVE2 SPREAD every 2 s (per horde the
##                           farthest member from its horde object); --bench-seconds the run's length
##   --scenario=smooth_churn (lane SMOOTH-1) creation / destruction churn: 30 rounds of 8 objects of every --spawn template made and destroyed; prints SMOOTH CHURN per
##                           round (the dynamic instancer's instances, palette rows, the worst pose / upload / sync time of the round's frames)
##   --render-size=WxH       render at WxH whatever the window size (content scale mode viewport); screenshots are WxH
##   --bench-caption=<text>  (lane PERF-1) smooth_bench's segment caption is "<segment> - <text>" instead of the interpolation before / after caption
##   --perf-overlay          (lane PERF-1) a frame-rate overlay in the corner: the clock's fps and the frame time (mean / slowest 1% over the last 2 s), and the
##                           frame's measured cost (main-thread process + render CPU, GPU) with the frame rate that cost allows. Under --write-movie the clock
##                           is the movie's fixed rate, so the cost line is the measurement there
##                           lane PERF-2: under it, one bar per CPU core with its load over the last 0.5 s and the cores busy in all (read from /proc/stat; not shown
##                           where that file does not exist)
##   --scenario=fx_battle    (lane FX-2) our siege engines (every --spawn) shell the opponent's building (the first --enemy) while the other --enemy objects fight our
##                           hordes: saves fx2-fire / fx2-impact / fx2-damaged / fx2-collapse / fx2-deaths in --shots, prints the live FX report and the frame times
##   --scenario=phys1_melee  (lane PHYS-1) every --spawn horde attacks an --enemy horde and back (crossing pairs) next to the --spawn / --enemy buildings; saves a numbered
##                           frame phys1-NNNNN.png in --shots every --phys-every render frames (default 2) for --fx-frames frames (the video frames of the collision lane)
##   --scenario=spellbook    (lane SPELL-2) the retail spell book movies: SpellStore.apt (buy every affordable science tier by tier, --points=<n>), then each
##                           slot of the Palantir's InGameSpellBook pressed and cast near the focus; saves <prefix>-store-* / -palantir-* / -targeting / -slotNN / -bar
##   --audio                 (lane FX-2) boot the retail audio manager (GameAudio): the effects' Sound nuggets and the other engine sounds play, heard from the camera
##   --fx-cam=<dx>,<dy>,<h>  (fx_battle) the camera looks at the building plus (dx, dy) from height h (default: between the siege and the building, 520)
##   --soft-particles=on|off (lane FX-3 round 2) the live FX player's soft particles (default: the project setting openbfme/rendering/soft_particles)
##   --army=<slot>:<Template>:<dx>,<dy>   (lane PERF-3, repeatable) an object for player slot 1..4 at exactly focus + (dx, dy) (no free-spot search: the
##                           benchmark's grid is laid out by its caller); slots 3 / 4 add Player_3 (--army3-faction, default FactionElves, our team) and
##                           Player_4 (--army4-faction, default FactionIsengard, the opponent's team)
##   --scenario=perf3_battle (lane PERF-3) the 4-player mass battle: every army attack-moves into the middle of all armies; per segment (approach, melee, pan:
##                           the camera sweeps over the fight) SMOOTH PERF / TIMER / LOW as smooth_bench prints them; --bench-seconds=<s> per segment
##   --perf-stat=<prefix>    (lane PERF-3, Linux with perf) every benchmark segment counts the main thread's and the whole process's user instructions and
##                           cycles with `perf stat` (written to <prefix>-<segment>-main.txt / -all.txt) and prints PERF3 STAT per render frame (the work
##                           per frame, which a loaded machine does not change, unlike the frame time)
##   --fx-frames=<n>         (fx_battle) the render frames to run at most (default 2400); --fx-speed=<f> the logic speed (default 1.0)
extends Node3D

var _map_name := "map mp fall back 4p"
var _faction := "FactionMen"
var _spawn_args: Array = []
var _enemy_args: Array = []
var _army_args: Array = []    # lane PERF-3: --army=<slot>:<Template>:<dx>,<dy>
var _perf_stat := ""          # lane PERF-3: --perf-stat=<prefix>
var _army_factions := {3: "FactionElves", 4: "FactionIsengard"}
var _army_ids := {1: [], 2: [], 3: [], 4: []}
var _alternate := false
var _soft_particles := ""   # lane FX-3 round 2: --soft-particles=on|off overrides the project setting openbfme/rendering/soft_particles
var _edge_scroll := false
var _scenario := ""
var _screenshot_path := ""
var _shots_dir := ""
var _print_report := false
var _shot_prefix := "proj1"   # lane PROJ-1: the projectiles scenario saves <prefix>-select / -volley / -impact / -aftermath
var _time_scale := 0.25
var _min_air := 6
var _bow_shot := false
var _show_placeholders := false
var _calls: Array = []
var _bench_seconds := 8.0
var _bench_interp := "on"
var _caption := ""
var _perf_overlay := false   # lane PERF-1: --perf-overlay
var _bench_caption := ""     # lane PERF-1: --bench-caption
var _overlay_label: Label
var _overlay_caption: Label    # lane PERF-2: the segment caption on the overlay layer
var _core_bars: CoreBars       # lane PERF-2: the per-core load under the overlay (Linux /proc/stat)
var _core_prev: Array = []      # [busy, total] jiffies per /proc/stat cpu line at the last sample
var _core_next_us := 0
var _self_prev_ticks := -1      # lane PERF-2: this process's user + system clock ticks (/proc/self/stat) at the last sample
var _self_prev_us := 0
var _overlay_times: Array = []
var _overlay_prev_us := 0
var _s2_caption := ""   # lane SMOOTH-2: --caption=<text> before the segment name (the before / after video)
var _s2_csv := ""       # lane SMOOTH-2: --s2-csv=<path> every member's drawn pose per render frame
var _s2_height := 240.0 # lane SMOOTH-3: --s2-height=<h> the smooth2 scenario's camera height (default 240)
var _s2_offset_y := 0.0 # lane SMOOTH-3: --s2-offset-y=<d> added to the followed centre (the look-at point is not the screen centre at a low camera)
var _m2_hordes: Array = []           # lane MOVE-2: --m2-horde=<Template> (repeatable) the hordes of the move2 scenario
var _m2_from := Vector2(1921.5, 871.5) # lane MOVE-2: --m2-from=x,y / --m2-to=x,y the march (default: the slot-distance test's route on fall back 4p)
var _m2_to := Vector2(3568.5, 1618.5)
var _m2_fixed_camera := false          # lane MOVE-2: --m2-fixed-camera the camera stays where it starts (the frame pacing of the units alone)
var _s2_follow := -1    # lane SMOOTH-3: --s2-follow=<n> the camera follows the members of the n-th --spawn only (default: all of ours)
var _logic_thread := true   # lane SMOOTH-1: --logic-thread=on|off (the logic worker, S-810, or the main-thread fallback)
var _spell_science := "SCIENCE_EyeofSauron"   # lane SPELL-1: the spell scenario buys this science ...
var _spell_power := "SpellBookEyeofSauron"    # ... and casts this power
var _spell_text := ""                         # the spell book state the label shows (the APT spell book is not ported: S-529)
var _spell_points := 60                       # lane SPELL-2: --points=<n> (the spellbook scenario's purchase points)
var _spell_wait := 45                         # lane SPELL-2: --cast-wait=<render frames> after each cast (the summons hatch in a few seconds)
var _spawn_ids: Array = []   # lane FX-2: every --spawn / --enemy object, in order (the dictionaries keep one id per template)
var _enemy_ids: Array = []
var _fx_frames := 2400
var _phys_every := 2   # lane PHYS-1: phys1_melee saves a frame every n render frames
var _phys_cam_height := 300.0   # lane PHYS-1: phys1_melee's camera height (old stand-in scale, see _place_camera)
var _phys_cam_offset := Vector2.ZERO   # lane PHYS-1: added to the followed centre
var _fx_speed := 1.0
var _audio_on := false
var _fx_cam := ""
var _audio: Node
var _build := ""               # lane AUDIO-2: the audio scenario presses this unit's command button on the barracks

var _fs: RefCounted
var _builder: RefCounted
var _root: Node3D
var _world: Node3D
var _camera: Camera3D
var _hud: Node2D
var _layer: CanvasLayer
var _focus := Vector2.ZERO
var _cam_target := Vector2.ZERO
var _cam_height := 520.0
var _cam_back := 420.0
var _spawned := {}
var _local_index := -1
var _enemy_index := -1
var _enemies := {}
var _label: Label


func _ready() -> void:
	print("FRAME PACING ", preload("res://scripts/frame_pacing.gd").apply_from_args(OS.get_cmdline_user_args())) # lane SMOOTH-1
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--faction="):
			_faction = arg.substr(10)
		elif arg.begins_with("--spawn="):
			_spawn_args.append(arg.substr(8))
		elif arg.begins_with("--enemy="):
			_enemy_args.append(arg.substr(8))
		elif arg.begins_with("--army="):
			_army_args.append(arg.substr(7))
		elif arg.begins_with("--perf-stat="):
			_perf_stat = arg.substr(12)
		elif arg.begins_with("--army3-faction="):
			_army_factions[3] = arg.substr(16)
		elif arg.begins_with("--army4-faction="):
			_army_factions[4] = arg.substr(16)
		elif arg.begins_with("--soft-particles="):
			_soft_particles = arg.substr(17)
		elif arg == "--alternate-mouse":
			_alternate = true
		elif arg == "--edge-scroll":
			_edge_scroll = true
		elif arg.begins_with("--prefix="):
			_shot_prefix = arg.substr(9)
		elif arg == "--bow-shot":
			_bow_shot = true
		elif arg.begins_with("--air="):
			_min_air = int(arg.substr(6))
		elif arg.begins_with("--timescale="):
			_time_scale = float(arg.substr(12))
		elif arg.begins_with("--scenario="):
			_scenario = arg.substr(11)
			if _scenario == "audio":
				_audio_on = true # lane AUDIO-2: the audio scenario needs the audio manager
		elif arg == "--no-audio":
			_audio_on = false
		elif arg.begins_with("--build="):
			_build = arg.substr(8)
		elif arg.begins_with("--screenshot="):
			_screenshot_path = arg.substr(13)
		elif arg.begins_with("--shots="):
			_shots_dir = arg.substr(8)
		elif arg == "--report":
			_print_report = true
		elif arg.begins_with("--render-size="):
			# lane HUD-3: render the scene and the HUD at WxH whatever the window (a 1920x1080 picture on a 1280x800 display); screenshots come out at WxH
			var wh: PackedStringArray = arg.substr(14).split("x")
			get_window().content_scale_mode = Window.CONTENT_SCALE_MODE_VIEWPORT
			get_window().content_scale_aspect = Window.CONTENT_SCALE_ASPECT_IGNORE
			get_window().content_scale_size = Vector2i(int(wh[0]), int(wh[1]))
		elif arg.begins_with("--call="):
			_calls.append(arg.substr(7))
		elif arg.begins_with("--science="):
			_spell_science = arg.substr(10)
		elif arg.begins_with("--power="):
			_spell_power = arg.substr(8)
		elif arg.begins_with("--points="):
			_spell_points = int(arg.substr(9))
		elif arg.begins_with("--cast-wait="):
			_spell_wait = int(arg.substr(12))
		elif arg == "--placeholders":
			_show_placeholders = true
		elif arg.begins_with("--bench-seconds="):
			_bench_seconds = float(arg.substr(16))
		elif arg == "--perf-overlay":
			_perf_overlay = true
		elif arg.begins_with("--bench-caption="):
			_bench_caption = arg.substr(16)
		elif arg.begins_with("--interp="):
			_bench_interp = arg.substr(9)
		elif arg.begins_with("--caption="):
			_s2_caption = arg.substr(10)
		elif arg.begins_with("--s2-csv="):
			_s2_csv = arg.substr(9)
		elif arg.begins_with("--s2-height="):
			_s2_height = float(arg.substr(12))
		elif arg.begins_with("--s2-follow="):
			_s2_follow = int(arg.substr(12))
		elif arg == "--m2-fixed-camera":
			_m2_fixed_camera = true
		elif arg.begins_with("--m2-horde="):
			_m2_hordes.append(arg.substr(11))
		elif arg.begins_with("--m2-from="):
			var a: PackedStringArray = arg.substr(10).split(",")
			_m2_from = Vector2(float(a[0]), float(a[1]))
		elif arg.begins_with("--m2-to="):
			var b: PackedStringArray = arg.substr(8).split(",")
			_m2_to = Vector2(float(b[0]), float(b[1]))
		elif arg.begins_with("--s2-offset-y="):
			_s2_offset_y = float(arg.substr(14))
		elif arg.begins_with("--logic-thread="):
			_logic_thread = arg.substr(15) != "off"
		elif arg.begins_with("--fx-frames="):
			_fx_frames = int(arg.substr(12))
		elif arg.begins_with("--phys-every="):
			_phys_every = maxi(1, int(arg.substr(13)))
		elif arg.begins_with("--phys-cam="):
			var pc: PackedStringArray = arg.substr(11).split(",")
			_phys_cam_height = float(pc[0])
			if pc.size() >= 3:
				_phys_cam_offset = Vector2(float(pc[1]), float(pc[2]))
		elif arg.begins_with("--fx-cam="):
			_fx_cam = arg.substr(9)
		elif arg == "--audio":
			_audio_on = true
		elif arg.begins_with("--fx-speed="):
			_fx_speed = float(arg.substr(11))
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
	if not mount.ok:
		_fail("mount failed: %s" % [mount.errors])
		return
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0) # the base game shows black outside the map (owner, 2026-10-06)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	_camera = Camera3D.new()
	_camera.fov = 45
	_camera.near = 2.0
	_camera.far = 60000.0
	add_child(_camera)
	_builder = ClassDB.instantiate("MapTerrainBuilder")
	var terrain_opts := {"markers": false, "fog": false}
	_root = _builder.build_map(_fs, _map_name, terrain_opts)
	if _root == null:
		_fail("build_map failed: %s" % [_builder.get_report().errors])
		return
	add_child(_root)
	if _audio_on:
		_audio = ClassDB.instantiate("GameAudio")
		add_child(_audio)
		var booted: Dictionary = _audio.boot(_fs, {"seed": 4711})
		print("HUDVIEW audio booted: ", booted.get("ok", false), " ", booted.get("errors", []))
	_world = ClassDB.instantiate("GameWorld")
	var setup: Dictionary = _world.setup(_fs)
	if not setup.ok:
		_fail("object world failed: %s" % [setup.errors])
		return
	var slots := [
		{"player": "Player_1", "faction": _faction, "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1, "start_index": 1},
	]
	# lane PERF-3: the 4-player mass battle's third and fourth armies
	var army_slots := {}
	for a in _army_args:
		army_slots[int(String(a).get_slice(":", 0))] = true
	if army_slots.has(3) or army_slots.has(4):
		slots.append({"player": "Player_3", "faction": _army_factions[3], "human": false, "team": 0, "start_index": 2})
		slots.append({"player": "Player_4", "faction": _army_factions[4], "human": false, "team": 1, "start_index": 3})
	var rep: Dictionary = _world.load_map(_map_name, {"slots": slots, "seed": 4711, "logic_thread": _logic_thread})
	if not rep.ok:
		_fail("load_map failed: %s" % [rep.errors.slice(0, 5)])
		return
	_root.add_child(_world)
	if not _soft_particles.is_empty() and _world.get_fx_player() != null:
		_world.get_fx_player().soft_particles = _soft_particles == "on"
		print("HUDVIEW soft particles ", _world.get_fx_player().soft_particles)
	var idx := 0
	for pl in rep.players:
		print("  player ", idx, " ", pl)
		if "Player_1" in str(pl) and _local_index < 0:
			_local_index = idx
		if "Player_2" in str(pl) and _enemy_index < 0:
			_enemy_index = idx
		idx += 1
	for s in rep.stops:
		print("  live stop: ", String(s).substr(0, 120))
	_focus = _find_focus()
	_cam_target = _focus
	# the HUD
	_layer = CanvasLayer.new()
	_layer.layer = 10
	add_child(_layer)
	_hud = ClassDB.instantiate("InGameHudNode")
	_layer.add_child(_hud)
	_label = Label.new()
	_label.position = Vector2(10, 6)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	_layer.add_child(_label)
	_world.set_auto_advance(true)
	for a in _spawn_args:
		var f: PackedStringArray = a.split(":")
		var xy: PackedStringArray = f[1].split(",")
		var spot := _free_spot(_focus + Vector2(float(xy[0]), float(xy[1])))
		var id: int = _world.create_object(f[0], _local_index, spot.x, spot.y, 0.0)
		print("SPAWN %s at (%.0f, %.0f): object %d" % [f[0], spot.x, spot.y, id])
		_spawned[f[0]] = id
		_spawn_ids.append(id)
	for a in _enemy_args:
		var f2: PackedStringArray = a.split(":")
		var xy2: PackedStringArray = f2[1].split(",")
		var spot2 := _free_spot(_focus + Vector2(float(xy2[0]), float(xy2[1])))
		var id2: int = _world.create_object(f2[0], _enemy_index, spot2.x, spot2.y, 0.0)
		print("ENEMY %s at (%.0f, %.0f): object %d" % [f2[0], spot2.x, spot2.y, id2])
		_enemies[f2[0]] = id2
		_enemy_ids.append(id2)
	if not _army_args.is_empty():
		var slot_index := {}
		var pi := 0
		for pl in rep.players:
			for k in [1, 2, 3, 4]:
				if ("Player_%d" % k) in str(pl) and not slot_index.has(k):
					slot_index[k] = pi
			pi += 1
		for a in _army_args:
			var f3: PackedStringArray = String(a).split(":")
			var k3 := int(f3[0])
			var xy3: PackedStringArray = f3[2].split(",")
			var at := _focus + Vector2(float(xy3[0]), float(xy3[1]))
			var id3: int = _world.create_object(f3[1], slot_index.get(k3, -1), at.x, at.y, 0.0)
			if id3 < 0:
				_fail("army %s: object not created" % a)
				return
			_army_ids[k3].append(id3)
		print("ARMY objects %d (slots 1..4: %d %d %d %d hordes / objects)" % [_world.get_object_count(), _army_ids[1].size(), _army_ids[2].size(), _army_ids[3].size(), _army_ids[4].size()])
	await get_tree().process_frame
	var hs: Dictionary = _hud.setup(_fs, _world, _camera, "Player_1", {"alternate_mouse": _alternate, "show_placeholders": _show_placeholders, "camera_start": Vector3(_focus.x, 0.0, -_focus.y), "edge_scroll": _edge_scroll})
	print("HUD setup: ok=%s errors=%s" % [hs.ok, hs.errors])
	if not hs.ok:
		_fail("HUD setup failed")
		return
	if _audio_on:
		_boot_audio()
	if not _scenario.is_empty():
		_run_scenario()
	elif not _screenshot_path.is_empty():
		for i in 40:
			await get_tree().process_frame
		_save(_screenshot_path)
		_finish()


## lane AUDIO-2: the retail audio manager and TheEva (GameAudio), the live game's audio side (GameWorld.attach_audio: owners, the player filter, ambient sounds);
## the unit voices come from the HUD's command and selection path, VoiceCreated from production.
func _boot_audio() -> void:
	if not ClassDB.class_exists("GameAudio"):
		_fail("the openbfme extension has no GameAudio class; run build.bat")
		return
	var b: Dictionary = {"ok": true, "events": -1}
	if _audio == null: # FX-2's early boot (before the world) usually made it already
		_audio = ClassDB.instantiate("GameAudio")
		_audio.name = "GameAudio"
		add_child(_audio)
		b = _audio.boot(_fs, {"seed": 4711})
	if not b.get("ok", false):
		_fail("GameAudio boot failed: %s" % [b.get("errors", [])])
		return
	var at: Dictionary = _world.attach_audio()
	print("AUDIO boot: %d events, attach %s" % [b.get("events", 0), at])
	if not at.get("ok", false):
		_fail("attach_audio failed: %s" % [at])


func _update_listener() -> void:
	# the listener stands where the tactical camera looks (ZH: the microphone near the look-at point; S-243)
	if _audio == null or _hud == null:
		return
	var c: Dictionary = _hud.get_camera()
	if c.is_empty():
		return
	var p: Vector2 = c.get("position", Vector2.ZERO)
	var a: float = c.get("angle", 0.0)
	_audio.set_listener(Vector3(p.x, p.y, c.get("ground_level", 0.0)), Vector3(cos(a), sin(a), 0.0))


func _fail(message: String) -> void:
	push_error(message)
	print("HUDVIEW FAIL: ", message)
	get_tree().quit(1)


func _find_focus() -> Vector2:
	# the centre of the map's own objects of the first player slot, else the middle of the objects
	var sum := Vector2.ZERO
	var n := 0
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false) and o.get("owner", "") == "Player_1":
			sum += Vector2(o.x, o.y)
			n += 1
	if n == 0:
		for id in _world.get_object_ids():
			var o: Dictionary = _world.get_object(id)
			if o.get("ok", false):
				sum += Vector2(o.x, o.y)
				n += 1
	return sum / maxf(n, 1)


func _free_spot(near: Vector2) -> Vector2:
	var pts: Array = []
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false):
			pts.append(Vector2(o.x, o.y))
	for ring in 40:
		var per := 1 if ring == 0 else 8 * ring
		for k in per:
			var a := TAU * float(k) / float(per)
			var p := near + Vector2(cos(a), sin(a)) * 60.0 * ring
			var ok := true
			for q in pts:
				if q.distance_to(p) < 140.0:
					ok = false
					break
			if ok:
				return p
	return near


## The scenarios frame their shots through the retail camera: look at a ground point and take a height above the ground (the old stand-in heights 240 .. 520 map to the
## retail range 120 .. 300 as height * 300 / 240: the old close-ups are the retail maximum, which frames a fight of two hordes).
## The pixel of a ground point under the camera as the Godot camera draws it against the pixel the HUD's picking view gives it (they must be the same camera).
## The ribbon quads of the world's streak mesh (RENDER-1): per quad the two vertices on either side of the strip point, in Godot axes.
func _streak_quads() -> Array:
	var out: Array = []
	var node: Node = _world.get_node_or_null("Streaks")
	if node == null or node.mesh == null:
		return out
	for s in node.mesh.get_surface_count():
		var v: PackedVector3Array = node.mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX]
		var i := 0
		while i + 5 < v.size():
			out.append([v[i], v[i + 1]])  # the first triangle of a quad is (strip[i], strip[i + 1], strip[i + 2])
			i += 6
	return out


func _pick_error() -> float:
	var c: Dictionary = _hud.get_camera()
	var worst := 0.0
	for off in [Vector2(0, 0), Vector2(150, 90), Vector2(-200, 60)]:
		var p: Vector2 = c.position + off
		var h: float = _world.get_ground_height(p.x, p.y)
		var shown: Vector2 = _camera.unproject_position(Vector3(p.x, h, -p.y))
		var picked: Vector2 = _hud.world_to_pixel(p)
		worst = maxf(worst, shown.distance_to(picked))
	return worst


func _cam_report(c: Dictionary) -> Dictionary:
	return {"position": c.position, "angle": c.angle, "zoom": c.zoom, "height": c.height_above_ground, "ground": c.ground_level, "eye": c.eye, "target": c.target, "constraint": c.constraint}


func _place_camera() -> void:
	if _hud == null:
		return
	_hud.camera_look_at(_cam_target)
	_hud.camera_set_height(clampf(_cam_height * 300.0 / 240.0, 120.0, 300.0))


# lane PROJ-1: the projectile objects of the logic drawn as streaks (the retail W3DStreakDraw module is not ported: stop S-365). The position is interpolated between the logic
# frame before and the current one with the world's alpha (rendering never touches the simulation).
var _proj_nodes := {}
var _proj_meshes := {}


func _proj_mesh(template: String) -> Mesh:
	if _proj_meshes.has(template):
		return _proj_meshes[template]
	var big: bool = template.contains("Rock") or template.contains("Stone") or template.contains("Boulder") or template.contains("Bolt") or template.contains("Ballista")
	var m := BoxMesh.new()
	m.size = Vector3(5.0, 5.0, 14.0) if big else Vector3(1.6, 1.6, 20.0)
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(0.45, 0.42, 0.40) if big else (Color(1.0, 0.85, 0.5) if template.contains("Fire") else Color(0.95, 0.95, 0.85))
	m.material = mat
	_proj_meshes[template] = m
	return m


func _update_projectiles() -> void:
	if _world == null:
		return
	var alpha: float = clampf(_world.get_alpha(), 0.0, 1.0)
	var live := {}
	for p in _world.get_projectiles():
		var id: int = p.id
		if not String(p.template).contains("Arrow"):
			continue # a stone or a bolt has a model of its own: the drawable draws it; an arrow has none (W3DStreakDraw), so the streak stands in
		live[id] = true
		var node: MeshInstance3D = _proj_nodes.get(id)
		if node == null:
			node = MeshInstance3D.new()
			node.mesh = _proj_mesh(p.template)
			add_child(node)
			_proj_nodes[id] = node
		var a := Vector3(p.px, p.pz, -p.py)
		var b := Vector3(p.x, p.z, -p.y)
		var pos: Vector3 = a.lerp(b, alpha) if p.in_flight else b
		var dir := Vector3(p.dx, p.dz, -p.dy)
		if dir.length() < 0.001:
			dir = (b - a).normalized() if (b - a).length() > 0.001 else Vector3.FORWARD
		var half: float = node.mesh.size.z * 0.5
		node.global_position = pos - dir * half
		node.look_at(node.global_position + dir, Vector3.UP)
	for id in _proj_nodes.keys():
		if not live.has(id):
			_proj_nodes[id].queue_free()
			_proj_nodes.erase(id)


# lane PERF-2: the bars of the per-core load (0 .. 1 each) and the machine's busy cores
class CoreBars extends Control:
	var loads: Array = []
	var busy := 0.0
	var game_busy := -1.0

	func _draw() -> void:
		var n := loads.size()
		if n == 0:
			return
		var w := 18.0
		var h := 60.0
		var gap := 4.0
		var x0 := size.x - n * (w + gap)
		for i in range(n):
			var x := x0 + i * (w + gap)
			draw_rect(Rect2(x, 0, w, h), Color(0, 0, 0, 0.55))
			var v: float = clampf(loads[i], 0.0, 1.0)
			var col := Color(0.3, 1.0, 0.3).lerp(Color(1.0, 0.25, 0.2), v)
			draw_rect(Rect2(x, h * (1.0 - v), w, h * v), col)
		var font := ThemeDB.fallback_font
		var text := "machine: %.1f of %d hardware threads busy" % [busy, n]
		if game_busy >= 0.0:
			text += "   this game: %.1f" % game_busy
		var tx := size.x - font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, 18).x # right-aligned under the bars
		draw_string_outline(font, Vector2(tx, h + 22), text, HORIZONTAL_ALIGNMENT_LEFT, -1, 18, 5, Color.BLACK)
		draw_string(font, Vector2(tx, h + 22), text, HORIZONTAL_ALIGNMENT_LEFT, -1, 18, Color(1.0, 1.0, 0.4))


# lane PERF-2: every 0.5 s, each core's busy share since the last sample (/proc/stat: user nice system idle iowait irq softirq steal ...; idle + iowait
# is idle); the whole machine's load (other processes count too)
func _sample_core_load() -> void:
	var now := Time.get_ticks_usec()
	if now < _core_next_us:
		return
	_core_next_us = now + 500000
	var f := FileAccess.open("/proc/stat", FileAccess.READ)
	if f == null:
		return
	var rows: Array = []
	while not f.eof_reached():
		var line := f.get_line()
		if not line.begins_with("cpu"):
			if rows.size() > 0:
				break
			continue
		if line.begins_with("cpu "):
			continue # the sum line
		var parts := line.split(" ", false)
		var total := 0
		for i in range(1, parts.size()):
			total += int(parts[i])
		var idle := int(parts[4]) + (int(parts[5]) if parts.size() > 5 else 0)
		rows.append([total - idle, total])
	if _core_prev.size() == rows.size() and rows.size() > 0:
		var loads: Array = []
		var busy := 0.0
		for i in range(rows.size()):
			var dt: int = rows[i][1] - _core_prev[i][1]
			var db: int = rows[i][0] - _core_prev[i][0]
			var v := 0.0 if dt <= 0 else float(db) / float(dt)
			loads.append(v)
			busy += v
		_core_bars.loads = loads
		_core_bars.busy = busy
		_core_bars.queue_redraw()
	_core_prev = rows
	# this process: fields 14 / 15 of /proc/self/stat (utime, stime) in clock ticks (100 per second), after the ")" that ends the command name
	var sf := FileAccess.open("/proc/self/stat", FileAccess.READ)
	if sf != null:
		var stat := sf.get_line()
		var fields := stat.substr(stat.rfind(")") + 2).split(" ", false)
		if fields.size() > 13:
			var ticks := int(fields[11]) + int(fields[12])
			if _self_prev_ticks >= 0 and now > _self_prev_us:
				_core_bars.game_busy = float(ticks - _self_prev_ticks) / 100.0 / (float(now - _self_prev_us) / 1000000.0)
			_self_prev_ticks = ticks
			_self_prev_us = now


func _update_perf_overlay() -> void:
	# lane PERF-1: the clock's frame times of the last 2 s and this frame's measured cost (Godot's process time, the viewport's render CPU / GPU times)
	var now := Time.get_ticks_usec()
	if _overlay_prev_us != 0:
		_overlay_times.append((now - _overlay_prev_us) / 1000.0)
	_overlay_prev_us = now
	var sum := 0.0
	var kept: Array = []
	for i in range(_overlay_times.size() - 1, -1, -1):
		if sum > 2000.0:
			break
		sum += _overlay_times[i]
		kept.append(_overlay_times[i])
	_overlay_times = _overlay_times.slice(_overlay_times.size() - kept.size())
	if _overlay_label == null:
		var layer := CanvasLayer.new()
		layer.layer = 100
		add_child(layer)
		_overlay_label = Label.new()
		_overlay_label.add_theme_font_size_override("font_size", 20)
		_overlay_label.add_theme_color_override("font_color", Color(1.0, 1.0, 0.4))
		_overlay_label.add_theme_color_override("font_outline_color", Color.BLACK)
		_overlay_label.add_theme_constant_override("outline_size", 5)
		_overlay_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		_overlay_label.size = Vector2(760, 60)
		_overlay_label.position = Vector2(get_viewport().get_visible_rect().size.x - 770, 36) # under the caption line
		layer.add_child(_overlay_label)
		_core_bars = CoreBars.new()
		_core_bars.size = Vector2(760, 90)
		_core_bars.position = Vector2(get_viewport().get_visible_rect().size.x - 770, 104)
		_overlay_caption = Label.new()
		_overlay_caption.add_theme_font_size_override("font_size", 18)
		_overlay_caption.add_theme_color_override("font_outline_color", Color.BLACK)
		_overlay_caption.add_theme_constant_override("outline_size", 5)
		_overlay_caption.position = Vector2(10, 40) # below the top band the movie captures on this display cut off
		layer.add_child(_overlay_caption)
		layer.add_child(_core_bars)
	_sample_core_load()
	if kept.is_empty():
		return
	kept.sort()
	var n := kept.size()
	var low_n := maxi(1, n / 100)
	var low := 0.0
	for i in range(n - low_n, n):
		low += kept[i]
	low /= low_n
	var mean := sum / n
	var vp := get_viewport().get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp, true)
	var process_ms := Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0
	var render_cpu := RenderingServer.viewport_get_measured_render_time_cpu(vp) + RenderingServer.get_frame_setup_time_cpu()
	var render_gpu := RenderingServer.viewport_get_measured_render_time_gpu(vp)
	var cost := maxf(process_ms + render_cpu, render_gpu)
	var clock := "%.0f fps  (frame %.2f ms, 1%% low %.0f fps)" % [1000.0 / maxf(mean, 0.001), mean, 1000.0 / maxf(low, 0.001)]
	if not Engine.get_write_movie_path().is_empty():
		clock = "movie capture (fixed clock): each frame's measured cost"
	_overlay_label.text = "%s\nframe cost: CPU %.2f ms (process %.2f + render %.2f)  GPU %.2f ms  ->  %.0f fps" % [clock, process_ms + render_cpu, process_ms, render_cpu,
		render_gpu, 1000.0 / maxf(cost, 0.001)]
	# lane PERF-2: the segment caption again on the overlay's own layer, top left (the caption label of the HUD layer was not visible in the movie captures)
	_overlay_caption.text = _caption


func _process(delta: float) -> void:
	if _perf_overlay:
		_update_perf_overlay()
	if _hud == null or not _hud.is_ready():
		return
	if _audio != null and _camera != null:
		# the listener stands where the tactical camera looks (ZH sets it from the view; inference for RotWK), SAGE space
		var cam: Dictionary = _hud.get_camera()
		if cam.has("position"):
			var cpos: Vector2 = cam.position
			var ca: float = cam.get("angle", 0.0)
			_audio.set_listener(Vector3(cpos.x, cpos.y, cam.get("ground_level", 0.0)), Vector3(cos(ca), sin(ca), 0.0))
	# lane RENDER-1: arrows draw through GameWorld's W3DStreakDraw port (the textured streak of the template's Draw block); the stand-in boxes
	# of _update_projectiles stay available with --placeholders
	if _show_placeholders:
		_update_projectiles()
	if not _caption.is_empty():
		_label.text = _caption # lane SMOOTH-1: the benchmark's segment caption (the video); no per-frame HUD query (it would wait for the logic worker)
		return
	var sel: Array = _hud.get_selection()
	_label.text = "selected %s  logic %s  cursor %s" % [sel, _hud.get_logic_selection(), _hud.get_state().get("cursor", "")]
	if not _spell_text.is_empty():
		_label.text += "\n" + _spell_text
	if _audio != null:
		var playing: PackedStringArray = _audio.get_playing()
		_label.text += "\naudio: %s" % [", ".join(playing.slice(0, 6))]


func _save(path: String) -> void:
	var image := get_viewport().get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var err := image.save_png(path)
	print("HUDVIEW screenshot %s -> %s (%dx%d)" % [path, error_string(err), image.get_width(), image.get_height()])


func _finish() -> void:
	if _audio != null:
		print("AUDIO REPORT ", JSON.stringify(_audio.get_report()))
		print("EVA REPORT ", JSON.stringify(_audio.get_eva_report()))
		print("WORLD AUDIO ", JSON.stringify(_world.get_audio_report()))
	if _print_report:
		print("HUD REPORT ", JSON.stringify(_hud.get_report()))
		print("HUD STATE ", JSON.stringify(_hud.get_state()))
		# lane HUD-2: the HUD movie's draw operations (what the device draws, one line each)
		for child in _hud.get_children():
			if child.has_method("describe_ops"):
				for line in child.describe_ops():
					print("HUD OP ", line)
	if _audio != null:
		print("HUDVIEW audio stats ", _audio.get_stats())
		var ar: Dictionary = _audio.get_report()
		for k in ar.keys():
			if typeof(ar[k]) != TYPE_ARRAY:
				print("HUDVIEW audio report ", k, " = ", ar[k])
		print("HUDVIEW audio errors ", (ar.get("errors", []) as Array).slice(0, 6))
		_audio.shutdown()
		await _frames(2)
	get_tree().quit(0)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _box_select(center: Vector2) -> void:
	var a: Vector2 = _hud.world_to_pixel(center + Vector2(-90, 70))
	var b: Vector2 = _hud.world_to_pixel(center + Vector2(90, -70))
	_hud.inject_mouse_move(a)
	await _frames(3)
	_hud.inject_mouse_button(1, true, a, false)
	for t in 12:
		_hud.inject_mouse_move(a.lerp(b, (t + 1) / 12.0))
		await _frames(2)
	_hud.inject_mouse_button(1, false, b, false)
	await _frames(3)


func _click_world(pos: Vector2, button := 1, dbl := false) -> void:
	var px: Vector2 = _hud.world_to_pixel(pos)
	_hud.inject_mouse_move(px)
	await _frames(2)
	_hud.inject_mouse_button(button, true, px, dbl)
	await _frames(2)
	_hud.inject_mouse_button(button, false, px, false)
	await _frames(3)


func _run_scenario() -> void:
	await _frames(30)
	if _scenario == "probe":
		for c in _calls:
			var f: PackedStringArray = c.split("|")
			var args := PackedStringArray()
			if f.size() > 2 and not f[2].is_empty():
				args = f[2].split(",")
			var r: Dictionary = _hud.invoke_at(f[0], f[1], args)
			print("CALL ", c, " -> ", r)
			await _frames(6)
		await _frames(40)
		if not _screenshot_path.is_empty():
			_save(_screenshot_path)
		print(_hud.dump_tree(3))
		_finish()
		return
	if _scenario == "tree":
		print(_hud.dump_tree(6))
		_finish()
		return
	var shots := _shots_dir if not _shots_dir.is_empty() else "user://screens"
	match _scenario:
		"spell":
			# lane SPELL-1: the player's spell book (its state in the label: the InGameSpellBook APT is not ported, S-529), buy --science through
			# MSG_PURCHASE_SCIENCE, cast --power from the spell book at a point next to the focus through MSG_DO_SPECIAL_POWER_AT_LOCATION, follow
			# the result. Saves <prefix>-spellbook / -cast / -summon (prefix: --prefix, default spell1).
			if _shot_prefix == "proj1":
				_shot_prefix = "spell1"
			var at := _focus + Vector2(340, 260)
			_cam_target = at
			_cam_height = 420.0
			_cam_back = 330.0
			_place_camera()
			_spell_text = _spell_state()
			await _frames(30)
			_spell_text = _spell_state()
			print("SPELLBOOK ", JSON.stringify(_world.get_spellbook(_local_index)))
			_save(shots.path_join(_shot_prefix + "-spellbook.png"))
			print("PURCHASE ", _spell_science, " ", _world.purchase_science(_local_index, _spell_science))
			await _frames(20)
			_spell_text = _spell_state()
			var before: int = _world.get_object_count()
			print("CAST ", _spell_power, " ", _world.cast_special_power(_local_index, _spell_power, at.x, at.y))
			for i in 12:
				await _frames(5)
				_spell_text = _spell_state()
			print("AFTER CAST objects %d -> %d" % [before, _world.get_object_count()])
			print("SPELLBOOK ", JSON.stringify(_world.get_spellbook(_local_index)))
			_save(shots.path_join(_shot_prefix + "-cast.png"))
			for i in 30:
				await _frames(5)
				_spell_text = _spell_state()
			_save(shots.path_join(_shot_prefix + "-summon.png"))
		"spellbook":
			# lane SPELL-2: the RETAIL spell book movies. The Palantir's InGameSpellBook (SpellBookUI) shows the bought powers with their images, states and
			# recharge timers; SpellStore.apt is opened as the Palantir's spell store button does (OnBttnSpellStore), the harness adds purchase points
			# (debug_add_science_points: a viewer shortcut for the ranks a game earns), clicks every _active button tier by tier (OnBttnSpell: pending),
			# closes the store (the purchases are sent) and opens it again (purchased); then each slot of the cast bar is pressed as the movie's
			# OnAptInGameSpellBookButtonPressed does: a NEED_TARGET_POS power shows the radius ring and the ground click casts it.
			# Saves <prefix>-palantir-empty / -store-start / -store-pending / -store-purchased / -palantir-book / -targeting / -<power> / -bar.
			if _shot_prefix == "proj1":
				_shot_prefix = "spell2"
			var ring = load("res://scripts/spell_target_ring.gd").new()
			_root.add_child(ring)
			ring.setup(_hud, _world)
			var at := _focus + Vector2(340, 260)
			_cam_target = at
			_cam_height = 420.0
			_place_camera()
			await _frames(40)
			var sb: Dictionary = _hud.get_spellbook_state()
			print("SPELLBOOK palantir path=%s shown=%s" % [sb.get("path", ""), sb.get("shown", false)])
			_save(shots.path_join(_shot_prefix + "-palantir-empty.png"))
			print("STORE OPEN ", _hud.open_spell_store())
			await _frames(45)
			print("STORE ", JSON.stringify(_hud.get_spellbook_state().store))
			_save(shots.path_join(_shot_prefix + "-store-start.png"))
			print("POINTS ", _world.debug_add_science_points(_local_index, _spell_points))
			for round in 6:
				await _frames(6)
				var st: Dictionary = _hud.get_spellbook_state().store
				var clicked := false
				for i in st.states.size():
					if st.states[i] == "_active":
						if _hud.spell_store_click(i):
							print("STORE CLICK ", st.sciences[i] if i < st.sciences.size() else i)
							clicked = true
						break
				if not clicked:
					break
			await _frames(15)
			print("STORE ", JSON.stringify(_hud.get_spellbook_state().store))
			_save(shots.path_join(_shot_prefix + "-store-pending.png"))
			_hud.close_spell_store()
			await _frames(20)
			_hud.open_spell_store()
			await _frames(45)
			print("STORE REOPENED ", JSON.stringify(_hud.get_spellbook_state().store))
			_save(shots.path_join(_shot_prefix + "-store-purchased.png"))
			_hud.close_spell_store()
			await _frames(30)
			sb = _hud.get_spellbook_state()
			print("SPELLBOOK slots ", sb.slots)
			_save(shots.path_join(_shot_prefix + "-palantir-book.png"))
			var targeted := false
			for k in 24:
				sb = _hud.get_spellbook_state()
				if sb.slots[k] == "_unused":
					break
				if sb.slots[k] != "_up":
					continue
				var spot := at + Vector2(float((k % 3) - 1) * 90.0, float(k / 3) * 60.0)
				_cam_target = spot
				_place_camera()
				await _frames(10)
				var before: int = _world.get_object_count()
				var pressed: bool = _hud.press_spell_slot(k)
				await _frames(2)
				sb = _hud.get_spellbook_state()
				var power: String = sb.get("target_power", "")
				_spell_text = "SPELL-2: spell book slot %d %s" % [k + 1, power if not power.is_empty() else "(no target)"]
				if sb.targeting:
					var px: Vector2 = _hud.world_to_pixel(spot)
					ring.mouse = px
					_hud.inject_mouse_move(px)
					await _frames(8)
					if not targeted:
						_save(shots.path_join(_shot_prefix + "-targeting.png"))
						targeted = true
					_hud.inject_mouse_button(1, true, px, false)
					_hud.inject_mouse_button(1, false, px, false)
				print("CAST slot %d %s pressed=%s" % [k + 1, power, pressed])
				await _frames(_spell_wait)
				print("AFTER slot %d objects %d -> %d" % [k + 1, before, _world.get_object_count()])
				_save(shots.path_join(_shot_prefix + "-slot%02d.png" % (k + 1)))
			_spell_text = "SPELL-2: the spell book recharging"
			await _frames(30)
			print("SPELLBOOK slots ", _hud.get_spellbook_state().slots)
			_save(shots.path_join(_shot_prefix + "-bar.png"))
		"garrison_hud":
			# lane UI-1: the first --spawn is a container (GondorKeep), the second a horde (GondorArcherHorde): the container selected empty (its EXIT_CONTAINER
			# buttons disabled), the horde ordered in (MSG_ENTER), the container selected again (the rider on the first button), <prefix>-empty / -garrisoned
			if _shot_prefix == "proj1":
				_shot_prefix = "ui1-garrison"
			var keys: Array = _spawned.keys()
			var tower_id: int = _spawned[keys[0]]
			var horde_id: int = _spawned[keys[1]]
			var t: Dictionary = _world.get_object(tower_id)
			_cam_target = Vector2(t.x, t.y)
			_cam_height = 420.0
			_cam_back = 330.0
			_place_camera()
			await _frames(30)
			await _click_world(Vector2(t.x, t.y))
			await _frames(60)
			print("EMPTY ", JSON.stringify(_hud.get_state().get("control_bar", {})))
			_save(shots.path_join(_shot_prefix + "-empty.png"))
			# the HUD's own selection would replace the order's: deselect first (a click on open ground next to the tower)
			await _click_world(Vector2(t.x, t.y) + Vector2(0, -260))
			await _frames(20)
			print("ENTER ", _world.order_garrison([horde_id], tower_id))
			for i in 900:
				await _frames(1)
				var g: Dictionary = _world.get_garrison(tower_id)
				if int(g.get("count", 0)) > 0 and not g.get("entering", false):
					break
			await _frames(30)
			for attempt in 6:
				await _click_world(Vector2(t.x, t.y) + Vector2(0, attempt * 15))
				await _frames(40)
				if _hud.get_selection().has(tower_id):
					break
			await _frames(30)
			print("GARRISONED ", JSON.stringify(_world.get_garrison(tower_id)), " ", JSON.stringify(_hud.get_state().get("control_bar", {})))
			_save(shots.path_join(_shot_prefix + "-garrisoned.png"))
		"hud4":
			# lane HUD-4: --spawn=DwarvenSiegeWorks:.. --spawn=RohanRohirrimHorde:..: the Forge Works' side bar (its technologies), the Rohirrim's arc and its
			# weapon toggle pressed on the Palantir (bow -> spear image), the radar's shroud picture; saves hud4-*.png
			var keys: Array = _spawned.keys()
			var fw: Dictionary = _world.get_object(_spawned[keys[0]])
			var rh: Dictionary = _world.get_object(_spawned[keys[1]])
			_cam_target = Vector2(fw.x, fw.y)
			_place_camera()
			await _frames(20)
			await _click_world(Vector2(fw.x, fw.y))
			await _frames(60)
			print("HUD4 forge ", JSON.stringify(_hud.get_command_buttons()))
			_save(shots.path_join("hud4-forge-works.png"))
			_cam_target = Vector2(rh.x, rh.y)
			_cam_height = 330.0
			_cam_back = 260.0
			_place_camera()
			await _frames(40)
			var a4: Vector2 = _hud.world_to_pixel(Vector2(rh.x, rh.y) + Vector2(-110, 90))
			var b4: Vector2 = _hud.world_to_pixel(Vector2(rh.x, rh.y) + Vector2(110, -90))
			_hud.inject_mouse_move(a4)
			await _frames(2)
			_hud.inject_mouse_button(1, true, a4, false)
			await _frames(2)
			_hud.inject_mouse_move((a4 + b4) * 0.5)
			await _frames(2)
			_hud.inject_mouse_move(b4)
			await _frames(2)
			_hud.inject_mouse_button(1, false, b4, false)
			await _frames(60)
			_save(shots.path_join("hud4-rohirrim-spear.png"))
			for b in _hud.get_command_buttons():
				if b.command == "TOGGLE_WEAPONSET" and b.in_palantir:
					var w: Dictionary = _hud.find_button_window(b.frame)
					print("HUD4 toggle ", JSON.stringify(b), " window ", JSON.stringify(w))
					if w.get("found", false):
						_hud.inject_mouse_move(Vector2(w.x, w.y))
						await _frames(2)
						_hud.inject_mouse_button(1, true, Vector2(w.x, w.y), false)
						await _frames(2)
						_hud.inject_mouse_button(1, false, Vector2(w.x, w.y), false)
					break
			await _frames(90)
			print("HUD4 after toggle ", JSON.stringify(_hud.get_state().get("control_bar", {})))
			_save(shots.path_join("hud4-rohirrim-bow.png"))
			# D: the aggressive stance (STANCE_AGGRESSIVE, DIK 0x20)
			_hud.inject_key(0x20, true)
			await _frames(2)
			_hud.inject_key(0x20, false)
			await _frames(30)
			# Ctrl+H: the next hero (SELECT_HERO: LCTRL 0x1D, H 0x23)
			_hud.inject_key(0x1D, true)
			await _frames(2)
			_hud.inject_key(0x23, true)
			await _frames(2)
			_hud.inject_key(0x23, false)
			_hud.inject_key(0x1D, false)
			await _frames(60)
			print("HUD4 hero ", _hud.get_selection())
			_save(shots.path_join("hud4-select-hero.png"))
		"select_building":
			# the first spawned object: select it by a click, wait for the bar to fill, save the picture
			var name: String = _spawned.keys()[0]
			var o: Dictionary = _world.get_object(_spawned[name])
			if not o.get("ok", false):
				_fail("the spawned object %s does not exist (%s)" % [name, o])
				return
			_cam_target = Vector2(o.x, o.y)
			_place_camera()
			await _frames(10)
			await _click_world(Vector2(o.x, o.y))
			await _frames(60)
			print("OBJ ", o, "\nSELECT ", _hud.get_selection(), " ", JSON.stringify(_hud.get_state().get("control_bar", {})))
			_save(shots.path_join("hud1-select-building.png"))
		"select_horde":
			var hn: String = _spawned.keys()[0]
			var ho: Dictionary = _world.get_object(_spawned[hn])
			if not ho.get("ok", false):
				_fail("the spawned object %s does not exist" % hn)
				return
			_cam_target = Vector2(ho.x, ho.y)
			_cam_height = 330.0
			_cam_back = 260.0
			_place_camera()
			await _frames(40)
			var a: Vector2 = _hud.world_to_pixel(Vector2(ho.x, ho.y) + Vector2(-90, 70))
			var b: Vector2 = _hud.world_to_pixel(Vector2(ho.x, ho.y) + Vector2(90, -70))
			_hud.inject_mouse_move(a)
			await _frames(2)
			_hud.inject_mouse_button(1, true, a, false)
			await _frames(2)
			_hud.inject_mouse_move((a + b) * 0.5)
			await _frames(2)
			_hud.inject_mouse_move(b)
			await _frames(2)
			_hud.inject_mouse_button(1, false, b, false)
			await _frames(60)
			print("SELECT ", _hud.get_selection(), " ", JSON.stringify(_hud.get_state().get("control_bar", {})))
			_save(shots.path_join("hud1-select-horde.png"))
		"radar":
			var rn: String = _spawned.keys()[0]
			var ro: Dictionary = _world.get_object(_spawned[rn])
			_cam_target = Vector2(ro.x, ro.y) + Vector2(900, 500)
			_place_camera()
			await _frames(40)
			var sq: Rect2 = _hud.get_radar_square()
			print("RADAR square ", sq)
			var target: Vector2 = sq.position + sq.size * Vector2(0.48, 0.55)
			_hud.inject_mouse_move(target)
			await _frames(2)
			_hud.inject_mouse_button(1, true, target, false)
			await _frames(2)
			_hud.inject_mouse_button(1, false, target, false)
			await _frames(60)
			_save(shots.path_join("hud1-radar.png"))
		"camera":
			# lane CAM-1: a tour of the retail camera with the game's own input: scroll with the arrow keys, zoom to both limits with the wheel, rotate with the middle button, reset
			# with a middle click, scroll into the map border, jump by a radar click; a screenshot at each stage (cam1-*.png) and the camera state printed
			var centre := Vector2(get_viewport().get_visible_rect().size * 0.5)
			var c0: Dictionary = _hud.get_camera()
			print("CAMERA start ", JSON.stringify(_cam_report(c0)))
			print("CAMERA map overrides ", c0.map_overrides, " height field ready ", c0.height_field_ready)
			_save(shots.path_join("cam1-start.png"))
			_hud.inject_mouse_move(centre)
			await _frames(5)
			_hud.inject_key(0xCD, true)
			await _frames(90)
			_hud.inject_key(0xCD, false)
			await _frames(10)
			var c1: Dictionary = _hud.get_camera()
			print("CAMERA after 90 frames of the right arrow: position ", c1.position, " (was ", c0.position, ")")
			_save(shots.path_join("cam1-scroll-right.png"))
			var worst_pick := 0.0
			for i in 60:
				_hud.inject_mouse_wheel(1, centre)
				await _frames(1)
				worst_pick = maxf(worst_pick, _pick_error())
			await _frames(60)
			var c2: Dictionary = _hud.get_camera()
			print("CAMERA picking vs the displayed camera while zooming: worst error %.2f px" % worst_pick)
			if worst_pick > 1.5:
				_fail("the HUD's projection disagrees with the displayed camera by %.2f px" % worst_pick)
				return
			print("CAMERA zoomed in: height ", c2.height_above_ground, " (min ", c2.min_height, ") zoom ", c2.zoom, " eye ", c2.eye)
			_save(shots.path_join("cam1-zoom-in.png"))
			for i in 90:
				_hud.inject_mouse_wheel(-1, centre)
				await _frames(1)
			await _frames(60)
			var c3: Dictionary = _hud.get_camera()
			print("CAMERA zoomed out: height ", c3.height_above_ground, " (max ", c3.max_height, ") zoom ", c3.zoom, " eye ", c3.eye)
			_save(shots.path_join("cam1-zoom-out.png"))
			_hud.inject_mouse_button(3, true, centre, false)
			await _frames(2)
			for i in 40:
				_hud.inject_mouse_move(centre + Vector2(i * 6, 0))
				await _frames(1)
			_hud.inject_mouse_button(3, false, centre + Vector2(240, 0), false)
			await _frames(20)
			var c4: Dictionary = _hud.get_camera()
			print("CAMERA rotated: angle ", c4.angle)
			_save(shots.path_join("cam1-rotated.png"))
			_hud.inject_mouse_move(centre)
			_hud.inject_mouse_button(3, true, centre, false)
			await _frames(2)
			_hud.inject_mouse_button(3, false, centre, false)
			await _frames(40)
			var c5: Dictionary = _hud.get_camera()
			print("CAMERA after a middle click: angle ", c5.angle, " height ", c5.height_above_ground)
			_save(shots.path_join("cam1-reset.png"))
			_hud.inject_key(0xCB, true)
			await _frames(400)
			_hud.inject_key(0xCB, false)
			await _frames(30)
			var c6: Dictionary = _hud.get_camera()
			print("CAMERA at the left border: position ", c6.position, " constraint ", c6.constraint)
			_save(shots.path_join("cam1-border.png"))
			var sq2: Rect2 = _hud.get_radar_square()
			var jump: Vector2 = sq2.position + sq2.size * Vector2(0.7, 0.5)
			_hud.inject_mouse_move(jump)
			await _frames(2)
			_hud.inject_mouse_button(1, true, jump, false)
			await _frames(2)
			_hud.inject_mouse_button(1, false, jump, false)
			await _frames(60)
			var c7: Dictionary = _hud.get_camera()
			print("CAMERA after the radar click: position ", c7.position, " height ", c7.height_above_ground, " terrain ", c7.terrain_height)
			_save(shots.path_join("cam1-radar-jump.png"))
		"idle":
			await _frames(30)
			_save(shots.path_join("hud1-idle.png"))
		"audio":
			# lane AUDIO-2: box-select our horde (VoiceSelect), move it (VoiceMove), select the barracks (the second --spawn) and press the unit's button
			# (--build=<Template>: VoiceCreated / Eva when it comes out), select the horde again and attack the enemy (VoiceAttack), follow the fight
			var mine_a: int = _spawned[_spawned.keys()[0]]
			var theirs_a: int = _enemies[_enemies.keys()[0]]
			var ma: Dictionary = _world.get_object(mine_a)
			var ea: Dictionary = _world.get_object(theirs_a)
			_cam_target = Vector2(ma.x, ma.y)
			_cam_height = 330.0
			_cam_back = 260.0
			_place_camera()
			await _frames(45)
			await _box_select(Vector2(ma.x, ma.y))
			print("AUDIO select: ", _audio.get_playing() if _audio else [])
			await _frames(150) # a unit does not talk over itself (ZH violatesVoice): the next order waits for the voice
			await _click_world(Vector2(ma.x, ma.y) + Vector2(-170, -130))
			print("AUDIO move: ", _audio.get_playing() if _audio else [])
			await _frames(150)
			if _spawned.size() > 1:
				var bo: Dictionary = _world.get_object(_spawned[_spawned.keys()[1]])
				_cam_target = Vector2(bo.x, bo.y)
				_place_camera()
				await _frames(30)
				await _click_world(Vector2(bo.x, bo.y))
				await _frames(30)
				if not _build.is_empty():
					print("AUDIO objects before build: ", _world.get_object_ids().size())
					var pressed: bool = _hud.press_command_button(_build)
					if not pressed:
						_world.queue_unit(_local_index, _spawned[_spawned.keys()[1]], _build)
						pressed = true
					print("AUDIO build ", _build, ": ", pressed)
					await _frames(20)
					print("AUDIO production: ", _world.get_production(_local_index, _spawned[_spawned.keys()[1]]))
				_world.set_time_scale(4.0) # the build time passes quickly (the logic speed only)
				for i in 420:
					await get_tree().process_frame
					if i % 60 == 0 and _audio and not _audio.get_playing().is_empty():
						print("AUDIO building ", i, ": ", _audio.get_playing())
				_world.set_time_scale(1.0)
				print("AUDIO objects after build: ", _world.get_object_ids().size(), " world audio ", _world.get_audio_report().get("unit_voices_without_handler", -1))
				print("AUDIO after build: ", _audio.get_playing() if _audio else [], " eva ", _audio.get_eva_report() if _audio else {})
			ma = _world.get_object(mine_a)
			ea = _world.get_object(theirs_a)
			_cam_target = (Vector2(ma.x, ma.y) + Vector2(ea.x, ea.y)) * 0.5
			_cam_height = 380.0
			_cam_back = 300.0
			_place_camera()
			await _frames(30)
			await _box_select(Vector2(ma.x, ma.y))
			await _frames(150)
			await _click_world(Vector2(ea.x, ea.y))
			print("AUDIO attack: ", _audio.get_playing() if _audio else [])
			for i in 700:
				var hoa: Dictionary = _world.get_object(theirs_a)
				var oursa: Dictionary = _world.get_object(mine_a)
				if hoa.get("ok", false) and oursa.get("ok", false):
					_cam_target = _cam_target.lerp((Vector2(hoa.x, hoa.y) + Vector2(oursa.x, oursa.y)) * 0.5, 0.1)
					_place_camera()
				await get_tree().process_frame
				if i % 60 == 0:
					print("AUDIO fight ", i, ": ", _audio.get_playing() if _audio else [])
				if not hoa.get("ok", false):
					break
			await _frames(90)
			_save(shots.path_join("audio2-aftermath.png"))
		"combat":
			# lane COMBAT-1: our horde (the first --spawn) and the opponent's horde (the first --enemy). Box-select ours, click the enemy (the game's attack order), follow the
			# fight, save combat1-select / combat1-fight / combat1-aftermath, and print the combat report. Needs --spawn=<Horde>:dx,dy --enemy=<Horde>:dx,dy.
			var mine: int = _spawned[_spawned.keys()[0]]
			var theirs: int = _enemies[_enemies.keys()[0]]
			var mo: Dictionary = _world.get_object(mine)
			var eo: Dictionary = _world.get_object(theirs)
			if not mo.get("ok", false) or not eo.get("ok", false):
				_fail("combat needs a spawned horde and an enemy horde (%s, %s)" % [mo, eo])
				return
			var mid := (Vector2(mo.x, mo.y) + Vector2(eo.x, eo.y)) * 0.5
			_cam_target = mid
			_cam_height = 380.0
			_cam_back = 300.0
			_place_camera()
			await _frames(45)
			var pa: Vector2 = _hud.world_to_pixel(Vector2(mo.x, mo.y) + Vector2(-90, 70))
			var pb: Vector2 = _hud.world_to_pixel(Vector2(mo.x, mo.y) + Vector2(90, -70))
			_hud.inject_mouse_move(pa)
			await _frames(3)
			_hud.inject_mouse_button(1, true, pa, false)
			for t in 12:
				_hud.inject_mouse_move(pa.lerp(pb, (t + 1) / 12.0))
				await _frames(2)
			_hud.inject_mouse_button(1, false, pb, false)
			await _frames(40)
			print("SELECT ", _hud.get_selection())
			_save(shots.path_join("combat1-select.png"))
			await _click_world(Vector2(eo.x, eo.y))
			await _frames(30)
			print("ORDERED: enemy ", _world.get_object(theirs).get("health", -1.0), " report ", _world.get_combat_report())
			var saved_fight := false
			for i in 900:
				var ho: Dictionary = _world.get_object(theirs)
				var ours: Dictionary = _world.get_object(mine)
				if ho.get("ok", false) and ours.get("ok", false):
					_cam_target = _cam_target.lerp((Vector2(ho.x, ho.y) + Vector2(ours.x, ours.y)) * 0.5, 0.1)
					_place_camera()
				await get_tree().process_frame
				var rep2: Dictionary = _world.get_combat_report()
				if not saved_fight and rep2.get("kills", 0) >= 3:
					saved_fight = true
					_save(shots.path_join("combat1-fight.png"))
				if not ho.get("ok", false):
					break
			await _frames(60)
			print("FIGHT OVER: ", _world.get_combat_report(), " ours ", _world.get_object(mine).get("members", []).size(), " members left")
			_save(shots.path_join("combat1-aftermath.png"))
		"horde2":
			# lane HORDE-2: our horde (the first --spawn, e.g. RohanRohirrimHorde) charges the opponent's horde (the first --enemy: MordorFighterHorde for the charge,
			# GondorTowerShieldGuardHorde for pikes against cavalry). Box-select ours, click the enemy (the game's attack order), follow the charge at the logic speed of
			# --timescale (default 0.25), save <prefix>-select / -contact / -melee / -aftermath (--prefix, e.g. horde2-charge) and print the combat report.
			var mine2: int = _spawned[_spawned.keys()[0]]
			var theirs2: int = _enemies[_enemies.keys()[0]]
			var mo2: Dictionary = _world.get_object(mine2)
			var eo2: Dictionary = _world.get_object(theirs2)
			if not mo2.get("ok", false) or not eo2.get("ok", false):
				_fail("horde2 needs a spawned horde and an enemy horde (%s, %s)" % [mo2, eo2])
				return
			_cam_target = (Vector2(mo2.x, mo2.y) + Vector2(eo2.x, eo2.y)) * 0.5
			_cam_height = 330.0
			_cam_back = 260.0
			_place_camera()
			await _frames(40)
			var qa: Vector2 = _hud.world_to_pixel(Vector2(mo2.x, mo2.y) + Vector2(-90, 70))
			var qb: Vector2 = _hud.world_to_pixel(Vector2(mo2.x, mo2.y) + Vector2(90, -70))
			_hud.inject_mouse_move(qa)
			await _frames(3)
			_hud.inject_mouse_button(1, true, qa, false)
			for t2 in 12:
				_hud.inject_mouse_move(qa.lerp(qb, (t2 + 1) / 12.0))
				await _frames(2)
			_hud.inject_mouse_button(1, false, qb, false)
			await _frames(20)
			_save(shots.path_join(_shot_prefix + "-select.png"))
			_world.set_time_scale(_time_scale)
			await _click_world(Vector2(eo2.x, eo2.y))
			var shot_contact := false
			var shot_melee := false
			var start_kills: int = _world.get_combat_report().get("kills", 0)
			for i2 in 1800:
				var ho2: Dictionary = _world.get_object(theirs2)
				var ours2: Dictionary = _world.get_object(mine2)
				if ho2.get("ok", false) and ours2.get("ok", false):
					_cam_target = _cam_target.lerp((Vector2(ho2.x, ho2.y) + Vector2(ours2.x, ours2.y)) * 0.5, 0.06)
					_place_camera()
				await get_tree().process_frame
				var k2: int = _world.get_combat_report().get("kills", 0) - start_kills
				if not shot_contact and k2 >= 1:
					shot_contact = true
					_save(shots.path_join(_shot_prefix + "-contact.png"))
				if not shot_melee and k2 >= 6:
					shot_melee = true
					_save(shots.path_join(_shot_prefix + "-melee.png"))
				if not ho2.get("ok", false) or not ours2.get("ok", false):
					break
			_world.set_time_scale(1.0)
			await _frames(90)
			print("HORDE2 OVER: ", _world.get_combat_report())
			_save(shots.path_join(_shot_prefix + "-aftermath.png"))
		"projectiles":
			# lane PROJ-1: our shooters (the first --spawn) at the opponent's horde (the first --enemy), the game's own attack order, the logic slowed down so the flights can be seen:
			# proj1-select, proj1-volley (projectiles in the air), proj1-impact (the first loss), proj1-aftermath. Prints the combat report with the projectile counters.
			var mine: int = _spawned[_spawned.keys()[0]]
			var theirs: int = _enemies[_enemies.keys()[0]]
			var mo: Dictionary = _world.get_object(mine)
			var eo: Dictionary = _world.get_object(theirs)
			if not mo.get("ok", false) or not eo.get("ok", false):
				_fail("projectiles needs a spawned unit and an enemy horde (%s, %s)" % [mo, eo])
				return
			_cam_target = (Vector2(mo.x, mo.y) + Vector2(eo.x, eo.y)) * 0.5
			_cam_height = 240.0
			_cam_back = 190.0
			_place_camera()
			await _frames(45)
			var pa2: Vector2 = _hud.world_to_pixel(Vector2(mo.x, mo.y) + Vector2(-90, 70))
			var pb2: Vector2 = _hud.world_to_pixel(Vector2(mo.x, mo.y) + Vector2(90, -70))
			_hud.inject_mouse_move(pa2)
			await _frames(3)
			_hud.inject_mouse_button(1, true, pa2, false)
			for t in 12:
				_hud.inject_mouse_move(pa2.lerp(pb2, (t + 1) / 12.0))
				await _frames(2)
			_hud.inject_mouse_button(1, false, pb2, false)
			await _frames(30)
			print("SELECT ", _hud.get_selection())
			_save(shots.path_join(_shot_prefix + "-select.png"))
			await _click_world(Vector2(eo.x, eo.y))
			_world.set_time_scale(_time_scale)
			if _bow_shot:
				# RENDER-2: the shooters up close (the retail camera's lowest height) until arrows are in the air just in front of them (they start at the ARROW bone of the bow)
				_cam_target = Vector2(mo.x, mo.y) + Vector2(70, 10)
				_cam_height = 0.0
				_place_camera()
				for i in 900:
					await get_tree().process_frame
					var near := 0
					for pr in _world.get_projectiles():
						var dist := Vector2(pr.x, pr.y).distance_to(Vector2(mo.x, mo.y))
						if dist > 30.0 and dist < 100.0:
							near += 1
					if near >= 4:
						await _frames(6) # the streak ribbons need a few trail points
						_save(shots.path_join(_shot_prefix + "-bow.png"))
						break
				_cam_target = (Vector2(mo.x, mo.y) + Vector2(eo.x, eo.y)) * 0.5
				_cam_height = 240.0
				_place_camera()
			var saved_volley := false
			var saved_impact := false
			var det_at_volley := 0
			var max_air := 0
			for i in 1500:
				await get_tree().process_frame
				var ps: Array = _world.get_projectiles()
				max_air = maxi(max_air, ps.size())
				var rep3: Dictionary = _world.get_combat_report()
				var ho2: Dictionary = _world.get_object(theirs)
				var ours2: Dictionary = _world.get_object(mine)
				if ho2.get("ok", false) and ours2.get("ok", false):
					_cam_target = _cam_target.lerp((Vector2(ho2.x, ho2.y) + Vector2(ours2.x, ours2.y)) * 0.5, 0.02)
					_place_camera()
				if not saved_volley and ps.size() >= _min_air:
					saved_volley = true
					det_at_volley = rep3.get("projectiles_detonated", 0)
					_save(shots.path_join(_shot_prefix + "-volley.png"))
				elif saved_volley and not saved_impact and rep3.get("projectiles_detonated", 0) >= det_at_volley + mini(6, _min_air) and ps.size() >= 1:
					saved_impact = true
					_save(shots.path_join(_shot_prefix + "-impact.png"))
				elif saved_impact and (rep3.get("projectiles_detonated", 0) >= det_at_volley + 40 or max_air < 0 or not ho2.get("ok", false)):
					break
			print("PROJECTILES: max in the air ", max_air, " report ", _world.get_combat_report())
			_save(shots.path_join(_shot_prefix + "-aftermath.png"))
		"streaks_paused":
			# lane CAM-1 review r2: the streak ribbons face the camera of the frame also while the game is paused. Arrows in the air (--spawn archers, --enemy a horde), the
			# game paused, the camera turned with the middle button: every ribbon's width must be perpendicular to its view ray and the world hash must not change.
			var mine_p: int = _spawned[_spawned.keys()[0]]
			var theirs_p: int = _enemies[_enemies.keys()[0]]
			var mo_p: Dictionary = _world.get_object(mine_p)
			var eo_p: Dictionary = _world.get_object(theirs_p)
			if not mo_p.get("ok", false) or not eo_p.get("ok", false):
				_fail("streaks_paused needs a spawned unit and an enemy horde")
				return
			_cam_target = (Vector2(mo_p.x, mo_p.y) + Vector2(eo_p.x, eo_p.y)) * 0.5
			_place_camera()
			await _frames(45)
			var pa3: Vector2 = _hud.world_to_pixel(Vector2(mo_p.x, mo_p.y) + Vector2(-90, 70))
			var pb3: Vector2 = _hud.world_to_pixel(Vector2(mo_p.x, mo_p.y) + Vector2(90, -70))
			_hud.inject_mouse_move(pa3)
			await _frames(3)
			_hud.inject_mouse_button(1, true, pa3, false)
			for t in 12:
				_hud.inject_mouse_move(pa3.lerp(pb3, (t + 1) / 12.0))
				await _frames(2)
			_hud.inject_mouse_button(1, false, pb3, false)
			await _frames(30)
			await _click_world(Vector2(eo_p.x, eo_p.y))
			_world.set_time_scale(_time_scale)
			var strip_quads := 0
			for i in 1500:
				await get_tree().process_frame
				strip_quads = _streak_quads().size()
				if strip_quads >= 3:
					break
			if strip_quads < 3:
				_fail("no arrow ribbons to check (%d)" % strip_quads)
				return
			_world.set_paused(true)
			await _frames(5)
			var hash_before: int = _world.get_state_hash()
			var frame_before: int = _world.get_frame()
			var angle_before: float = _hud.get_camera().angle
			var mid := Vector2(get_viewport().get_visible_rect().size * 0.5)
			_hud.inject_mouse_move(mid)
			_hud.inject_mouse_button(3, true, mid, false)
			await _frames(2)
			for i in 30:
				_hud.inject_mouse_move(mid + Vector2(i * 8, 0))
				await _frames(1)
			_hud.inject_mouse_button(3, false, mid + Vector2(240, 0), false)
			await _frames(10)
			var turned: float = _hud.get_camera().angle - angle_before
			var quads: Array = _streak_quads()
			var worst := 0.0
			var eye: Vector3 = _camera.global_position
			for q in quads:
				var width: Vector3 = (q[0] - q[1]).normalized()
				var ray: Vector3 = ((q[0] + q[1]) * 0.5 - eye).normalized()
				worst = maxf(worst, absf(width.dot(ray)))
			print("STREAKS PAUSED: camera turned %.3f rad, %d ribbon quads, worst |width . view ray| = %.4f, hash %d -> %d, frame %d -> %d" % [turned, quads.size(), worst, hash_before, _world.get_state_hash(), frame_before, _world.get_frame()])
			if absf(turned) < 0.5:
				_fail("the camera did not turn while paused")
				return
			if quads.is_empty() or worst > 0.05:
				_fail("the ribbons do not face the camera while paused (worst %.3f)" % worst)
				return
			if _world.get_state_hash() != hash_before or _world.get_frame() != frame_before:
				_fail("the world moved while paused")
				return
			_world.set_paused(false)
		"combat_building":
			# lane COMBAT-2: our horde (the first --spawn) attacks the opponent's building (the first --enemy). A second --spawn (a structure of ours) keeps our side in the game once the
			# victory rules are on. Box-select ours, click the building (the game's attack order), follow the fight: combat2-select, combat2-attack (the first hits), combat2-damaged
			# (the damage state REALLYDAMAGED), combat2-collapse (the building sinks), combat2-rubble (it is gone), combat2-victory (the opponent is eliminated). Prints the reports.
			var attackers: int = _spawned[_spawned.keys()[0]]
			var building: int = _enemies[_enemies.keys()[0]]
			var ao: Dictionary = _world.get_object(attackers)
			var bo2: Dictionary = _world.get_object(building)
			if not ao.get("ok", false) or not bo2.get("ok", false):
				_fail("combat_building needs a spawned horde and an enemy building (%s, %s)" % [ao, bo2])
				return
			_world.start_victory_rules(PackedStringArray(["Player_1", "Player_2"]))
			var bpos := Vector2(bo2.x, bo2.y)
			_cam_target = (Vector2(ao.x, ao.y) + bpos) * 0.5
			_cam_height = 420.0
			_cam_back = 330.0
			_place_camera()
			await _frames(45)
			var qa: Vector2 = _hud.world_to_pixel(Vector2(ao.x, ao.y) + Vector2(-90, 70))
			var qb: Vector2 = _hud.world_to_pixel(Vector2(ao.x, ao.y) + Vector2(90, -70))
			_hud.inject_mouse_move(qa)
			await _frames(3)
			_hud.inject_mouse_button(1, true, qa, false)
			for t in 12:
				_hud.inject_mouse_move(qa.lerp(qb, (t + 1) / 12.0))
				await _frames(2)
			_hud.inject_mouse_button(1, false, qb, false)
			await _frames(40)
			print("SELECT ", _hud.get_selection())
			_save(shots.path_join("combat2-select.png"))
			await _click_world(bpos)
			await _frames(30)
			var max_health: float = _world.get_object(building).get("max_health", 1.0)
			print("ORDERED: building ", _world.get_object(building).get("health", -1.0), "/", max_health, " report ", _world.get_combat_report())
			var saved := {}
			for i in 9000:
				var bd: Dictionary = _world.get_object(building)
				var od: Dictionary = _world.get_object(attackers)
				if od.get("ok", false):
					_cam_target = _cam_target.lerp((bpos + Vector2(od.x, od.y)) * 0.5, 0.05)
					_place_camera()
				await get_tree().process_frame
				if not bd.get("ok", false):
					break
				var hp: float = bd.get("health", max_health)
				if not saved.has("attack") and hp < max_health * 0.9:
					saved["attack"] = true
					_save(shots.path_join("combat2-attack.png"))
				if not saved.has("damaged") and bd.get("damage_state", 0) >= 2 and hp > 0.0:
					saved["damaged"] = true
					_save(shots.path_join("combat2-damaged.png"))
					print("DAMAGED: ", bd)
				if not saved.has("collapse") and bd.get("z", 0.0) < -20.0:
					saved["collapse"] = true
					_save(shots.path_join("combat2-collapse.png"))
					print("COLLAPSE: ", bd, " report ", _world.get_combat_report())
			await _frames(40)
			print("BUILDING GONE: ", _world.get_combat_report())
			_save(shots.path_join("combat2-rubble.png"))
			await _frames(30)
			var vr: Dictionary = _world.get_victory_report()
			print("VICTORY: ", vr)
			# the end-of-game message reaches the client: the viewer shows what the retail game tells the players (the logic's VictoryConditions events)
			var banner := Label.new()
			var lines := PackedStringArray()
			for ev in vr.get("events", []):
				if ev.kind == "player_defeated":
					lines.append("%s has been defeated (frame %d)" % [ev.player_name, ev.frame])
				else:
					lines.append("%s and allies are the last standing (frame %d)" % [ev.player_name, ev.frame])
			banner.text = ("VICTORY\n" if vr.get("local_victorious", false) else ("DEFEAT\n" if vr.get("local_defeated", false) else "")) + "\n".join(lines)
			banner.add_theme_font_size_override("font_size", 48)
			banner.add_theme_color_override("font_color", Color(1.0, 0.85, 0.4))
			banner.add_theme_color_override("font_outline_color", Color(0, 0, 0))
			banner.add_theme_constant_override("outline_size", 8)
			banner.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
			banner.set_anchors_and_offsets_preset(Control.PRESET_CENTER_TOP)
			banner.offset_top = 120.0
			var layer := CanvasLayer.new()
			layer.layer = 100
			layer.add_child(banner)
			add_child(layer)
			await _frames(10)
			_save(shots.path_join("combat2-victory.png"))
		"smooth_bench":
			await _smooth_bench()
		"perf3_battle":
			await _perf3_battle()
		"smooth_churn":
			await _smooth_churn()
		"smooth2":
			await _smooth2()
		"move2":
			await _move2()
		"fx_battle":
			await _fx_battle(shots)
		"phys1_melee":
			await _phys1_melee(shots)
		"showcase":
			# a recorded walkthrough (--write-movie): box-select the horde, order it away and follow it, make control group 1, deselect, recall the group,
			# order it back, then click the building to show its command buttons. Needs --spawn=<Building>:dx,dy --spawn=<Horde>:dx,dy (the building first).
			var names: Array = _spawned.keys()
			var bo: Dictionary = _world.get_object(_spawned[names[0]])
			var hid: int = _spawned[names[names.size() - 1]]
			var h0: Dictionary = _world.get_object(hid)
			if not bo.get("ok", false) or not h0.get("ok", false):
				_fail("showcase needs a spawned building and horde (%s, %s)" % [bo, h0])
				return
			_cam_target = Vector2(h0.x, h0.y)
			_cam_height = 330.0
			_cam_back = 260.0
			_place_camera()
			await _frames(45)
			var a: Vector2 = _hud.world_to_pixel(Vector2(h0.x, h0.y) + Vector2(-90, 70))
			var b: Vector2 = _hud.world_to_pixel(Vector2(h0.x, h0.y) + Vector2(90, -70))
			_hud.inject_mouse_move(a)
			await _frames(3)
			_hud.inject_mouse_button(1, true, a, false)
			for t in 12:
				_hud.inject_mouse_move(a.lerp(b, (t + 1) / 12.0))
				await _frames(2)
			_hud.inject_mouse_button(1, false, b, false)
			await _frames(40)
			await _click_world(Vector2(h0.x, h0.y) + Vector2(420, 260))
			await _follow(hid, 240)
			# control group 1 (Ctrl+1), deselect (Escape), recall it (1)
			_hud.inject_key(0x1D, true)
			_hud.inject_key(0x02, true)
			await _frames(3)
			_hud.inject_key(0x02, false)
			_hud.inject_key(0x1D, false)
			await _frames(20)
			_hud.inject_key(0x01, true)
			await _frames(3)
			_hud.inject_key(0x01, false)
			await _frames(30)
			_hud.inject_key(0x02, true)
			await _frames(3)
			_hud.inject_key(0x02, false)
			await _frames(30)
			await _click_world(Vector2(bo.x, bo.y) + Vector2(-60, -160))
			await _follow(hid, 240)
			_cam_target = Vector2(bo.x, bo.y)
			_place_camera()
			await _frames(20)
			await _click_world(Vector2(bo.x, bo.y))
			await _frames(120)
	_finish()


func _spell_state() -> String:
	var sb: Dictionary = _world.get_spellbook(_local_index)
	if not sb.get("ok", false):
		return "spell book: none"
	var line := "rank %d  power points %d  sciences %s" % [sb.rank, sb.points, ", ".join(PackedStringArray(sb.sciences))]
	for pw in sb.powers:
		if pw.name == _spell_power:
			line += "\n%s: %s  %d%%" % [pw.name, ("READY" if pw.ready else ("recharging" if pw.owned else "not bought")), int(pw.percent * 100.0)]
	return line


func _follow(id: int, frames: int) -> void:
	for i in frames:
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false):
			_cam_target = _cam_target.lerp(Vector2(o.x, o.y), 0.08)
			_place_camera()
		await get_tree().process_frame


# ---- lane SMOOTH-1: the frame-time benchmark and the motion probe ----------------------------------------------------------------------------------------

func _centroid(ids: Array) -> Vector2:
	var sum := Vector2.ZERO
	var n := 0
	for id in ids:
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false):
			sum += Vector2(o.x, o.y)
			n += 1
	return sum / maxf(n, 1)


func _probe_members(ids: Array, want: int) -> Array:
	# members of the live hordes (the objects the player sees walk), spread over the hordes; a horde-less object stands for itself
	var out: Array = []
	var k := 0
	while out.size() < want and k < 4:
		for id in ids:
			var o: Dictionary = _world.get_object(id)
			if not o.get("ok", false):
				continue
			var members: Array = o.get("members", [])
			if members.is_empty():
				if k == 0:
					out.append(id)
			elif k < members.size():
				out.append(members[(k * 5 + members.size() / 2) % members.size()])
			if out.size() >= want:
				break
		k += 1
	return out


func _pct(sorted: Array, q: float) -> float:
	if sorted.is_empty():
		return 0.0
	return sorted[clampi(int(q * (sorted.size() - 1) + 0.5), 0, sorted.size() - 1)]


func _summary(values: Array) -> String:
	var v := values.duplicate()
	v.sort()
	var sum := 0.0
	for x in v:
		sum += x
	return "mean %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f" % [sum / maxf(v.size(), 1), _pct(v, 0.5), _pct(v, 0.95), _pct(v, 0.99), _pct(v, 1.0)]


func _bench_segment(label: String, seconds: float, interp: bool) -> void:
	# (has_method: the same script measures an older build without these calls, the "before" of the benchmark)
	if _world.has_method("set_render_interpolation"):
		_world.set_render_interpolation(interp)
	var have_timers: bool = _world.has_method("get_frame_timings")
	var have_pose: bool = _world.has_method("get_render_pose")
	_caption = "%s  -  %s" % [label, "AFTER: interpolated between logic frames (SMOOTH-1)" if interp else "BEFORE: drawn at the 5 Hz logic transform (no interpolation)"]
	if not _bench_caption.is_empty():
		_caption = "%s  -  %s" % [label, _bench_caption]
	var times: Array = []
	var keys := ["logic_ms", "sync_ms", "streak_ms", "dyn_pose_ms", "dyn_upload_ms", "dyn_mapper_ms", "static_mapper_ms", "worker_frame_ms", "main_cpu_ms"]
	var timers := {}
	var hud_keys := ["hud_update_ms", "hud_camera_ms", "hud_cursor_ms"]
	for k in keys + hud_keys + ["render_cpu_ms", "render_gpu_ms", "draw_calls", "primitives_k"]:
		timers[k] = []
	var vp := get_viewport().get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp, true)
	var probes := _probe_members(_spawn_ids, 8)
	var last_pos := {}
	var last_logic := {}
	var moving := {}
	var steps: Array = []   # per probe and render frame where its logic moves: the on-screen step over the frame time (units per second)
	var stills := 0
	var travelled := 0.0
	var t_prev := Time.get_ticks_usec()
	var frames := 0
	var hitches: Array = []
	var cpu0 := _main_thread_cpu_ms()
	var stat_pids: Array = _perf_stat_start(label)
	var game_time := 0.0 # the engine's frame deltas (with --write-movie the frames are rendered slower than real time: the segment is game time)
	while game_time < seconds:
		await get_tree().process_frame
		game_time += get_process_delta_time()
		frames += 1
		var now := Time.get_ticks_usec()
		var dt := (now - t_prev) / 1000.0
		times.append(dt)
		t_prev = now
		var t: Dictionary = _world.get_frame_timings() if have_timers else _old_timings()
		for k in keys:
			timers[k].append(t.get(k, 0.0))
		# lane PERF-1: a hitch (a render frame over 12 ms, under 84 fps) with what the frame did (the C++ parts of sync_ms, the first-use work)
		if dt > 12.0 and hitches.size() < 40:
			hitches.append("t=%.2fs frame %.1f ms: logic %.2f shroud %.2f views %.2f (new models %d in %.2f ms) streaks %.2f (new materials %d) fx %.2f pose %.2f upload %.2f" % [game_time, dt,
				t.get("logic_ms", 0.0), t.get("shroud_ms", 0.0), t.get("views_ms", 0.0), t.get("new_models", 0), t.get("new_model_ms", 0.0), t.get("streak_ms", 0.0),
				t.get("new_streak_materials", 0), t.get("fx_ms", 0.0), t.get("dyn_pose_ms", 0.0), t.get("dyn_upload_ms", 0.0)])
		var ht: Dictionary = _hud.get_frame_timings() if _hud.has_method("get_frame_timings") else {}
		for k in hud_keys:
			timers[k].append(ht.get(k, 0.0))
		timers["render_cpu_ms"].append(RenderingServer.viewport_get_measured_render_time_cpu(vp) + RenderingServer.get_frame_setup_time_cpu())
		timers["render_gpu_ms"].append(RenderingServer.viewport_get_measured_render_time_gpu(vp))
		# lane PERF-3: the frame's draw calls and primitives (thousands), all viewports
		timers["draw_calls"].append(float(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME)))
		timers["primitives_k"].append(float(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME)) / 1000.0)
		for id in probes if have_pose else []:
			var rp: Dictionary = _world.get_render_pose(id)
			if not rp.get("ok", false):
				continue
			var pos := Vector2(rp.x, rp.y)
			var lp := Vector2(rp.logic_x, rp.logic_y)
			if last_pos.has(id):
				var logic_step: float = lp.distance_to(last_logic[id])
				# the logic moved within the last logic frame (0.2 s of game time): the drawable should move on screen in every render frame of it
				if logic_step > 0.01:
					moving[id] = game_time + 0.2
				if game_time <= moving.get(id, -1.0):
					var d: float = pos.distance_to(last_pos[id])
					steps.append(d * 1000.0 / maxf(dt, 0.001))
					travelled += d
					if d < 0.0001:
						stills += 1
			last_pos[id] = pos
			last_logic[id] = lp
	var mean_ms := 0.0
	for x in times:
		mean_ms += x
	mean_ms /= maxf(times.size(), 1)
	var cpu1 := _main_thread_cpu_ms()
	await _perf_stat_end(label, stat_pids, frames)
	if cpu0 >= 0.0 and frames > 0:
		print("SMOOTH CPU %s interp=%s main thread %.2f ms CPU per render frame (%.0f fps if the main thread were alone on its core)" % [label, interp, (cpu1 - cpu0) / frames,
			1000.0 / maxf((cpu1 - cpu0) / frames, 0.001)])
	var end_t: Dictionary = _world.get_frame_timings() if have_timers else {}
	print("SMOOTH PERF %s interp=%s frames=%d fps=%.1f frame_ms: %s objects=%d held_presentations=%d (since load: render frames the late logic worker held)" % [label, interp, frames,
		1000.0 / maxf(mean_ms, 0.001), _summary(times), _world.get_object_count(), end_t.get("held_presentations", -1)])
	for k in timers.keys():
		print("SMOOTH TIMER %s %s: %s" % [label, k, _summary(timers[k])])
	var sorted_times := times.duplicate()
	sorted_times.sort()
	var low_n := maxi(1, sorted_times.size() / 100)
	var low_sum := 0.0
	for i in range(sorted_times.size() - low_n, sorted_times.size()):
		low_sum += sorted_times[i]
	print("SMOOTH LOW %s interp=%s fps_1pct_low=%.1f (mean of the slowest 1%% of frames: %.2f ms) hitches(>12ms)=%d" % [label, interp, 1000.0 / maxf(low_sum / low_n, 0.001), low_sum / low_n,
		times.filter(func(x): return x > 12.0).size()])
	for h in hitches:
		print("SMOOTH HITCH %s %s" % [label, h])
	var mean_v := 0.0
	for v in steps:
		mean_v += v
	mean_v /= maxf(steps.size(), 1)
	var var_v := 0.0
	for v in steps:
		var_v += (v - mean_v) * (v - mean_v)
	var cv := sqrt(var_v / maxf(steps.size(), 1)) / maxf(mean_v, 0.0001)
	print("SMOOTH MOTION %s interp=%s probes=%d moving_samples=%d still_samples=%d (%.1f%%) travelled=%.0f speed_mean=%.1f/s cv=%.3f" % [label, interp, probes.size(), steps.size(), stills, 100.0 * stills / maxf(steps.size(), 1), travelled, mean_v, cv])


# lane PERF-3: --perf-stat: `perf stat` on the main thread (its id is the process id) and on the whole process for one segment
func _perf_stat_start(label: String) -> Array:
	if _perf_stat.is_empty():
		return []
	var pid := str(OS.get_process_id())
	var ev := "instructions:u,cycles:u"
	return [OS.create_process("perf", ["stat", "-x", ",", "-e", ev, "-t", pid, "-o", "%s-%s-main.txt" % [_perf_stat, label]]),
		OS.create_process("perf", ["stat", "-x", ",", "-e", ev, "-p", pid, "-o", "%s-%s-all.txt" % [_perf_stat, label]])]


func _perf_stat_end(label: String, pids: Array, frames: int) -> void:
	if pids.is_empty():
		return
	for p in pids:
		OS.execute("kill", ["-INT", str(p)])
	await _frames(10)
	var parts := PackedStringArray()
	for which in ["main", "all"]:
		var f := FileAccess.open("%s-%s-%s.txt" % [_perf_stat, label, which], FileAccess.READ)
		if f == null:
			parts.append("%s: no output" % which)
			continue
		for line in f.get_as_text().split("\n"):
			var c := line.split(",")
			if c.size() > 2 and c[0].is_valid_int():
				parts.append("%s %s/frame=%.2fM" % [which, c[2].split(":")[0], float(c[0]) / maxf(frames, 1) / 1.0e6])
	print("PERF3 STAT %s frames=%d %s" % [label, frames, " ".join(parts)])


func _old_timings() -> Dictionary:
	# a build without get_frame_timings: the same numbers from get_stats (which also hashes the state: its frame times are not comparable, its timers are)
	var st: Dictionary = _world.get_stats()
	var dyn: Dictionary = st.get("dynamic", {})
	return {"logic_ms": st.get("last_logic_ms", 0.0), "sync_ms": st.get("last_sync_ms", 0.0), "dyn_pose_ms": dyn.get("pose_ms", 0.0), "dyn_upload_ms": dyn.get("upload_ms", 0.0),
		"dyn_mapper_ms": dyn.get("mapper_ms", 0.0), "static_mapper_ms": st.get("static", {}).get("mapper_ms", 0.0)}


func _smooth_bench() -> void:
	if _spawn_ids.is_empty() or _enemy_ids.is_empty():
		_fail("smooth_bench needs --spawn and --enemy objects")
		return
	var modes: Array = [false, true] if _bench_interp == "both" else [_bench_interp != "off"]
	var ours := _centroid(_spawn_ids)
	var theirs := _centroid(_enemy_ids)
	var dir := (theirs - ours).normalized()
	var side := Vector2(-dir.y, dir.x)
	_cam_target = ours
	_cam_height = 300.0
	_place_camera()
	await _frames(20)
	# march across the enemy's line of approach, then turn 90 degrees towards it, then the fight
	print("SMOOTH ORDER march ", _world.order_move(_spawn_ids, ours.x + side.x * 600.0, ours.y + side.y * 600.0, {}))
	for interp in modes:
		await _bench_follow_segment("march", interp)
	var here := _centroid(_spawn_ids)
	print("SMOOTH ORDER turn ", _world.order_move(_spawn_ids, here.x + dir.x * 500.0, here.y + dir.y * 500.0, {}))
	for interp in modes:
		await _bench_follow_segment("turn", interp)
	theirs = _centroid(_enemy_ids)
	here = _centroid(_spawn_ids)
	print("SMOOTH ORDER fight ", _world.order_move(_spawn_ids, theirs.x, theirs.y, {"type": "attack"}), " ", _world.order_move(_enemy_ids, here.x, here.y, {"type": "attack"}))
	for interp in modes:
		await _bench_follow_segment("fight", interp)
	print("SMOOTH COMBAT ", _world.get_combat_report())


# lane PERF-3: the 4-player mass battle (about 3000 objects with --army grids from tools/perf3/battle_args.py): both teams attack-move into the middle of all
# armies; the frame time is measured while they close in, while they fight, and while the camera sweeps over the fight (the instancer's culling and sorting)
func _perf3_battle() -> void:
	var ours: Array = _army_ids[1] + _army_ids[3]
	var theirs: Array = _army_ids[2] + _army_ids[4]
	if ours.is_empty() or theirs.is_empty():
		_fail("perf3_battle needs --army objects on both teams")
		return
	var mid := (_centroid(ours) + _centroid(theirs)) * 0.5
	_cam_target = mid
	_cam_height = 520.0
	_place_camera()
	await _frames(20)
	print("PERF3 ORDER approach ", _world.order_move(ours, mid.x, mid.y, {"type": "attack"}), " ", _world.order_move(theirs, mid.x, mid.y, {"type": "attack"}))
	if not _shots_dir.is_empty():
		_save(_shots_dir.path_join("perf3-start.png"))
	await _bench_segment("approach", _bench_seconds, true)
	await _bench_segment("melee", _bench_seconds, true)
	if not _shots_dir.is_empty():
		_save(_shots_dir.path_join("perf3-melee.png"))
	var t0 := Time.get_ticks_msec()
	var sweep := func() -> void:
		var u := (Time.get_ticks_msec() - t0) / 1000.0 / maxf(_bench_seconds, 0.001)
		_cam_target = mid + Vector2(cos(u * TAU) * 300.0, sin(u * TAU) * 200.0)
		_place_camera()
	get_tree().process_frame.connect(sweep)
	await _bench_segment("pan", _bench_seconds, true)
	get_tree().process_frame.disconnect(sweep)
	print("PERF3 COMBAT ", _world.get_combat_report(), " objects ", _world.get_object_count())


func _bench_follow_segment(label: String, interp: bool) -> void:
	# the camera stays put during a segment (the probe measures the drawable in world space, but a still camera keeps the video readable); it jumps to the action between segments
	var probes := _probe_members(_spawn_ids, 1)
	var o: Dictionary = _world.get_object(probes[0] if not probes.is_empty() else -1)
	if o.get("ok", false):
		_cam_target = Vector2(o.x, o.y)
		_place_camera()
	await _bench_segment(label, _bench_seconds, interp)


func _smooth2() -> void:
	if _spawn_ids.is_empty() or _enemy_ids.is_empty():
		_fail("smooth2 needs --spawn and --enemy objects")
		return
	var ours := _centroid(_spawn_ids)
	var theirs := _centroid(_enemy_ids)
	var dir := (theirs - ours).normalized()
	var side := Vector2(-dir.y, dir.x)
	_cam_target = ours
	_cam_height = _s2_height
	_place_camera()
	await _frames(20)
	var csv: FileAccess = FileAccess.open(_s2_csv, FileAccess.WRITE) if not _s2_csv.is_empty() else null
	if csv:
		csv.store_line("segment,t,dt,id,x,y,angle,frame,alpha,wall_us,held,horde")
	print("SMOOTH2 ORDER march ", _world.order_move(_spawn_ids, ours.x + side.x * 500.0, ours.y + side.y * 500.0, {}), " ", _world.order_move(_enemy_ids, theirs.x + side.x * 500.0, theirs.y + side.y * 500.0, {}))
	await _smooth2_segment("march", csv)
	var here := _centroid(_spawn_ids)
	theirs = _centroid(_enemy_ids)
	print("SMOOTH2 ORDER turn ", _world.order_move(_spawn_ids, here.x + dir.x * 220.0, here.y + dir.y * 220.0, {}), " ", _world.order_move(_enemy_ids, theirs.x - dir.x * 220.0, theirs.y - dir.y * 220.0, {}))
	await _smooth2_segment("turn", csv)
	here = _centroid(_spawn_ids)
	theirs = _centroid(_enemy_ids)
	print("SMOOTH2 ORDER charge ", _world.order_move(_spawn_ids, theirs.x, theirs.y, {"type": "attack"}), " ", _world.order_move(_enemy_ids, here.x, here.y, {"type": "attack"}))
	await _smooth2_segment("charge", csv)
	await _smooth2_segment("melee", csv)
	here = _centroid(_spawn_ids)
	theirs = _centroid(_enemy_ids)
	print("SMOOTH2 ORDER reform ", _world.order_move(_spawn_ids, here.x - dir.x * 300.0, here.y - dir.y * 300.0, {}), " ", _world.order_move(_enemy_ids, theirs.x + dir.x * 300.0, theirs.y + dir.y * 300.0, {}))
	await _smooth2_segment("reform", csv)
	if csv:
		csv.close()


func _smooth2_segment(label: String, csv: FileAccess) -> void:
	# the members of every live horde, read once (get_object waits for the logic worker); per render frame only the drawn poses (no wait)
	var ids: Array = []
	for h in _spawn_ids + _enemy_ids:
		var o: Dictionary = _world.get_object(h)
		if o.get("ok", false):
			ids.append_array(o.get("members", []))
	var ours: Array = []
	for h in (_spawn_ids if _s2_follow < 0 or _s2_follow >= _spawn_ids.size() else [_spawn_ids[_s2_follow]]):
		var o: Dictionary = _world.get_object(h)
		if o.get("ok", false):
			ours.append_array(o.get("members", []))
	_caption = "%s%s" % [_s2_caption + "  -  " if not _s2_caption.is_empty() else "", label]
	var last := {}
	var last_angle := {}
	var last_step := {}
	var reversals := 0
	var moving_member_frames := 0
	var steps: Array = []
	var max_step := 0.0
	var max_turn := 0.0
	var times: Array = []
	var held0: int = _world.get_frame_timings().get("held_presentations", 0)
	var game_time := 0.0
	var t_prev := Time.get_ticks_usec()
	var cam := _cam_target
	while game_time < _bench_seconds:
		await get_tree().process_frame
		var dt := get_process_delta_time()
		game_time += dt
		var now := Time.get_ticks_usec()
		times.append((now - t_prev) / 1000.0)
		t_prev = now
		var sum := Vector2.ZERO
		var n := 0
		var timing: Dictionary = _world.get_frame_timings()
		for id in ids:
			var rp: Dictionary = _world.get_render_pose(id)
			if not rp.get("ok", false):
				continue
			var p := Vector2(rp.x, rp.y)
			if csv:
				csv.store_line("%s,%.5f,%.5f,%d,%.4f,%.4f,%.5f,%d,%.4f,%d,%d,0" % [label, game_time, dt, id, p.x, p.y, rp.angle, rp.frame, timing.get("presented_alpha", 0.0), now,
					timing.get("held_presentations", 0)])
			if ours.has(id):
				sum += p
				n += 1
			if last.has(id):
				var step: Vector2 = p - last[id]
				var turn := absf(wrapf(rp.angle - last_angle[id], -PI, PI))
				max_turn = maxf(max_turn, turn)
				# moving: faster than 3 units / s
				if step.length() > 3.0 * dt:
					moving_member_frames += 1
					steps.append(step.length())
					max_step = maxf(max_step, step.length())
					if last_step.has(id) and last_step[id].length() > 3.0 * dt and step.dot(last_step[id]) < 0.0:
						reversals += 1
				last_step[id] = step
			last[id] = p
			last_angle[id] = rp.angle
		# lane MOVE-2: the horde objects' drawn poses too (the devlog camera's focus follows them), flagged in the last column
		if csv:
			for h in _spawn_ids + _enemy_ids:
				var hp: Dictionary = _world.get_render_pose(h)
				if hp.get("ok", false):
					csv.store_line("%s,%.5f,%.5f,%d,%.4f,%.4f,%.5f,%d,%.4f,%d,%d,1" % [label, game_time, dt, h, hp.x, hp.y, hp.angle, hp.frame, timing.get("presented_alpha", 0.0), now,
						timing.get("held_presentations", 0)])
		# the camera follows our members' drawn centre, eased (a still camera would lose them; the ease keeps the video readable)
		if n > 0:
			cam = cam.lerp(sum / n + Vector2(0.0, _s2_offset_y), clampf(dt * (2.0 if _s2_follow < 0 else 6.0), 0.0, 1.0))
			_cam_target = cam
			_place_camera()
	var held1: int = _world.get_frame_timings().get("held_presentations", 0)
	steps.sort()
	var median: float = steps[steps.size() / 2] if not steps.is_empty() else 0.0
	var member_seconds := maxf(moving_member_frames * _bench_seconds / maxf(times.size(), 1), 0.001)
	print("SMOOTH2 MOTION %s members=%d moving_member_frames=%d reversals=%d (%.3f per moving member-second) max_step=%.2f median_step=%.3f jump_ratio=%.1f max_turn_per_frame=%.3f" % [label, ids.size(), moving_member_frames, reversals, reversals / member_seconds, max_step, median, max_step / maxf(median, 0.0001), max_turn])
	var mean := 0.0
	for x in times:
		mean += x
	mean /= maxf(times.size(), 1)
	var dev := 0.0
	for x in times:
		dev += (x - mean) * (x - mean)
	print("SMOOTH2 PACING %s frames=%d fps=%.1f frame_ms: %s stdev %.2f held_presentations=%d" % [label, times.size(), 1000.0 / maxf(mean, 0.001), _summary(times), sqrt(dev / maxf(times.size(), 1)), held1 - held0])


# lane MOVE-2 (FEEDBACK-1 F4): the slot-distance test's march (engine/tests/test_move2_slot_distance.cpp) drawn: every --m2-horde side by side 110 apart at
# --m2-from, each ordered on its own to --m2-to (offset sideways as it started), then left standing; the camera follows the members' drawn centre from
# --s2-height. Every 2 s it prints MOVE2 SPREAD: per horde the largest distance of a member from its horde object (a lost member stays far behind).
func _move2() -> void:
	if _m2_hordes.is_empty():
		_fail("move2 needs --m2-horde objects")
		return
	var dir := (_m2_to - _m2_from).normalized()
	var side := Vector2(-dir.y, dir.x)
	var ids: Array = []
	for i in _m2_hordes.size():
		var off: float = (float(i) - float(_m2_hordes.size() - 1) * 0.5) * 110.0
		var p := _m2_from + side * off
		var id: int = _world.create_object(_m2_hordes[i], _local_index, p.x, p.y, atan2(dir.y, dir.x))
		if id <= 0:
			_fail("move2: cannot create " + str(_m2_hordes[i]))
			return
		ids.append(id)
	_cam_target = _m2_from
	_cam_height = _s2_height
	_place_camera()
	await _frames(30)
	for i in ids.size():
		var off2: float = (float(i) - float(ids.size() - 1) * 0.5) * 110.0
		var q := _m2_to + side * off2
		_world.order_move([ids[i]], q.x, q.y, {})
	_caption = "%s%s" % [_s2_caption + "  -  " if not _s2_caption.is_empty() else "", "march"]
	var t := 0.0
	var next_report := 0.0
	var cam := _cam_target
	# lane MOVE-2 r2: --s2-csv writes smooth2's rows (segment "march", the members' drawn poses, the wall clock) for tools/move/jitter_report.py; the frame
	# times are summarised as MOVE2 PACING at the end
	var csv: FileAccess = FileAccess.open(_s2_csv, FileAccess.WRITE) if not _s2_csv.is_empty() else null
	if csv:
		csv.store_line("segment,t,dt,id,x,y,angle,frame,alpha,wall_us,held,horde")
	var member_ids: Array = []
	var member_lists := {}
	for h0 in ids:
		member_lists[h0] = _world.get_object(h0).get("members", [])
		member_ids.append_array(member_lists[h0])
	var times: Array = []
	var t_prev := Time.get_ticks_usec()
	var held0: int = _world.get_frame_timings().get("held_presentations", 0)
	while t < _bench_seconds:
		await get_tree().process_frame
		var dt := get_process_delta_time()
		t += dt
		var now := Time.get_ticks_usec()
		times.append((now - t_prev) / 1000.0)
		t_prev = now
		if csv:
			var timing: Dictionary = _world.get_frame_timings()
			for mid in member_ids:
				var mp: Dictionary = _world.get_render_pose(mid)
				if mp.get("ok", false):
					csv.store_line("march,%.5f,%.5f,%d,%.4f,%.4f,%.5f,%d,%.4f,%d,%d,0" % [t, dt, mid, mp.x, mp.y, mp.angle, mp.frame, timing.get("presented_alpha", 0.0), now,
						timing.get("held_presentations", 0)])
		# per frame only the drawn poses (get_object waits for the logic worker: the member lists are read again only at the 2 s report)
		var report := t >= next_report
		var sum := Vector2.ZERO
		var n := 0
		var spread: Array = []
		for h in ids:
			var hp: Dictionary = _world.get_render_pose(h)
			var worst := 0.0
			for m in (_world.get_object(h).get("members", []) if report else member_lists.get(h, [])):
				var rp: Dictionary = _world.get_render_pose(m)
				if rp.get("ok", false):
					sum += Vector2(rp.x, rp.y)
					n += 1
					if hp.get("ok", false):
						worst = maxf(worst, Vector2(rp.x - hp.x, rp.y - hp.y).length())
			spread.append("%.0f" % worst)
		if n > 0 and not _m2_fixed_camera:
			cam = cam.lerp(sum / n, clampf(dt * 1.5, 0.0, 1.0))
			_cam_target = cam
			_place_camera()
		if report:
			next_report += 2.0
			for h1 in ids:
				member_lists[h1] = _world.get_object(h1).get("members", [])
			print("MOVE2 SPREAD t=%.0f %s" % [t, " ".join(spread)])
	if csv:
		csv.close()
	var held1: int = _world.get_frame_timings().get("held_presentations", 0)
	var mean := 0.0
	for x in times:
		mean += x
	mean /= maxf(times.size(), 1)
	print("MOVE2 PACING frames=%d fps=%.1f frame_ms: %s held_presentations=%d" % [times.size(), 1000.0 / maxf(mean, 0.001), _summary(times), held1 - held0])


func _smooth_churn() -> void:
	var templates: Array = _spawned.keys()
	var dyn_of := func() -> Dictionary: return _world.get_stats().get("dynamic", {})
	for round in 30:
		var made: Array = []
		for t in templates:
			for k in 8:
				var id: int = _world.create_object(t, _local_index, _focus.x + 60.0 * k - 240.0, _focus.y + 80.0 * (round % 5), 0.0)
				if id > 0:
					made.append(id)
		var worst := {"dyn_pose_ms": 0.0, "dyn_upload_ms": 0.0, "sync_ms": 0.0, "frame_ms": 0.0}
		var t_prev := Time.get_ticks_usec()
		for f in 20:
			await get_tree().process_frame
			var now := Time.get_ticks_usec()
			worst.frame_ms = maxf(worst.frame_ms, (now - t_prev) / 1000.0)
			t_prev = now
			var t: Dictionary = _world.get_frame_timings() if _world.has_method("get_frame_timings") else _old_timings()
			for k in ["dyn_pose_ms", "dyn_upload_ms", "sync_ms"]:
				worst[k] = maxf(worst[k], t.get(k, 0.0))
		for id in made:
			var o: Dictionary = _world.get_object(id)
			for m in o.get("members", []):
				_world.destroy_object(m)
			_world.destroy_object(id)
		await _frames(3)
		var d: Dictionary = dyn_of.call()
		print("SMOOTH CHURN round %d made %d: instances %d palette_rows %d worst pose %.2f upload %.2f sync %.2f frame %.2f ms" % [round, made.size(), d.get("instances", 0),
			d.get("palette_rows", 0), worst.dyn_pose_ms, worst.dyn_upload_ms, worst.sync_ms, worst.frame_ms])


# lane FX-2: the live effects in a battle. Our siege engines (the --spawn objects that are not hordes) are box-selected and ordered onto the opponent's building (the first
# --enemy); our hordes and the other --enemy objects find each other on their own. Screenshots follow the effects the logic calls and the drawables' states start.
## lane PHYS-1: a big melee near buildings, recorded as numbered frames (ffmpeg makes the video)
func _phys1_melee(shots: String) -> void:
	var ours := []
	var theirs := []
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for pair in [[_spawn_ids, ours], [_enemy_ids, theirs]]:
		for id in pair[0]:
			var o: Dictionary = _world.get_object(id)
			if not o.get("ok", false):
				continue
			lo = Vector2(minf(lo.x, o.x), minf(lo.y, o.y))
			hi = Vector2(maxf(hi.x, o.x), maxf(hi.y, o.y))
			if not (o.get("members", []) as Array).is_empty():
				pair[1].append(id)
	if ours.is_empty() or theirs.is_empty():
		_fail("phys1_melee needs --spawn and --enemy hordes")
		return
	_cam_target = (lo + hi) * 0.5
	_cam_height = _phys_cam_height
	_cam_back = 200.0
	_place_camera()
	await _frames(20)
	for i in ours.size():
		print("PHYS1 ORDER ", _world.order_attack([ours[i]], theirs[(i + 1) % theirs.size()]))
	for i in theirs.size():
		print("PHYS1 ORDER ", _world.order_attack([theirs[i]], ours[(i + 1) % ours.size()]))
	_world.set_time_scale(_fx_speed)
	var n := 0
	for i in _fx_frames:
		await get_tree().process_frame
		if i % 10 == 0:
			# follow the fight: the camera looks at the centre of the hordes still alive
			var csum := Vector2.ZERO
			var alive := 0
			for id in ours + theirs:
				var ob: Dictionary = _world.get_object(id)
				if ob.get("ok", false):
					csum += Vector2(ob.x, ob.y)
					alive += 1
			if alive > 0:
				_cam_target = csum / alive + _phys_cam_offset
				_place_camera()
		if i % _phys_every == 0:
			var image := get_viewport().get_texture().get_image()
			image.save_png(shots.path_join("phys1-%05d.png" % n))
			n += 1
	print("PHYS1 FRAMES %d logic frame %d" % [n, _world.get_frame()])


func _fx_battle(shots: String) -> void:
	var building: int = _enemy_ids[0]
	var bo: Dictionary = _world.get_object(building)
	if not bo.get("ok", false) or _spawn_ids.is_empty():
		_fail("fx_battle needs --spawn objects and an enemy building (%s)" % [bo])
		return
	var bpos := Vector2(bo.x, bo.y)
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	var siege := []
	for id in _spawn_ids:
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false) and (o.get("members", []) as Array).is_empty():
			siege.append(id)
			lo = Vector2(minf(lo.x, o.x), minf(lo.y, o.y))
			hi = Vector2(maxf(hi.x, o.x), maxf(hi.y, o.y))
	_cam_target = ((lo + hi) * 0.5 + bpos) * 0.5
	_cam_height = 520.0
	_cam_back = 420.0
	if not _fx_cam.is_empty():
		var c: PackedStringArray = _fx_cam.split(",")
		_cam_target = bpos + Vector2(float(c[0]), float(c[1]))
		_cam_height = float(c[2])
	_place_camera()
	await _frames(30)
	if not siege.is_empty():
		# the game's attack order through the command path (box-selecting units spread over the map is the HUD lanes' scenario)
		print("FX2 ORDER ", _world.order_attack(siege, building))
	_world.set_time_scale(_fx_speed)
	var saved := {}
	var last_die := 0
	var last_fire := 0
	var times: Array = []
	var adv: Array = []
	var peak := {"particles": 0, "systems": 0, "objects": 0}
	var t_prev := Time.get_ticks_usec()
	var max_health: float = bo.get("max_health", 1.0)
	var damaged_at := -1
	var sync_sum := 0.0
	var fx_us_sum := 0.0
	var fx_us_max := 0.0
	var building_now: Dictionary = bo # lane PERF-3: the building as last asked (every 5 frames)
	for i in _fx_frames:
		await get_tree().process_frame
		var now := Time.get_ticks_usec()
		times.append((now - t_prev) / 1000.0)
		t_prev = now
		# lane PERF-3: the frame's timers from get_frame_timings (get_stats hashes the whole simulation on this thread: the measurement was mostly that)
		var st: Dictionary = _world.get_frame_timings() if _world.has_method("get_frame_timings") else _world.get_stats()
		adv.append(st.get("advance_ms", st.get("last_advance_ms", 0.0)))
		var fx: Dictionary = _world.get_fx_report()
		var ps: Dictionary = fx.get("particles", {})
		sync_sum += st.get("sync_ms", st.get("last_sync_ms", 0.0))
		fx_us_sum += ps.get("step_us", 0.0) + ps.get("build_us", 0.0) + ps.get("upload_us", 0.0)
		fx_us_max = maxf(fx_us_max, ps.get("step_us", 0.0) + ps.get("build_us", 0.0) + ps.get("upload_us", 0.0))
		peak.particles = maxi(peak.particles, ps.get("particles", 0))
		peak.systems = maxi(peak.systems, ps.get("systems", 0))
		if i % 30 == 0: # lane PERF-3: a live query (it waits for the logic worker), not every frame
			peak.objects = maxi(peak.objects, _world.get_object_count())
		var played: Dictionary = fx.get("played", {})
		if i % 300 == 0:
			print("FX2 TICK %d frame %d objects %d particles %d systems %d played %s" % [i, _world.get_frame(), _world.get_object_count(), ps.get("particles", 0), ps.get("systems", 0), played])
		var fire: int = played.get("FireFX", 0)
		var die: int = played.get("FXListDie", 0)
		if not saved.has("fire") and fire > last_fire and fire > 0:
			saved["fire"] = i + 4
		if not saved.has("impact") and die > last_die and die > 0:
			saved["impact"] = i + 3
		last_fire = fire
		last_die = die
		if i % 5 == 0: # lane PERF-3: the building is a live query (it waits for the logic worker): every 5 frames
			building_now = _world.get_object(building)
			var bd: Dictionary = building_now
			if bd.get("ok", false):
				if damaged_at < 0 and bd.get("damage_state", 0) >= 1 and fx.get("attached_live", 0) > 0:
					damaged_at = i
					saved["damaged"] = i + 90
				if not saved.has("collapse") and bd.get("z", 0.0) < -8.0:
					saved["collapse"] = i
			elif not saved.has("collapse"):
				saved["collapse"] = i
		if not saved.has("deaths") and played.get("SlowDeath INITIAL", 0) >= 6:
			saved["deaths"] = i + 2
		for k in ["fire", "impact", "damaged", "collapse", "deaths"]:
			if saved.has(k) and typeof(saved[k]) == TYPE_INT and saved[k] == i:
				_save(shots.path_join("fx2-%s.png" % k))
				saved[k] = "done"
		if not building_now.get("ok", false) and saved.get("collapse", 0) is String and i > damaged_at + 450:
			break
	times.sort()
	adv.sort()
	var n := times.size()
	var sum := 0.0
	for t in times:
		sum += t
	var asum := 0.0
	for t in adv:
		asum += t
	print("FX2 PERF frames=%d mean_ms=%.2f p50_ms=%.2f p95_ms=%.2f max_ms=%.2f advance_mean_ms=%.2f advance_p95_ms=%.2f peak=%s" % [n, sum / maxf(n, 1), times[n / 2], times[int(n * 0.95)], times[n - 1], asum / maxf(n, 1), adv[int(n * 0.95)], peak])
	print("FX2 PERF render side: sync_mean_ms=%.2f (views + live FX + particles) fx_step_build_upload_mean_us=%.1f max_us=%.1f" % [sync_sum / maxf(n, 1), fx_us_sum / maxf(n, 1), fx_us_max])
	var rep: Dictionary = _world.get_fx_report()
	rep.erase("stops")
	print("FX2 REPORT ", JSON.stringify(rep))
	print("FX2 SAVED ", saved)


# lane PERF-1: the main thread's CPU time in ms (Linux: /proc/self/task/<pid>/schedstat, nanoseconds on the CPU; -1 elsewhere): a frame cost that other
# processes' load does not inflate, readable with any build of the extension
func _main_thread_cpu_ms() -> float:
	var f := FileAccess.open("/proc/self/task/%d/schedstat" % OS.get_process_id(), FileAccess.READ)
	if f == null:
		return -1.0
	var parts := f.get_line().split(" ") # /proc files report size 0: read the line, not the "whole" file
	return float(parts[0]) / 1.0e6 if parts.size() > 0 else -1.0
