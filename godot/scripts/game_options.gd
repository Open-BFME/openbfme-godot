## Lane OPTS-2: the game options in effect (owner, 2026-10-10: "rethink the openbfme options menu, it needs more options for the game").
##
## Two kinds of setting, normally kept in the player's Options.ini (AptMenuPlayer get_options / set_option / save_options):
##   * RotWK's own (retail keys, retail text, retail effect): the static detail level (StaticGameLOD and the nine advanced keys: GameLODManager applies them,
##     AptMenuPlayer get_detail_state gives what this node hands to the renderer), Brightness (the display gamma, RW 0x641E8B), ScrollFactor (the camera's
##     scroll factors, RW 0x641E0C), AllHealthBars (GlobalData + 0x9BE through the detail, RW 0x601CF9) and AlternateMouseSetup (RW 0x641E72).
##   * OpenBFME's extras (NOT RETAIL; every key starts with "OpenBFME", EXTRAS below). Their defaults are retail behaviour (or, where retail had no such
##     thing, what the renderer drew before). None touches the simulation: they set the viewport, the environment, the audio buses, the camera and the
##     HUD's decorations (client state the logic never reads).
## The display settings of lane OPTS-1 (window mode, resolution, vsync, frame cap, counter, render scale) stay in openbfme-display.cfg
## (scripts/display_settings.gd), which the launcher reads too.
## The main menu style (lane MENUSTYLE-1) is an OpenBFME key of Options.ini like the others (OpenBFMEMainMenuStyle = rotwk / bfme2); game.gd reads it
## before the shell boots (menu_style_from_file), since it picks the shell's presentation source.
##
## What the retail detail level drives here (each with its source; the rest is stop S-3402, docs/OPTIONS.md):
##   max_particle_count, min_particle_priority, min_particle_skip_priority -> the live FX player (FXPlayer.set_particle_lod: GlobalData + 0xAC8 and the
##     manager's + 0x17A0 / + 0x17A4, read by ParticleSystem::createParticle RW 0x5FA2CE);
##   texture_reduction -> GameWorld.set_texture_reduction (W3D textures lose mip levels; models loaded afterwards: the next game);
##   all_health_bars -> the HUD's decorations (DecalLOD Off forces them, RW 0x601CD3).
## Brightness: RW 0x641E8B's gamma drawn as DX8Wrapper::Set_Gamma's ramp (DONOR ZH: out = pow(i / 256, 1 / gamma)) by a full-window pass over everything;
## INFERENCE: retail's W3DDisplay skips the ramp in a window (it would change the desktop's); this pass is the window's own, so it applies in every mode.
extends Node

const DisplaySettings := preload("res://scripts/display_settings.gd") # lane OWNERFIX-1: TAA through its scaler
const PREFIX := "OpenBFME"
const MENU_STYLE_KEY := "OpenBFMEMainMenuStyle"
const MENU_STYLES := [["rotwk", "Rise of the Witch-king"], ["bfme2", "Battle for Middle-earth II"]]

## key -> { default, choices } (every value is text, as Options.ini holds it)
const EXTRAS := {
	"OpenBFMEMainMenuStyle": {"default": "rotwk", "choices": ["rotwk", "bfme2"]},
	"OpenBFMEAntiAliasing": {"default": "off", "choices": ["off", "fxaa", "msaa2", "msaa4", "taa"]},
	"OpenBFMEAnisotropic": {"default": "16x", "choices": ["off", "2x", "4x", "8x", "16x"]},
	"OpenBFMELook": {"default": "original", "choices": ["original", "enhanced"]},
	"OpenBFMESoftParticles": {"default": "yes", "choices": ["yes", "no"]},
	"OpenBFMEUIScale": {"default": "100", "choices": ["100", "125", "150", "200"]},
	"OpenBFMEFreeCamera": {"default": "no", "choices": ["yes", "no"]},
	# lane ZOOM-1: the Max zoom-out, times retail's highest camera (TacticalCamera::setZoomOutFactor). The owner's default is 2.5 (2026-10-10: "by default
	# have the option to zoom out like 2.5x as far"), the one extra whose default is not retail's behaviour; "1" is the original game
	"OpenBFMEMaxZoomOut": {"default": "2.5", "choices": ["1", "1.5", "2", "2.5", "3"]},
	"OpenBFMEEdgeScroll": {"default": "retail", "choices": ["retail", "always", "off"]},
	# lane UI-6 (FB-0021, the owner's decision 2026-10-11): the Widescreen view. "original" is RotWK's Vert- view (a 50 degree horizontal angle at every
	# aspect, RW 0x4874A7 / 0x533630); "wider" keeps 4:3's vertical view and widens the horizontal one on wider screens (TacticalCamera::setWidescreenView)
	"OpenBFMEWidescreen": {"default": "original", "choices": ["original", "wider"]},
	"OpenBFMEConfineMouse": {"default": "no", "choices": ["yes", "no"]},
	"OpenBFMEInvertDragScroll": {"default": "no", "choices": ["yes", "no"]},
	"OpenBFMERotateSpeed": {"default": "100", "choices": ["50", "75", "100", "150", "200"]},
	"OpenBFMEHealthBars": {"default": "retail", "choices": ["retail", "damaged", "always"]},
	"OpenBFMEGameClock": {"default": "no", "choices": ["yes", "no"]},
	# the ping wheel (not retail: scripts/ping_wheel.gd): on by default, an addition with no retail behaviour to keep; the key it opens on
	"OpenBFMEPingWheel": {"default": "yes", "choices": ["yes", "no"]},
	"OpenBFMEPingKey": {"default": "g", "choices": ["g", "altg", "j", "mouse4", "mouse5"]},
	# lane UBER-1: the Uber performance mode (docs/UBER.md): the next lobby and game run the Uber simulation profile; never compatible with the original game
	"OpenBFMEUberPerformance": {"default": "no", "choices": ["yes", "no"]},
	"OpenBFMEShareResults": {"default": "yes", "choices": ["yes", "no"]}, # lane ONLINEHUB-1: the platform's game results (docs/PLATFORM_API.md)
	"OpenBFMEShowPing": {"default": "yes", "choices": ["yes", "no"]}, # lane ONLINEMENU-1: the online menus' corner shows the player's ping
	"OpenBFMEMasterVolume": {"default": "100", "range": [0, 100]},
	"OpenBFMEMuteUnfocused": {"default": "no", "choices": ["yes", "no"]},
	"OpenBFMEAudioDevice": {"default": "Default", "device": true},
}
## the retail keys the OpenBFME screen also shows (their own text and meaning; Options.apt shows them too)
const RETAIL_KEYS := {
	# the Scroll Speed slider: any text (RW 0x6E59A9: atoi, below 0 -> 1, above 100 -> 100, * 0.02); "" = no entry, GlobalData + 0xAFC's default (the
	# slider shows get_detail_state's scroll_slider); Save writes the slider, at least 1 (RW 0x91FE9E)
	"ScrollFactor": {"default": "", "retail_text": true},
	"AllHealthBars": {"default": "no", "choices": ["yes", "no"]},     # the health bars box (RW 0x6E6179: "yes")
	"AlternateMouseSetup": {"default": "no", "choices": ["yes", "no"]}, # the box (RW 0x920A7B): yes = the left click orders
}
const AA_LABELS := {"off": "Off", "fxaa": "FXAA", "msaa2": "MSAA 2x", "msaa4": "MSAA 4x", "taa": "TAA"}

var shell: Node                  # the AptMenuPlayer (Options.ini)
var game: Node                   # game.gd
var problems := PackedStringArray()
var _gamma_layer: CanvasLayer
var _gamma_rect: ColorRect
var _clock_layer: CanvasLayer
var _clock: Label
var _world: Node
var _hud: Node
var _focus_muted := false
var _in_game := false
var _detail := {}
var _last_clock := -1


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	_gamma_layer = CanvasLayer.new()
	_gamma_layer.name = "Brightness"
	_gamma_layer.layer = 127
	_gamma_rect = ColorRect.new()
	_gamma_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_gamma_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	var shader := Shader.new()
	shader.code = """shader_type canvas_item;
uniform sampler2D screen_tex : hint_screen_texture, filter_nearest;
uniform float inv_gamma = 1.0;
void fragment() {
	// DX8Wrapper::Set_Gamma's ramp (ZH, bright 0, contrast 1): entry i of 256 = pow(i / 256, 1 / gamma)
	vec3 i = floor(clamp(texture(screen_tex, SCREEN_UV).rgb, 0.0, 1.0) * 255.0 + 0.5);
	COLOR = vec4(pow(i / 256.0, vec3(inv_gamma)), 1.0);
}
"""
	var mat := ShaderMaterial.new()
	mat.shader = shader
	_gamma_rect.material = mat
	_gamma_layer.add_child(_gamma_rect)
	_gamma_layer.visible = false
	add_child(_gamma_layer)
	_clock_layer = CanvasLayer.new()
	_clock_layer.name = "GameClock"
	_clock_layer.layer = 119
	_clock = Label.new()
	_clock.name = "Clock"
	_clock.set_meta("game_text", true) # a player's option, not developer text (game.gd _dev_text_on_screen)
	_clock.add_theme_color_override("font_color", Color(0.88, 0.92, 0.96))
	_clock.add_theme_color_override("font_outline_color", Color.BLACK)
	_clock.add_theme_constant_override("outline_size", 4)
	_clock.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_clock_layer.add_child(_clock)
	_clock_layer.visible = false
	add_child(_clock_layer)


# ---------------------------------------------------------------------------------------------------------------------------------
# values
# ---------------------------------------------------------------------------------------------------------------------------------
## lane DEVPORT-8: the main menu style in the options file at `path` (AptMenuPlayer.read_options_file: the shell's own reader), before the shell boots:
## { ok, style, problem } - an absent file or key is the default; a value the key does not take is the default with a problem (as every extra in
## current()); an unreadable file is not ok
static func menu_style_from_file(path: String) -> Dictionary:
	var read: Dictionary = ClassDB.class_call_static("AptMenuPlayer", "read_options_file", path)
	if not read.get("ok", false):
		return {"ok": false, "error": "Options.ini: " + str(read.get("error", "unreadable"))}
	var problems := []
	var style := value_of(read.get("values", {}), MENU_STYLE_KEY, problems)
	return {"ok": true, "style": style, "problem": "" if problems.is_empty() else str(problems[0])}


static func spec(key: String) -> Dictionary:
	return EXTRAS.get(key, RETAIL_KEYS.get(key, {}))


## the value of `key` in `opts` (Options.ini's entries), or its default; a value the key does not take is reported in `out_problems`, never used
static func value_of(opts: Dictionary, key: String, out_problems = null) -> String:
	var s := spec(key)
	if not opts.has(key):
		return s.get("default", "")
	var v := str(opts[key])
	if valid(key, v):
		return v
	if out_problems != null:
		out_problems.append("%s = '%s' is not a value it takes; its default '%s' is in effect" % [key, v, s.get("default", "")])
	return s.get("default", "")


static func valid(key: String, v: String) -> bool:
	var s := spec(key)
	if s.has("choices"):
		return (s.choices as Array).has(v)
	if s.has("range"):
		if not v.is_valid_int():
			return false
		var n := v.to_int()
		return n >= s.range[0] and n <= s.range[1]
	if s.get("device", false):
		return not v.is_empty()
	if s.get("retail_text", false):
		return not v.contains("\n") and not v.contains("\r")
	return false


## every setting the OpenBFME screen shows, as Options.ini text (the file's values, defaults where absent)
func current() -> Dictionary:
	var opts: Dictionary = shell.get_options() if shell != null else {}
	var out := {}
	var found := PackedStringArray()
	for key in EXTRAS:
		out[key] = value_of(opts, key, found)
	for key in RETAIL_KEYS:
		out[key] = value_of(opts, key, found)
	problems = found
	return out


static func defaults() -> Dictionary:
	var out := {}
	for key in EXTRAS:
		out[key] = EXTRAS[key].default
	for key in RETAIL_KEYS:
		out[key] = RETAIL_KEYS[key].default
	return out


## the Scroll Speed slider's position for ScrollFactor's text as retail's InitGadgets sets it (RW 0x9209D8): the getter's clamp, the default when absent
func scroll_slider(text: String) -> int:
	if text.is_empty():
		return int(shell.get_detail_state().get("scroll_slider", 50)) if shell != null else 50
	var v := text.to_int()
	return 1 if v < 0 else mini(v, 100)


## writes `values` into Options.ini (the shell's entries, then the file); a default extra is written too, so the file says what the player chose; an
## empty value removes the entry (ScrollFactor's default is its absence)
func commit(values: Dictionary) -> Dictionary:
	if shell == null:
		return {"ok": false, "error": "no shell"}
	if values.has(MENU_STYLE_KEY) and not valid(MENU_STYLE_KEY, str(values[MENU_STYLE_KEY])):
		return {"ok": false, "error": "Unknown main menu style: " + str(values[MENU_STYLE_KEY])}
	for key in values:
		if not shell.set_option(key, values[key]):
			return {"ok": false, "error": "Options.ini cannot hold %s = %s" % [key, values[key]]}
	var saved: Dictionary = shell.save_options()
	if not saved.get("ok", false):
		return saved
	# lane MENUSTYLE-1: the new style takes effect at the next clean main menu (game.gd reboots the shell with its presentation source)
	if values.has(MENU_STYLE_KEY) and game != null:
		game.request_menu_style(str(values[MENU_STYLE_KEY]))
	return {"ok": true}


# ---------------------------------------------------------------------------------------------------------------------------------
# applying
# ---------------------------------------------------------------------------------------------------------------------------------
## everything that does not need a game: the viewport, the look, the audio, the brightness, the detail's texture reduction; `values` from current()
func apply_global(values: Dictionary) -> PackedStringArray:
	var done := PackedStringArray()
	var root: Viewport = get_tree().root
	var aa: String = values.OpenBFMEAntiAliasing
	var renderer := str(ProjectSettings.get_setting_with_override("rendering/renderer/rendering_method"))
	root.msaa_3d = {"msaa2": Viewport.MSAA_2X, "msaa4": Viewport.MSAA_4X}.get(aa, Viewport.MSAA_DISABLED)
	root.screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA if aa == "fxaa" and renderer != "gl_compatibility" else Viewport.SCREEN_SPACE_AA_DISABLED
	# lane OWNERFIX-1 (the owner: "the new anti-aliasing ... looks awful, super blurry"): Godot's TAA (Viewport.use_taa) has no sharpening and smears the
	# picture while the camera moves: measured at 2560x1440 during a pan (tools/render/sharpness.py, godot/scripts/ownerfix1_player.gd aapan) it keeps
	# 0.45 of the detail of no anti-aliasing, a mip bias of -0.5 does not help (0.47). FSR 2 at the render scale in effect is Godot's other temporal
	# anti-aliasing, with its sharpening pass (fsr_sharpness, Godot's default 0.2): 1.2 .. 1.35 of the detail with smooth edges. So TAA is FSR 2
	# (DisplaySettings.temporal_aa keeps it when the display settings apply their scaler)
	var taa: bool = aa == "taa" and renderer == "forward_plus"
	root.use_taa = false
	DisplaySettings.temporal_aa = taa
	var display_scaler: String = str(game._display.scaler) if game != null and "_display" in game and game._display != null else ""
	if taa:
		root.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
	elif not display_scaler.is_empty():
		root.scaling_3d_mode = DisplaySettings.scaling_mode_of(display_scaler)
	if (aa == "fxaa" and renderer == "gl_compatibility") or (aa == "taa" and renderer != "forward_plus"):
		problems.append("%s: the %s renderer has no %s" % ["OpenBFMEAntiAliasing", renderer, AA_LABELS[aa]])
	done.append("anti-aliasing " + aa)
	root.anisotropic_filtering_level = {"off": Viewport.ANISOTROPY_DISABLED, "2x": Viewport.ANISOTROPY_2X, "4x": Viewport.ANISOTROPY_4X,
		"8x": Viewport.ANISOTROPY_8X, "16x": Viewport.ANISOTROPY_16X}[values.OpenBFMEAnisotropic]
	done.append("anisotropic " + values.OpenBFMEAnisotropic)
	apply_look(values.OpenBFMELook)
	done.append("look " + values.OpenBFMELook)
	ProjectSettings.set_setting("openbfme/rendering/soft_particles", values.OpenBFMESoftParticles == "yes")
	var master := AudioServer.get_bus_index("Master")
	AudioServer.set_bus_volume_db(master, linear_to_db(values.OpenBFMEMasterVolume.to_int() / 100.0) if values.OpenBFMEMasterVolume.to_int() > 0 else -80.0)
	AudioServer.set_bus_mute(master, values.OpenBFMEMasterVolume.to_int() == 0 or (_focus_muted and values.OpenBFMEMuteUnfocused == "yes"))
	done.append("master volume %s%%" % values.OpenBFMEMasterVolume)
	var device: String = values.OpenBFMEAudioDevice
	if Array(AudioServer.get_output_device_list()).has(device):
		if AudioServer.output_device != device:
			AudioServer.output_device = device
	else:
		problems.append("OpenBFMEAudioDevice: '%s' is not an output device now (%s); the system's default plays" % [device, ", ".join(AudioServer.get_output_device_list())])
		AudioServer.output_device = "Default"
	done.append("audio device " + AudioServer.output_device)
	apply_brightness()
	apply_detail()
	if _hud != null:
		_hud.set_view_options(view_options(values))
	_apply_confine(values)
	_clock_layer.visible = _in_game and values.OpenBFMEGameClock == "yes"
	var wheel = game.get("_ping_wheel") if game != null else null
	if wheel != null and is_instance_valid(wheel):
		wheel.configure(values.OpenBFMEPingWheel == "yes", values.OpenBFMEPingKey)
	_clock.add_theme_font_size_override("font_size", roundi(16 * values.OpenBFMEUIScale.to_int() / 100.0))
	if game != null and game.get("_display_overlay") != null:
		var counter = game._display_overlay.get_node_or_null("FpsCounter")
		if counter != null:
			counter.add_theme_font_size_override("font_size", roundi(16 * values.OpenBFMEUIScale.to_int() / 100.0))
	return done


## "original": the renderer's look as it was (linear tone mapping, no glow); "enhanced": AgX tone mapping and glow on what is brighter than white (the
## additive effects' overbright)
func apply_look(look: String) -> void:
	var env: Environment = _environment()
	if env == null:
		return
	if look == "enhanced":
		env.tonemap_mode = Environment.TONE_MAPPER_AGX
		env.glow_enabled = true
		env.glow_hdr_threshold = 1.0
		env.glow_intensity = 0.6
		env.glow_bloom = 0.05
	else:
		env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
		env.glow_enabled = false


func _environment() -> Environment:
	if game == null:
		return null
	var we = game.get("_env")
	return we.environment if we != null else null


## RW 0x641E8B: the gamma of Brightness (get_detail_state's display_gamma); 1.0 draws nothing
func apply_brightness() -> void:
	if shell == null or hold_gamma:
		return
	var st: Dictionary = shell.get_detail_state()
	set_gamma(st.get("display_gamma", 1.0))


## lane OPTS-5: true while the Options menu is up: its brightness slider's pending gamma stays through every other edit (apply_global runs per edit);
## ACCEPT / CANCEL set the saved one (the shell's Save / Cancel) after the menu clears it
var hold_gamma := false
## lane OPTS-5: TheDisplay's gamma now (the brightness slider moving, the Options screen's Reset / Cancel; the saved one through apply_brightness)
var gamma_in_effect := 1.0
func set_gamma(gamma: float) -> void:
	gamma_in_effect = gamma
	_gamma_layer.visible = gamma != 1.0
	(_gamma_rect.material as ShaderMaterial).set_shader_parameter("inv_gamma", 1.0 / clampf(gamma, 0.6, 6.0)) # DX8Wrapper::Set_Gamma's bound


## the static detail level's effects that need no game (the texture reduction for what loads next) and, in a game, the FX player's particle limits
func apply_detail() -> Dictionary:
	if shell == null:
		return {}
	_detail = shell.get_detail_state()
	if not _detail.get("ok", false):
		problems.append("detail: " + str(_detail.get("error", "no level")))
		return _detail
	ClassDB.class_call_static("GameWorld", "set_texture_reduction", _detail.texture_reduction)
	if _world != null and _world.has_method("get_fx_player"):
		var fx = _world.get_fx_player()
		if fx != null:
			fx.set_particle_lod(_detail.max_particle_count, _detail.min_particle_priority, _detail.min_particle_skip_priority)
	return _detail


## the HUD's settings of `values`: the retail keys' effects and the OpenBFME camera / interface extras
func view_options(values: Dictionary) -> Dictionary:
	var all_bars: bool = values.AllHealthBars == "yes"
	if _detail.get("ok", false) and not _detail.get("decals", true):
		all_bars = true # RW 0x601CD3: DecalLOD Off shows them all
	return {
		"scroll_factor_option": values.ScrollFactor if not values.ScrollFactor.is_empty() else null,
		"edge_scroll_mode": values.OpenBFMEEdgeScroll,
		"invert_drag_scroll": values.OpenBFMEInvertDragScroll == "yes",
		"rotate_speed_scale": values.OpenBFMERotateSpeed.to_int() / 100.0,
		"alternate_mouse": values.AlternateMouseSetup != "yes",
		"all_health_bars": all_bars,
		"health_bar_mode": values.OpenBFMEHealthBars,
		"ui_scale": values.OpenBFMEUIScale.to_int() / 100.0,
		"free_camera": values.OpenBFMEFreeCamera == "yes" or (game != null and game.get("_free_camera_flag") == true),
		"zoom_out_factor": _zoom_out_factor(values),
		"widescreen_wider": _widescreen_wider(values),
	}


## lane UI-6: the Widescreen view in effect: game.gd's --widescreen=original|wider for a run, else Options.ini OpenBFMEWidescreen
func _widescreen_wider(values: Dictionary) -> bool:
	if game != null and game.get("_widescreen_flag") != null and not str(game.get("_widescreen_flag")).is_empty():
		return str(game.get("_widescreen_flag")) == "wider"
	return values.OpenBFMEWidescreen == "wider"


## lane ZOOM-1: the Max zoom-out factor in effect: game.gd's --zoom-out=<f> for a run, else Options.ini OpenBFMEMaxZoomOut
func _zoom_out_factor(values: Dictionary) -> float:
	if game != null and game.get("_zoom_out_flag") != null and float(game.get("_zoom_out_flag")) > 0.0:
		return float(game.get("_zoom_out_flag"))
	return values.OpenBFMEMaxZoomOut.to_float()


func _apply_confine(values: Dictionary) -> void:
	var confine: bool = _in_game and values.OpenBFMEConfineMouse == "yes"
	var want := Input.MOUSE_MODE_CONFINED if confine else Input.MOUSE_MODE_VISIBLE
	if Input.mouse_mode != want and Input.mouse_mode in [Input.MOUSE_MODE_VISIBLE, Input.MOUSE_MODE_CONFINED]:
		Input.mouse_mode = want


## a game started: its world and HUD take the settings in effect
func game_started(world: Node, hud: Node) -> void:
	_world = world
	_hud = hud
	_in_game = true
	_last_clock = -1
	var values := current()
	var done := apply_global(values)
	print("GAME OPTIONS game: %s; detail %s%s" % [", ".join(done), _detail.get("level_name", "?"), "" if problems.is_empty() else " PROBLEMS " + str(problems)])


func game_ended() -> void:
	_world = null
	_hud = null
	_in_game = false
	_clock_layer.visible = false
	_apply_confine(current())


func _process(_delta: float) -> void:
	if not _clock_layer.visible or _world == null or not _world.has_method("get_presented_frame"):
		return
	var frame: int = _world.get_presented_frame() # lane RENDERWAIT-1: get_frame waits for the logic worker; the clock shows the presented frame
	var seconds := frame / 5 # 5 logic frames a second
	if seconds == _last_clock:
		return
	_last_clock = seconds
	_clock.text = "%d:%02d:%02d" % [seconds / 3600, (seconds / 60) % 60, seconds % 60] if seconds >= 3600 else "%d:%02d" % [seconds / 60, seconds % 60]
	var size := get_viewport().get_visible_rect().size
	_clock.position = Vector2(size.x * 0.5 - _clock.size.x * 0.5, 4)


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT or what == NOTIFICATION_APPLICATION_FOCUS_IN:
		_focus_muted = what == NOTIFICATION_APPLICATION_FOCUS_OUT
		if shell == null:
			return
		var values := current()
		var master := AudioServer.get_bus_index("Master")
		AudioServer.set_bus_mute(master, values.OpenBFMEMasterVolume.to_int() == 0 or (_focus_muted and values.OpenBFMEMuteUnfocused == "yes"))
