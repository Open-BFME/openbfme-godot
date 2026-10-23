// OpenBFME. GPL-3.0.
//
// Device layer: the live game as a Godot node (lane LOGIC-1). GameWorld owns one LiveGame (GameClient/LiveGame.h: retail templates, map,
// players, GameLogic with its retail scheduler, live objects, drawables) and shows it:
//
//   * ticks the logic on retail's clock: advance(delta) feeds wall time to LogicFrameClock (5 logic frames per second, each run as the six
//     phases of GameLogic::update), the render side runs at any rate and draws drawables interpolated between the logic frames (stop S-151);
//   * one DYNAMIC W3DInstancer for every drawable of a live object: each render frame the draw modules step their animations
//     (W3DScriptedModelDraw::advance), the interpolated pose of the drawable is handed to the instancer (set_instance_transform / set_instance_pose),
//     a model that changes with the object's model condition is swapped, objects that die lose their instances;
//   * one STATIC W3DInstancer for the map's client-only trees, shrubs and props (not objects: they never change).
//
// Godot's frame: SAGE (x east, y north, z up) -> Godot (x, z, -y), spec maps-and-terrain.md 2.1.
//
// What this class does not decide: nothing about the simulation. The logic state is only read (drawables) and, through the explicit
// calls (destroy_object), changed by the caller on purpose.

#pragma once

#include "GameLogic/Object/ObjectGeometry.h"

#include <array>

#include "GameClient/VideoPlayer.h" // lane CAMP-1H: m_videos (complete for the unique_ptr)
#include "GameClient/EndGame.h"
#include "GameClient/CommandLogLoad.h" // lane SAVE-2: m_saveLoad (complete for the unique_ptr)
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameClient/ScriptCameraDirector.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LiveGameAudio.h"
#include "GameClient/MapCache.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANLobby.h"
#include "GameNetwork/NetGameSession.h"
#include "GameNetwork/ProfileChoice.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DStreakDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DVegetationSway.h"
#include "GodotDevice/GodotW3DMaterial.h"
#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h" // lane HCKEY-1: EntryView::staticKeyPalette

#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

class ArchiveW3DFileSource;
class WW3DAssetManager;
class RetailObjectWorld;
struct GameLogicSettings;
class LiveGame;
class SpellStoreModel;
class InGameSpellBookModel;
class LiveFX;
class MapObjectRuntime;
class TreeTopple;
struct MapObjectOptions;
class TributeSource;

namespace godot
{

class FXPlayer;
class RetailFileSystem;
class W3DInstancer;

class GameWorld : public Node3D
{
	friend class GameWorldTribute; // lane PLAY-1: the tribute screen's source reads the live game
	GDCLASS(GameWorld, Node3D)

public:
	GameWorld();
	~GameWorld() override;

	// Loads the retail object world (the subsystem INI load with the real parsers, PlayerTemplates and the typed runtime modules: a few
	// seconds), the GameData / AIData settings and the asset manager. Once per mounted file system. { ok, errors, seconds, templates, factions }
	Dictionary setup(const Ref<RetailFileSystem> &fs);

	// C++ access for the other device classes (the shell's lobby lists the logic's PlayerTemplate store and colour list): null / empty until setup() succeeded
	RetailObjectWorld *object_world() const { return m_world.get(); }
	const GameLogicSettings &logic_settings() const { return *m_settings; }

	// Loads `map_name` ("map mp fall back 4p") as live objects and builds the scene under this node. options (all optional):
	//   slots: Array of { player: "Player_1", faction: "FactionMen", human: bool, team: int, color: 0xRRGGBB, start_index: int }  lobby slots taking over
	//          the map's players of those names
	//   starting_money: int (>= 0 overrides every template; default: the faction's StartMoney, else `default_cash`), default_cash: int (0 = GameData DefaultStartingCash)
	//   seed: int (the logic RNG seed, 1)       animations: bool (true)     house_colors: bool (true)     texture_animation: bool (true)
	//   max_frames_per_advance: int (10)     six_tick_pacing: bool (false: a frame runs atomically; true: one logic phase per 30 Hz engine tick)
	//   logic_thread: bool (SMOOTH-1's logic worker; default false here, true for start_new_game: set_logic_thread changes that default)
	//   presentation_delay: float seconds (0.03 threaded, 0)
	//   hash_every_frame: bool (false; get_frame_hashes)
	//   terrain: bool (false; lane CAH-1: the map's terrain under this node, as start_new_game builds it)
	// Returns the report ({ ok, errors, stops, map, objects, drawables, players, loop, instancing, timings_ms, lighting, focus, ... }).
	Dictionary load_map(const String &map_name, const Dictionary &options);

	// ---- lane START-1: starting a skirmish from the lobby's new-game message --------------------------------------------------------------------------------
	// prepare_new_game(message): the logic consumes the NewGameMessage (take_new_game's Dictionary, GodotGameStart.h) at frame 0: seeds the logic RNG from the seed in it,
	// resolves every random start position, faction and colour in retail's order (GameLogic/NewGame/SkirmishRandom.h; every draw is lockstep state) and returns
	// { ok, errors, resolved: the message with every choice resolved, map: the LiveGame map name, notes }.  The host builds the load screen from `resolved`, then
	// calls start_new_game.  A second prepare replaces the first.
	Dictionary prepare_new_game(const Dictionary &message);
	// start_new_game(options): loads the prepared game: the map, the sides and players, the map's objects, every player's starting structure and units, the client
	// terrain and the live scene under this node, calling options.progress (a Callable taking the percentage 0..100; the milestones of LiveGame::progressMilestones
	// then 96 terrain, 97 object assets, 98 live objects, 100 done) synchronously.  options: progress: Callable, terrain_options: Dictionary (MapTerrainBuilder),
	// animations / house_colors / texture_animation / max_frames_per_advance as load_map.  Returns load_map's report plus
	// start: { slot_players, starting_objects: [{ template, id, x, y, z, structure }], errors, progress: [percent], local_player, local_start: Vector3 (Godot space, the
	// local player's start structure or start waypoint) }.
	Dictionary start_new_game(const Dictionary &options);

	// Feeds `delta` seconds of wall time: runs the logic frames that are due, steps the animations, refreshes the poses (and creates or removes
	// instances for objects that appeared or died). Does nothing while paused.
	void advance(double delta);
	// When on, _process calls advance(delta) (default off: scripts drive it)
	void set_auto_advance(bool enabled) { m_autoAdvance = enabled; }
	bool get_auto_advance() const { return m_autoAdvance; }
	void set_paused(bool paused) { m_paused = paused; }
	bool is_paused() const { return m_paused; }
	// multiplies the wall time before it reaches the logic clock (1.0 = retail speed)
	void set_time_scale(double scale) { m_timeScale = scale; }
	double get_time_scale() const { return m_timeScale; }

	int64_t get_frame() const;

	int64_t get_presented_frame() const; ///< lane PATHPERF-1: the published snapshot's frame, no wait
	int64_t get_state_hash() const; // the deterministic world state hash (GameLogic::computeStateHash), as an unsigned 32-bit value
	double get_alpha() const;
	int64_t get_object_count() const;
	Array get_object_ids() const; // ids of the live objects in the object list's order

	// { ok, id, template, team, owner, x, y, z, angle, health, max_health, damage_state, contained_by, members: [ids], kind_of_structure, drawable, models: [names] }
	Dictionary get_object(int64_t id) const;
	// destroys an object like any logic caller would (GameLogic::destroyObject): it is deleted at the end of the next logic frame's phase 5
	bool destroy_object(int64_t id);
	// lane COMBAT-1: { ok, kills, damage_applications, bounty_paid, in_flight, projectiles_launched, projectiles_detonated, projectile_ground_hits, unported_nuggets, auto_acquire }
	Dictionary get_combat_report() const;
	// VIS-1: the shroud of the local player (cells, counts, settings), one point's cell status (0 clear, 1 fogged, 2 shrouded), an object's status
	// (0 always visible, 1 clear, 2 partially clear, 3 fogged, 4 shrouded), and whether the client draws it (a debug view turns it off)
	Dictionary get_shroud_report() const;
	// lane AI-2 r5: every computer player's live tactics (kind, started, step, the teams' member ids and centres, the target, waypoints) for viewers / videos
	Array get_ai_report() const;
	int64_t get_shroud_status_at(double x, double y) const;
	int64_t get_object_shroud_status(int64_t objectId) const;
	void set_shroud_drawn(bool on);
	PackedByteArray get_shroud_cells() const;
	// lane PROJ-1: the projectile objects in flight (KindOf PROJECTILE with a BezierProjectileBehavior): id, template, position, the position of the previous logic frame, the unit
	// X axis of the basis, the flight step / segment count and whether it has landed. The client draws them (the streak draw module is not ported: stop S-365).
	Array get_projectiles() const;
	Dictionary get_victory_report() const;
	void start_victory_rules(const PackedStringArray &players = PackedStringArray());
	// lane END-1: the end of the game (GameClient/EndGame.h). The end sequence runs on every presented logic frame (the human player's win / loss scripts, the
	// defeat messages, Eva, showEndGame / hideEndGame); its presentation requests are taken by the host ({ kind, text, name, evil, sound, cheer, eva }).
	Dictionary get_end_game_state() const;
	int hud_end_game_timer() const;
	Array take_end_game_requests();
	// GameLogic::clearGameData (RW 0x7792BC): keeps the score screen's data (when `show_score_screen`) and tears the game down (the worker stopped, the
	// network session, audio, effects and objects released); a new game may start afterwards. { ok, score_screen: { type, frames, entries: [...] } }
	Dictionary clear_game_data(bool show_score_screen);
	// lane END-2, the quit menu: what QuitMenu.apt asks of the game ({in_game, mode, kind, replay, local_defeated, allied_victory}), and the surrender /
	// exit message (MSG_SELF_DESTRUCT with `transfer`, the local player's, on the lockstep command path)
	Dictionary get_quit_menu_context();
	// lane HUD-5: what the players screen's Status page needs from the live game (AptMenuPlayer::set_player_status): { ok, game (the resolved new game message),
	// orig: [{ template, color }] per slot, local_slot, network, mode, show_random_template, show_random_color, slots: [{ has_player, defeated, observer, connected }] }
	Dictionary get_player_status_state();
	Dictionary self_destruct(bool transfer);
	// the score screen data of the last clear_game_data (AptMenuPlayer.set_score_screen_from_world copies it)
	const ScoreScreenData &scoreScreenData() const { return m_scoreScreen; }
	// debug / test hook (refused in a network or threaded-logic game, like debug_grant_upgrade): kills every object of `player` except objects of the
	// template `keep` (empty: none), through Object::kill (the death path, no score credit)
	Dictionary debug_kill_player_objects(const String &player, const String &keep);
	Dictionary debug_damage_object(int64_t id, double amount, int64_t source_id);
	Dictionary debug_set_object_status(int64_t id, const String &status, bool on); // lane SPELL-3: a test hook (start option test_hooks): an object status by name
	// lane SPELL-3 (review r3): a test hook (start option test_hooks): the global weather back to none, its map attribute modifier removed from every object
	// (GlobalWeatherSystem::clear, RW 0x71A04F) - the spell harness isolates the weather powers' casts with it
	Dictionary debug_clear_weather();
	// lane BACKLOG-8 r2 (proof captures): AIData UseFormations for the running game (test hooks only, never in a network game)
	Dictionary debug_set_use_formations(bool on);
	// read-only: the live objects of `player` [{ id, template, structure, commandcenter, x, y, z }] (between logic frames)
	Array get_player_objects(const String &player);
	Array get_logic_trees(double x, double y, double radius) const; // lane SPELL-3: the logic's map trees within radius (2D): x, y, id, toppled, value
	// the first object of a template name ("" never matches)
	int64_t find_object_by_template(const String &template_name) const;
	// lane BUILD-1: the first real model of the template's draw modules (its default model condition state), "" when it has none: what the placement ghost shows
	String get_template_model(const String &template_name) const;
	// Ping wheel (not retail, presentation only): the BeaconName of player `player_index`'s PlayerTemplate (RotWK's multiplayer beacon object, e.g. its
	// MultiplayerBeacon), "" when the player or the name is missing. Reads the start-time player template only, never the simulation's state
	String get_player_beacon_template(int64_t player_index) const;
	// lane PLAY-1: what PlayerTribute.apt reads of this game and its Send (GameClient/GUI/TributeInfo.h); valid while the world lives (null-safe without a game)
	TributeSource *tribute_source();

	// lane PLAY-1: { model, state, hidden: [sub object names], shown: [...], unread: [script lines] } of the template's BUILD_PLACEMENT_CURSOR look (the ghost);
	// lane PLAY-3: a castle also gives castle = true, base, castle_error and pieces: [{ template, model, hidden, x, y, z, angle }] (PlacementGhost::castleLookOf)
	Dictionary get_placement_ghost(const String &template_name) const;

	// ---- lane PROD-1: production by player command ----
	// makes an object of `template_name` for the player with index `player` (PlayerList order) at map position (x, y), ground height from the terrain;
	// returns its id, or -1 with the reason in the report's "errors". It exists at once (creation happens outside the command path: it is the map-less
	// stand-in for a placed starting building).
	int64_t create_object(const String &template_name, int64_t player, double x, double y, double angle);
	// player commands, appended to the lockstep command list; they run in the next logic frame (advance). `queue_unit` selects `producer` and issues
	// MSG_QUEUE_UNIT_CREATE (secondary = the second production queue), `cancel_unit` MSG_CANCEL_UNIT_CREATE, `set_rally_point` MSG_SET_RALLY_POINT.
	double get_ground_height(double x, double y) const;
	void queue_unit(int64_t player, int64_t producer, const String &template_name, bool secondary);
	void cancel_unit(int64_t player, int64_t producer, const String &template_name, bool all);
	void set_rally_point(int64_t player, int64_t producer, double x, double y);
	// lane UPGRADE-1: selects `producer` and issues MSG_QUEUE_UPGRADE with the upgrade's mask bit (the research runs in the producer's ProductionUpdate);
	// get_upgrades: the names of the object's upgrade mask and of its controlling player's completed upgrades { ok, object: [...], player: [...] }
	void queue_upgrade(int64_t player, int64_t producer, const String &upgrade_name);
	Dictionary get_upgrades(int64_t id) const;
	// { ok, money, queue: [{ template, percent, quantity_total, quantity_produced, cost }], rally: { set, x, y }, door_states, errors: command errors so far,
	//   unhandled: { message name: count } }
	Dictionary get_production(int64_t player, int64_t producer) const;
	// ---- lane SPELL-1: the spell book through the command path ----
	// MSG_PURCHASE_SCIENCE for `player` (by index) of the science named `science`; false when the name is not a Science
	bool purchase_science(int64_t player, const String &science);
	// MSG_DO_SPECIAL_POWER_AT_LOCATION of the SpecialPower named `power` from the player's spell book object at (x, y); false when there is no such power / book
	bool cast_special_power(int64_t player, const String &power, double x, double y);
	// { ok, rank, points, skill_points, book: id, sciences: [names], powers: [ { name, science, owned, ready, percent } ] } of the player's spell book
	Dictionary get_spellbook(int64_t player) const;
	// lane SPELL-3 (review r1): what a spell book power's module did so far (its class and counters), for the harness's effect checks
	Dictionary get_spell_power_effect(int64_t player, const String &power) const;
	// lane SPELL-4: the BuffNugget buffs shown now (GameClient/BuffDraw.h): { count, drawn (instance updates so far), errors, buffs: [ { object, type,
	// thing, model, opacity, state, instance } ] }
	Dictionary get_buff_state() const;
	// ---- lane SPELL-2: the spell book screens (GameClient/SpellBookUI.h) ----
	// the store (RotWK AptSpellStore): open for `player`; { open, set, points, pending: [science], buttons: [ { index, science, cost, image, label,
	// state: "purchased" | "pending" | "available" | "locked" } ] }; a click (OnBttnSpell), Reset, and the close that sends the pending purchases
	bool spell_store_open(int64_t player);
	Dictionary get_spell_store();
	bool spell_store_click(int64_t index);
	void spell_store_reset();
	int64_t spell_store_close();
	// the cast bar: { ok, book, targeting, target: index, buttons: [ { index, power, image, owned, usable, ready, percent, needs_position, radius,
	// radius_cursor } ] }; a press (casts a no-target power or starts the targeting), the targeting's ground click at SAGE (x, y), its cancel
	Dictionary get_spell_bar(int64_t player);
	bool spell_bar_press(int64_t player, int64_t index);
	bool spell_bar_click_world(int64_t player, double x, double y);
	void spell_bar_cancel();
	// a viewer harness only (hud_viewer's spellbook scenario, like --spawn): adds purchase points to the player's science record directly, outside the
	// command path (a real game earns them by rank); returns the new total
	int64_t debug_add_science_points(int64_t player, int64_t points);
	// lane HUD-5 (a test hook, logged): the object's experience tracker gains levels up to `rank` (ExperienceTracker::gainLevels, feedback on); its rank after
	int64_t debug_set_object_rank(int64_t id, int64_t rank);
	// lane PROJ-2 (scenario helper, outside the command path like create_object): the player's upgrade completes at once (Player::addUpgrade COMPLETE, RW 0x6AEE22:
	// the player's objects' upgrade modules run); false for an unknown player or upgrade name
	bool debug_grant_upgrade(int64_t player, const String &upgrade);
	// ---- lane STEALTH-1 (GodotDevice/GodotGameWorldStealth.cpp): scenario helpers of the stealth viewer ----
	// the player whose view the client shows (shroud, stealth look); a viewer helper outside the command path
	bool set_local_player(int64_t player);
	// StancesBehavior::setStance (RW 0x8620DD) on each object (0 .. 5: Uninitialized, Battle, Aggressive, HoldGround, Porcupine, HoldGroundMoving); outside the command path
	int64_t set_stance(const Array &ids, int64_t stance);
	// the logic's terrain tree list (RW TheTerrainLogic + 0x578: the map's TREE objects): up to `max` positions
	PackedVector3Array get_terrain_trees(int64_t max) const;
	// { ok, type (0 STEALTH, 1 CAMOUFLAGE, 2 none), detected, look (for the local player: InvisibilityManager::clientLook), stealthed_for_enemy (for `enemy`),
	// draw_opacity (the client's stealth opacity factor now) }
	Dictionary get_invisibility(int64_t id, int64_t enemy) const;
	// ---- lane STEALTH-2 (GodotGameWorldStealth.cpp) ----
	// MSG_DO_SPECIAL_POWER_AT_LOCATION of the object's power `power` at (x, y) (Move Unseen); false when the object or power does not exist
	bool cast_object_power_at(int64_t player, int64_t id, const String &power, double x, double y);
	// MSG_ONE_RING for the player's group `ids` (RW 0x7729A6)
	bool order_one_ring(int64_t player, const Array &ids);
	// viewer scenario hook (like debug_grant_upgrade: never in a started, network or threaded game): "unpause" resumes every paused special power of the object;
	// "object_upgrade" gives it the upgrade `name`; "ring_times" sets RingAnimTimeOn / Off 5 and RingDelayAfterRemoving 20 frames on its StealthUpdate's data (no
	// RotWK 2.01 object sets them: the ring is otherwise unreachable; viewer only)
	bool debug_stealth_scenario(int64_t id, const String &action, const String &name);
	// { ok, hidden (status HIDDEN), ring, disguise (the disguise template's name or ""), disguise_player, disguise_shown, stealthed (status) }
	Dictionary get_stealth_state(int64_t id) const;
	// ---- lane HERO-1 (GodotDevice/GodotGameWorldHeroes.cpp): heroes through the command path ----
	// the player's hero list (RW Player + 0x758): [ { index, template, dead, in_production, cost, frames, progress } ] (cost / frames through `producer`'s
	// ProductionModifiers, 0: none)
	Array get_heroes(int64_t player, int64_t producer) const;
	// MSG_QUEUE_UNIT_CREATE / MSG_CANCEL_UNIT_CREATE with the build-index flag for the record `index`, `producer` selected first
	void queue_hero(int64_t player, int64_t producer, int64_t index);
	void cancel_hero(int64_t player, int64_t producer, int64_t index);
	// the special power modules of an object: [ { name, ready, paused, percent, update_module_starts_attack } ]
	Array get_object_powers(int64_t id) const;
	// MSG_DO_SPECIAL_POWER (target 0) or MSG_DO_SPECIAL_POWER_AT_OBJECT of the object's power `power`; false when the object or power does not exist
	bool cast_object_power(int64_t player, int64_t id, const String &power, int64_t target);
	// lane HERO-1 part 2 (the hero viewer): every unpaused special power of the object is made ready now (its recharge since it was made skipped)
	int64_t ready_object_powers(int64_t id);
	// lane HERO-1 review (the viewer's effect label): MOUNTED, the attribute modifier lists, the experience and per special power its update's counters
	Dictionary get_ability_state(int64_t id) const;
	// lane HERO-1 review (the viewer): an UNRESISTABLE hit of `amount` without a source (a wound for a heal ability to show)
	void damage_object(int64_t id, double amount);
	// test / scenario helper (like create_object, outside the command path): `levels` levels of experience for the object (ExperienceTracker::gainExpForLevel)
	void gain_hero_levels(int64_t id, int64_t levels);
	// scenario helper: a killing UNRESISTABLE hit without a source through the object's body (a hero's RespawnBody makes its revive record)
	void kill_hero(int64_t id);
	// scenario helper: money for a player (Player::depositMoney)
	void give_money(int64_t player, int64_t amount);
	// lane HERO-2: the system Create-a-Heroes of the archives (data\\systemheroes\\*.cah): [{ name, class, subclass, unique_id, valid }]
	void set_hero_stats_profile(const std::string &profile) { m_heroStatsProfile = profile; }
	Array get_system_heroes() const;
	// lane HERO-2: a Create-a-Hero record for a game setup's slot, read at the setup (never during play): `name` is a system hero's name or the path of a
	// .cah file; { ok, record: PackedByteArray (the .cah form a slot's create_a_hero takes: load_map's slots, the new game message, the LAN lobby),
	// name, class, subclass, unique_id, errors }. The record must load with a matching checksum and pass CreateAHeroSystem::validateHero
	Dictionary get_create_a_hero_record(const String &name) const;
	// lane HERO-2: the installed Create-a-Hero of `player` (from the game setup; empty when none): { name, unique_id, class, subclass, surcharge, can_build }
	Dictionary get_create_a_hero(int64_t player) const;
	// ---- lane GARRISON-1 (GodotDevice/GodotGameWorldGarrison.cpp): garrisons through the command path ----
	// MSG_ENTER: `ids` selected (a select message of the first object's player), then the enter message with `container` (RW 0x77AC26)
	Dictionary order_garrison(const Array &ids, int64_t container);
	// MSG_EVACUATE with `container` selected (RW 0x77AD4E): its riders come out
	Dictionary order_evacuate(int64_t container);
	// { ok, garrisonable, count, max, riders [ids], members, members_hidden, entering, points_in_use, point_counts [3], horde_garrisoned } of a container
	Dictionary get_garrison(int64_t container) const;
	// lane GARRISON-2: a unit's turret (angle, machine state, shots); a weapon set flag on an object (a horde: and its members), the viewer's bow mode
	Dictionary get_turret(int64_t id) const;
	Dictionary debug_set_weapon_set_flag(int64_t id, const String &flag, bool on);
	// lane GARRISON-2: AIUpdateInterface::aiAttackObject(target, CMD_FROM_AI) for the viewer (a player's attack order meets the shroud, S-565)
	Dictionary debug_ai_attack(int64_t id, int64_t target);
	// lane ATTACK-1: the logic's auto-acquire switch (CombatState::setAutoAcquireEnabled, the tests' "only the order acts") for a viewer scene; refused like
	// debug_ai_attack
	Dictionary debug_set_auto_acquire(bool on);
	// lane ATTACK-1: Object::friend_onBuildComplete (RW 0x68D252) for an object create_object made, as a finished production runs it (a hero's
	// ToggleMountedSpecialAbilityUpdate TriggerInstantlyOnCreate mounts there, RW 0x8B11FE); refused like debug_ai_attack
	Dictionary debug_build_complete(int64_t id);
	Dictionary debug_ai_move(int64_t id, double x, double y);
	// lane COMBAT-3: MSG_HORDE_TOGGLE_FORMATION for a horde (the pikemen's porcupine formation) through the command path
	Dictionary order_toggle_formation(int64_t horde);
	// ---- lane CAH-1 (GodotDevice/GodotGameWorldCah.cpp): the Create-a-Hero builder's map mode ----
	// TheCreateAHeroSystem + 0x18C (the builder is up: RW 0x80ACE3 leaves the map mode upgrades alone); false without a world
	bool cah_builder(bool on);
	// the builder's hero (a .cah record) on the loaded map's preview object (RW 0x9C0E03 then RW 0x80ACE3 through CreateAHeroGame::applyBuilder): { ok, error };
	// refused in a started skirmish, a network game or under the logic worker, and while cah_builder is off
	Dictionary cah_preview_apply(int64_t object_id, const PackedByteArray &record);
	// the builder's map locations (AptMyHero + 0x164, RW 0x9C09FC: an object whose map name holds '_' is the location atoi(after the '_')), indexed by
	// MapLocation: [{ id (0: none), name, template, x, y, z, angle }]
	Array cah_preview_locations() const;
	// a subclass's ViewInfo (RW 0xD9EE08): { ok, near: [pitch, zoom, floor, dist, shift], far, close_up, portrait, normal_cam, camera_angle, map_location }
	Dictionary cah_view_info(int64_t cls, int64_t sub) const;
	static void bindCreateAHeroMethods();
	static void bindGarrisonMethods();
	static void bindHeroMethods();
	static void bindStealthMethods();
	// ---- lane SCRIPT-1 (GodotDevice/GodotGameWorldScripts.cpp): the map script engine ----
	// the script engine's client requests since the last call, applied to the script camera director: [ { frame, action, player, script, params: [ { type, int,
	// real, string } ], has_position, x, y, z } ]
	Array take_script_requests();
	// steps the script camera director by `delta_ms` of render time: { moving, has_target, x, y, z (SAGE), has_angle, angle, reset_view, letterbox,
	// input_disabled, caption (label), caption_text (the game text), caption_ms, fade }; `camera` gives the current look-at { x, y, z, angle } (the start of a move)
	Dictionary update_script_view(double delta_ms, const Dictionary &camera);
	// lane CAMPUI-1: InGameUI.ini's MilitaryCaption* rows with language.ini's caption fonts (MilitaryCaptionSettings): { ok, error, color (Color),
	// position (Vector2 fractions), centered, font, point_size, bold, title_font, title_point_size, title_bold, delay_ms }
	Dictionary get_military_caption_settings() const;
	// lane CAMPUI-1: TheMissionObjectiveTracker (GameLogic::missionObjectives): { has_list, screen_enabled, objectives: [{ label, text (the game text, UTF-8),
	// bonus, shown, completed }] in list order (main then bonus), held (read now; else the last held frame's copy) }
	Dictionary get_mission_objectives();
	// lane CAMPUI-1: { ok, image }: the ThingTemplate's ButtonImage (RW 0x73CFEF: ThingTemplate + 0x78; "" when it has none); ok false: no such template
	// (DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE then keeps the type's icon, RW 0x6D1305)
	Dictionary get_template_button_image(const String &name) const;
	// { loaded, sides: [ { name, scripts, groups } ], scripts, groups, updates, scripts_evaluated, scripts_fired, false_fired, actions_run, conditions_evaluated,
	// unported_conditions: { name: count }, unported_actions, client_requests, notes, end_requests: [ { frame, player, victory, action } ], setup (the sides' lists) }
	Dictionary get_script_report() const;
	// a map waypoint by name (SAGE coordinates): { ok, x, y, z }
	Dictionary get_waypoint(const String &name) const;
	// lane SCRIPT-2 test hooks (the start option test_hooks, never in a network game; between logic frames): a named unit placed at a trigger area's
	// centre or a waypoint, a named unit killed (a horde through its members, its indestructible flag lifted): the player's part of a campaign capture
	Dictionary debug_script_place(const String &name, const String &where);
	Dictionary debug_script_kill(const String &name);
	Dictionary debug_script_victory(); // lane CAMPSAVE-1: QUICKVICTORY for the local side (test hooks only)
	// lane SCRIPT-3 (test_hooks only, never in a network game): a named unit's / a script team's position, a unit placed at a point, a team killed
	Dictionary debug_script_unit(const String &name);
	Dictionary debug_script_action(const String &name, const Array &params); ///< lane BACKLOG3 (test hook)
	Dictionary debug_script_team(const String &name);
	Dictionary debug_script_place_at(const String &name, double x, double y);
	Dictionary debug_script_kill_team(const String &name);
	static void bindScriptMethods();
	// ---- lane CAMP-1 (GodotDevice/GodotGameWorldCampaign.cpp): the linear campaigns ----
	// TheLinearCampaignManager's campaigns: [ { name, display_label, intro_movie, carryover: [..], missions: [ { name, map, intro_movie, load_screen_image,
	// load_screen_music, fade_up_frames, delay_carryover: [..] } ] } ] (the world must be set up)
	Array get_campaigns() const;
	// loads a campaign mission as the game (load_map with the campaign rules): options { campaign, mission (index), difficulty (0 easy, 1 normal, 2 hard),
	// logic_thread, progress, test_hooks, record_memory (lane SAVE-2: the mission is recorded in memory so it can be saved) }; the report of start_new_game plus "start" { local_player, local_player_index, camera_start (the map's
	// InitialCameraPosition waypoint, else Player_1_Start), slot_players [], starting_objects [] } and "mission" (the mission's record)
	Dictionary start_campaign_mission(const Dictionary &options);
	// the running mission's end: { ended, victory, action, frame } (the script engine's VICTORY / QUICKVICTORY / DEFEAT, ScriptEngine::endRequests)
	Dictionary campaign_status() const;
	// the campaign progress sidecar of lanes CAMP-1 .. SAVE-4 (CampaignProgress's text): load -> { ok, campaign, mission, difficulty, victorious, error }.
	// Lane CAMPSAVE-1: no build writes it any more; scripts/campaign_flow.gd migrates an old one into the auto-save
	Dictionary campaign_progress_load(const String &path) const;
	Dictionary campaign_progress_step(const Dictionary &progress, const String &step) const; // lane CAMPSAVE-1
	// lane CAMP-1H: a movie by its Video title (TheVideoPlayer, GameClient/VideoPlayer.h: RW 0x490EE6's lookup): { ok, error, title, path, width, height,
	// frames, duration_ms, audio_events: [ "<file>_Music"?, "<file>"? ] (the ones the audio INIs define, RW 0x49112C), stops: [..] }. The Video INIs are read
	// on the first call; the movie files are the install's loose Data\Movies (ROTWK_INSTALL / user://install-paths.cfg)
	Dictionary get_movie(const String &title);
	Dictionary get_play_intro(); // lane CAMP-2: GameData PlayIntro (GlobalData + 0xAF2): { ok, play_intro, from_ini, error }
	static void bindCampaignMethods();
	// ---- lane SAVE-2 (GodotDevice/GodotGameWorldSave.cpp): the command-log save of a single player game (docs/SAVES.md) ----
	// save_game(path, { description, kind (1 Campaign, 2 Skirmish), map_display, date, time }): the running recorded skirmish as an .obfmesave file (the log so
	// far + its header, Common/System/SaveGame/SaveGameFile.h) -> { ok, error, frame, hash, bytes, commands }. Refused (named) without a recording, in a network
	// game or a replay, or when the recording is incomplete. Lane SAVE-4e: the snapshot at F too (snapshot_bytes; option "snapshot": false writes a
	// log-only save as the earlier alphas did); a snapshot the game refuses fails the save, named (review r1).
	Dictionary save_game(const String &path, const Dictionary &options);
	// lane CAMPSAVE-1: CampaignMenu.apt's state written as a campaign menu save (the auto-save, the menu's Save): options campaign, mission, difficulty,
	// victorious, ready, advanced, description, auto_save -> {ok, path, map, menu_mission} or {ok false, error}
	Dictionary save_campaign_menu(const String &path, const Dictionary &options);
	SaveGameCompatibility menuSaveVerdict(const SaveGameHeader &header) const; // lane CAMPSAVE-1: one verdict for the listing and check_save
	static std::string campaignMapPath(const std::string &map);   // lane CAMPSAVE-1: "MAP ANG Angmar" -> "maps/map ang angmar/map ang angmar.map"
	std::string campaignMapDisplay(const std::string &map) const; // its map cache display name (the map itself when the cache lacks it)
	// list_saves(dir): the saved games of a folder, newest first: the .obfmesave files read by their header (path, name, map, date, time, kind, frame,
	// build, loadable, refusal, cross_profile) and retail's own saves (*.BfME2Skirmish / *.BfME2Campaign) as { retail_import: true, loadable: false }
	Array list_saves(const String &directory) const;
	// prepare_save_load(path): reads and checks the save (the build, the data, the log) and prepares its game like prepare_replay -> { ok, errors, resolved,
	// map, local_slot, frame, description, cross_profile, note }; start_save_load(options) loads that game like start_new_game (options "record": the
	// recording continues the save's log there) and installs the catch-up; save_load_step(budget_ms) then runs it: { state ("catching_up", "ready",
	// "failed"), frame, start, target, percent, error, hashes_compared, elapsed_ms, mode, snapshot_ms }. At "ready" the game is at the saved frame and the
	// player's. Lane SAVE-4e: mode "snapshot" (prepare_save_load's and start_save_load's "mode" too): the save's snapshot is loaded by start_save_load (the
	// first step answers "ready"); "catch_up": a log-only save caught up as above. A refused snapshot fails the load, named, the game as it was.
	// check_save(path): what prepare_save_load would refuse, without touching the running game -> { ok, error }
	Dictionary check_save(const String &path) const;
	Dictionary prepare_save_load(const String &path);
	Dictionary start_save_load(const Dictionary &options);
	Dictionary save_load_step(int64_t budget_ms);
	// lane SAVE-4e review r1 (Sol): a load over the running game keeps that game's own save in memory first (keep_running_game: save_game's log and
	// snapshot, { ok, frame, hash, error }); when the chosen save is refused after the running game went, prepare_kept_load prepares the kept one like
	// prepare_save_load and the host starts and steps it as any load, so the player is back in the same game (its state hash and snapshot bytes);
	// drop_kept_game forgets it once the chosen save is in
	Dictionary keep_running_game();
	Dictionary prepare_kept_load();
	void drop_kept_game();
	static void bindSaveMethods();
	// ---- player commands (lane MOVE-1): everything goes through the lockstep command path (LiveGame::commands(), executed by the logic's command list row of the
	// next logic frame), never by touching the AI directly ----
	// Orders the objects `ids` to (x, y) in SAGE coordinates: a select message and a move message of the first object's controlling player. options: type: "move" (default),
	// "force", "attack" (the movement part), "formation" (with angle: radians, the units' final facing), "waypoint" (appends); player: int (overrides the issuing player).
	// { ok, error, player, objects, messages }
	Dictionary order_move(const Array &ids, double x, double y, const Dictionary &options);
	// A stop message for the same selection
	Dictionary order_stop(const Array &ids);

	// lane BUILD-2 (recorded wall / repair scenario, tests through the HUD use the messages): a wall hub made complete on the player's pad farthest from its first command
	// centre { ok, error, hub, x, y, cx, cy }; a structure started on a free pad of the player { ok, error, id, x, y }; MSG_WALL_HUB_CONSTRUCT_SPAN from the hub; an
	// UNRESISTABLE hit; MSG_DO_REPAIR for the selected dozers
	Dictionary build2_wall_hub(const String &player, const String &hub_template);
	Dictionary build2_construct_on_plot(const String &player, const String &template_name);
	bool build2_wall_span(int64_t hub, const String &cap_template, double x0, double y0, double x1, double y1, int64_t options);
	bool build2_damage(int64_t id, double amount);
	bool build2_repair(const Array &dozers, int64_t target);

	// Everything that is cheap to ask: { frame, alpha, state_hash, objects, drawables, dropped_frames, animated_drawables, last_advance_ms,
	// last_logic_ms, last_sync_ms, instances, static, dynamic }
	Dictionary get_stats() const;
	// lane SMOOTH-1: the cheap per-frame numbers of the frame-time benchmark (no state hash, unlike get_stats): { logic_ms, sync_ms, streak_ms, advance_ms,
	// logic_frames (run by the last advance), alpha, dyn_pose_ms, dyn_upload_ms, dyn_mapper_ms, static_mapper_ms }
	Dictionary get_frame_timings() const;
	// lane RENDERWAIT-1: the wait log of this game (LiveGame::logicWaitSites): [{site, count, total_ms, worst_ms}], worst total first; and its reset
	Array get_logic_wait_sites() const;
	void reset_logic_wait_sites();
	// lane RENDERWAIT-1: the live logic may be read now without waiting (the client window is open, or the worker is idle): per-frame scripts skip their
	// live reads otherwise; the client windows' counts and their cost to the logic (LiveGame::ClientWindowStats)
	bool is_logic_held() const;
	Dictionary get_client_window_stats() const;
	void beginClientWindow(); // SceneTree process_frame
	void endClientWindow();   // RenderingServer frame_pre_draw
	// lane SMOOTH-1: LiveGame::setRenderInterpolation (off: the stepped 5 Hz look, for comparisons)
	void set_render_interpolation(bool enabled);
	bool get_render_interpolation() const;
	// lane PERF-3: the pose culling of the live objects' instancer (W3DInstancer::set_pose_culling) on / off, for before / after measurements in one build;
	// it changes no pixel of a frame and nothing of the simulation. Default on
	void set_perf3_client(bool enabled);
	bool get_perf3_client() const { return m_perf3Client; }
	// SMOOTH-1 (S-810): the logic worker thread on / off (the single-thread fallback for debugging); also the default of the next load (option logic_thread)
	void set_logic_thread(bool enabled);
	bool get_logic_thread() const;
	// SMOOTH-1: [[frame, hash], ...] of every completed frame when the game was loaded with hash_every_frame
	Array get_frame_hashes() const;
	// lane SMOOTH-1: the drawable's render pose of an object (the interpolated transform the instancer draws) next to its logic position, SAGE coordinates:
	// { ok, x, y, z, angle, logic_x, logic_y, logic_z, logic_angle }
	Dictionary get_render_pose(int64_t id) const;
	// lane VISUAL-2 (scripts and proof captures): the live particle systems whose position is within `radius` of (x, y) and their particles
	Dictionary get_particles_near(double x, double y, double radius) const;
	// lane VISUAL-2 (scripts and proof captures): the trees that sway (by sway type), the sway's breeze version and the current SwayOffsets 1 .. 10
	Dictionary get_tree_sway() const;
	// the load report; RENDER-1 adds streak_errors (W3DStreakDraw textures that failed to load while drawing: also in errors, ok false)
	Dictionary get_report() const;
	// lane QA-1: the live game's report now (the load report is taken once): { ok, frame, errors, stops, stop_hits: { text: count } (GameLogic::noteStop),
	// unported_modules: { module: objects } }
	Dictionary get_live_report() const;
	// Lane ECON-1, what the economy overlay shows: { frame, players: [ { index, name, faction, playable, money, earned, spent, cp_used, cp_limit, cp_available } ],
	// income_events (the bounded income log), resource_grid: { width, height, blocked, claimants } }
	Dictionary get_economy() const;
	// lane FX-2: the live effects (GameClient/LiveFX through FXPlayer, a child node kept across loads): { ok, setup_errors, played: { site: count }, skipped,
	// missing_fx_lists, unresolved_particle_systems (lane FX-3: retail's NULL templates, not played), fire_fx_at_bone, fire_fx_on_object, attached_live, attached_created, attached_destroyed, bone_misses, sounds,
	// logic_events, stops, particles: FXPlayer.get_stats() }
	Dictionary get_fx_report() const;
	// the FXPlayer that draws the live effects (null before setup() succeeded)
	Node3D *get_fx_player() const;
	// lane OPTS-2: the texture reduction of the static detail level for the W3D textures loaded from now on (W3DMaterialFactory::Set_Texture_Reduction)
	static void set_texture_reduction(int64_t reduction);
	static int64_t get_texture_reduction();
	// a select message and an attack-object message (MSG_DO_ATTACK_OBJECT) for the objects `ids` on `target`, through the lockstep command path. { ok, error, player, objects }
	Dictionary order_attack(const Array &ids, int64_t target);

	void _process(double delta) override;
	void _notification(int what); // lane RENDER-3: installs the gamma-space transparent pass (S-831)

	// ---- lane MP-1: a LAN game (GameNetwork/LANLobby.h, NetGameSession.h). net_host / net_join open the lobby; net_poll returns {state: "lobby" | "started" |
	// "error", message (the NewGameMessage Dictionary every peer starts, for prepare_new_game), local_slot, error}; prepare_new_game then takes the lobby's local
	// slot; after start_new_game, net_begin({record: path, script: bool, early_orders: bool (lane NETLAG-2, default true)}) installs the lockstep driver; advance() then services the network and runs a frame
	// only when every player's commands for it are in. net_status: frame, CRC checks, desync reports, waiting slots, stalls, errors.
	Dictionary net_host(int64_t port, const Dictionary &message, int64_t run_ahead, int64_t crc_interval);
	Dictionary net_join(const String &address, const String &name);
	Dictionary net_poll();
	// lane ONLINE-2: an online game: the START JSON the lobby service sent (GameNetwork/OnlineStart.h); {ok, slot, relay | error}; then net_poll / net_begin as
	// for a LAN game. net_profile_digest: this install's profile identity MD5 (the lobby compares the players')
	Dictionary net_online(const String &start_json);
	String net_profile_digest() const;
	Dictionary net_begin(const Dictionary &options);
	Dictionary net_status() const;
	// the local player leaves the game (PLAYERLEAVE after the frames it announced), the recording is closed; waits up to 2 s for the acks
	void net_finish();
	// lane MP-3: the per-frame census of a net_begin({"census": true}) game as CSV (frame, sim_us, battalions, troops, objects); {ok, rows | error}
	Dictionary net_write_census(const String &path) const;
	// lane MP-2: the disconnect screen's buttons (AptMenuPlayer.take_disconnect_actions): Kick of a row (a vote for its slot), Quit (votes, then the local
	// player leaves: net_status().disconnect.quit_requested)
	void net_disconnect_kick(int64_t row);
	void net_disconnect_quit();
	// lane ONLINE-3: the multiplayer pause (OpenBFME addition, S-3901: any player pauses every peer before one frame, any player resumes); net_status().pause
	// says who paused. A rejoin START (net_online with the lobby's /v1/games/<id>/rejoin START) makes net_begin rejoin the running game: _process runs the
	// catch-up (the relay's log, no rendering of the skipped frames) until net_status().rejoin.state is "live" (or "failed", with the reason)
	void net_pause();
	void net_resume();
	// lane ONLINE-3: a spectator START (net_online with the lobby's /v1/games/<id>/spectate START: {ok, spectator, delay}) makes net_begin watch the game:
	// no slot, the ReplayObserver side is the local player (the map revealed, RW 0x62F91A), _process catches up from the relay's log and then follows
	// it (NetGameSession::spectateStep; nothing is sent, the local input is ignored); net_status().spectate: state ("binding" / "catching_up" / "live" /
	// "ended" / "failed"), frame, served, crcs_compared, crc_mismatches, failure, observed. net_observe_next(forward) is retail's observeNextPlayer
	// (RW 0x6A8D2B: the next human player's view, then back to every player's); net_observed() names the observed player and its resources
	Dictionary net_observe_next(bool forward);
	Dictionary net_observed() const;
	// RENDERWAIT-1: net_status()'s per-frame copy of net_observed(): read fresh only where the logic is held, else the last one read for the same player
	Dictionary net_observed_unwaiting() const;
	Dictionary caster_status() const;
	bool caster_observe(int64_t player, bool team = false);
	bool caster_playback(bool paused, double speed);
	Dictionary caster_step();
	Dictionary caster_transport_status() const;
	Dictionary caster_restart_live(const Dictionary &options = Dictionary());
	Dictionary caster_seek_step(int64_t target, int64_t budget_ms = 20);
	Array caster_events(int64_t from_frame = 0) const;
	Dictionary caster_metrics(int64_t from_frame = 0) const;
	Dictionary replay_seek_step(int64_t target, int64_t budget_ms = 20);
	// lane MP-2: replays. start_new_game({record: path}) records a skirmish (Common/Recorder.h: every frame's commands and hash); a LAN game records with
	// net_begin({record}). prepare_replay(path) reads a recording and prepares its game (the profile identity must match; { ok, resolved, map, local_slot,
	// final_frame, errors } like prepare_new_game); start_replay(options) loads it like start_new_game and plays the recording back (the local input is
	// ignored); replay_status: { active, path, finished, frame, final_frame, hashes_compared, mismatches,
	// first_mismatch_frame, lost }. recording_status: { active, path }. list_replays(dir): the replay files of a directory with their header (map, players,
	// frames, date)
	// lane MP-2: the LAN lobby LanLobby.apt drives (GameNetwork/LANAPI.h). lan_open({ name, port_base, ports, broadcast, targets: ["a.b.c.d"], run_ahead,
	// crc_interval }) binds the lobby and game sockets: { ok, error, lobby_port, game_port }; AptMenuPlayer.set_lan(world) hands it to the shell. lan_begin, once
	// every player has the start (the shell request "LanGameStart"), takes the lobby's start and game socket for the network game: { ok, error, message,
	// local_slot, run_ahead, crc_interval } like net_poll's "started", then prepare_new_game / start_new_game / net_begin as for net_host. The lobby keeps
	// answering late starts while the game runs; lan_close drops it. lan_status: { open, in_game, host, slot, started, games, players, humans, accepted (the
	// joiners'), accepted_me, map, errors, log }
	Dictionary lan_open(const Dictionary &options);
	void lan_close();
	Dictionary lan_begin();
	// lane HERO-2: the local player's Create-a-Hero for its slot (get_create_a_hero_record's record; empty: none), sent to the host with the whole record
	// (LANAPI::requestSlotCreateAHero); { ok, error }
	Dictionary lan_set_create_a_hero(const PackedByteArray &record);
	Dictionary lan_status() const;
	LANAPI *lan() const { return m_lan.get(); }
	// lane ONLINE-4: the faction and colour counts the online lobby client checks the lobby's game views against (false before setup())
	bool onlineLimits(int &factions, int &colors) const;
	Dictionary prepare_replay(const String &path);
	Dictionary start_replay(const Dictionary &options);
	Dictionary replay_status() const;
	Dictionary recording_status() const;
	Array list_replays(const String &directory) const;
	ProfileIdentity net_profile() const;
	// lane UBER-1: the simulation profile this player runs (the OpenBFME option "Uber performance mode", docs/UBER.md): "faithful" or "uber". It decides the
	// profile identity (LAN, replays) and every game this world starts must carry it (prepare_new_game refuses another). { ok, error }: refused while a
	// LAN game (not the lobby list) is joined or hosted
	Dictionary set_simulation_profile(const String &name);
	// lane UBER-1 (the game's Uber gate, scripts/uber1_player.gd): { troops, battalions, objects } of GameClient/FrameCensus.h (living INFANTRY / CAVALRY /
	// MONSTER: horde members one by one; living hordes; every object) and the playable extent { extent_x, extent_y } (TerrainLogic::getExtent(0)); {} without
	// a game. Diagnostics only: reads the logic between frames
	Dictionary get_census() const;
	String simulation_profile() const;
	// lane UBERJOIN-1 (GameClient/GUI/ProfileSwitch.h): the lobby screens switch the profile for ONE game (a game list's Yes / No box: "Switch to Uber mode
	// and join?"). The player's own setting (set_simulation_profile, the option) is kept in m_choice.own and never written; restore_simulation_profile
	// (bound: scripts/game.gd calls it when a game has ended, the screens when the room is left) puts it back. Refused inside a LAN game
	bool switch_profile_for_game(SimulationProfile profile, std::string *why);
	void restore_simulation_profile(); // false from the ProfileChoice (a LAN room held) leaves the switch standing; see profile_switched()
	// the game is over / the room left for good: leaves a LAN room that is still held (it is kept after a game so the players return to it), then restores
	void leave_lan_room_and_restore_profile();
	Dictionary switch_simulation_profile_for_game(const String &name); // the screens' switch as a scripted call (the headless lifecycle test): { ok, error }
	bool profile_switched() const { return m_choice.switched; }
	SimulationProfile current_profile() const { return m_choice.current; }
	std::string profile_digest_for(SimulationProfile profile) const { return profileIdentityFor(profile).digest; }

	// lane HUD-1 (C++ only, not bound): what the in-game HUD node needs of the game; null until setup() / load_map() ran
	LiveGame *hud_game() const { return m_game.get(); }
	LiveFX *live_fx() const { return m_liveFX.get(); } // lane FX-4: the HUD takes the camera shakes (LiveFX::takeViewShakes)
	// AUDIO-2: connects the loaded game to the installed audio manager (a booted GameAudio): object / drawable owned sounds follow their owners, the
	// player filter knows the local player, the objects' ambient sounds run. { ok, error }
	Dictionary attach_audio();
	// { attached, ambient_started, ambient_stopped, ambient_playing, ambient_out_of_range, ambient_unknown_event, battle_ambient_templates, calls_without_audio,
	//   unit_voices_without_handler, stops }
	Dictionary get_audio_report() const;
	RetailObjectWorld *hud_world() const { return m_world.get(); }

protected:
	static void _bind_methods();

private:
	mutable Dictionary m_observedRead; ///< net_observed_unwaiting(): the last net_observed() read where the logic was held
	Dictionary load_internal(const LiveGame::Options &lo, const String &map_name, const Dictionary &options, const Callable &progress, bool withTerrain);
	std::unique_ptr<NewGameStart> m_start;           ///< the prepared new game (LiveGame::Options::start points into it)
	std::vector<MapCacheEntry> m_mapCache;           ///< maps\\mapcache.ini, read by setup()
	struct EntryView
	{
		int64_t instance = -1;
		std::string model;              ///< the lower-case model the instance shows ("" none)
		std::string rawModel, rawLowered; ///< lane PERF-1: the last model name as the draw gave it and its lower-case form (lowered once per change)
		const void *swapSource = nullptr; ///< community PR #37 (S-5015): the swap list swapSuffix was made from (a model condition state's, stable)
		std::string swapSuffix;            ///< its W3DInstancer::Texture_Swap_Suffix, made once per change (not every frame)
		std::vector<std::string> hidden;
		std::uint32_t hiddenGeneration = 0; ///< lane PERF-3: the draw's hidden set generation and model `hidden` was made from (names made only when they change)
		const void *hiddenModel = nullptr;
		const void *streakSource = nullptr;  ///< lane PERF-3: the entry's module data the streak cast below was made from
		const W3DStreakDrawModuleData *streak = nullptr; ///< lane PERF-3: that data as a W3DStreakDraw's (refresh_streaks), null for other modules
		bool streakKnown = false;
		bool posed = false;
		float offsetZ = 0.0f;           ///< RENDER-2: the construction offset the instance's transform carries (DrawEntry::constructionOffsetZ)
		float opacity = 1.0f;           ///< PROJ-2: the drawable's fade the instance shows (Drawable::drawOpacity)
		unsigned tireChanges = 0xFFFFFFFFu; ///< lane COMBAT-4: DrawEntry::tireChanges the instance's bone spins show
		int64_t tireInstance = -1;          ///< the instance they were handed to
		bool fogLook = false;               ///< lane VISUAL-2: the instance is drawn with the fogged light (a last-seen object in the fog)
		int64_t fogLookInstance = -1;       ///< the instance it was handed to
		std::uint32_t colorsRevision = 0;   ///< lane CAH-2: Drawable::customColorRevision the model choice saw
		// lane HCKEY-1: a static entry's model key (lowered name + floor swap suffix + palette) and the inputs it was made from, rebuilt only
		// when one of them changes (community PR #60 made it every frame for every coloured static drawable)
		bool staticKeyValid = false;
		std::string staticKeySource;       ///< DrawEntry::staticModel the key was made from
		const void *staticKeyData = nullptr; ///< DrawEntry::data (the floor swaps' module data)
		int staticKeyKind = -1, staticKeyWeather = -1, staticKeyCustomKind = -1;
		bool staticKeyHouse = false;        ///< m_houseColors when it was made
		std::uint32_t staticKeyOwner = 0, staticKeyCustom[3] = { 0u, 0u, 0u };
		std::string staticKey;             ///< the key
		HouseColorParams staticKeyPalette; ///< and what it was made from (used when a new model is built)
		bool staticKeyColored = false;
		std::vector<std::pair<std::string, std::string>> staticKeySwaps;
	};
	struct DrawableView
	{
		bool valid = false;
		unsigned seenChange = 0xFFFFFFFFu;
		Transform3D lastXf;
		bool haveXf = false;
		bool fogHidden = false; ///< VIS-1: the local player cannot see the object (RW 0xB4E890 SHROUDED)
		std::vector<EntryView> entries;
	};

	Array m_commandErrors;
	void clear_scene();
	void build_static_layer(const Ref<RetailFileSystem> &fs, Dictionary &report, Array &errors, Array &stops);
	void refresh_views(double deltaMs);
	// VIS-1: the local player's shroud as a cell texture on the terrain materials (S-567)
	void refresh_shroud(bool force);
	Ref<godot::ImageTexture> m_shroudTexture;
	bool m_shroudDrawn = true;
	// lane BIGBATTLE-1: the shroud parameters the terrain's materials were last given (refresh_shroud visits them only when one changes)
	bool m_shroudParamsSet = false;
	bool m_shroudParamEnabled = false;
	Vector2 m_shroudParamOrigin, m_shroudParamExtent;
	Color m_shroudParamColor;
	void refresh_streaks(bool record = true); ///< RENDER-1: W3DStreakDraw trails of the live drawables (S-391)
	void remove_view(uint32_t drawableId);

	Ref<RetailFileSystem> m_fs;
	std::unique_ptr<RetailObjectWorld> m_world;
	// lane PLAY-3: the castle layouts the placement ghost reads (its own store over the archives: the client never touches the logic's)
	mutable std::unique_ptr<class CastleTemplateStore> m_ghostCastles;
	std::unique_ptr<VideoPlayer> m_videos; // lane CAMP-1H: TheVideoPlayer's Video blocks (get_movie)
	std::string m_videosError;             // lane CAMP-1H: why the Video INIs did not load
	std::unique_ptr<GameLogicSettings> m_settings;   ///< GameData / AIData / MultiplayerSettings (and the lobby colours), read once by setup()
	std::unique_ptr<MapObjectOptions> m_options;
	std::unique_ptr<W3DFileSource> m_source;
	std::unique_ptr<WW3DAssetManager> m_assets;
	mutable UnsignedInt m_netHashFrame = 0;   ///< lane RENDERWAIT-1: net_status's last state hash (computed where the logic is held) and its frame
	mutable std::uint32_t m_netHash = 0;
	std::string m_heroStatsProfile;
	std::unique_ptr<LiveGame> m_game;
	// Per loaded game/map; cleared alongside m_game, including data-source resets.
	mutable bool m_cahCameraOffsetValid = false;
	mutable Vector3 m_cahCameraOffset;
	std::unique_ptr<TributeSource> m_tribute; ///< lane PLAY-1 (tribute_source)
	std::unique_ptr<SpellStoreModel> m_spellStore;     // lane SPELL-2
	void waitSpellIdle(const char *file = __builtin_FILE(), int line = __builtin_LINE());
	std::unique_ptr<InGameSpellBookModel> m_spellBar;  // lane SPELL-2
	// lane MP-1 (after m_game: the session goes first; every m_game.reset() drops it before)
	std::unique_ptr<LANAPI> m_lan; // lane MP-2
	ProfileChoice m_choice; // lane UBER-1 / UBERJOIN-1: the player's profile (own), the one the next game runs (current) and a lobby's switch for one game
	std::unique_ptr<UDP> m_netSocket;
	std::unique_ptr<LANLobbyHost> m_netHost;
	std::unique_ptr<LANLobbyClient> m_netClient;
	LobbyStart m_netStart;
	bool m_netStarted = false;
	std::unique_ptr<NetGameSession> m_netSession;
	// lane MP-2: the recording of a local game (start_new_game option "record") and the playback of a replay (start_replay)
	NewGameMessage m_startMessage;                    ///< the message prepare_new_game resolved (a replay's header holds it, unresolved)
	Dictionary gameRecordBase(GameLogic &logic, const std::vector<int> &slotPlayers) const; // lane ONLINEHUB-1 (clear_game_data's "record")
	NewGameMessage m_recordMessage;                   ///< lane ONLINEHUB-1: the resolved message of the game (the game record's players, GameClient/GameRecords.h)
	int m_recordLocalSlot = -1;
	std::unique_ptr<ReplayWriter> m_localWriter;
	std::unique_ptr<LockstepDriver> m_localDriver;
	std::unique_ptr<ReplayFile> m_replayFile;
	std::unique_ptr<ReplayFile> m_pendingReplay; ///< prepare_replay's, until start_replay
	std::unique_ptr<ReplayPlayback> m_replayPlayback;
	std::string m_replayPath, m_recordPath;
	// lane SAVE-2: the save being loaded (prepare_save_load / start_save_load / save_load_step)
	std::unique_ptr<SaveGameFile> m_pendingSave;
	std::unique_ptr<SaveGameFile> m_keptSave; ///< lane SAVE-4e review r1: keep_running_game
	bool buildSave(const Dictionary &options, SaveGameFile &f, UnsignedInt &frame, std::uint32_t &hash, std::string &error); ///< save_game's file
	Dictionary prepareSaveLoad(std::unique_ptr<SaveGameFile> file); ///< prepare_save_load's checks and preparation of a read file
	std::unique_ptr<CommandLogLoad> m_saveLoad;
	std::string m_saveRecordPath; ///< "" with m_saveRecordContinues: in memory (a campaign mission)
	bool m_saveRecordContinues = false;
	// lane SAVE-2: the running campaign mission's start (kept for its saves; empty campaign: not a campaign mission), and the one a pending load starts
	struct CampaignStart
	{
		std::string campaign, map;
		int mission = 0, difficulty = 1;
	};
	CampaignStart m_campaignStart, m_pendingCampaign;
	// the identity a game of `profile` runs under (net_profile is the player's option's; a campaign mission always runs Faithful)
	ProfileIdentity profileIdentityFor(SimulationProfile profile) const;
	void releaseLocalDrivers();
	bool m_netLoaded = false, m_netScript = false;
	bool m_netRejoining = false; ///< lane ONLINE-3: the rejoin's catch-up runs (NetGameSession::rejoinStep)
	bool m_netSpectating = false; ///< lane ONLINE-3: a spectator START: NetGameSession::spectateStep runs the game, no Network
	UnsignedInt m_netScriptFrame = 0xFFFFFFFFu;
	int m_netPlayer = -1, m_netStartPos = -1;
	unsigned long long m_netScripted = 0;
	std::unique_ptr<LiveGameAudio> m_audio; ///< AUDIO-2: reset before m_game everywhere
	// lane END-1
	std::unique_ptr<EndGameController> m_endGame;
	std::unique_ptr<ScriptCameraDirector> m_scriptView;     // lane SCRIPT-1
	std::vector<ScriptClientRequest> m_pendingScriptRequests; // lane SCRIPT-1: taken from the game, not applied to the director yet
	Dictionary m_objectivesView; // lane CAMPUI-1: the tracker as last read in a held frame (get_mission_objectives)
	std::vector<ScriptClientRequest> m_pendingScriptAudio;    // lane RENDERWAIT-1 r2: taken, their audio not applied yet (waits for a held logic)
	unsigned long long m_scriptAudioApplied = 0; // RENDERWAIT-1 r3: requests handed to the audio side (test counter)
	void applyPendingScriptAudio();
public:
	void debug_defer_script_audio(const String &sound); // RENDERWAIT-1 r3 (tests)
	int64_t get_pending_script_audio() const;
	int64_t get_script_audio_applied() const;
private:
	UnsignedInt m_endGameFrame = 0;
	std::vector<EndGameRequest> m_endGameRequests;
	std::vector<std::string> m_endGameErrors;
	ScoreScreenData m_scoreScreen;
	bool m_testHooks = false; ///< start option test_hooks: the debug_* end-game hooks may act on a started (non-network) game
	void startEndGame();
	void updateEndGame();
	std::unique_ptr<MapObjectRuntime> m_clientRuntime;
	// lane SPELL-3: the client-only trees that can fall (the logic's TerrainTrees events: Devastation): the animator, per tree its static instance and its
	// SAGE placement, and how many logic tree events were applied
	struct ToppleTreeView
	{
		int64_t instance = -1;
		float basis[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		Coord3D position{};
		float scale = 1.0f;
	};
	std::unique_ptr<TreeTopple> m_treeTopple;
	std::vector<ToppleTreeView> m_treeViews; ///< index = the animator's tree index
	// lane TREES1: the client-only shrubs and props a construction (TheTerrainVisual slot 0x48) or a felled tree (slot 0x4C, props only) removes
	struct FootprintView
	{
		int64_t instance = -1;
		Coord3D position{};
		float scale = 1.0f;
		bool prop = false; ///< MAPOBJ_CLIENT_PROP (the prop buffer), else a shrub (the shrub buffer)
		bool removed = false;
	};
	std::vector<FootprintView> m_footprintViews;
	void clearFootprint(const Coord3D &pos, float angle, const std::vector<ObjectGeometry::Shape> &shapes, bool propsOnly);
	size_t m_treeEventsSeen = 0;
	void heldClientWork(); // lane RENDERWAIT-1: the render side's reads of the live logic, where it is held
	void updateTreeTopple(double dt);
	std::unique_ptr<W3DVegetationSway> m_vegSway; ///< lane VISUAL-2: the W3DTreeDraw trees' sway (null: the map has none)
	std::array<int64_t, 11> m_swayTypeCounts{}; ///< lane VISUAL-2: the trees given each sway type (0: TaintedTree)
	void updateTreeSway(double dt);
	uint64_t m_staticId = 0, m_dynamicId = 0;
	std::vector<DrawableView> m_views;       ///< index = drawable id
	std::map<std::string, int64_t> m_dynamicModels, m_staticModels;
	Dictionary m_report;
	bool m_autoAdvance = false;
	bool m_paused = false;
	bool m_animations = true;
	bool m_houseColors = true;
	bool m_perf3Client = true; // lane PERF-3: set_perf3_client
	bool m_textureAnimation = true;
	double m_timeScale = 1.0;
	double m_textureClock = 0.0;
	double m_stealthClockMs = 0.0; ///< lane STEALTH-1: the client clock of the friend's invisibility opacity pulse
	double m_lastAdvanceMs = 0.0, m_lastLogicMs = 0.0, m_lastSyncMs = 0.0, m_lastStreakMs = 0.0;
	// lane PERF-1: what the last advance spent where, for the hitch report of the benchmarks (get_frame_timings): the shroud texture, the views, the effect
	// player, and the first-use work (models the dynamic instancer built, streak materials created) with its time
	double m_lastShroudMs = 0.0, m_lastViewsMs = 0.0, m_lastFxMs = 0.0, m_lastNewModelMs = 0.0;
	int m_lastNewModels = 0, m_lastNewStreakMaterials = 0;
	double m_mainCpuAtAdvance = -1.0, m_lastMainCpuMs = 0.0; ///< lane PERF-1: the main thread's CPU time between two advances (one render frame of work)
	int m_lastLogicFrames = 0;
	bool m_renderInterpolation = true; ///< SMOOTH-1: handed to every LiveGame this world makes
	bool m_logicThreadDefault = true;  ///< SMOOTH-1: the logic worker of the next load (option logic_thread)
	double m_fxPendingDt = 0.0; ///< SMOOTH-1: render time the effect player has not stepped yet (the worker was busy)
	unsigned long long m_shroudVersionShown = ~0ull; ///< SMOOTH-1: the shroud view version of the shroud texture
	const void *m_shroudViewShown = nullptr;
	size_t m_animatedDrawables = 0;
	// RENDER-1: W3DStreakDraw (arrows): one trail per drawable entry, drawn into one ImmediateMesh (a surface per texture / blend)
	struct StreakView
	{
		W3DStreakTrail trail;
		const W3DStreakDrawModuleData *data = nullptr;
		bool seen = false;
		float opacity = 1.0f; ///< PROJ-2: the drawable's opacity (RW 0x672FC4, Drawable::drawOpacity)
		bool hidden = false;  ///< lane VISUAL-2 r2: the drawable is not drawn (Drawable::presentedLook): no point recorded, no ribbon
	};
	W3DDrawPoseView m_viewPose;               ///< lane PERF-3: refresh_views reads the pose request by reference (W3DScriptedModelDraw::poseView)
	int m_streakWeather = 0;                  ///< lane PERF-1: the map's WeatherType for the streak textures (0 NORMAL, 1 SNOWY), set by load_map
	std::map<uint64_t, StreakView> m_streaks;  ///< key = drawable id << 8 | entry index
	std::unique_ptr<W3DMaterialFactory> m_streakTextures;
	std::map<std::string, Ref<ShaderMaterial>> m_streakMaterials; ///< key = additive flag + texture name
	Ref<Shader> m_streakShader[2];             ///< [additive]
	MeshInstance3D *m_streakNode = nullptr;
	Ref<ImmediateMesh> m_streakMesh;
	W3DStreakDiagnostics m_streakDiag;         ///< texture failures (skipped materials + messages), reset together per scene
	void reset_streaks();                      ///< drops trails, materials, errors and the texture factory (it references m_fs)
	// lane FX-2
	FXPlayer *m_fxPlayer = nullptr;            ///< child node, kept by clear_scene
	std::unique_ptr<LiveFX> m_liveFX;          ///< destroyed before m_game (it is the logic's FX sink and the drawables' FX host)
	// lane SPELL-4: the BuffNugget buffs (GameClient/BuffDraw.h) as instances of the live objects' instancer, by TBuff serial
	std::map<uint64_t, int64_t> m_buffInstances;
	unsigned long long m_buffsDrawn = 0;
	void syncBuffs(double dt);
	Array m_fxSetupErrors;
};

} // namespace godot
