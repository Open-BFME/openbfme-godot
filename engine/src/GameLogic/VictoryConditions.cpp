// OpenBFME. GPL-3.0.
// See GameLogic/VictoryConditions.h for the target facts, the donors and what is inference.

#include "GameLogic/VictoryConditions.h"

#include "GameLogic/SelfDestruct.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>

VictoryConditions::VictoryConditions(GameLogic &logic)
	: m_logic(logic)
{
}

void VictoryConditions::reset()
{
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		m_players[i] = nullptr;
		m_isDefeated[i] = false;
	}
	m_localSlot = -1; // RW 0x808A5A
	m_endFrame = 0;
	m_localPlayerDefeated = false;
	m_singleAllianceRemaining = false;
	m_isObserver = false;
	m_defeatCount = 0;
	m_cached = 0;
	m_flag90 = false;
	m_flag91 = false;
	m_events.clear();
	m_objectsKilled = 0;
	m_ruleUnported = 0;
	m_eliminationBonuses = 0;
	m_selfDestructs = 0;
	m_transfersUnported = 0;
	m_rule = RULE_BASE_OR_BUILDERS;
}

// RW 0x808BD4
bool VictoryConditions::cachePlayer(Player *p)
{
	const PlayerList &list = m_logic.players();
	if (!p || p == list.getNeutralPlayer() || !p->getPlayerTemplate() || p->isObserver())
	{
		return false;
	}
	const PlayerTemplate *civilian = list.templates().findPlayerTemplate("FactionCivilian"); // the binary's own string (RW 0xC24144)
	if (civilian && p->getPlayerTemplate() == civilian)
	{
		return false;
	}
	if (m_cached >= MAX_PLAYER_COUNT)
	{
		throw std::logic_error("VictoryConditions: more than 20 players");
	}
	m_players[m_cached] = p;
	if (p == list.getLocalPlayer())
	{
		m_localSlot = m_cached;
	}
	++m_cached;
	return true;
}

// RW 0x808A43 (reset, the rule: 2, or 3 in the game mode 3) then cachePlayerPtrs RW 0x808B6A
void VictoryConditions::init()
{
	reset();
	m_rule = m_logic.economy().context().gameMode == EconomyContext::MODE_REPLAY ? RULE_BASE_OR_ARMY : RULE_BASE_OR_BUILDERS;
	if (!m_logic.economy().isMultiplayerGame())
	{
		return; // RW 0x808B6D: the recorder says the game is not a multiplayer one: no players are cached
	}
	const PlayerList &list = m_logic.players();
	for (int i = 0; i < list.getPlayerCount(); ++i)
	{
		cachePlayer(list.getNthPlayer(i));
	}
	if (m_localSlot < 0)
	{
		m_localPlayerDefeated = true; // RW 0x808BBB: no local player: the game is observed
		m_isObserver = true;
	}
}

void VictoryConditions::initWithPlayers(const std::vector<Player *> &players)
{
	reset();
	m_rule = m_logic.economy().context().gameMode == EconomyContext::MODE_REPLAY ? RULE_BASE_OR_ARMY : RULE_BASE_OR_BUILDERS;
	for (Player *p : players)
	{
		cachePlayer(p);
	}
	if (m_localSlot < 0)
	{
		m_localPlayerDefeated = true;
		m_isObserver = true;
	}
}

// RW 0x8089F2
bool VictoryConditions::areAllies(const Player *a, const Player *b)
{
	return a != b && a->getRelationship(b->getDefaultTeam()) == ALLIES && b->getRelationship(a->getDefaultTeam()) == ALLIES;
}

// RW 0x6ABD73 -> 0x7A0830: the player's live non-structure, non-projectile, non-IGNORE_FOR_VICTORY objects of this KindOf
bool VictoryConditions::anyObjectOfKind(const Player &p, const char *kindName) const
{
	for (const Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != &p || o->isEffectivelyDead() || o->isDestroyed())
		{
			continue;
		}
		if (o->isKindOfName("STRUCTURE") || o->isKindOfName("PROJECTILE") || o->isKindOfName("IGNORE_FOR_VICTORY"))
		{
			continue;
		}
		if (o->isKindOfName(kindName))
		{
			return true;
		}
	}
	return false;
}

// RW 0x6ABDC2 -> 0x7A1A81 -> 0x7A089B: the player's live objects that are not STRUCTURE, PROJECTILE or IGNORE_FOR_VICTORY and that the unit filter accepts
bool VictoryConditions::anyUnitMatching(const Player &p, const ObjectFilter &filter) const
{
	for (const Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != &p || o->isEffectivelyDead() || o->isDestroyed())
		{
			continue;
		}
		if (o->isKindOfName("STRUCTURE") || o->isKindOfName("PROJECTILE") || o->isKindOfName("IGNORE_FOR_VICTORY"))
		{
			continue;
		}
		if (ObjectFilterMatch::allows(m_logic, filter, *o, nullptr))
		{
			return true;
		}
	}
	return false;
}

// RW 0x809404 (changes nothing: `ruleUnported` reports that a rule this port does not model decided)
bool VictoryConditions::defeatedByRule(const Player *player, bool *ruleUnported) const
{
	if (player->isDefeated())
	{
		return true; // the player was killed (Player + 0x754)
	}
	const GameLogicSettings &settings = m_logic.settings();
	if (!settings.victoryRulesLoaded)
	{
		throw std::logic_error("VictoryConditions: GameLogicSettings has no VictoryConditionStructureObjectFilter / VictoryConditionUnitObjectFilter (GameData not loaded)");
	}
	// RW 0x80942A..0x809457: the script engine veto (RW 0x441E4A, skipped in the game mode 3) and the game type 9 answer "not defeated": neither is modelled (stop S-344)
	const Player &p = *player;
	switch (m_rule)
	{
	case RULE_BASE_OR_BUILDERS:
		if (m_logic.economy().hasObjectMatching(p, *settings.victoryStructureFilter, false))
		{
			return false;
		}
		return !anyObjectOfKind(p, "DOZER");
	case RULE_BASE_OR_ARMY:
		if (m_logic.economy().hasObjectMatching(p, *settings.victoryStructureFilter, false))
		{
			return false;
		}
		return !anyUnitMatching(p, *settings.victoryUnitFilter);
	default:
		if (ruleUnported)
		{
			*ruleUnported = true; // RW 0x809508 / 0x8094F8 (S-344)
		}
		return false;
	}
}

bool VictoryConditions::wouldBeDefeated(const Player *player) const
{
	if (!player || player->isObserver())
	{
		return false;
	}
	return m_flag90 || defeatedByRule(player, nullptr);
}

// RW 0x80953C
bool VictoryConditions::hasSinglePlayerBeenDefeated(const Player *player)
{
	if (!player || player->isObserver())
	{
		return false;
	}
	if (m_flag90)
	{
		return true;
	}
	bool unported = false;
	const bool defeated = defeatedByRule(player, &unported);
	m_ruleUnported += unported ? 1u : 0u;
	if (defeated)
	{
		++m_defeatCount;
	}
	return defeated;
}

// RW 0x808AA8 (const: see the header)
bool VictoryConditions::hasAchievedVictory(const Player *player) const
{
	if (!player || player->isObserver())
	{
		return false;
	}
	if (!m_singleAllianceRemaining || m_defeatCount <= 0)
	{
		return false;
	}
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		const Player *q = m_players[i];
		if (q && !wouldBeDefeated(q) && (q == player || areAllies(player, q)))
		{
			return true;
		}
	}
	return false;
}

// RW 0x808B2B (const: see the header)
bool VictoryConditions::hasBeenDefeated(const Player *player) const
{
	if (!player || player->isObserver() || !m_singleAllianceRemaining || m_defeatCount <= 0)
	{
		return false;
	}
	return !hasAchievedVictory(player);
}

// RW 0x808C4C (vslot 0x48, the script condition MULTIPLAYER_ALLIED_VICTORY, RW 0x7EC1D3)
bool VictoryConditions::localAlliedVictory() const
{
	if (m_isObserver)
	{
		return false;
	}
	// RW 0x808C5B: TheGameInfo vslot 0x50 answers "no" for a skirmish / LAN game (INFERENCE, S-1062)
	if (m_localSlot < 0 || m_localSlot >= MAX_PLAYER_COUNT)
	{
		return false;
	}
	return hasAchievedVictory(m_players[m_localSlot]);
}

// RW 0x808C88 (vslot 0x4C, MULTIPLAYER_ALLIED_DEFEAT, RW 0x7EC1E3)
bool VictoryConditions::localAlliedDefeat() const
{
	if (m_isObserver)
	{
		return m_defeatCount > 0 && m_singleAllianceRemaining;
	}
	if (m_localSlot < 0 || m_localSlot >= MAX_PLAYER_COUNT)
	{
		return false;
	}
	return hasBeenDefeated(m_players[m_localSlot]);
}

// RW 0x808CBA (vslot 0x50): an observer answers no; else + 0x84 (set only by init for a game without a local player)
bool VictoryConditions::localDefeatedAnswer() const
{
	return m_isObserver ? false : m_localPlayerDefeated;
}

// RW 0x7E526C (MULTIPLAYER_PLAYER_DEFEAT, RW 0x7EC1F3)
bool VictoryConditions::localPlayerOnlyDefeat() const
{
	return localDefeatedAnswer() && !localAlliedDefeat();
}

int VictoryConditions::cachedPlayerIndex(int cacheIndex) const
{
	return cacheIndex >= 0 && cacheIndex < MAX_PLAYER_COUNT && m_players[cacheIndex] ? m_players[cacheIndex]->getPlayerIndex() : -1;
}

// lane END-2: RW 0x77CA3D, MSG_SELF_DESTRUCT (1096). The quit menu sends it: its Forfeit / Surrender button with false (RW 0x921841, a multiplayer game that the
// local alliance has not won), its Exit with true (RW 0x625E36: a multiplayer game whose TheGameInfo vslot 0x50 answers no). A message of no player does
// nothing (RW 0x77CA3D: [ebp - 0x14] null). With `transferToAlly` the first other player (list order) that is mutually ALLIES with the sender (RW 0x6ADBEB on
// each other's default team, both 2) and that hasSinglePlayerBeenDefeated (vslot 0x40, the counting one) does not call defeated receives the sender's assets
// (RW 0x6AF598(sender, 1): ported by lane MP-2, GameLogic/SelfDestruct.h, stop S-1122) and the sender is killed (RW 0x6ABE7C); no such ally: the sender is killed. Without it the sender is
// killed (then RW 0x77CB05 .. the Living World's branch when TheGameLogic's campaign object says so: not a skirmish / LAN game, not ported). The killed player
// is defeated at the next VictoryConditions::update, as any other.
void VictoryConditions::selfDestruct(Player *player, bool transferToAlly)
{
	if (!player)
	{
		return;
	}
	++m_selfDestructs;
	// lane MP-2: the ally search, RW 0x6AF598's transfer (ported: GameLogic/SelfDestruct.h) and the kill
	SelfDestruct::execute(m_logic, *player, transferToAlly);
}

void VictoryConditions::registerHandlers(GameLogicDispatch &dispatcher)
{
	dispatcher.registerHandler(MSG_SELF_DESTRUCT, "END-2", [](GameLogic &logic, const GameMessage &m) {
		const GameMessageArgument *a = m.getArgument(0);
		if (!a)
		{
			return false; // malformed: counted unhandled by the dispatcher
		}
		logic.victory().selfDestruct(logic.players().getNthPlayer(m.getPlayerIndex()), a->boolean);
		return true;
	});
}

// RW 0x6ABE7C, the part that decides what happens to the objects (the order they are killed in is inference, S-344)
void VictoryConditions::killPlayer(Player *p)
{
	std::vector<Object *> victims;
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == p && !o->isDestroyed() && !o->isEffectivelyDead() && o->getBodyModule())
		{
			victims.push_back(o);
		}
	}
	for (Object *o : victims)
	{
		if (!o->isEffectivelyDead() && !o->isDestroyed())
		{
			o->kill(DEATH_NORMAL); // RW 0x7A4CAE: push 0 (death type NORMAL), push 8 (damage type UNRESISTABLE) into Object::kill, whose first argument is the damage type (see the header)
			++m_objectsKilled;
		}
	}
	p->setDefeated(true); // RW 0x6ABEFD: Player + 0x754
	// lane END-1: RW 0x6ABF3C .. 0x6ABF57: ThePlayerList vslot 0x40 (the team relationship refresh, not ported: S-1062), then every coin the player holds is
	// withdrawn (Money::withdraw(Player + 0x94, no score keeper, playSound 1), RW 0x7B17EF)
	p->getMoney()->withdraw(p->getMoney()->countMoney(), nullptr, true);
	// RW 0x6ABF5E .. 0x6ABF7A: a single player game keeps a player with Player + 0x5C == 1 alive (not a skirmish: not ported); RW 0x6ABF7C .. 0x6ABF90: for the
	// local player the in-game UI's vslot 0xBC(0) (the client's)
	// RW 0x6ABF96 .. 0x6ABFEE: the player that last hurt us (ScoreKeeper + 0x104) gains our newest per-frame score * PlayerEliminatedMultiplier (mulss), RW 0x79DBC6
	const ScoreKeeper &mine = p->getScoreKeeper();
	if (mine.lastAttackerIndex() >= 0 && !mine.perFrameStats().empty())
	{
		if (Player *attacker = m_logic.players().getNthPlayer(mine.lastAttackerIndex())) // RW 0x6A844E: by index
		{
			const float bonus = SimMath::mulf32(mine.perFrameStats().back().score, m_logic.settings().score.playerEliminated);
			attacker->getScoreKeeper().addEliminationBonus(bonus);
			++m_eliminationBonuses;
		}
	}
}

// RW 0x808F53
void VictoryConditions::update()
{
	const unsigned frame = m_logic.getFrame();
	const GameLogicSettings &settings = m_logic.settings();
	// ftol(5 * SecondsBeforeBaseCheckActive) (RW 0x808F81: cvtsi2ss [0xD9F608] = 5, mulss, _ftol)
	const unsigned delay = (unsigned)SimMath::truncToInt32(SimMath::mulf32(SimMath::sseFromInt32(5), settings.secondsBeforeBaseCheckActive));
	if (!m_logic.economy().isMultiplayerGame())
	{
		return;
	}
	// RW 0x808FBD tests the local slot and the observer flag here; they are peer-local, and after init() one of them always holds, so the one deterministic condition left is that the
	// players were cached at all (an uninitialised object would otherwise declare a single alliance of nobody)
	if (m_cached == 0)
	{
		return;
	}
	if (frame < delay)
	{
		return;
	}
	if (!m_singleAllianceRemaining)
	{
		const Player *first = nullptr;
		bool onlyOneAlliance = true;
		for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
		{
			const Player *p = m_players[i];
			if (p && !hasSinglePlayerBeenDefeated(p))
			{
				if (first)
				{
					if (!areAllies(first, p))
					{
						onlyOneAlliance = false;
						break;
					}
				}
				else
				{
					first = p;
				}
			}
		}
		if (onlyOneAlliance)
		{
			m_singleAllianceRemaining = true;
			m_endFrame = frame;
			// RW 0x809032 .. 0x80903F: TheGameLogic + 0x98 (the keep-score switch) clears unless TheGameInfo's vslot 0x50 says so (not identified; a skirmish or
			// LAN game takes the clearing branch: INFERENCE, the same vslot would also veto MULTIPLAYER_ALLIED_VICTORY, S-1062)
			m_logic.economy().setScoring(false);
			if (m_defeatCount > 0)
			{
				m_events.push_back(Event{ frame, Event::ALLIANCE_VICTORY, first ? first->getPlayerIndex() : -1 });
			}
		}
	}
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		Player *p = m_players[i];
		if (p && !m_isDefeated[i] && hasSinglePlayerBeenDefeated(p))
		{
			m_isDefeated[i] = true;
			p->setDefeatFrame(frame); // RW 0x809098: Player + 0x4CC, which is the ScoreKeeper's end frame (Player + 0x3DC + 0xF0, read by RW 0x79DC0C)
			p->getScoreKeeper().setEndFrame(frame);
			if (frame > 1)
			{
				m_events.push_back(Event{ frame, Event::PLAYER_DEFEATED, p->getPlayerIndex() });
			}
			killPlayer(p);
		}
	}
}

void VictoryConditions::crc(StateHasher &h) const
{
	// m_localSlot, m_localPlayerDefeated and m_isObserver are this peer's own identity: not part of the shared state
	h.addI32(m_rule);
	h.addU32(m_endFrame);
	h.addBool(m_singleAllianceRemaining);
	h.addI32(m_defeatCount);
	h.addI32(m_cached);
	h.addBool(m_flag90);
	h.addBool(m_flag91);
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		h.addI32(m_players[i] ? m_players[i]->getPlayerIndex() : -1);
		h.addBool(m_isDefeated[i]);
	}
	h.addU32((std::uint32_t)m_events.size());
	for (const Event &e : m_events)
	{
		h.addU32(e.frame);
		h.addI32(e.kind);
		h.addI32(e.playerIndex);
	}
}

std::vector<std::string> VictoryConditions::stops()
{
	return {
		"[S-344] victory: the rules 2 (base or builders: the skirmish default) and 3 are ported from RW 0x809404; the rules 0 and 1, the game mode 9 and the script engine veto (RW 0x441E4A) are not "
		"(a mode other than 2 / 3 counts as not defeated); the order killPlayer kills a loser's objects in is the logic object list's (retail: the team lists)",
		"[S-1061] score keeper: the RotWK ScoreKeeper (layout RW 0x79ECCB, calculateScore RW 0x79DFFA, built / destroyed / lost RW 0x79F0E1 / 0x79F303 / 0x79F486, per-frame stats RW 0x79F704; "
		"END-2: the spend by kind RW 0x79DFC2 from the unit queue / cancel, the plot and dozer builds and the wall span, the HORDE map + 0x1E4, the favourite unit RW 0x79E2A8 and the hero "
		"counts RW 0x79E404) is ported; not ported: eight of the ten addObjectBuilt callers (the sell refund RW 0x88D077, a structure building itself RW 0x85677A, the object replacements "
		"RW 0x853EDF / 0x8B140D / 0x8B228C / 0x856DF9 / 0x6AF769, the command dispatcher RW 0x77B2BB) and the temporary count switches around them, the spend by kind of the sell path "
		"(RW 0x88D06B) and of RW 0x8AD7BE, the heroes / units vetted and the + 0xC / + 0x10 (allied money) adders, the objectives term (TheObjectiveList RW 0xDE8C94: none in a "
		"skirmish), an AI player's per-frame money term (its skirmish AI info record + 0xC -> + 0x14, RW 0x6A9999); the maps are keyed by template name (retail: by pointer, so a tie "
		"of the favourite unit may differ); the per-frame stats are recorded at the end of the logic frame (Player::update's caller, PlayerList::update RW 0x6A84DB, is a subsystem table "
		"entry that was not located: INFERENCE); the GameData score multipliers start at RW 0x643D31 .. 0x643D48's defaults (1.0; PlayerEliminatedMultiplier 0.1)",
		"[S-1062] victory (END-1): VictoryConditions::update runs at its binary place (phase 5 after the destroy list, RW 0x62EBCE); killPlayer withdraws the loser's money and gives its "
		"last attacker the elimination bonus (RW 0x6ABF3C .. 0x6ABFEE); the keep-score switch clears with the single alliance (RW 0x80903F). Not ported: TheGameInfo's vslot 0x50 (taken "
		"as false: a skirmish / LAN game clears the switch and allows MULTIPLAYER_ALLIED_VICTORY), ThePlayerList vslot 0x40 in killPlayer, the rule that kills the remaining computer "
		"players when no human is left (RW 0x809271 .. 0x80936E: its Living World record RW 0x6B5DE0 + 0x444 is not identified), the shared-AI branch of killPlayer (AISkirmishPlayer "
		"+ 0x178, RW 0x6C7518), the game slot defeat frames (RW 0x809370 ..) and RW 0x625F7C, the map reveal of a defeated player (RW 0xB4D940). END-2: MSG_SELF_DESTRUCT "
		"(RW 0x77CA3D, the quit menu's surrender / exit) kills the sender; the asset transfer to a living ally of the exit (RW 0x6AF598) is MP-2's port (S-1122), and the Living World "
		"branch of the surrender (RW 0x77CB05 ..) is not ported",
	};
}

std::vector<std::string> VictoryConditions::report() const
{
	std::vector<std::string> out = stops();
	std::string eliminated;
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		if (m_isDefeated[i])
		{
			eliminated += (eliminated.empty() ? "" : ", ") + std::to_string(m_players[i]->getPlayerIndex());
		}
	}
	out.push_back("[S-344 counters] cached players " + std::to_string(m_cached) + ", eliminated {" + eliminated + "}, single alliance " + (m_singleAllianceRemaining ? "from frame " + std::to_string(m_endFrame) : std::string("no")) +
		", objects killed " + std::to_string(m_objectsKilled) + ", rule fallbacks " + std::to_string(m_ruleUnported) + ", elimination bonuses " + std::to_string(m_eliminationBonuses) + ", self-destructs " + std::to_string(m_selfDestructs) +
		", asset transfers not ported " + std::to_string(m_transfersUnported));
	return out;
}
