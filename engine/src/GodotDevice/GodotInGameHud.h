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
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
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
	// the HUD movie's own report (unverified rules, errors) and the stops
	Dictionary get_report() const;
	// lane SMOOTH-1: the parts of the last _process in ms { hud_update_ms (the HUD: APT movie, control bar, radar, input), hud_camera_ms, hud_cursor_ms }
	Dictionary get_frame_timings() const;
	// lane BUILD-1: { placing, template, source, has_ghost, x, y, angle, legal (LegalBuildCode, 0 = may be placed) } of the building waiting for its site
	Dictionary get_placement() const;
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
	// edge scrolling is on in fullscreen (retail: GlobalData Windowed = No); a test or a windowed run can force it
	void set_edge_scroll(bool on);
	// scripted camera moves for the viewers (the same View::lookAt / setHeightAboveGround the radar click and the wheel call)
	void camera_look_at(const Vector2 &sage_xy);
	void camera_set_height(float height_above_ground);
	Vector2 pixel_to_world(const Vector2 &pixel) const;
	Vector2 world_to_pixel(const Vector2 &world_xy) const;
	// engine -> movie: calls `function` on the clip at `path` below the HUD movie's root ("" = the root) with string arguments; { ok, result, error }
	Dictionary invoke_at(const String &path, const String &function, const PackedStringArray &args);
	// the radar picture's square in window pixels (Rect2; size 0 before the movie shows it)
	Rect2 get_radar_square() const;
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
	bool open_spell_store();
	void close_spell_store();
	bool spell_store_click(int64_t index);
	bool press_spell_slot(int64_t slot);

	void _process(double delta) override;
	void _input(const Ref<InputEvent> &event) override;

protected:
	static void _bind_methods();

private:
	void handleInput(const Ref<InputEvent> &event); ///< SMOOTH-1: _input's work (replayed for input that waited for the logic worker)
	std::unique_ptr<HudDevice> d;
};

} // namespace godot
