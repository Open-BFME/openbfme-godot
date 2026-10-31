// OpenBFME. GPL-3.0.
//
// LivingWorldGame: the War of the Ring strategic game as Godot data (lane WOTR-1).  It owns a LivingWorld::LivingWorldLogic over the Living World
// stores the GameWorld's RetailObjectWorld parsed, and marshals the static map data (regions, connections, spots, scenarios, icons), the live
// state (turn, phase, players, regions, armies, battles, events) and the player's orders between GDScript and the logic.  It decides nothing: every
// rule is the logic's (GameLogic/LivingWorld/LivingWorldLogic.h).

#pragma once

#include "GameLogic/GameMessage.h"

#include "GameLogic/LivingWorld/BattleResultCapture.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameNetwork/StrategicSession.h"
#include "GameNetwork/Transport.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <memory>

namespace godot
{

class GameWorld;

class LivingWorldGame : public RefCounted
{
	GDCLASS(LivingWorldGame, RefCounted)

public:
	// The stores of the world's subsystem load: { ok, error, scenarios, player_templates, regions, map_info, region_object, icons, buildings }
	Dictionary setup(Object *game_world);
	// { campaign, seed, slots: [{ name, faction, human, ai, team, color, start_region }] } -> { ok, error }
	Dictionary start(const Dictionary &setup);
	void update();
	// lane WOTR2 milestone 3: the strategic game's save (LivingWorldLogic::saveState) -> { ok, error, state: PackedByteArray, state_hash, campaign, turn }
	Dictionary save_state() const;
	// a saved game in place of the running one (setup() first: the save's data parts come from the world, as start's do); the loaded game's state hash
	// must be `state_hash` (the save header's), then the generator starts again from 0 as retail's (LivingWorldLogic::reseedAfterLoad) -> { ok,
	// error }; on a refusal the running game stays
	// `players`: the player count the host's session holds (its lobby slots), checked before the swap
	Dictionary load_state(const PackedByteArray &state, int64_t state_hash, int64_t players, bool battle = false); // battle: a save inside a fought battle (WOTR2 milestone 6)
	// review r2: the same checks without the swap (a load from a running RTS game asks first, so a refused save leaves that game alone)
	Dictionary check_state(const PackedByteArray &state, int64_t state_hash, int64_t players, bool battle = false) const;
	Dictionary get_strategic_state() const;
	// lane WOTR-2: a string field of a thing template ("ButtonImage": ThingTemplate::getButtonImage, the strategic HUD's army portraits WB 0x1654B30;
	// "DisplayName": a hero army's name); "" when the template or the field is absent
	String template_field(Object *game_world, const String &template_name, const String &field) const;
	Dictionary army_panel(Object *game_world, int64_t army_id) const; // lane WOTR-2: AptStrategicHUD::ArmyPanel's data
	// lane WOTR-2: the W3D shaders' light set from LivingWorldMapInfo (the strategic map and the setup preview; a battle's map replaces it): { ok, lights }
	Dictionary apply_map_lighting() const;
	int64_t get_state_hash() const;

	Dictionary order_move(int64_t player, int64_t army, int64_t region);
	Dictionary cancel_move(int64_t player, int64_t army);
	bool can_swap_army_members(int64_t a, const Array &keysA, int64_t b, const Array &keysB) const;
	Dictionary swap_army_members(int64_t a, const Array &keysA, int64_t b, const Array &keysB);
	Dictionary cancel_army_member_move(int64_t army, int64_t key);
	Dictionary order_build(int64_t player, int64_t region, const String &building, int64_t plot = -1);
	Dictionary disband_army_member(int64_t player, int64_t army, int64_t key);
	bool can_disband_army_member(int64_t army, int64_t key) const;
	// WOTR2 milestone 5: MSG_LW_MARK_UNIT_FOR_UPGRADE 0x6BF / MSG_LW_UNMARK_UNIT_FOR_UPGRADE 0x6C0 (the UpgradeUnitButton, spec H 3.2 / K)
	Dictionary order_upgrade_army_member(int64_t player, int64_t army, int64_t key);
	Dictionary cancel_upgrade_army_member(int64_t player, int64_t army, int64_t key);
	bool can_upgrade_army_member(int64_t army, int64_t key) const;
	bool can_move_army_members_no_room(int64_t armyA, const Array &keysA, int64_t armyB) const; // RW 0x6B7938 (the unit swapper's gate)
	Dictionary region_stats(int64_t player, int64_t region) const;
	// WOTR2 milestone 5: the notices the logic recorded since the last call ({kind, player, hero, defeated, repelled: [{hero, region}]}), then forgotten
	Array take_notices();
	const LivingWorld::LivingWorldLogic *logic() const { return m_logic.get(); } // WOTR2 milestone 5: the score screen's source
	// WOTR2 milestone 5: the map hover help of an army (ArmyHelp RW 0x9735D3): {title (the summary's name label), heroes: [labels], lines: [{label, count}]};
	// {} for no such army or no ArmySummaryDescription data
	Dictionary army_help(Object *game_world, int64_t army) const;
	// the map hover help of a building template (BuildingHelp RW 0x9736A0): {title, text: labels, farm: the farm line's amount or -1 (RW 0x6E0FC4)}
	Dictionary building_help(const String &building) const; // the palantir's region stats tray (LivingWorldLogic::regionStats)
	bool can_move_army_member(int64_t army, int64_t key, int64_t target) const;
	Dictionary order_recruit(int64_t player, int64_t region, int64_t building_slot, int64_t army_index);
	Dictionary end_turn(int64_t player);
	// WOTR2 milestone 6: the retreat phase (MSG_LW_DONE_PHASE 0x6A6 in phase 4; the move order's checks for the cursor)
	Dictionary end_phase(int64_t player);
	bool can_end_phase(int64_t player) const;
	bool can_order_move(int64_t player, int64_t army, int64_t region) const;
	Array buildable_in(int64_t player, int64_t region) const;
	// the SpawnArmy entries a completed building in the region offers: [{ slot, building, armies: [{ index, player_army, hero, cost, cp, build_time, image }] }]
	Array recruitable_in(int64_t player, int64_t region) const;
	bool can_recruit(int64_t player, int64_t region, int64_t building_slot, int64_t army_index) const; // lane WOTR-2: LivingWorldLogic::canRecruit
	Dictionary order_dequeue_at(int64_t player, int64_t region, int64_t building_slot, int64_t queue_index); // orderDequeueAt
	Dictionary order_destroy_building(int64_t player, int64_t region, int64_t building_slot);              // orderDestroyBuilding
	int64_t pending_human_battle() const;
	Dictionary choose_auto_resolve(int64_t battle);
	Dictionary choose_retreat(int64_t battle, int64_t player);   // WOTR-2
	int64_t resolution_options(int64_t battle, int64_t player) const; // WOTR-2: RESOLVE_AUTO 2 | RESOLVE_RETREAT 4 | RESOLVE_REAL_TIME 8
	// { ok, error, battle, region, map, players, spawns: [{ player, army, member, template, quantity }], buildings, starting_cash, starting_cash_with_fort }
	Dictionary choose_rts(int64_t battle);
	void set_local_player(int64_t player); // WOTR2 milestone 8
	Array message_log() const;
	// ---- WOTR2 milestone 8 (step 2b): a LAN war (GameNetwork/StrategicSession.h; GameWorld::lan_strategic_begin attaches it). The orders go to the
	// lockstep (post: { ok, queued }); update() does nothing; net_tick runs the strategic frames at the logic rate (5 a second) and returns { frames,
	// executed: [{ frame, sender, type, name, ok, error }] (every command run since the last call), errors }; net_status: { network, frame, timer (seconds,
	// -1 none), waiting (the local player ended its phase while another live human has not), dropped: [slots], quit_requested, quit_reason }
	bool attachNetwork(std::unique_ptr<Transport> transport, const NetworkConfig &config, int phaseTimerSeconds, int battleChoice, std::string *error);
	bool networked() const { return m_session != nullptr; }
	// [S-1342] the battle maps' start positions for the network vote (LivingWorldLogic::setMapStartCount)
	void setMapStartCount(std::function<int(const std::string &)> f)
	{
		if (m_logic)
		{
			m_logic->setMapStartCount(std::move(f));
		}
	}
	Dictionary net_tick();
	Dictionary net_status() const;
	// WOTR2 milestone 8 (step 4): the LAN war's save request (MSG_LW_OPENBFME_SAVE, RW 0x629750) and the leave-for-now vote (MSG 0x6B9), from the local
	// player; lan_save_state: the strategic state of the frame a save was ordered on, with its MD5 (retail's save identity) and state hash
	Dictionary net_save_ask(const String &name);
	Dictionary net_save_answer(bool yes);
	Dictionary net_leave_vote(bool yes);
	Dictionary lan_save_state() const;
	// a LAN war's save, resumed from the START (RW 0x779C1F: each peer loads its own copy, the generator reseeded with 0): load_lan_state replaces the
	// war (before lan_strategic_begin); check_lan_state only loads and compares ("" when the copy loads with that state hash: the lobby's check)
	Dictionary load_lan_state(const PackedByteArray &state, int64_t state_hash, int64_t players, int64_t timer, int64_t battle_choice);
	String check_lan_state(const PackedByteArray &state, int64_t state_hash, int64_t players, int64_t timer, int64_t battle_choice) const;
	void net_leave(); // the local player leaves from the map: its surrender goes out first (StrategicSession::leave); the session is detached
	void detachNetwork(); // the session and its transport go (a new war, a load, the war left, a battle: net_park); the logic stays
	Dictionary take_battle_setup(); // WOTR2 milestone 8 (step 3): the battle a LAN war's vote decided (choose_rts's dictionary), {ok: false} none
	bool save_state_allowed() const { return m_logic && !m_session; } // the single player save; a LAN war saves through the vote (lan_save_state, S-1340)
	// result { winner, members: [{ army, member, survivors }] }
	Dictionary apply_battle_result(int64_t battle, const Dictionary &result);

	// ---- the real-time battle's side (client reads of the live game) ----
	// the live game's player index of a player name (the start report's slot_players), -1 when absent
	int64_t battle_player_index(Object *game_world, const String &player_name) const;
	// the ids of `ids` whose objects still live in the game (not dead, not destroyed)
	Array battle_alive(Object *game_world, const Array &ids) const;
	Dictionary collect_battle_result(Object *game_world, const Dictionary &spawned, const Array &players, bool left) const; // WOTR2 milestone 6
	// WOTR2 milestone 8 (step 3): a LAN war's battle result, read at the end of frame T_end on every peer (GameLogic/LivingWorld/BattleResultCapture.h)
	Dictionary arm_battle_capture(Object *game_world, const Array &players);
	bool battle_capture_done() const;
	Dictionary captured_battle_result() const;
	Dictionary battle_reinforcements(Object *game_world) const;           // WOTR2 milestone 7
	Dictionary call_in_reinforcement(Object *game_world, int64_t army) const;

protected:
	static void _bind_methods();

private:
	Dictionary orderResult(bool ok, const std::string &error) const;
	void battleSetupInto(Dictionary &out, const LivingWorld::BattleSetup &setup) const; // choose_rts's battle fields
	// load_state's restore and checks into `out` (not swapped in); "" when it loaded
	// `networkedTimer` >= 0 (WOTR2 milestone 8 step 4): a LAN war's save: the loaded logic is made networked (the timer and battle choice rules) before its
	// state hash, which holds the network clock, is compared
	std::string loadChecked(const PackedByteArray &state, int64_t state_hash, int64_t players, bool battle, std::unique_ptr<LivingWorld::LivingWorldLogic> &out,
		int networkedTimer = -1, int battleChoice = 0) const;

	std::shared_ptr<const LivingWorld::LivingWorldStores> m_stores; // lane WOTR-2: shared with the world's load (see setup)
	LivingWorld::UnitInfoSource m_units;
	std::unique_ptr<LivingWorld::LivingWorldLogic> m_logic;
	BattleResultCapture m_battleCapture; // WOTR2 milestone 8 (step 3)
	// WOTR2 milestone 8: the strategic command stream (the orders as MSG_LW_* messages) and the player this client orders for
	struct LoggedMessage
	{
		int turn = 0, phase = 0, sender = -1;
		GameMessage message{ 0, -1 };
		bool ok = false;
	};
	std::vector<LoggedMessage> m_messageLog;
	int m_localPlayer = 0;
	Dictionary post(const GameMessage &message, int sender);
	// WOTR2 milestone 8 (step 2b): the LAN war's network
	std::unique_ptr<Transport> m_transport;
	std::unique_ptr<StrategicSession> m_session;
	std::uint64_t m_nextFrameMs = 0;
	size_t m_executedSeen = 0;
	// lane WOTR-2: the world's TheRankInfoStore ranks and RTS PlayerTemplate rank fields at setup (the power points: GameSetup::ranks)
	std::vector<LivingWorld::SciencePurchasePointTracker::RankRow> m_ranks;
	std::map<std::string, LivingWorld::GameSetup::RankTemplate> m_rankTemplates;
	float m_resourceMultiplierLimit = 0.0f; // lane WOTR-2: GameData ResourceMultiplierLimit (EconomySettings) at setup
};

} // namespace godot
