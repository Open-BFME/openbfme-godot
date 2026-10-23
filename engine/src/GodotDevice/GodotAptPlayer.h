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
#include <godot_cpp/classes/input_event_key.hpp>
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
class GameWindow;
class Shell;
class AptGodotHost;
class AptTextureStore;
class GameTextTable;
class FontSubstitution;
struct AptCanvasList;
class GadgetDrawList;
struct AptCanvasOp;
struct AptCanvasSegment;
struct AptCanvasRedrawPlan;

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
	// lane OPTS-6: the menus without a mouse (GameClient/GUI/MenuNavigator.h). menu_input: a key event as the menus take it (the in-game HUD hands the
	// keys over while a menu has the input; true when it was used); menu_has_input: a menu has the input (the shell's top screen, or the quit menu over
	// a game); menu_pointer_moved: the real pointer moved (the focus frame goes); menu_nav_state: { level, focus, frame, focus_rect (stage), targets:
	// [{ id, kind, rect }], log: [String] } (scripted checks)
	bool menu_input(const Ref<InputEvent> &event);
	bool menu_controller_section(int delta);
	bool menu_has_input() const;
	// lane SAVE-UI1 / OPTS-6: modal_menu: a menu is the input's top (the world's input is cancelled and not served); menu_escape: the top screen's own Escape
	// procedure took the key (SaveLoad.apt closes on its release)
	bool modal_menu() const;
	bool menu_escape(bool pressed);
	void menu_pointer_moved();
	Dictionary menu_nav_state();
	String hit_test_stage(const Vector2 &stage);
	Dictionary gadget_pick(const Vector2 &stage);
	void _ready() override;
	void _process(double delta) override;

	// ---- shell mode (lane START-1): the real shell instead of a bare Apt player ---------------------------------------------------------
	// boot_shell replaces boot(): the Apt player is the WindowManager's (WindowManager.h is the AptHost: fscommands run the screens' commands,
	// extern reads reach their providers), the Shell holds the screen stack (AptScreenFactoryTable of the retail screens), the native gadget layer
	// (list boxes, combo boxes, text entries ... of the lobby) is drawn after the movies, and the Skirmish lobby is fed from `world`'s PlayerTemplate
	// store (a GameWorld that setup() ran on the same file system) so a slot's faction index is the logic's.  config: { seed: int (the lobby's game
	// seed; 0 = from the clock), profile: String (optional: the Skirmish profile name to start with), options_file: String (optional, lane OPTS-2: the
	// Options.ini to use instead of the user data folder's; the checks), presentation_fs: optional verified BFME2 shell source }. Returns { ok, errors, load_ms, strings, fonts }.
	Dictionary boot_shell(const Ref<RetailFileSystem> &fs, Object *world, const Dictionary &config);
	void shell_show_shell_map(bool use); // lane FB7-1: Shell::showShellMap
	void shell_hide_background();        // lane FB7-1: WindowManager hide of the front-end background (RW 0x622C88(0))
	// lane CAH-2 (S-1914): the host has no screen for the main menu's request `action`: AptMainMenu::screenUnavailable (a message box tells the player,
	// its Ok makes the main menu usable again). Returns the stop line ("" when the main menu is not up).
	String shell_screen_unavailable(const String &action);
	// lane CAMP-2: { backdrop_image: the shell backdrop the canvas drew ("" none), background_level, background_commands: the visible commands of the
	// front-end background's level in the last list, background_mode: the window manager's }
	Dictionary get_backdrop_state() const;
	// lane MENU-1: the front end's layering this frame (GameClient/GUI/Shell/ShellLayering.h): { top, screens, movies, overprints, shown_gadgets,
	// enabled_gadgets, input_blocked, unanchored_gadgets, problems } (problems empty: one screen, one sub-screen, no overprinting titles)
	Dictionary get_layering_report();
	// lane MENU-1: the loading screen a game start shows (GameClient/GUI/StillImageLoadScreen.h, RW 0x626A87): spec { game_mode, screen_kind
	// (GameLogic + 0x114), loading_save, picture } -> { kind: "still" | "players" | "other", picture }
	Dictionary choose_load_screen(const Dictionary &spec);
	// lane MENU-1: the picture loading screen of a campaign mission (RW 0x7583CC) over everything until hide_still_load_screen: { ok, error }
	Dictionary show_still_load_screen(const String &picture);
	void hide_still_load_screen();
	Dictionary get_still_load_screen() const; // { on, slots: [{ image, rect }], movie, movie_playing, movie_error, text_label, progress, progress_text }
	// lane SAVE-4e: the catch-up of an old (log-only) save on the picture screen: `text` and a bar at `percent` (0 .. 100) between the "Loading" text and
	// the picture, in the screen's font and colour (OpenBFME's: retail's picture screen has no bar, S-5012); a negative percent removes them
	void set_still_load_progress(double percent, const String &text);
	PackedInt32Array shell_levels() const; // lane CAMP-2: the Apt level of each screen of the stack, bottom first
	bool level_drawn(int level) const;     // lane CAMP-2: the last render list drew something visible of the level
	// lane CAMP-2: the BinkMovie components on screen: [{ path, title, frame, frames, error }]
	Array get_movies() const;
	bool is_shell_mode() const;
	// Shell::push / pop of a screen file ("MainMenu.apt"); false + the shell's errors in the report when the shell refused.
	bool shell_push(const String &filename);
	// lane OPTS-6: AptOptions::OpenScreen (RW 0x91ED91): Options.apt pushed (unless it is up), its four mode bytes, the front-end background shown
	// (RW 0x91EE08: setBackground(1)); the quit menu passes (0, 0, 0, 0) (RW 0x921783). False when the screen did not come.
	bool open_options(bool network_enabled, bool allow_resolution, bool allow_advanced, bool advanced_only);
	void shell_pop();
	// the screens of the Shell stack, bottom first ("AptLevel0.apt", "MainMenu.apt" ...); the top one's `_level` slot
	PackedStringArray shell_stack() const;
	int shell_top_level() const;
	// lane PLAY-1: an entry of the user's Options.ini (the shell's OptionPreferences, as loaded at boot and saved by the Options screen); null when the
	// file has no such key (the caller applies the retail default, e.g. GlobalData's for AlternateMouseSetup)
	Variant get_option(const String &key) const;
	// lane OPTS-2: every entry of Options.ini; set_option replaces one (an empty / null value removes it; false for a key or value Options.ini cannot
	// hold); save_options writes the file ({ ok, error, path }). The retail keys keep their retail text, the OpenBFME extras use the "OpenBFME" prefix.
	Dictionary get_options() const;
	bool set_option(const String &key, const Variant &value);
	Dictionary save_options();
	// lane OPTS-2: the static detail in effect (GameLODManager::state, RW 0x601C62's targets) and the display gamma of Brightness (RW 0x641E8B):
	// { ok, level, level_name, stop, model_lod, ..., texture_reduction, use_shadow_mapping, max_particle_count, display_gamma }
	Dictionary get_detail_state() const;
	// lane OPTS-2: the page Options.apt shows when it is the shell's top screen (AptOptions + 0x27C: 1 basic, 2 advanced), else 0
	int options_page() const;
	// lane OPTS-2: the CommandMap's hot keys for the Controls tab: { ok, error, rows: [{ name, key, transition, ctrl, shift, alt, game, shell, category,
	// display_name, description }] } in file order
	Dictionary get_command_map();
	Dictionary font_metrics(const String &name, int point_size, bool bold, const String &text) const; // lane UI-4
	Array drawn_texts(bool clear); // lane UI-4: the gadgets' and render callbacks' texts drawn since the last clear
	// lane OPTS-4 (scripted layout checks): the gadget layer's draw commands of the last frame, in draw order: { kind ("image" "fill" "rect" "line" "text"
	// "clip" "clip_end"), window, root (the top-level window's index in this list's order of first appearance, -1: the page's own drawing), lone (the
	// open drop-down's), rect (window pixels), root_name, root_rect (the top-level window's box, window pixels), root_stage_rect (the same in
	// stage pixels), image, text }
	Array gadget_draw_list();
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
	// replays: [{ path, name, map, date, time, compatible }] }; its Load comes back as the shell request "LoadReplayFile" with the path. Lane SAVE-2, the
	// saved-game pages: mode 3 save / 2 load, flags 1 Campaign | 2 Skirmish, in_game (opened over a game), saves: [{ path, name, map, date, time, kind (1 / 2),
	// loadable, refusal, retail_import }]; Save, Load and Delete come back as "SaveGameFile" (the typed name), "LoadGameFile" / "DeleteGameFile" (the path).
	// Called again while the screen is open (after a delete) it fills the lists again.
	void set_save_load(const Dictionary &info);
	// lane CAMPSAVE-1: CampaignMenu.apt's content ({have_mission, mission_name, load_screen_image, victorious}, RW 0x5EADDE), the bonus unlock (Options.ini
	// BCU = "true" and TheGlobalData + 0xAF5, RW 0x927EC6) and the Save folder MainMenuContinueCampaign looks in
	void set_campaign_menu(const Dictionary &content);
	Dictionary unlock_bonus_campaign();
	bool is_bonus_campaign_unlocked() const;
	void set_save_folder(const String &folder);
	// lane SAVE-2: the host's answer to "SaveGameFile": the screen's message box shows GUI:GameSaved (ok) or `error`; false when SaveLoad.apt is not the top
	bool save_load_saved(bool ok, const String &error);
	// lane SAVE-2: the host refused a "LoadGameFile" (GameWorld.check_save): the screen shows GUI:ErrorLoadingGame with `name` and `error`, and stays
	bool save_load_refused(const String &name, const String &error);
	// lane MP-2: hands the world's LAN lobby (GameWorld.lan_open) to LanLobby.apt, set before it is pushed (null / no lobby: the screen says so). The screen keeps
	// the pointer: pop it before GameWorld.lan_close
	bool set_lan(Object *world);
	// lane ONLINE-4: the online screens (OnlineShell.apt, GameClient/GUI/AptScreens/AptOnlineShell.h) on the OpenBFME lobby service. set_online makes the shell's
	// lobby client before OnlineShell.apt is pushed: options { lobby_url (the requests' prefix), remembered_name (the guest name of the last sign-in),
	// poll_ms }; the world gives the profile identity and the faction / colour counts the lobby's views are checked against. { ok, error }.
	// online_take_requests: the HTTP requests to send now [{ id, method, path, body, token }] (scripts/online_http.gd sends them; at most one in flight);
	// online_deliver: an answer (the HTTP status and body, or the transport error). online_status: { signed_in, name, page, state, in_game, host, slot,
	// games: ["name|map|players|lock"], chat, motd, boxes, connecting, remembered_name }. online_apply: scripted input (tests, the proof walk) { nickname,
	// game_name, game_password, select_row, command (an fscommand name), invoke (a function of the shell movie's root, with args) } -> { ok, errors }
	Dictionary set_online(Object *world, const Dictionary &options);
	Array online_take_requests();
	void online_deliver(int64_t id, int64_t status, const String &body, const String &error);
	Dictionary online_status();
	Dictionary online_apply(const Dictionary &spec);
	Dictionary online_social_status() const;
	Array online_social_events();
	Dictionary online_social_apply(const Dictionary &spec);
	Dictionary online_messenger_sync(const Dictionary &spec);
	// Sol r1: the host could not play the START OnlineShell.apt handed on (GameWorld.net_online refused it): the screens show a recoverable lobby error
	// and go back to the game list (false: no online screens). online_redact: a text with its credentials masked (OnlineLobbyClient::redact: relay,
	// rejoin and spectator tickets, session tokens), for anything the game prints of the lobby's
	bool online_start_failed(const String &error);
	// lane ONLINEUX-1: online_game_failed: an online game the screens started could not go on (game.gd _online_game_failed): the message the online
	// screens show once they are up again, and the game is not left (OnlineShellContext::gameFailed: r2, the lobby keeps offering it for a rejoin);
	// online_set_counts: the lobby's counts (GET /v1/stats: {players, games_open, games_running}) for the home page's ticker
	void online_game_failed(const String &text);
	void online_set_counts(const Dictionary &counts);
	static String online_redact(const String &text);
	// lane DEVPORT-8 (MENUSTYLE-1): the options file read before the shell boots (the main menu style decides the shell's presentation source):
	// the path boot_shell uses without an options_file (the user data folder's Options.ini), and that file's entries through OptionPreferences (the
	// shell's reader). { ok, exists, values: { key: value }, error }: a missing file is no entries (the first run, ok); an unreadable one is an error
	static String default_options_path();
	static Dictionary read_options_file(const String &path);
	// lane ONLINEHUB-1 (docs/PLATFORM_API.md): the OpenBFME platform API (Quick Match, tournaments, community statistics, game results) and the games this
	// installation recorded ("Your stats"). set_platform: the client { base_url (shown in reports only), install (32 hex digits), nick, share_results
	// (the Options row), search_timeout_s } -> { ok, error }; platform_take_requests / platform_deliver as the lobby's (scripts/online_http.gd sends them:
	// [{ id, method, path, body }]); platform_events: the host's events [{ kind, id, code, text }] (a result sent / dropped). set_game_records: the
	// record file's text -> { ok, count, rejected }; record_game: one finished game (GameWorld.clear_game_data's "record" completed by the host: id,
	// kind, mode, start, end) -> { ok, line (to append to the file), error, reported, not_reported (why not) }. platform_status / platform_apply: the
	// TOURNAMENT and STATS pages' state and scripted input (tests, the proof walk)
	Dictionary set_platform(const Dictionary &options);
	void set_share_results(bool share); // the Options row, at once: off drops the results still queued or waiting for their retry
	Array platform_take_requests();
	void platform_deliver(int64_t id, int64_t status, const String &body, const String &error);
	Array platform_events();
	Dictionary set_game_records(const String &text);
	Dictionary record_game(const String &json);
	Dictionary platform_status();
	Dictionary platform_apply(const Dictionary &spec);
	// lane CAH-1, the Create-a-Hero builder (CreateAHero.apt, GameClient/GUI/AptScreens/AptCreateAHero.h): set_create_a_hero gives the shell the world's
	// TheCreateAHeroSystem and the hero list (the system heroes of the archives and the profile's MyHero*.cah files in `profile_dir`; the lobby's Hero combo
	// lists the same heroes): { ok, heroes, errors }. get_create_a_hero_view: what the builder's 3D view shows { up, page ("M", "C", "A", "P" or ""),
	// revision (moves when the hero changes), record (the shown hero's .cah bytes; empty when none), class, subclass, rotate_left, rotate_right, zoom_in,
	// zoom_out }. get_create_a_hero_heroes: [{ name, unique_id, system, class, subclass, record }]. set_create_a_hero_view_texture: the texture drawn into
	// the movie's CreateAHero::DrawMapComponent clip (RW 0x91A3A9: the tactical view in the clip's rectangle); null removes it
	Dictionary set_create_a_hero(Object *world, const String &profile_dir);
	Dictionary get_create_a_hero_view() const;
	Array get_create_a_hero_heroes() const;
	void set_create_a_hero_portrait(const String &image);
	void takeCreateAHeroPicture(); // lane CAHPIC-1: Create-a-Hero's TAKE PICTURE (the guard's frames, the armed picture saved), from _process
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
	// lane DEVPORT-8 (MENUSTYLE-1): a property of the clip at `path` on `level` set as ActionScript's `clip.<name> = value` does (AptCharacterInst::setProperty:
	// _visible, _x, _y, _alpha ...); false when there is no such clip or property. game.gd hides BFME2's own MainMenu.apt campaign buttons with it while the
	// BFME2 campaigns are switched off (game.gd BFME2_CAMPAIGNS)
	bool set_clip_property(int level, const String &path, const String &name, double value);
	// the game text of `label` ({ found, text }; a missing label is the retail "MISSING: 'label'" text and found false)
	Dictionary fetch_text(const String &label);
	// lane OPTS-1: the shell's skin for the OpenBFME options screen (a Godot UI drawn over the shell, not a retail screen): a mapped image as the gadget
	// layer draws it ({ok, texture, region (texture pixels), size (the image's own size)}, or {ok false, error}) and the font a request (family name,
	// stage pixel size) draws with ({font, size, fallback: the engine default because neither fontsubstitution.ini nor the corpus fonts have it})
	Dictionary shell_mapped_image(const String &name);
	Dictionary shell_font(const String &name, float size);
	// lane OPTS-3: the OpenBFME page of Options.apt (GUI/AptScreens/OpenBFMEOptionsPage.h; not retail). set_openbfme_available: the device has the
	// page's settings (the basic page shows the OPENBFME button). openbfme_page_open / close: Options.apt's page 3 on / off ({ ok, error }).
	// openbfme_page_show(spec) (re)builds it: { subtitle, tabs_header, tabs: [String], tab, panel_header, note, rows: [{ kind: "section" | "choice" |
	// "check" | "slider" | "text", key, label, help, items: [String | { text, enabled }], value, min, max, value_text, enabled }], list: [[a, b], ...]
	// (optional: a two-column list instead of rows) }. openbfme_page_set_row(row) updates one row in place. openbfme_page_events: [{ kind: "value" |
	// "tab" | "cancel" | "reset" | "accept" | "entry", key, value }]. openbfme_page_state: { available, open, entry: Rect2 (stage; absent when not
	// shown), tab, tabs: [Rect2], rows: { key: { rect, value, enabled } }, buttons: { cancel, reset, accept: Rect2 }, help }. openbfme_page_choose: a
	// value through the gadget as a click gives it (scripted checks; { ok, error }).
	void set_openbfme_available(bool available);
	Dictionary openbfme_page_open();
	void openbfme_page_close();
	Dictionary openbfme_page_show(const Dictionary &spec);
	bool openbfme_page_set_row(const Dictionary &row);
	Array openbfme_page_events();
	Dictionary openbfme_page_state() const;
	Dictionary openbfme_page_choose(const String &key, int value);
	// lane ONLINEMENU-1: the online FRIENDS button (label; empty: none) and page (a page spec as openbfme_page_show's, plus list_from_row and
	// list_activates; events as openbfme_page_events' with "submit" / "list_select" / "list_activate" and a "text"; state: open, button rect, tabs,
	// rows, list rows and rect, help)
	void online_friends_button(const String &label);
	bool online_ask(const String &key, const String &title, const String &text); // lane ONLINEMENU-1: AptOnlineShell::askYesNo
	PackedStringArray online_answers();
	bool online_asking(const String &key);
	Dictionary online_friends_page_show(const Dictionary &spec);
	void online_friends_page_close();
	Array online_friends_page_events();
	Dictionary online_friends_page_state() const;
	// lane OPTS-5: the one Options menu (AptOptionsScreen::kOpenBFMEMenu). openbfme_page_show's rows take "entry" (a text entry: value_text, max) and
	// "column" (0 / 1); its events take "release" (a slider let go), and "entry" is the menu's opening (value 1: show the detail settings first).
	// options_retail_rows: { key: row } of the original game's settings as the page shows them ("Retail.<gadget>": kind, label, help, help_label,
	// items, value, min, max, value_text, enabled); openbfme_menu_finish(accept): RotWK's Save or Cancel, the screen closes ({ ok, error });
	// openbfme_menu_reset: RotWK's Reset of every original setting
	Dictionary options_retail_rows() const;
	Dictionary openbfme_menu_finish(bool accept);
	Dictionary openbfme_menu_reset();
	Dictionary get_timeline_stats(); // lane END-2
	Dictionary quit_menu(const Dictionary &request);
	// lane HOVER-1: the menus' tool tip (CursorTooltip.h): the label, the text GuiFX shows, the GuiFX calls made, the pointer, the errors
	Dictionary get_cursor_tooltip() const;
	Array list_buttons(int level);
	Array list_gadgets();         // lane MENUWORK-1: every live gadget window (name, kind, stage rect, enabled, hidden, text, checked)
	PackedStringArray take_commands(); // lane MENUWORK-1: WindowManager::takeInvokedCommands
	String apt_text(const String &name) const;
	Dictionary screen_state() const; // lane MENUWORK-1: the top screen's engine state (the menu inventory's evidence of a press) // lane MENUWORK-1: the text record `name` (bfme_setAptText, e.g. APT:_level5.MessageBox_Text), "" when none
	// SetBarTo(local card, percent) on the load screen; returns false when the screen is not up or the movie has not loaded yet (the call is made by the next update)
	bool set_load_progress(double percent);
	Array take_load_progress_calls();
	// Test / automation hook of the Skirmish lobby (the screen must be on the stack and its movie settled): applies a setup through the SAME gadget messages a click on the
	// gadget sends (the C++ click-through tests prove the real mouse path; a windowed run drives the lobby with these because the drop-down geometry needs the gadget
	// layer's hit tests).  spec: { profile: String (types it into the add-profile popup and accepts), map: String (a map cache key), slots: [{ slot: int, state: int
	// (SlotState item data of the Player combo: 2..5 AI levels, 1 closed), faction: String (a PlayerTemplate name), color: int (colour list index), team: int
	// (-1 none, else 0-based) }] }; on LanLobby.apt (lane MP-2) the same slots, or { join_row: int } selects a row of the game list and presses Join.  Returns { ok, errors, game: the lobby's current GameInfo as the new-game Dictionary }.
	Dictionary lobby_apply(const Dictionary &spec);
	// lane UI-6 (checks): the Skirmish lobby's game { found, map, rules: [10], slots: [{ state, template, color, team, start, name }], restored (the kept game
	// it began from, "" for the defaults) }; found false when no lobby is up
	Dictionary lobby_state();
	// lane FB7-1 (scripted input runs): the stage rectangle of a lobby gadget: "<slot>/<Leaf>" (a slot combo), "list/<slot>/<Leaf>" (its drop-down list),
	// "spot/<n>" (a start spot of the map window): { found, x, y, w, h }
	Dictionary lobby_gadget_rect(const String &name);
	// Everything the shell reported (notes of the window manager, shell errors, gadget layer errors and notes, unported commands): the unverified list
	Dictionary get_shell_report() const;
	// lane SHELLFIX-1 (tests): where the shell's pictures come from now: { style ("rotwk-201" / "bfme2-106"), view3d_style, movies: { file: the disk
	// path of the archive the shell read it from } for every screen on the stack, textures: { name: archive } for every textured shape of the frame
	// drawn last, original_images: [names] the BFME2 shell took from the game's own archives (images BFME2 1.06 does not have; map pictures, "file:", are
	// game content and not counted) }
	Dictionary shell_drawn_sources() const;
	// Signals (the hooks of other lanes): `shell_service(kind, a, b)` for every ShellServices call (kind "sound" a = the sound name: the AUDIO-1 hook for
	// UI sounds and music cues; "background", "mouse_visible", "tooltip"); `shell_request(action, argument)` for the main menu actions the shell does not
	// handle itself (ExitGame, Lan, LoadGame ...; `action` is the ShellAction name); `shell_screen(filename)` whenever the top screen changes.

	// ---- lane HUD-1: a window manager owned and stepped elsewhere (the in-game HUD) ------------------------------------------------------------
	// After boot() with an empty level list (it loads the strings and the fonts): the player draws the window manager's Apt player and no longer steps or reads input itself.
	// The native hook draws the clips the movie tags for the engine.
	void attach_window_manager(WindowManager *wm);
	void set_native_hook(AptNativeHook *hook);
	// lane HUD-6: the font the movie's texts draw with for (name, point size): fontsubstitution.ini, the corpus fonts (C++ only; the HUD's help box)
	Ref<Font> font_for(const std::string &name, float size, float *drawSize, bool bold = false);
	// lane UI-6: `font` (from font_for) at `px` pixels with RotWK's GDI text cell (ascent = the cell's baseline, height = the char height)
	Ref<Font> font_cell(const Ref<Font> &font, int px);
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
	void recycleCanvas();                  // lane SELPERF-1: the items to the pool for the next full redraw
	RID takeItem(RID parent, int drawIndex); // a pooled (reset) or new canvas item
	void freeItemPool();
	void advanceMovies(double deltaMs); // lane CAMP-2: the BinkMovie components' frames
	// lane PERF-1 r2: the native components of one drawn segment (lane HUDREDRAW-1) redrawn alone
	void redrawNativePlaceholders(size_t segment);
	void redrawCallbackPlaceholders(size_t segment);                      // lane UI-4
	void drawRenderCallback(const AptCanvasOp &op, const RID &item);   // lane UI-4
	void drawCanvas(const AptCanvasList &list);
	// lane HUDREDRAW-1: the list's segments (AptCanvasSegments) on screen as `plan` says (AptCanvasPlanRedraw: kept, drawn again, drawn anew)
	void redrawSegments(const std::vector<AptCanvasSegment> &segments, const AptCanvasRedrawPlan &plan);
	void drawSegment(const AptCanvasList &list, size_t segment, const std::map<std::string, const GameWindow *> &gadgetOfPath);
	void countLevels(const AptCanvasRedrawPlan &plan); // get_stats "levels"
	std::map<std::string, const GameWindow *> gadgetPaths() const; // lane MENU-1
	// lane PERF-1 r2: the gadget layer's commands (false: not in shell mode), whether two lists draw the same, and the drawing
	bool buildGadgets(GadgetDrawList &commands);
	static bool sameGadgets(const GadgetDrawList &a, const GadgetDrawList &b);
	void drawGadgets(const GadgetDrawList &commands);
	void drawStillLoadScreen(); // lane MENU-1
	void pumpShellEvents();
	Vector2 windowSize() const;
	bool menuKey(const InputEventKey *key); // lane OPTS-6: the menus' keys (_input, menu_input)
	void syncNav() const;                   // lane OPTS-6: the navigator follows the menu that has the input
};

} // namespace godot
