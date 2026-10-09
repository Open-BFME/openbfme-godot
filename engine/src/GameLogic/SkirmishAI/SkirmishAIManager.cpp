// OpenBFME. GPL-3.0.
//
// TheSkirmishAIManager (lane AI-1, step 1). See SkirmishAIManager.h for the binary facts; each function cites the RW function it ports.

#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"

namespace
{
const std::uint32_t kLogicFramesPerSecond = 5; // RW 0xD9F608
const std::uint32_t kFirstAIFrame = 10;        // RW 0x6A96B6: cmp [TheGameLogic + 0x40], 0xA; jb
} // namespace

bool SkirmishAIManager::countsAsAI(const Player &p) const
{
	// RW 0x8EDAAB / 0x8EDADE: COMPUTER, or SkirmishAIData MakeAllSkirmishSidesAIControlled (manager + 0x96C)
	return p.getPlayerType() == PLAYER_COMPUTER || (m_store && m_store->data().makeAllSkirmishSidesAIControlled);
}

int SkirmishAIManager::newGame(bool skirmishOrMultiplayer, bool loadingSave, const std::string &mapFileName)
{
	// RW 0x6AA03F .. 0x6AA076
	if (!skirmishOrMultiplayer || loadingSave)
	{
		return 0;
	}
	if (!m_store || !m_store->dataBlockSeen())
	{
		m_errors.push_back("[S-410] skirmish ai: no SkirmishAIData was loaded (Default\\SkirmishAIData.ini): the computer players have no AI");
		return 0;
	}
	m_started = true;
	// RW 0x8F11D2 clears the global RW 0xDE9F04 (its users are not ported: S-412)
	int made = 0;
	PlayerList &players = m_logic.players();
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		Player *p = players.getNthPlayer(i);
		// RW 0x6AAC66: template && template->PlayableSide
		if (!p || !p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide)
		{
			continue;
		}
		if (countsAsAI(*p))
		{
			if (addAIPlayer(*p))
			{
				++made;
			}
		}
		else
		{
			m_infoPlayers.push_back(p->getPlayerIndex()); // RW 0x6AA0B7 .. 0x6AA0EE: map[+0xA3C][index] = new info(p)
		}
	}
	// RW 0x8EF287 -> 0x8EEBAC: the first AI's economy builder makes the farm site pool (the map's FarmTemplate objects)
	if (made > 0 && m_farmSites.empty())
	{
		m_farmSites = AIBaseBuilder::makeFarmSites(m_logic);
	}
	for (auto &g : m_groups)
	{
		AISkirmishPlayer *first = nullptr;
		for (auto &kv : g->players)
		{
			first = kv.second->firstOfGroup ? kv.second.get() : first;
		}
		for (auto &kv : g->players)
		{
			kv.second->farmSites = &m_farmSites;
			kv.second->groupFirst = first;
		}
	}
	// RW 0x6AA10B .. 0x6AA123: every group's AIs are initialised (RW 0x8EDBE1 -> RW 0x6C7581), then RW 0x6A9E48
	for (auto &g : m_groups)
	{
		for (auto &kv : g->players)
		{
			AISkirmishPlayer &ai = *kv.second;
			if (ai.disabled)
			{
				continue; // RW 0x6C7584
			}
			// RW 0x8F02CC AIPlayer::init: the base builder (+ 4) first; its DisableBaseBuilding flag (RW 0x8F0315); the other sub systems are S-412
			AIBaseBuilder::init(m_logic, ai, mapFileName, m_store->data().anyTypeTemplateDisabledSlots, m_errors);
			ai.build.baseBuilderDisabled = m_store->data().disableBaseBuilding;
			ai.build.units.disabled = m_store->data().disableUnitBuilding; // RW 0x8F033F -> + 0x3C
			ai.build.economy.disabled = m_store->data().disableEconomyBuilding; // RW 0x8F02CC -> + 0xC4
			// RW 0x9A10D8 (unit builder init): copies the army's HeroBuildOrder (heroes are S-416)
		}
	}
	// RW 0x6A9E48: every playable player's objects pass through the object event RW 0x6A9D9C (lane AI-2; RW walks the player's own object list (RW 0x66362D
	// on Player + 0x30C), S-890: taken in the game's object list order)
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		Player *p = players.getNthPlayer(i);
		if (!p || !p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide)
		{
			continue;
		}
		for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == p)
			{
				objectEnteredWorld(*o);
			}
		}
	}
	if (m_worldHooks == 0)
	{
		m_worldHooks = m_logic.addWorldHooks([this](Object &o) { objectEnteredWorld(o); }, [this](Object &o) { objectLeftWorld(o); });
	}
	return made;
}

bool SkirmishAIManager::groupAccepts(const AISkirmishGroup &g, const Player &player) const
{
	// RW 0x8EDBB0: the group's first player by key (std::map begin) and the relationship of the NEW player to it
	if (g.players.empty())
	{
		return false;
	}
	const Player *first = m_logic.players().getNthPlayer(g.players.begin()->first);
	return player.getRelationship(first) == ALLIES;
}

AISkirmishPlayer *SkirmishAIManager::addAIPlayer(Player &player)
{
	// RW 0x6A9EAD
	AISkirmishGroup *group = nullptr;
	for (auto &g : m_groups)
	{
		if (groupAccepts(*g, player))
		{
			group = g.get();
			break;
		}
	}
	if (!group)
	{
		m_groups.push_back(std::make_unique<AISkirmishGroup>());
		group = m_groups.back().get();
	}
	// RW 0x8EDF09: the AI, keyed by player index; the member list; the group active
	const ArmyDefinition *army = m_store->findArmy(player.getSide());
	auto ai = std::make_unique<AISkirmishPlayer>();
	ai->playerIndex = player.getPlayerIndex();
	ai->army = army;
	ai->armySide = player.getSide();
	ai->firstOfGroup = group->players.empty();
	ai->brain.chooser = ai->firstOfGroup; // RW 0x6C6DB1: only the group's first AI makes the target chooser
	ai->creationFrame = m_logic.getFrame();
	ai->retryTimer = kLogicFramesPerSecond * 30;
	ai->store = m_store;
	AISkirmishPlayer *raw = ai.get();
	AITacticalAI::makeGenerator(m_logic, *raw); // lane AI-2 r3: the brain's tactics generator is made in the AI's constructor (RW 0x6C6DB1 -> RW 0x90CADA)
	group->players[player.getPlayerIndex()] = std::move(ai);
	group->members.push_back(player.getPlayerIndex());
	group->active = true;
	// RW 0x6A9F46 .. 0x6A9F96
	if (army)
	{
		if (const std::string *dozer = m_store->findDozer(player.getSide()))
		{
			raw->dozerTemplate = *dozer; // RW 0x8F04F7
		}
	}
	else
	{
		raw->disabled = true; // RW 0x6A9F96: AI + 0x168 = 1
		m_errors.push_back("skirmish ai: side '" + player.getSide() + "' of player " + std::to_string(player.getPlayerIndex()) +
			" has no ArmyDefinition: its AI is disabled (RW 0x6A9F96)");
	}
	m_infoPlayers.push_back(player.getPlayerIndex()); // RW 0x6A9F9D .. 0x6A9FDD
	return raw;
}

int SkirmishAIManager::liveAICount(const AISkirmishGroup &g) const
{
	// RW 0x8EDA8E: members not defeated (Player + 0x754) that count as AI
	int n = 0;
	for (int index : g.members)
	{
		const Player *p = m_logic.players().getNthPlayer(index);
		if (p && !p->isDefeated() && countsAsAI(*p))
		{
			++n;
		}
	}
	return n;
}

Player *SkirmishAIManager::firstAIMember(const AISkirmishGroup &g) const
{
	// RW 0x8EDACE: the first member (join order) that counts as AI; defeat is not tested here
	for (int index : g.members)
	{
		Player *p = m_logic.players().getNthPlayer(index);
		if (p && countsAsAI(*p))
		{
			return p;
		}
	}
	return nullptr;
}

void SkirmishAIManager::update()
{
	// RW 0x6A96A0
	if (m_groups.empty() || m_logic.getFrame() < kFirstAIFrame)
	{
		++m_counters.gatedUpdates;
		return;
	}
	++m_counters.managerUpdates;
	// the player info records in player index order (std::map at + 0xA3C); of their update RW 0x8E3CF3 the army list's idle-army record RW 0x99E63B is ported
	// (lane AI-3: read by the best-target gate RW 0x90B2A0; the AIs' records, no other reader); the rest is S-412
	m_counters.playerInfoUpdates += m_infoPlayers.size();
	for (auto &g : m_groups)
	{
		for (auto &kv : g->players)
		{
			AITacticalAI::updateIdleArmy(m_logic, *kv.second);
		}
	}
	for (auto &g : m_groups)
	{
		updateGroup(*g);
	}
}

void SkirmishAIManager::updateGroup(AISkirmishGroup &g)
{
	// RW 0x8EDDF6
	if (!g.active)
	{
		return;
	}
	++m_counters.groupUpdates;
	int live = 0;
	if (!g.members.empty())
	{
		Player *first = firstAIMember(g);
		if (!first)
		{
			g.active = false; // RW 0x8EDE3F
		}
		else
		{
			live = liveAICount(g);
			++m_counters.sharedBrainUpdates; // RW 0x6C6397 on the first AI's brain + 0x14 (not ported)
		}
	}
	if (!g.active)
	{
		return;
	}
	g.lastLiveCount = live;
	// RW 0x8ED6DC (manager + 0xA74, before), every AI by player index, RW 0x8EDB2C (the group's tasks), RW 0x8ED726 (after): the + 0xA74 object and the
	// tasks are not ported (S-412)
	for (auto &kv : g.players)
	{
		updateAI(*kv.second, g);
	}
}

AISkirmishPlayer *SkirmishAIManager::groupFirstAI(AISkirmishGroup &g) const
{
	for (auto &kv : g.players)
	{
		if (kv.second->firstOfGroup)
		{
			return kv.second.get();
		}
	}
	return nullptr;
}

void SkirmishAIManager::updateAI(AISkirmishPlayer &ai, AISkirmishGroup &g)
{
	// RW 0x6C7946
	++m_counters.aiUpdates;
	if (ai.disabled)
	{
		// RW 0x6C79A1 .. 0x6C79D0: the brain still runs (RW 0x90CD00 on + 0x164) while the group has > 1 live AI, or exactly 1 and this AI has an army.
		// Lane AI-3 (QA-1 U22): a defeated AI's tactics must keep running: its teams lose their members, the tactics end and give their slots on the group's
		// shared targets back (RW 0x8F28DD -> 0x8F2006); without it a partner's target kept the dead AI's team count and the partner never attacked again
		const int live = liveAICount(g);
		if (live > 1 || (live == 1 && ai.army))
		{
			AITacticalAI::updateBrain(m_logic, ai, groupFirstAI(g));
			++m_counters.brainUpdates;
		}
		else
		{
			++m_counters.disabledSkips;
		}
		return;
	}
	const Player *p = m_logic.players().getNthPlayer(ai.playerIndex);
	if (p && p->isDefeated())
	{
		// RW 0x6C7981 .. 0x6C7998: the shutdown variant by + 0x178, then disabled
		++m_counters.shutdowns;
		ai.shutdownFrame = m_logic.getFrame();
		ai.disabled = true;
		return;
	}
	// RW 0x6C7961 .. 0x6C797A: RW 0x6C75B5 (+ 0x178 != -1; not ported), the game phase RW 0x6C7677, AIPlayer::update RW 0x8F0EB4 (step 2: the in-progress list, the
	// base builder and the request processor; the other sub systems are S-412), then the brain (S-412)
	AIBaseBuilder::updatePhase(m_logic, ai);
	AIBaseBuilder::update(m_logic, ai);
	// the team builder (+ 0x90, RW 0x9A3366) runs inside AIPlayer::update after the unit builder; it draws no random value and touches only the tactic teams,
	// so running it after the request processor keeps the retail order of every observable effect
	AITacticalAI::updateTeamBuilder(m_logic, ai);
	++m_counters.aiPlayerUpdates;
	// the brain (+ 0x164, RW 0x6C74C2); the group's first AI owns the target chooser the others share (+ 0x18)
	AITacticalAI::updateBrain(m_logic, ai, groupFirstAI(g));
	++m_counters.brainUpdates;
}

SkirmishAIManager::~SkirmishAIManager()
{
	// GameLogic's destructor resets (and so removes the hooks) while its hook list is alive; nothing is left to remove here
}

AISkirmishPlayer *SkirmishAIManager::eventAI(const Object &obj)
{
	// RW 0x6A9DA3 / 0x6A99D0: an object whose + 0x4D0 byte is set is skipped (S-890: the field is not identified; taken as clear); RW 0x6A950B: the AI of the
	// object's controlling player (RW 0x68B678); RW 0x6C7779 / 0x6C778A: nothing for a disabled AI
	if (!m_started)
	{
		return nullptr;
	}
	const Player *p = obj.getControllingPlayer();
	if (!p)
	{
		return nullptr;
	}
	for (auto &g : m_groups)
	{
		auto it = g->players.find(p->getPlayerIndex());
		if (it != g->players.end())
		{
			return it->second->disabled ? nullptr : it->second.get();
		}
	}
	return nullptr;
}

void SkirmishAIManager::objectEnteredWorld(Object &obj)
{
	if (AISkirmishPlayer *ai = eventAI(obj))
	{
		AIBaseBuilder::onObjectEntered(m_logic, *ai, obj);
	}
}

void SkirmishAIManager::objectLeftWorld(Object &obj)
{
	if (AISkirmishPlayer *ai = eventAI(obj))
	{
		AIBaseBuilder::onObjectLeft(m_logic, *ai, obj);
	}
}

void SkirmishAIManager::dozerIdle(Object &dozer)
{
	// RW 0x88BF4E: the dozer's controlling player's AI (no disabled test on this path)
	if (!m_started || !dozer.getControllingPlayer())
	{
		return;
	}
	for (auto &g : m_groups)
	{
		auto it = g->players.find(dozer.getControllingPlayer()->getPlayerIndex());
		if (it != g->players.end())
		{
			AIBaseBuilder::onDozerIdle(m_logic, *it->second, dozer);
			return;
		}
	}
}

void SkirmishAIManager::structureBuilt(Object &builder, Object &structure)
{
	// RW 0x86C1ED: the AI of the structure's controlling player; RW 0x6C77E4: nothing for a disabled AI
	if (AISkirmishPlayer *ai = eventAI(structure))
	{
		AIBaseBuilder::onStructureBuilt(m_logic, *ai, builder, structure, true);
	}
}

void SkirmishAIManager::reset()
{
	if (m_worldHooks != 0)
	{
		m_logic.removeWorldHooks(m_worldHooks);
		m_worldHooks = 0;
	}
	m_groups.clear();
	m_infoPlayers.clear();
	m_farmSites.clear();
	m_started = false;
	m_errors.clear();
	m_counters = Counters();
	m_attackedAtIds = 0; // RW 0x6A922D
}

const AISkirmishPlayer *SkirmishAIManager::findAI(int playerIndex) const
{
	for (const auto &g : m_groups)
	{
		auto it = g->players.find(playerIndex);
		if (it != g->players.end())
		{
			return it->second.get();
		}
	}
	return nullptr;
}

const AISkirmishGroup *SkirmishAIManager::findGroup(int playerIndex) const
{
	// RW 0x6A957E: the group whose map has the player
	for (const auto &g : m_groups)
	{
		if (g->players.count(playerIndex) != 0)
		{
			return g.get();
		}
	}
	return nullptr;
}

void SkirmishAIManager::crc(StateHasher &h) const
{
	h.addU32(0x534B4149u); // "SKAI"
	h.addBool(m_started);
	h.addU32(m_attackedAtIds);
	// the data the AI decides from (SkirmishAIData, armies, bases and their layouts, dozers, PlayerAITypes): a peer with other data diverges here
	h.addBool(m_store != nullptr);
	if (m_store)
	{
		m_store->crc(h);
	}
	h.addU32((std::uint32_t)m_infoPlayers.size());
	for (int i : m_infoPlayers)
	{
		h.addI32(i);
	}
	h.addU32((std::uint32_t)m_farmSites.size());
	for (const auto &f : m_farmSites)
	{
		AIBaseBuilder::crcRequest(*f, h);
	}
	h.addU32((std::uint32_t)m_groups.size());
	for (const auto &g : m_groups)
	{
		h.addBool(g->active);
		h.addI32(g->lastLiveCount);
		h.addU32((std::uint32_t)g->members.size());
		for (int m : g->members)
		{
			h.addI32(m);
		}
		for (const auto &kv : g->players)
		{
			const AISkirmishPlayer &ai = *kv.second;
			h.addI32(ai.playerIndex);
			h.addString(ai.armySide);
			h.addBool(ai.army != nullptr);
			h.addBool(ai.firstOfGroup);
			h.addBool(ai.disabled);
			h.addFloat(ai.field170);
			h.addU32(ai.creationFrame);
			h.addI32(ai.field178);
			h.addString(ai.dozerTemplate);
			h.addU32(ai.shutdownFrame);
			h.addU32(ai.retryTimer);
			AIBaseBuilder::crc(ai.build, h);
			AITacticalAI::crc(ai.brain, h);
		}
	}
}

std::vector<std::string> SkirmishAIManager::stopLines()
{
	return {
		"[S-410] skirmish ai data: SkirmishAIData / ArmyDefinition / AIBase / AIDozerAssignment / PlayerAIType parse with the binary's tables; RW's "
		"tables have no terminating row, so an unknown field crashes retail where the port reports it; a CombatChainDefinition without Unit writes "
		"before RW's table (the port rejects it, S-411)",
		"[S-412] skirmish ai: TheSkirmishAIManager's creation rules, groups and update cadence are ported (RW 0x6AA030, 0x6A9EAD, 0x6A96A0, 0x8EDDF6, "
		"0x6C7946); the bodies they call are not: the player info records (RW 0x8E3CF3), the manager object + 0xA74, the group tasks (RW 0x8EDB2C), "
		"AISkirmishPlayer RW 0x6C75B5 / the shutdown, the AIPlayer sub systems other than the base, unit and team builders (RW 0x8F0EB4: "
		"+ 0xC0 economy builder, + 0x108 unit upgrader, + 0x12C science builder, + 0xE4 wall builder): a computer player builds its base (S-413), trains its army "
		"(S-415) builds farms (S-419) and attacks with SimpleAttack teams (S-417)",
		"[S-417] skirmish ai tactical layer: the enemy manager, the targets and their ENEMY_STRUCTURE heuristics, the best target, the difficulty gate, the 10 "
		"offensive prototypes' applicability, team counts and team sizes (RW 0x90BE31: SimpleAttack x2, FormationAttack x2, FlankAttack, PincerAttack, FeintAttack, "
		"AIBasePenetrationTroopsTactic, SimpleSiege, SiegeGates) with the pick's draw, the SimpleAttack body, the team builder and (lane AI-2) the started "
		"tactic's idle counters, position and idle end (RW 0x8F28DD / 0x8F135E / 0x8F27AD / 0x8F175A / 0x9B4DE7) and (r5) the bodies of FormationAttack, "
		"FlankAttack, PincerAttack, FeintAttack and AIBasePenetrationTroopsTactic with their draws (FlankAttack RW 0x9B4642 / 0x9B4720, PincerAttack RW 0x9B4067, "
		"FeintAttack RW 0x9B398D / 0x9B3A5A / 0x9B3AE4) are ported; not ported: the AIGroup formation move's draw RW 0x774CEB (S-895), the siege bodies, the end's merge into a nearby AI team with its draw RW 0x8F1F08 (the units are released), "
		"the DEFENSIVE / EXPANSION heuristics and tactics (only the One Ring OPPORTUNITY heuristic is "
		"ported; the capture flag rule needs the manager's historical flag list), the targetless tactics, RW 0x90CCCB, the AI team objects; "
		"lane AI-3 ported the threat finder (S-1300), the targets' threat, the retreat (RW 0x8F2784 / 0x8F243A), the failed end's threat move (RW 0x6C644C), "
		"each team's AIGroup step (RW 0x8F144C -> 0x774F2B, S-1301), a defeated AI's brain (RW 0x6C79A1), the end's merge (RW 0x8F1D88), RW 0x6C7344 and the "
		"best target's threat gate (RW 0x90B2A0, S-1302)",
		"[S-419] skirmish ai economy builder (+ 0xC0, RW 0x8EEF8F): farms on the map's FarmTemplate sites (RW 0x8EEC71), the farm decision (RW 0x8EE765), the "
		"difficulty gate (RW 0x99327C), the farm priority growth (RW 0x99F313) and the unit request's effective priority (RW 0x9EDA17) are ported; inference: the "
		"farm site pool is made at newGame in object list order, the economy structure count is recounted from the living objects",
		"[S-420] skirmish ai vs ai: games end through the fortress fall, the spawned worker's fade on its structure's delete (RW 0x85750D -> 0x85730B) and "
		"unfinished structures as targets; INFERENCE: RW's AI target scan filter is not traced (the construction rule is ZH's AI::findClosestEnemy), the "
		"worker kill runs at the port's delete point, the defeat frames are engine pins",
		"[S-418] skirmish ai tactical layer inference: the player info lists are rebuilt each frame in id order; a team recruits every eligible unit with no "
		"maximum and is handed over at its minimum; RW 0x9EF9AF's path check is taken as reachable; the enemy score root is the double root of the float sum",
		"[S-415] skirmish ai unit builder (+ 0x38, RW 0x9A1196): the factories by unit (RW 0x9A0838, BuildAssistant slot 0x68), the composition from the "
		"ArmyDefinition percentages blended by the phase fraction (RW 0x9A0E2B / 0x82FCAB), the request list (RW 0x9A0D4F), one new request per frame among "
		"the members below their share of the command point limit with a free factory (RW 0x9A0BC0 / 0x9A0B26, one logic random draw), priority 500 in Rush "
		"else DefaultUnitPriority, and the unit request's check / queue through PROD-1 (RW 0x9EDBC6 / 0x9EDB44) are ported; not ported: heroes (HeroBuildOrder, "
		"RW 0x9A03A1 / 0x9A0993), the money reserve (+ 0x21, "
		"cost >= 1000), the living world part (RW 0x9A0426)",
		"[S-416] skirmish ai unit builder inference: a structure enters the factory map when it is first seen complete (RW: its completion event), the owned "
		"count of a unit is the player's living objects of the template (RW: the player info record's map, RW 0x8E3FD6), a unit request is complete when its "
		"factory no longer holds its queue entry (RW: the production event), the phase blend of RW 0x82FCAB is computed in double for the x87 chain",
		"[S-413] skirmish ai base builder: the game phase (RW 0x6C7677), the start position, the angle facing the map centre, the layout choice (RW 0x9BCA8E, one "
		"logic random draw), the slots, the structure requests, the request order and processor (RW 0x8F0F7E ..) and the check / execute path through BUILD-1 "
		"(canMakeUnit, legality options 0x85, DozerAIUpdate::construct) are ported; lane AI-2: the dozer manager (RW 0x9A23A4 / 0x9A221E / 0x9A19E0: dozers queued "
		"at the DOZER_FACTORY structures), the object events (RW 0x8F069A / 0x8F0C47 / 0x8F0BBC / 0x8F0660 / 0x8F06FF) and the rebuild of a lost structure "
		"(RW 0x90D16A); not ported: the fortress rebuild task (AIBase + 0x28, RW 0x9BC882), the money reserve (+ 0x21, PercentToSave, RW 0x8F0502 / 0x99E79A), "
		"expansions (bases after the first)",
		"[S-414] skirmish ai base builder inference: RW 0x97DAFC (a pathfinder cell scan of the site) is taken as no objection; the enemy filter asks the "
		"controlling player (RW: the object's team); the CRT acos / sin / cos of the angle and the rotation are SimMath's deterministic versions (S-167)",
		"[S-890] skirmish ai dozer manager inference (lane AI-2): a dozer is free again whenever its task ends (RW 0x88BF4E's notification is found only on the "
		"repair-complete branch RW 0x88DBCE); the starting objects are registered in object list order (RW 0x6A9E48 walks each player's own list); the object "
		"+ 0x4D0 byte that RW 0x6A9D9C / 0x6A99CB test is taken as clear; the created-structure call (RW 0x6AA688 -> RW 0x8F06FF(0)) is not made because the "
		"port creates a dozer's structure at the order (RW: PENDING_CONSTRUCTION, which that call skips)",
		"[S-891] skirmish ai tactic idle check inference (lane AI-2): a team member is engaged when its AI has a current victim (RW 0x7A03AB reads "
		"AIUpdate + 0x40 through RW 0x668303); (lane AI-3) the bit 0x70 at + 0x10C that RW 0x8F135E also tests is the model condition CAPTURING",
		"[S-892] horde member hub busy rule (lane AI-2; lane MOVE-2 r2 applied it in full): RW's move hub (RW 0x87468B) makes a member busy (AI command 0x31) "
		"unless both it and its horde are active, when its HORDE moves or it is idle (RW 0x874749: the isMoving is the horde's), unless it is busy; the member "
		"order's leash (RW 0x877B4F, edge distance RW 0x66352C) and active-member rule (RW 0x877B69) are applied with their prerequisite, the horde command "
		"hand-off (RW 0x89E169 -> slot 0x14 RW 0x87594C, S-1501); open: H + 0x2A0 is taken as the melee freeze and its branch (RW 0x874967: a member neither idle "
		"nor active gets aiIdle) is not run (no member pass in a melee)",
		"[S-1500] horde member slot destination inference (lane MOVE-2): RW 0x6F0889 (via RW 0x871897) is ported for the ground layer; pathfinder + 0x48, which "
		"it clears first, is taken as the ignored obstacle id; a cell of another layer than the horde's fails (the raised-layer tests RW 0x6E82B3 / 0x5E2F24 and "
		"the wall-scaling branch RW 0x86E214 are not ported: S-161, S-084)",
		"[S-1501] horde command hand-off inference (lane MOVE-2 r2): RW 0x89E169's container test (the container's contain vslot 0x10) is taken as \"the horde "
		"is contained\"; its porcupine / stance branch (contain vslots 0xF0 / 0x5C / 0x60, RW 0x89DE81 / 0x89DF11) is not read; slot 0x10 RW 0x8759FF(0) is "
		"run as acceptMemberFromGarrison for every member on its way (no re-form snap); the port's move variants without an identified RotWK command number "
		"(a group move with a final angle, a queued waypoint) count as moves; a wall scaler's ground placement RW 0x70C0AD is a full setPosition",
		"[S-1502] horde melee member pass (lane MOVE-2 r3, community feedback G2): ported from RW 0x872EFC / 0x870A1B / 0x877D89 / 0x98F819: while the horde "
		"melees the contain runs the melee behaviour's update (Amoeba RW 0x9902A1: steps are stored in the member records) and turns the horde to its target, "
		"then the member pass moves every member to its record's destination (else its formation slot) through the move hub (an explicit goal, the "
		"Amoeba's always-turn near arm). INFERENCE: HoldGround's record turn states (its slots 0x20 / 0x28 / 0x2C) are not modelled (the 10 degree rule), and "
		"the horde's turn is kept from carrying the members (the port's contain would place them); G2 measured, not solved: GondorFighterHorde engages 20 % of "
		"its members on average against MordorFighterHorde (test_move2_slot_distance's MOVE2 MELEE report)",
		"[S-893] skirmish ai targetless tactics (lane AI-2 r3): the 11 prototypes, FarmKillSquad's constructor draw, the per-frame pick RW 0x90C5E2 and FarmKillSquad's "
		"applicability, setup, cleanup and update flow are ported; the other 10 prototypes' applicability and bodies and FarmKillSquad's farm choice (RW 0x9BBA51 / "
		"0x9BB5E4 and their draws) are not (a nearest-farm stand-in)",
		"[S-894] produced member of a destroyed horde (lane AI-2 r4): a member the queue exit hands to a horde that is already destroyed is destroyed with it "
		"(HordeContain::acceptCreatedMember); RW's acceptance slot for a destroyed horde is not read",
		"[S-895] skirmish ai offensive tactic bodies (lane AI-2 r5): Formation, Flank, Pincer, Feint and BasePenetration launch / update with their draws "
		"are ported; the formation group manager their moves go through (AIGroup RW 0x7747DE / 0x94FCBA / 0x77348C, draw 0x874), the flank's threat finder "
		"position (lane AI-3: ported), the siege bodies and the end's merge's formation move are not (lane AI-3 ported the merge RW 0x8F1D88 with its draw "
		"AITactic.cpp 0x3FC; a candidate whose tactic has no target, RW 0x8F14C1, is skipped and counted)",
		"[S-1300] skirmish ai threat finder inference (lane AI-3): RW 0x7EE057 / 0x7EE166 / 0x68F0EC, the ThreatBreakdown categories (an object without one counts "
		"twice in the total, RW 0x7EDB72) and the counter table RW 0x7EDC2F / 0x7ED963 are ported; object + 0x4B8 is taken as its constructor value 1 (its writer "
		"RW 0x691A2F is not ported); an InactiveBody's health ratio is taken as 0; the ThreatBreakdown block is read from the template's raw lines (S-071): an "
		"unknown AIKindOf (a load error in retail) leaves the category -1",
		"[S-1301] skirmish ai team group step (lane AI-3): RW 0x774F2B's idle helpers (RW 0x7728E8) are ported; the group's speed matching RW 0x77005F(1) is "
		"not (fast members lead), and Object::isAbleToAttack (RW 0x691269) is ported only for its status / model condition / CAN_ATTACK tests",
		"[S-1302] skirmish ai best target threat gate inference (lane AI-3): RW 0x90B2A0 (the attacked-at records RW 0x6C7344 / 0x6C720A / 0x6C6F01, RW 0x6C6743, "
		"the idle army record RW 0x99E63B, the counter table RW 0x7EDC2F, the draw AITargetChooser.cpp 0xF7) is ported; inference: RW 0x6C7344 walks the "
		"player's teams in its team list order (the port: the tactics in list order, offensive before targetless, each by team index); the target finder's "
		"scan RW 0x6C6743 may reuse (RW 0x7EE166) is taken as a new scan; the idle army record is computed from the living army in id order (S-418)",
	};
}

std::vector<std::string> SkirmishAIManager::report() const
{
	std::vector<std::string> out = m_errors;
	if (m_started)
	{
		size_t ais = 0;
		for (const auto &g : m_groups)
		{
			ais += g->players.size();
		}
		out.push_back("[S-412] skirmish ai: " + std::to_string(ais) + " AI player(s) in " + std::to_string(m_groups.size()) + " group(s); " +
			std::to_string(m_counters.aiUpdates) + " AI updates ran with unported bodies (AIPlayer " + std::to_string(m_counters.aiPlayerUpdates) +
			", brain " + std::to_string(m_counters.brainUpdates) + ", player info " + std::to_string(m_counters.playerInfoUpdates) + ")");
		unsigned long long draws = 0, rejects = 0, retreats = 0, merges = 0;
		for (const auto &g : m_groups)
		{
			for (const auto &kv : g->players)
			{
				draws += kv.second->brain.gateDraws;
				rejects += kv.second->brain.gateRejects;
				retreats += kv.second->brain.retreats;
				merges += kv.second->brain.merges;
			}
		}
		out.push_back("[S-1302] skirmish ai: the best-target threat gate (RW 0x90B2A0) drew " + std::to_string(draws) + " time(s) and refused " +
			std::to_string(rejects) + " target(s) (the attacked-at records' team order is inferred); " + std::to_string(retreats) + " tactic retreat(s) (RW 0x8F243A), " +
			std::to_string(merges) + " team merge(s) (RW 0x8F1D88)");
	}
	return out;
}
