// OpenBFME. GPL-3.0.
//
// Device layer, lane HUD-1: the in-game HUD as a Godot node. It owns the core InGameHud (GameClient/InGameHud.h: HudInput, control bar, Palantir, radar), feeds it the Godot
// camera (the lane CAM-1 TacticalCamera, SAGE axes; it sets the Godot camera every frame), the mouse and keyboard events (window pixels, DirectInput key codes) and the render time, and draws the Palantir movie with the
// native render components (the radar, the command button images, the timers, the portrait) through an AptMenuPlayer attached to the HUD's window manager.
//
// The node never changes the simulation: orders reach the logic as GameMessages through the live game's command list (HudInput). The game scene (lane START-1) creates this node
// after the loading screen: setup(fs, world, camera, local_player); a test or viewer harness starts a map directly and does the same.

#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_with_modifiers.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>

struct INIEnvironment;
class InGameHud;
class LiveGame;

namespace godot
{

class AptMenuPlayer;
class GameWorld;
class RetailFileSystem;
struct HudDevice;

class InGameHudNode : public Node2D
{
	GDCLASS(InGameHudNode, Node2D)

public:
	InGameHudNode();
	~InGameHudNode() override;

	// Builds the HUD for the live game of `world` (its map is loaded) and the player named `local_player` ("Player_1"); `camera` is the game camera the picks use.
	// options: { show_placeholders: bool, alternate_mouse: bool, camera_start: Vector3 (REQUIRED: the ground point the camera looks at, Godot axes), edge_scroll: bool }. Returns { ok, errors }.
	Dictionary setup(const Ref<RetailFileSystem> &fs, GameWorld *world, Camera3D *camera, const String &local_player, const Dictionary &options);
	// The game scene's form (lane START-1, game.gd _install_hud): the HUD works on the shell-mode player that already runs the game's movies, with Palantir.apt pushed. `player` is
	// that AptMenuPlayer (shell mode); it keeps stepping its window manager and draws, the HUD feeds the camera and the input and draws the native components.
	Dictionary attach(const Ref<RetailFileSystem> &fs, GameWorld *world, Camera3D *camera, AptMenuPlayer *player, const String &local_player, const Dictionary &options);
	bool is_ready() const;

	// the objects selected on the client (newest first) and the logic's selection of the local player
	Array get_selection() const;
	Array get_logic_selection() const;
	// the messages that went to the command list so far, as text
	PackedStringArray get_message_log() const;
	// { cursor, selecting, gui_command, over_gui, messages: [...], stops: [...], palantir: { initialized, loaded: {...} } }
	Dictionary get_state() const;
	Array get_command_map() const; ///< lane INPUT-1: the CommandMap records in the order the MetaEventTranslator tries them
	// the HUD movie's own report (unverified rules, errors) and the stops
	Dictionary get_report() const;
	// lane HUD-6: { radial: { object, count, radius, centre, size, hilited, presses, buttons: [{ slot, rect, state, image, name }], errors }, help: { loaded,
	// render_name, state, shown, width, helps_shown, draws, name, calls } }
	Dictionary get_hud6_state() const;
	// ---- lane CHAT-1: the InGameUI message lines and the chat (GameClient/InGameUIMessages.h, GameClient/InGameChat.h) ----
	// take_chat_requests: CHAT_ALLIES (false) / CHAT_EVERYONE (true) presses since the last call. chat_text(label): the game text of a label ("" when
	// absent). chat_format(mode, text): the line Send sends ("<Team> hi": the mode's label formatted with the text). add_chat_line(text, rgb): a received
	// line (messageColor in the sender's colour 0xRRGGBB, or -1: the alternating colours, InGameUI::message) and the scroll-back. set_chat_history_shown:
	// the scroll-back over the message lines while the chat window is open (OpenBFME addition). message_lines_state: the lines, their colours, the history
	Array take_chat_requests();
	String chat_text(const String &label) const;
	String chat_format(int64_t mode, const String &text) const;
	void add_chat_line(const String &text, int64_t rgb);
	void set_chat_history_shown(bool shown);
	Dictionary message_lines_state() const;
	// lane CHAT-1: the chat and beacon keys work only in a network game (RW 0x441B7C); beacon_state: the beacons this client presented
	// { placed, too_many, failed, hidden, pulses, beacons: [ids], client_hidden: [ids], gui_command }
	void set_network_game(bool on);
	Dictionary beacon_state() const;
	// lane SMOOTH-1: the parts of the last _process in ms { hud_update_ms (the HUD: APT movie, control bar, radar, input), hud_camera_ms, hud_cursor_ms }
	Dictionary get_frame_timings() const;
	// lane BUILD-1: { placing, template, source, has_ghost, x, y, angle, legal (LegalBuildCode, 0 = may be placed) } of the building waiting for its site
	Dictionary get_placement() const;
	// lane BUILD-5: the MiscAudio field names the input asked to play since the last call (InGameUI::takeMiscSounds), e.g. BadBuildingPlacementSound
	PackedStringArray take_misc_sounds();
	// lane BUILD-5: the pointer as drawn now { name, direction, hidden, texture (null: none), hot } (proof screenshots draw it: a capture omits the OS pointer)
	Dictionary get_cursor_image() const;
	// lane BUILD-5 r2: the placement ghost as drawn (PlacementGhostRenderer::state): { template, castle, anchored, arrow, icons: [...], errors: [...] }
	Dictionary get_placement_ghost_state() const;
	// community PR #37 review: the canvas shader of an Apt image `_mode` ("" for ALPHA / an unknown mode), for the pixel test (tests/hud_image_mode_pixel_test.gd)
	static String image_mode_shader(const String &mode);
	// lane PLAY-1: the move hints to draw now (InGameUI::createMoveHint): { model (GameData MoveHintName), frame (client frame), made, hints: [{ slot, frame,
	// age (client frames, <= 40), position (Godot axes), x, y, z (SAGE) }] }
	Dictionary get_move_hints() const;
	// presses the command bar button that builds / constructs `template_name` (opening the pages of the set when it is on a later one): what a click on the Palantir does
	bool press_command_button(const String &template_name);
	// lane QA-1: the command bar of the current selection, one entry per shown button: { slot, in_palantir, position (the Palantir position that shows it), name,
	// command (the retail Command name), template, upgrade, power, state (ButtonState: 0 hidden, 1 enabled, 2 restricted, 3 can't afford, 4 not ready, 5 active),
	// cost, queued, frame (the movie clip of its position: a click there is the player's press) }
	Array get_command_buttons() const;
	// synthetic input (a viewer harness): window pixels
	void inject_mouse_move(const Vector2 &position);
	void inject_mouse_button(int button, bool down, const Vector2 &position, bool double_click);
	void inject_key(int dik_code, bool down);
	// the wheel: positive = zoom in, one step per notch (over the Palantir it belongs to the movie)
	void inject_mouse_wheel(int notches, const Vector2 &position);
	// world position (SAGE x, y) under a window pixel; (-1e9, -1e9) when the ray misses the ground
	// lane CAM-1: the retail tactical camera's state (position, angle, zoom, heights, pose, limits) for the viewers and the tests; empty before attach()
	Dictionary get_camera() const;
	// lane CHEAT-1: a line in the in-game message log (InGameHud::addPlayerMessageText) and the log's place ({ x, y, point_size }: InGameUI's
	// MessagePosition in 1024 x 768 units and its point size)
	void add_player_message(const String &text);
	Dictionary get_player_message_layout() const;
	// edge scrolling is on in fullscreen (retail: GlobalData Windowed = No); a test or a windowed run can force it
	void set_edge_scroll(bool on);
	// lane PLAY-1: the free camera (the owner's presentation option, not retail: TacticalCamera::setFreeCamera); also the attach option free_camera
	void set_free_camera(bool on);
	void hold_input(bool on);        ///< lane BACKLOG3 (QA): queue _input's events as while the logic worker runs
	void set_input_enabled(bool on); ///< lane BACKLOG3 (QA): InGameUI's input flag (the scripts' DISABLE_INPUT / ENABLE_INPUT)
	bool input_enabled() const; ///< lane BFME2CAMP1: InGameUI + 0x16 (setInputEnabled RW 0x69B5DA), the tutorial's objectives screen Return gate
	void script_input(bool enable);  ///< lane OWNERFIX-1: a map script's DISABLE_INPUT / ENABLE_INPUT as retail runs them (InGameHud::scriptInput)
	void set_broadcast_clean(bool on);
	// lane OPTS-2: the OpenBFME screen's camera / interface settings, live: { scroll_factor_option (Options.ini ScrollFactor text or null), edge_scroll_mode
	// ("retail" | "always" | "off"), invert_drag_scroll, rotate_speed_scale, alternate_mouse, all_health_bars, health_bar_mode ("retail" | "damaged" |
	// "always"), ui_scale, free_camera, zoom_out_factor (lane ZOOM-1: the Max zoom-out, 1 = retail), widescreen_wider (lane UI-6: the Widescreen view, false =
	// retail's Vert-, true = Hor+ beyond 4:3) }; every key optional
	Dictionary set_view_options(const Dictionary &options);
	// scripted camera moves for the viewers (the same View::lookAt / setHeightAboveGround the radar click and the wheel call)
	void camera_look_at(const Vector2 &sage_xy);
	void camera_set_height(float height_above_ground);
	// lane CAMPUI-1: a map script's RESET_CAMERA (view slot 0xC8, the game start's RW 0x63135B) and the letterbox's zoom limit (W3DDisplay::enableLetterBox
	// RW 0x443C81: TheTacticalView slot 0x1C setZoomLimited(!enable)) on the HUD's own camera, so a cinematic moves the camera the player then keeps
	void camera_reset(const Vector2 &sage_xy);
	void camera_set_zoom_limited(bool on);
	// lane CAMANIM-1: a map script's camera request in the client's order for the view's camera animation (MOVE_CAMERA_BY_ANIMATION starts the map's
	// animation of that name, the mode-writing camera actions end it: GameClient/CameraAnimation.h); true when the playback changed
	bool camera_animation_request(const String &action, const String &name);
	Dictionary camera_animation_state() const; ///< { active, name, time (client frames), animations (the map's count) }
	void camera_set_clip_depth_multiplier(double m); // lane MODCOMPAT-AI: SET_CAMERA_CLIP_DEPTH_MULTIPLIER (GlobalData + 0x950)
	// lane CAMPUI-1: the objectives screen's state for the Palantir's flag and a script's FLASH_*_BUTTON (slot 0 spell store, 1 objectives, 2 planning mode)
	void set_objectives_screen_open(bool open);
	void flash_palantir_button(int slot, int seconds);
	bool is_objectives_flag_flashing() const;
	Vector2 pixel_to_world(const Vector2 &pixel) const;
	Vector2 ping_point(const Vector2 &pixel) const; ///< the ping wheel (not retail): the map point a ping at this pixel marks (see the .cpp)
	Vector2 world_to_pixel(const Vector2 &world_xy) const;
	// lane IDLE-1: { ok, picked (the id the HUD's pick answers at `pixel`, 0 for none), target ("hit" / "miss" / "not_drawn" / "unknown" / "gone": the pick ray
	// against object `id`'s drawn model alone), target_t, selectable (HudObjects::isSelectable of `id`) } (the QA harness's failed-click verdict)
	Dictionary pick_probe(const Vector2 &pixel, int64_t id) const;
	// lane SELECT-1: the screen pixels of points inside object `id`'s oriented pick box (the first top-LOD W3D_BOX_ATTRIBUTE_ORIENTED box of its drawn model,
	// the one RotWK's pick tests: RW 0x59C680): the centre, then the centre +- 80 % of each half extent along the box's axes; empty without such a box
	PackedVector2Array pick_box_pixels(int64_t id) const;
	// engine -> movie: calls `function` on the clip at `path` below the HUD movie's root ("" = the root) with string arguments; { ok, result, error }
	Dictionary invoke_at(const String &path, const String &function, const PackedStringArray &args);
	// the radar picture's square in window pixels (Rect2; size 0 before the movie shows it)
	// lane PLAY-1: the window pixel of the radar that shows the SAGE point (x, y); (-1, -1) without a radar
	Vector2 world_to_radar_pixel(const Vector2 &world) const;
	bool create_radar_event(const Vector2 &world_xy, int type, bool jump_to = false); ///< lane RADAR-1 (QA): Radar::createEvent at SAGE (x, y), 4 seconds
	Rect2 get_radar_square() const;
	Dictionary get_radar_state() const;                 ///< lane HUD-7: the drawn picture, clip and view box beside the click rectangle
	Dictionary radar_probe(const Vector2 &world_xy) const; ///< lane HUD-7: the overlay cell of a world point, its alpha and drawn pixel
	void camera_set_angle_pitch(float angle, float pitch);  ///< lane HUD-7: a test hook (TacticalCamera::setAngle / setPitch)
	// the HUD movie's instance tree (a harness / log aid)
	String dump_tree(int max_depth) const;
	// the bounds of the HUD element that draws at `path` (stage coordinates -> window pixels), for harnesses that click a button: { found, x, y }
	Dictionary find_button_window(const String &path) const;
	// lane SPELL-2: a MappedImage of the HUD's collection as its texture and the source rectangle in texture pixels { found, texture, region } (the spell
	// book screens draw the command buttons' ButtonImage with it)
	Dictionary get_mapped_image(const String &name);
	// lane SPELL-2: { ok, path, shown, slots: [state names of the Palantir's 24 spell slots], targeting, radius, target_power, store: { open, initialized,
	// layout, points, set, states: [20 SetSpellButtonState names], sciences, level } }; the store's open / close (the purchases are sent at the close), a
	// click on store button `index`, and the movie's spell slot press
	Dictionary get_spellbook_state();
	// lane HUD-5: the drawable decorations of the last frame: { ops, health_bars, texts: [..], images: [..], drawn, notes }
	Dictionary get_icon_ui() const;
	void set_icons_covered(bool covered); // lane SAVE-UI1: a menu over the game hides the health bars and decorations
	// lane HUD-5 (a test hook, logged): the client selection becomes the one object (InGameUI::deselectAll + selectObject), as a click on it selects it
	bool select_object(int64_t id);
	// lane BACKLOG2 (proof captures): select_object, then the selection message a click sends (MSG_CREATE_SELECTED_GROUP_NO_SOUND, SelectionTranslator::
	// selectSingleWithoutSound), so the logic's selection follows and the object's command buttons act on it
	bool select_object_with_message(int64_t id);
	int64_t select_objects_with_message(const PackedInt64Array &ids);
	int64_t select_objects(const PackedInt64Array &ids); // lane SELPERF-1: the selection benchmark's N objects at once
	bool open_spell_store();
	void close_spell_store();
	void cancel_spell_store();
	bool spell_store_click(int64_t index);
	bool press_spell_slot(int64_t slot);

	void _process(double delta) override;
	void _input(const Ref<InputEvent> &event) override;

protected:
	static void _bind_methods();
	void _notification(int what); ///< lane BACKLOG3 (S-454): the window lost the focus: the keyboard's keys are up (HudInput::resetKeys)

private:
	void syncModifiers(const InputEventWithModifiers *event);
	void keyEvent(int dik, bool down, char32_t character);
	void syncModal(); ///< lane OPTS-6: a menu became (or stopped being) the input's top: the world's held input is cancelled ///< lane INPUT-1: inject_key with the layout's character ///< lane INPUT-1: Ctrl / Shift / Alt as the event reports them
	void handleInput(const Ref<InputEvent> &event); ///< SMOOTH-1: _input's work (replayed for input that waited for the logic worker)
	std::unique_ptr<HudDevice> d;
};

} // namespace godot
