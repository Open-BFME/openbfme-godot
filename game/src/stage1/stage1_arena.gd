extends Node3D
## Playable Stage 1 coordinator: input -> tick-stamped commands -> snapshots -> view.

const FIXED_DT := 1.0 / float(Stage1World.TICKS_PER_SECOND)
const Stage1BundleScript = preload("res://src/stage1_sim/stage1_bundle.gd")

@onready var camera_rig: Stage1Camera = $CameraRig
@onready var arena_view: Node3D = $Stage1View
@onready var hud: Stage1Hud = $Stage1Hud

var world: Stage1World
var current_snapshot: Dictionary = {}
var selected_ids: Array[int] = []
var attack_move_armed: bool = false
var accumulator: float = 0.0
var render_alpha: float = 1.0
var prebattle_seconds: float = 0.0
var last_alive_total: int = 0
var displayed_winner: int = Stage1Types.Team.NONE
var command_audio: AudioStreamPlayer
var impact_audio: AudioStreamPlayer

func _ready() -> void:
	hud.attack_move_pressed.connect(_arm_attack_move)
	hud.stop_pressed.connect(_stop_selected)
	hud.restart_pressed.connect(_restart)
	hud.menu_pressed.connect(_return_to_menu)
	_build_placeholder_audio()
	_restart()

func _restart() -> void:
	var bundle: Dictionary = Stage1BundleScript.load_bundle()
	world = Stage1BundleScript.build_world(bundle)
	current_snapshot = world.snapshot()
	selected_ids.clear()
	attack_move_armed = false
	camera_rig.command_mode = false
	accumulator = 0.0
	render_alpha = 1.0
	prebattle_seconds = 1.25
	displayed_winner = Stage1Types.Team.NONE
	last_alive_total = world.living_member_count(Stage1Types.Team.BLUE) + world.living_member_count(Stage1Types.Team.RED)
	hud.hide_winner()
	hud.set_selection(0, "Select the blue formation - battle begins in a moment")
	arena_view.configure(current_snapshot)
	arena_view.render_snapshot(current_snapshot, 1.0, selected_ids)
	var blue_fort := world.fortress_for(Stage1Types.Team.BLUE)
	if blue_fort != null:
		var blue_world: Vector3 = arena_view.fixed_to_world(blue_fort.position)
		camera_rig.focus_on(Vector2(blue_world.x + 8.0, blue_world.z))

func _process(delta: float) -> void:
	if world == null:
		return
	if prebattle_seconds > 0.0:
		prebattle_seconds = maxf(prebattle_seconds - delta, 0.0)
		accumulator = 0.0
	else:
		accumulator += minf(delta, 0.2)
	var steps := 0
	while accumulator >= FIXED_DT and steps < 8:
		accumulator -= FIXED_DT
		if world.winner == Stage1Types.Team.NONE:
			world.tick()
			current_snapshot = world.snapshot()
			_handle_combat_audio()
		steps += 1
	var alpha := clampf(accumulator / FIXED_DT, 0.0, 1.0)
	render_alpha = alpha
	_filter_selection()
	arena_view.render_snapshot(current_snapshot, alpha, selected_ids)
	var blue_alive := world.living_member_count(Stage1Types.Team.BLUE)
	var red_alive := world.living_member_count(Stage1Types.Team.RED)
	hud.set_sim_status(world.tick_index, world.state_hash_text(), blue_alive, red_alive)
	if world.winner != Stage1Types.Team.NONE and displayed_winner == Stage1Types.Team.NONE:
		displayed_winner = world.winner
		hud.show_winner("Blue" if world.winner == Stage1Types.Team.BLUE else "Red")

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and hud.selection_box.active:
		hud.selection_box.update_end((event as InputEventMouseMotion).position)
		return
	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.button_index == MOUSE_BUTTON_WHEEL_UP and mouse.pressed:
			camera_rig.zoom_steps(-1.0)
			get_viewport().set_input_as_handled()
			return
		if mouse.button_index == MOUSE_BUTTON_WHEEL_DOWN and mouse.pressed:
			camera_rig.zoom_steps(1.0)
			get_viewport().set_input_as_handled()
			return
		if mouse.button_index == MOUSE_BUTTON_LEFT:
			if mouse.pressed:
				hud.selection_box.begin(mouse.position)
			else:
				var rect := hud.selection_box.finish(mouse.position)
				_finish_selection(rect, mouse.position, mouse.shift_pressed)
			get_viewport().set_input_as_handled()
			return
		if mouse.button_index == MOUSE_BUTTON_RIGHT and mouse.pressed:
			_issue_pointer_command(mouse.position)
			get_viewport().set_input_as_handled()
			return
	if event is InputEventKey:
		var key := event as InputEventKey
		if not key.pressed or key.echo:
			return
		match key.keycode:
			KEY_A:
				if not selected_ids.is_empty():
					_arm_attack_move()
					get_viewport().set_input_as_handled()
			KEY_S:
				if not selected_ids.is_empty():
					_stop_selected()
					get_viewport().set_input_as_handled()
			KEY_SPACE:
				_focus_player_fortress()
				get_viewport().set_input_as_handled()
			KEY_R:
				_restart()
				get_viewport().set_input_as_handled()
			KEY_ESCAPE:
				_return_to_menu()
				get_viewport().set_input_as_handled()

func _finish_selection(rect: Rect2, click_position: Vector2, additive: bool) -> void:
	var found: Array[int] = []
	if rect.size.length() < 12.0:
		# Formations are intentionally loose and keep moving while the render view
		# interpolates. A generous screen-space radius makes every visible member a
		# reliable selection target without changing authoritative simulation state.
		var picked := _pick_horde(click_position, Stage1Types.Team.BLUE, 72.0)
		if picked != 0:
			found.append(picked)
	else:
		for horde: Dictionary in current_snapshot.get("hordes", []):
			if int(horde.team) != Stage1Types.Team.BLUE or int(horde.alive) <= 0:
				continue
			if _horde_intersects_screen_rect(horde, rect.grow(6.0)):
				found.append(int(horde.id))
	if additive:
		for id in found:
			if not selected_ids.has(id):
				selected_ids.append(id)
	else:
		selected_ids = found
	selected_ids.sort()
	attack_move_armed = false
	camera_rig.command_mode = false
	if selected_ids.is_empty():
		hud.set_selection(0, "No friendly horde at pointer - click a blue formation or drag-select")
	else:
		hud.set_selection(selected_ids.size(), "Ready - right-click ground to move or an enemy to attack")
		_play_command_tone(false)

func _issue_pointer_command(screen_position: Vector2) -> void:
	if selected_ids.is_empty() or world.winner != Stage1Types.Team.NONE:
		return
	var target_id := _pick_enemy_entity(screen_position)
	if target_id != 0 and not attack_move_armed:
		world.order_attack(selected_ids, target_id)
		hud.set_command_hint("Attack target queued for tick %d" % (world.tick_index + 1))
		_play_command_tone(true)
		return
	var ground: Variant = camera_rig.ground_point(screen_position)
	if ground == null:
		return
	var destination: Vector2i = arena_view.world_to_fixed(ground)
	destination.x = clampi(destination.x, 0, world.grid.width * Stage1Grid.CELL_SIZE - 1)
	destination.y = clampi(destination.y, 0, world.grid.height * Stage1Grid.CELL_SIZE - 1)
	if world.grid.is_blocked(world.grid.to_cell(destination)):
		hud.set_command_hint("Blocked ground - choose a walkable cell")
		return
	if attack_move_armed:
		world.order_attack_move(selected_ids, destination)
		hud.set_command_hint("Attack-move queued for tick %d" % (world.tick_index + 1))
		_play_command_tone(true)
	else:
		world.order_move(selected_ids, destination)
		hud.set_command_hint("Move queued for tick %d" % (world.tick_index + 1))
		_play_command_tone(false)
	attack_move_armed = false
	camera_rig.command_mode = false

func _pick_horde(screen_position: Vector2, team: int, radius: float) -> int:
	var best_id := 0
	var best_distance := radius
	for horde: Dictionary in current_snapshot.get("hordes", []):
		if int(horde.team) != team or int(horde.alive) <= 0:
			continue
		var distance := _horde_screen_distance(horde, screen_position)
		if distance < best_distance or (is_equal_approx(distance, best_distance) and (best_id == 0 or int(horde.id) < best_id)):
			best_distance = distance
			best_id = int(horde.id)
	return best_id

func _pick_enemy_entity(screen_position: Vector2) -> int:
	var best_id := 0
	var best_distance := 34.0
	for horde: Dictionary in current_snapshot.get("hordes", []):
		if int(horde.team) != Stage1Types.Team.RED or int(horde.alive) <= 0:
			continue
		var distance := _horde_screen_distance(horde, screen_position)
		if distance < best_distance:
			best_distance = distance
			best_id = int(horde.id)
	for fortress: Dictionary in current_snapshot.get("fortresses", []):
		if int(fortress.team) != Stage1Types.Team.RED or int(fortress.health) <= 0:
			continue
		var point := camera_rig.camera.unproject_position(arena_view.fixed_to_world(fortress.position, 1.2))
		var distance := point.distance_to(screen_position)
		if distance < best_distance:
			best_distance = distance
			best_id = int(fortress.id)
	return best_id

func _horde_screen_distance(horde: Dictionary, screen_position: Vector2) -> float:
	var anchor := _interpolated_fixed(horde.previous_anchor, horde.anchor)
	var anchor_point := camera_rig.camera.unproject_position(arena_view.fixed_to_world(anchor, 0.4))
	var best := anchor_point.distance_to(screen_position)
	for member: Dictionary in horde.members:
		if int(member.health) <= 0:
			continue
		var position := _interpolated_fixed(member.previous_position, member.position)
		var member_point := camera_rig.camera.unproject_position(arena_view.fixed_to_world(position, 0.35))
		best = minf(best, member_point.distance_to(screen_position))
	return best

func _horde_intersects_screen_rect(horde: Dictionary, rect: Rect2) -> bool:
	var anchor := _interpolated_fixed(horde.previous_anchor, horde.anchor)
	if rect.has_point(camera_rig.camera.unproject_position(arena_view.fixed_to_world(anchor, 0.4))):
		return true
	for member: Dictionary in horde.members:
		if int(member.health) <= 0:
			continue
		var position := _interpolated_fixed(member.previous_position, member.position)
		var screen := camera_rig.camera.unproject_position(arena_view.fixed_to_world(position, 0.35))
		if rect.has_point(screen):
			return true
	return false

func _interpolated_fixed(previous: Vector2i, current: Vector2i) -> Vector2i:
	return Vector2i(
		int(round(lerpf(float(previous.x), float(current.x), render_alpha))),
		int(round(lerpf(float(previous.y), float(current.y), render_alpha)))
	)

func _arm_attack_move() -> void:
	if selected_ids.is_empty():
		return
	attack_move_armed = true
	camera_rig.command_mode = true
	hud.set_command_hint("Attack-move armed - right-click walkable ground")
	_play_command_tone(true)

func _stop_selected() -> void:
	if selected_ids.is_empty():
		return
	world.order_stop(selected_ids)
	attack_move_armed = false
	camera_rig.command_mode = false
	hud.set_command_hint("Stop queued for tick %d" % (world.tick_index + 1))
	_play_command_tone(false)

func _filter_selection() -> void:
	var keep: Array[int] = []
	for id in selected_ids:
		var horde := world.get_horde(id)
		if horde != null and horde.team == Stage1Types.Team.BLUE and horde.is_alive():
			keep.append(id)
	if keep.size() != selected_ids.size():
		selected_ids = keep
		hud.set_selection(selected_ids.size())

func _focus_player_fortress() -> void:
	var fortress := world.fortress_for(Stage1Types.Team.BLUE)
	if fortress == null:
		return
	var position: Vector3 = arena_view.fixed_to_world(fortress.position)
	camera_rig.focus_on(Vector2(position.x, position.z))

func _return_to_menu() -> void:
	get_tree().change_scene_to_file("res://scenes/boot.tscn")

func _handle_combat_audio() -> void:
	var alive := world.living_member_count(Stage1Types.Team.BLUE) + world.living_member_count(Stage1Types.Team.RED)
	if alive < last_alive_total and impact_audio != null and not impact_audio.playing:
		impact_audio.play()
	last_alive_total = alive

func _build_placeholder_audio() -> void:
	if DisplayServer.get_name() == "headless":
		return
	command_audio = AudioStreamPlayer.new()
	command_audio.stream = _make_tone(520.0, 0.055)
	add_child(command_audio)
	impact_audio = AudioStreamPlayer.new()
	impact_audio.stream = _make_tone(145.0, 0.08)
	add_child(impact_audio)

func _play_command_tone(urgent: bool) -> void:
	if command_audio == null:
		return
	command_audio.pitch_scale = 1.28 if urgent else 1.0
	command_audio.play()

func _make_tone(frequency: float, duration: float) -> AudioStreamWAV:
	var sample_rate := 22050
	var frames := int(float(sample_rate) * duration)
	var bytes := PackedByteArray()
	bytes.resize(frames * 2)
	for i in frames:
		var envelope := 1.0 - float(i) / float(frames)
		var sample := int(sin(TAU * frequency * float(i) / float(sample_rate)) * 4800.0 * envelope)
		bytes.encode_s16(i * 2, sample)
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = sample_rate
	stream.stereo = false
	stream.data = bytes
	return stream

func _exit_tree() -> void:
	if command_audio != null:
		command_audio.stop()
		command_audio.stream = null
	if impact_audio != null:
		impact_audio.stop()
		impact_audio.stream = null
