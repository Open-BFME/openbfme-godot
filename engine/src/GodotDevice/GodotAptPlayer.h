// OpenBFME. GPL-3.0.
//
// Device layer, step A8 of menus-apt.md: the retail menus on screen.  AptMenuPlayer owns the Apt player (Libraries/Source/Apt), its
// host, the texture / string / font data read through the mounted retail archives, and draws the player's render list every
// update step through RenderingServer canvas items:
//
//   Apt::buildRenderList -> BuildAptCanvas (GameClient/AptCanvas.cpp, headless-tested) -> canvas_item_add_triangle_array per mesh op,
//   Font::draw_string per text op, a clip-children canvas group per mask.
//
// The stage (1024x768 for every retail movie) is stretched to the window's visible rect (donor rule, AptCanvas.h); `fit` keeps the
// aspect instead.  Godot mouse events are mapped back to stage coordinates and posted to the player's input queue (AptInput).
//
// Nothing is defaulted silently: boot() and render() report every failed resource, `get_report()` lists the unverified rendering
// rules in force, the labels the string table lacks, the fonts that were not found and the placeholders (native components) that
// are drawn as nothing.  Host events (fscommands, load requests, extern reads of unknown names, script errors) are queued for
// take_events() so a script may react without re-entering the player.

#pragma once

#include <godot_cpp/classes/font_file.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class Apt;
class AptFileSource;
class WindowManager;
class Shell;
class AptGodotHost;
class AptTextureStore;
class GameTextTable;
class FontSubstitution;
struct AptCanvasList;
class GadgetDrawList;
struct AptCanvasOp;

namespace godot
{

class RetailFileSystem;

// lane HUD-1: the owner of the native render components of a movie (clips tagged `_type`: AptPalantir::RenderRadar, RenderImage, TimerOverlay ...). The player calls it for
// every such placeholder with a canvas item it may draw into (window pixels); what the hook does not draw is not drawn.
class AptNativeHook
{
public:
	virtual ~AptNativeHook() = default;
	virtual void drawPlaceholder(const AptCanvasOp &op, RID canvasItem) = 0;
	// lane CAH-1: whether the hook draws this placeholder; a placeholder it leaves goes the player's own way (a RenderImage's mapped image)
	virtual bool handlesPlaceholder(const AptCanvasOp &op) const
	{
		(void)op;
		return true;
	}
};

class AptMenuPlayer : public Node2D
{
	GDCLASS(AptMenuPlayer, Node2D)

public:
	AptMenuPlayer();
	~AptMenuPlayer() override;

	// config: { levels: [[level, "Movie"], ...] (loaded in order), extern_values: { name: "value" }, component_movies: ["Movie", ...]
	// (instances of their exported symbols are native components: placeholders) }.  Returns { ok, errors: [...], load_ms, strings, fonts }.
	Dictionary boot(const Ref<RetailFileSystem> &fs, const Dictionary &config);
	bool is_booted() const { return m_booted; }

	// ---- time and drawing -----------------------------------------------------------------------------------------------------------
	// Advance the player by `delta` seconds (AptUpdate with the elapsed milliseconds; the fractional millisecond carries).  Returns the
	// number of player steps run.
	int tick(double delta);
	// Build the canvas for the current player state and draw it.  Called by _process after tick(); cheap when nothing stepped.
	void render(bool force);
	void set_auto_process(bool enabled);
	bool get_auto_process() const { return m_autoProcess; }
	void set_fit(bool fit);
	bool get_fit() const { return m_fit; }
	void set_show_placeholders(bool show);
	bool get_show_placeholders() const { return m_showPlaceholders; }

	// ---- levels ----------------------------------------------------------------------------------------------------------------------
	Dictionary load_movie(int level, const String &movie);
	bool unload_level(int level);
	void set_level_visible(int level, bool visible);
	bool has_level(int level) const;
	Dictionary invoke(int level, const String &function, const PackedStringArray &args);
	void set_extern_value(const String &name, const String &value);
	// The player's tree of a level, for logs.
	String dump_tree(int level, int max_depth);
	// { found, x, y, path } stage centre of the first button at or under `path` ("SoloPlayNav" resolves under the level root).
	Dictionary find_button(int level, const String &path);
	// { found, type, depth, x, y (stage, of the instance origin), visible, frame, total_frames }
	Dictionary instance_info(int level, const String &path);
	Rect2 component_rect(const String &instance_path) const; // lane PLAY-1: a gadget's window in stage space (empty: none)

	// ---- input (stage coordinates are the movie's pixels) -----------------------------------------------------------------------------------
	Vector2 window_to_stage(const Vector2 &window_position) const;
	Vector2 stage_to_window(const Vector2 &stage_position) const;
	void post_mouse_move_stage(const Vector2 &stage);
	void post_mouse_button(bool down);
	void post_mouse_wheel(int delta);
	void post_key(int vk_code, bool down);
	void _input(const Ref<InputEvent> &event) override;
	void _ready() override;
	void _process(double delta) override;

	// ---- shell mode (lane START-1): the real shell instead of a bare Apt player ---------------------------------------------------------
	// boot_shell replaces boot(): the Apt player is the WindowManager's (WindowManager.h is the AptHost: fscommands run the screens' commands,
	// extern reads reach their providers), the Shell holds the screen stack (AptScreenFactoryTable of the retail screens), the native gadget layer
	// (list boxes, combo boxes, text entries ... of the lobby) is drawn after the movies, and the Skirmish lobby is fed from `world`'s PlayerTemplate
	// store (a GameWorld that setup() ran on the same file system) so a slot's faction index is the logic's.  config: { seed: int (the lobby's game
	// seed; 0 = from the clock), profile: String (optional: the Skirmish profile name to start with) }.  Returns { ok, errors, load_ms, strings, fonts }.
	Dictionary boot_shell(const Ref<RetailFileSystem> &fs, Object *world, const Dictionary &config);
	void shell_show_shell_map(bool use); // lane FB7-1: Shell::showShellMap
	void shell_hide_background();        // lane FB7-1: WindowManager hide of the front-end background (RW 0x622C88(0))
	// lane CAH-2 (S-1914): the host has no screen for the main menu's request `action`: AptMainMenu::screenUnavailable (a message box tells the player,
	// its Ok makes the main menu usable again). Returns the stop line ("" when the main menu is not up).
	String shell_screen_unavailable(const String &action);
	// lane CAMP-2: { backdrop_image: the shell backdrop the canvas drew ("" none), background_level, background_commands: the visible commands of the
	// front-end background's level in the last list, background_mode: the window manager's }
	Dictionary get_backdrop_state() const;
	PackedInt32Array shell_levels() const; // lane CAMP-2: the Apt level of each screen of the stack, bottom first
	bool level_drawn(int level) const;     // lane CAMP-2: the last render list drew something visible of the level
	// lane CAMP-2: the BinkMovie components on screen: [{ path, title, frame, frames, error }]
	Array get_movies() const;
	bool is_shell_mode() const;
	// Shell::push / pop of a screen file ("MainMenu.apt"); false + the shell's errors in the report when the shell refused.
	bool shell_push(const String &filename);
	void shell_pop();
	// the screens of the Shell stack, bottom first ("AptLevel0.apt", "MainMenu.apt" ...); the top one's `_level` slot
	PackedStringArray shell_stack() const;
	int shell_top_level() const;
	// lane PLAY-1: an entry of the user's Options.ini (the shell's OptionPreferences, as loaded at boot and saved by the Options screen); null when the
	// file has no such key (the caller applies the retail default, e.g. GlobalData's for AlternateMouseSetup)
	Variant get_option(const String &key) const;
	// lane PLAY-1: the live game PlayerTribute.apt shows and sends tribute in (a GameWorld; null when the game ends)
	void set_tribute_world(Object *world);
	// lane HUD-5: the players screen's Status rows (GUI/PlayerStatusInfo.h) from GameWorld.get_player_status_state(), built with the shell's factions, colours
	// and game text; PlayerTribute.apt reads them when it loads its Status page. Returns { ok, rows: [[name, army, team, status, colour]], error }
	Dictionary set_player_status(const Dictionary &state);
	// lane MP-2: a movie -> engine command as the movie's fscommand runs it (WindowManager::invokeCallback: "AptLanLobby::OnCreateGameBttn",
	// "MpGameSetup::OnReadyPress" ...): scripted lobbies. false: no screen registered the name
	bool shell_fscommand(const String &command, const String &argument);
	// WindowManager::invokeAS on a level: the engine -> movie call (ShowMainMenu, SetBarTo ...)
	Dictionary shell_invoke(int level, const String &function, const PackedStringArray &args);
	// The pending new-game message the lobby's StartGame queued (SkirmishGameInfo as a Dictionary, see new_game_to_dictionary in GodotGameStart.h);
	// empty Dictionary when there is none.  Taking it clears it.
	Dictionary take_new_game();
	// The load screen's data (the Apt text records LoadingScreen::PlayerName%d ... and the providers of LoadScreen.apt read it): { cards: [{ name, army, rank, team,
	// color, load_music }...], local_card, loading_type: int, map }.  Must be set before the load screen is pushed.
	void set_load_screen(const Dictionary &info);
	// The same from a RESOLVED new-game message (take_new_game's Dictionary after GameWorld.prepare_new_game): the cards are the occupied slots, the faction names
	// come from the logic's store through the lobby's setup source.  Returns the card data as set_load_screen's Dictionary (for reports).
	Dictionary set_load_screen_from_game(const Dictionary &resolved_game);
	// lane END-1: the end of the game. end_game_screen carries out a GameWorld end-game request on GuiFX.apt (loaded on demand): "show_end_game" sets the Apt text
	// ":VictoryDefeat" to the game text of `text` and calls ShowEndGame("0" evil / "1" otherwise, sound, cheer); "hide_end_game" calls HideEndGame. { ok, level, error }
	Dictionary end_game_screen(const Dictionary &request);
	// lane MP-2: the disconnect screen (DisconnectScreen.apt, loaded by the window manager outside the shell stack while `state.visible`, released when it is not;
	// GameNetwork/DisconnectManager.h decides what it shows): state = GameWorld.net_status().disconnect { visible, rows: [{ used, name, votes, bar, kick }] }.
	// { ok, level, loaded, error }. take_disconnect_actions: the buttons pressed since the last call, [{ kind: "kick", row } | { kind: "quit" }]
	Dictionary disconnect_screen(const Dictionary &state);
	Array take_disconnect_actions();
	// lane MP-2: what SaveLoad.apt's replay page lists, set before it is pushed: { mode: 2 load, flags: 4 replay, last_replay: the GUI:LastReplay name,
	// replays: [{ path, name, map, date, time, compatible }] }; its Load comes back as the shell request "LoadReplayFile" with the path
	void set_save_load(const Dictionary &info);
	// lane MP-2: hands the world's LAN lobby (GameWorld.lan_open) to LanLobby.apt, set before it is pushed (null / no lobby: the screen says so). The screen keeps
	// the pointer: pop it before GameWorld.lan_close
	bool set_lan(Object *world);
	// lane CAH-1, the Create-a-Hero builder (CreateAHero.apt, GameClient/GUI/AptScreens/AptCreateAHero.h): set_create_a_hero gives the shell the world's
	// TheCreateAHeroSystem and the hero list (the system heroes of the archives and the profile's MyHero*.cah files in `profile_dir`; the lobby's Hero combo
	// lists the same heroes): { ok, heroes, errors }. get_create_a_hero_view: what the builder's 3D view shows { up, page ("M", "C", "A", "P" or ""),
	// revision (moves when the hero changes), record (the shown hero's .cah bytes; empty when none), class, subclass, rotate_left, rotate_right, zoom_in,
	// zoom_out }. get_create_a_hero_heroes: [{ name, unique_id, system, class, subclass, record }]. set_create_a_hero_view_texture: the texture drawn into
	// the movie's CreateAHero::DrawMapComponent clip (RW 0x91A3A9: the tactical view in the clip's rectangle); null removes it
	Dictionary set_create_a_hero(Object *world, const String &profile_dir);
	Dictionary get_create_a_hero_view() const;
	Array get_create_a_hero_heroes() const;
	void set_create_a_hero_view_texture(const Ref<Texture2D> &texture);
	// lane CAH-2: retail's Create-a-Hero save folder (GameClient/UserDataFolder.h): the application data folder (OS::get_data_dir: %APPDATA% on Windows,
	// CSIDL_APPDATA) + the install's gi.dat UserDataLeafName + "Save" (RW 0x644148 / 0x6DD398). { ok, folder, user_data, leaf, error }
	Dictionary create_a_hero_save_folder(const String &rotwk_install) const;
	// automation (scripted runs and videos, like lobby_apply): the builder's name entry gets `name` as if typed; false when the Appearance page is not up
	bool create_a_hero_type_name(const String &name);
	// copies the score screen data of the world's last clear_game_data into TimeLine.apt's environment
	bool set_score_screen_from_world(Object *world);
	// the graph of the TimeLine.apt on the shell stack for the graph mode `mode` ("Units", "Structures", "Resources", "FinalScore"; "" reads the movie's _graphMode
	// at `path`): { ok, mode, axis: { step, top, samples, y_labels, x_labels, total_time }, lines: [{ color, result, local, points: PackedVector2Array (x 0..1
	// over the samples, y 0..1 over the top), marks: PackedVector2Array }] }
	Dictionary get_timeline_graph(const String &mode, const String &path);
	// a member of a movie instance as a string (`found` false when the instance or member does not exist)
	Dictionary get_member(int level, const String &path, const String &name);
	// the game text of `label` ({ found, text }; a missing label is the retail "MISSING: 'label'" text and found false)
	Dictionary fetch_text(const String &label);
	Dictionary get_timeline_stats(); // lane END-2
	Dictionary quit_menu(const Dictionary &request);
	Array list_buttons(int level);
	// SetBarTo(local card, percent) on the load screen; returns false when the screen is not up or the movie has not loaded yet (the call is made by the next update)
	bool set_load_progress(double percent);
	Array take_load_progress_calls();
	// Test / automation hook of the Skirmish lobby (the screen must be on the stack and its movie settled): applies a setup through the SAME gadget messages a click on the
	// gadget sends (the C++ click-through tests prove the real mouse path; a windowed run drives the lobby with these because the drop-down geometry needs the gadget
	// layer's hit tests).  spec: { profile: String (types it into the add-profile popup and accepts), map: String (a map cache key), slots: [{ slot: int, state: int
	// (SlotState item data of the Player combo: 2..5 AI levels, 1 closed), faction: String (a PlayerTemplate name), color: int (colour list index), team: int
	// (-1 none, else 0-based) }] }; on LanLobby.apt (lane MP-2) the same slots, or { join_row: int } selects a row of the game list and presses Join.  Returns { ok, errors, game: the lobby's current GameInfo as the new-game Dictionary }.
	Dictionary lobby_apply(const Dictionary &spec);
	// lane FB7-1 (scripted input runs): the stage rectangle of a lobby gadget: "<slot>/<Leaf>" (a slot combo), "list/<slot>/<Leaf>" (its drop-down list),
	// "spot/<n>" (a start spot of the map window): { found, x, y, w, h }
	Dictionary lobby_gadget_rect(const String &name);
	// Everything the shell reported (notes of the window manager, shell errors, gadget layer errors and notes, unported commands): the unverified list
	Dictionary get_shell_report() const;
	// Signals (the hooks of other lanes): `shell_service(kind, a, b)` for every ShellServices call (kind "sound" a = the sound name: the AUDIO-1 hook for
	// UI sounds and music cues; "background", "mouse_visible", "tooltip"); `shell_request(action, argument)` for the main menu actions the shell does not
	// handle itself (ExitGame, Lan, LoadGame ...; `action` is the ShellAction name); `shell_screen(filename)` whenever the top screen changes.

	// ---- lane HUD-1: a window manager owned and stepped elsewhere (the in-game HUD) ------------------------------------------------------------
	// After boot() with an empty level list (it loads the strings and the fonts): the player draws the window manager's Apt player and no longer steps or reads input itself.
	// The native hook draws the clips the movie tags for the engine.
	void attach_window_manager(WindowManager *wm);
	void set_native_hook(AptNativeHook *hook);
	// the shell mode's window manager and shell (null in the other modes) and a switch that hands the mouse and keys to the HUD node instead of this player (C++ only)
	WindowManager *window_manager() const;
	Shell *shell_object() const;
	void set_input_forwarded(bool forwarded);

	// ---- observation -----------------------------------------------------------------------------------------------------------------
	// Host events since the last call: [{ kind: "fscommand"|"load"|"url"|"trace"|"extern_get"|"extern_set"|"extern_unknown"|"script_error",
	// a, b, frame }].
	Array take_events();
	// { frames, steps, last_ops, last_triangles, last_text_ops, canvas_items, rebuilds, steps_us (avg), list_us, canvas_us, submit_us,
	//   texture_loads, texture_load_ms, ... }
	Dictionary get_stats() const;
	void reset_timing();
	// { unverified: [...], errors: [...], missing_labels: [...], font_substitutions: [...], font_fallbacks: [...], placeholders: [...],
	//   notes: { kind: count }, script_errors: [...] }
	Dictionary get_report() const;
	Vector2 get_stage_size() const;
	// One line per canvas operation of the last build (kind, path, texture, triangles, first vertex colour, window-space bounds), for logs.
	PackedStringArray describe_ops() const;
	// Test hooks (godot/tests/apt_canvas_pixel_test.gd): a texture by name, and a list of synthetic canvas ops drawn through the same
	// drawCanvas as the player's.  ops: [{ kind: "mesh"|"mask_begin"|"mask_content"|"mask_end", rect: Rect2 (window pixels), color: Color,
	// texture: String, add: Color, mask_shape: bool }].
	void add_test_texture(const String &name, const Ref<Image> &image);
	void draw_test_ops(const Array &ops);

protected:
	static void _bind_methods();

private:
	struct Impl;
	std::unique_ptr<Impl> m;
	bool m_booted = false;
	bool m_autoProcess = true;
	bool m_fit = false;
	bool m_showPlaceholders = false;

	void clearCanvas();
	void advanceMovies(double deltaMs); // lane CAMP-2: the BinkMovie components' frames
	// lane PERF-1 r2: whether two canvas lists draw the same (every op field drawCanvas reads); the native components of the drawn list redrawn alone
	static bool sameCanvas(const AptCanvasList &a, const AptCanvasList &b);
	void redrawNativePlaceholders();
	void redrawCallbackPlaceholders();                                  // lane UI-4
	void drawRenderCallback(const AptCanvasOp &op, const RID &item);   // lane UI-4
	void drawCanvas(const AptCanvasList &list);
	// lane PERF-1 r2: the gadget layer's commands (false: not in shell mode), whether two lists draw the same, and the drawing
	bool buildGadgets(GadgetDrawList &commands);
	static bool sameGadgets(const GadgetDrawList &a, const GadgetDrawList &b);
	void drawGadgets(const GadgetDrawList &commands);
	void pumpShellEvents();
	Vector2 windowSize() const;
};

} // namespace godot
