// OpenBFME. GPL-3.0.
// See GameNetwork/StrategicSession.h.

#include "GameNetwork/StrategicSession.h"

#include "Common/MpGameRules.h"
#include "GameLogic/GameMessage.h"

namespace
{
bool fail(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}

// the slot's name for the war's player (ASCII-safe: a code unit above 0x7F becomes UTF-8)
std::string utf8(const std::u16string &s)
{
	std::string out;
	for (char16_t c : s)
	{
		if (c < 0x80)
		{
			out += (char)c;
		}
		else if (c < 0x800)
		{
			out += (char)(0xC0 | (c >> 6));
			out += (char)(0x80 | (c & 0x3F));
		}
		else
		{
			out += (char)(0xE0 | (c >> 12));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
	}
	return out;
}
} // namespace

bool BuildWarStart(const SkirmishGameInfo &g, const LivingWorld::LivingWorldStores &stores, const std::vector<std::string> &playerTemplateNames, WarStart &out,
	std::string *error)
{
	out = WarStart();
	out.slotPlayer.fill(-1);
	if (!g.warOfTheRing())
	{
		return fail(error, "the game is not a War of the Ring (game mode " + std::to_string(g.gameMode) + ")");
	}
	const LivingWorld::Campaign *campaign = stores.findCampaign(g.scenario);
	if (!campaign || !campaign->scenario)
	{
		return fail(error, "no LivingWorldCampaign with a Scenario named '" + g.scenario + "'");
	}
	const LivingWorld::RegionCampaign *regions = stores.findRegionCampaign(campaign->scenario->regionCampaign);
	if (!regions)
	{
		return fail(error, "the scenario '" + g.scenario + "' names the unknown region campaign '" + campaign->scenario->regionCampaign + "'");
	}
	out.setup.campaign = g.scenario;
	out.setup.seed = g.seed; // InitGameLogicRandom(game + 0x50) right after MSG 0x1F (RW 0x6D3261)
	// the Rules tab's victory type (+ 0x74) and phase timer (+ 0x78); a game with no rule block keeps the defaults (victory type 0, no timer)
	if (!MpGameRules::isNone(g.rules))
	{
		out.setup.victoryType = g.rules[RULE_VICTORY_TYPE];
		out.phaseTimerSeconds = g.rules[RULE_STRATEGIC_PHASE_TIMER];
		out.battleChoice = g.rules[RULE_BATTLE_CHOICE];
		if (out.phaseTimerSeconds < 0)
		{
			return fail(error, "the strategic phase timer " + std::to_string(out.phaseTimerSeconds) + " s");
		}
	}
	for (int i = 0; i < g.serializedSlotCount(); ++i)
	{
		const SkirmishGameSlot &s = g.slots[i];
		if (s.state < SLOT_EASY_AI) // RW 0x932EB8 takes the slots that are not closed; an open slot has no player (FUN_008009a7 / the occupied test)
		{
			continue;
		}
		const std::string slot = "slot " + std::to_string(i);
		// RW 0x932EB8: a random faction (-1) is resolved there (RW 0x9326A3), an observer (-2) plays FactionObserver; the host's start checks refuse both in
		// a war (GUI:DisabledFaction, RW 0x8431B8) [S-1338: neither reaches the port's war]
		if (s.playerTemplate < 0 || s.playerTemplate >= (int)playerTemplateNames.size())
		{
			return fail(error, slot + " has no faction (" + std::to_string(s.playerTemplate) + ")");
		}
		// RW 0x932EB8: a slot whose start is -1 gets no player; the host's start checks refuse it (GUI:NeedStartSpots) and so does the port
		if (s.startPos < 0 || s.startPos >= (int)regions->regions.size())
		{
			return fail(error, slot + " has no start region (" + std::to_string(s.startPos) + ")");
		}
		LivingWorld::SetupSlot ss;
		ss.name = utf8(s.name);
		ss.faction = playerTemplateNames[(size_t)s.playerTemplate];
		ss.human = s.state == SLOT_PLAYER;
		ss.aiDifficulty = s.state; // RW 0x6E189C(slot + 4)
		ss.team = s.teamNumber;    // + 0x1C
		ss.color = s.color;        // + 0xC (a random colour, -1, is resolved by RW 0x932A8D: S-1338)
		ss.handicap = -s.handicap; // -(+ 0x20)
		ss.startRegion = regions->regions[(size_t)s.startPos].name;
		out.slotPlayer[(size_t)i] = (int)out.setup.slots.size(); // slot + 0x4C = the player's id (AddPlayer RW 0x6BB3B5)
		out.setup.slots.push_back(ss);
	}
	return true;
}

StrategicSession::StrategicSession(NetTransport &transport, NetworkConfig config, LivingWorld::LivingWorldLogic &logic, int phaseTimerSeconds, int battleChoice)
	: m_logic(logic)
{
	config.messages = NetPacket::MessageFamily::Strategic;
	m_localPlayer = config.localSlot >= 0 && config.localSlot < MAX_SLOTS ? config.slotPlayerIndex[(size_t)config.localSlot] : -1;
	m_network = std::make_unique<Network>(config, transport);
	m_logic.setNetworked(phaseTimerSeconds, battleChoice);
	m_network->sendLoadComplete();
}

bool StrategicSession::post(const GameMessage &m, std::string *error)
{
	if ((m.getType() < MSG_LW_FIRST || m.getType() > MSG_LW_LAST) && m.getType() != MSG_LW_OPENBFME_SAVE)
	{
		return fail(error, "message type " + std::to_string(m.getType()) + " is not a strategic order");
	}
	GameMessage copy(m);
	copy.friend_setPlayerIndex(m_localPlayer);
	if (m.getType() == MSG_LW_DONE_PHASE)
	{
		m_doneSentTurn = m_logic.turn(); // L + 0x10A: the button and the timer send it once
		m_doneSentPhase = m_logic.phase();
	}
	m_local.append(copy);
	return true;
}

void StrategicSession::run(const GameMessage &m, int sender)
{
	Executed e;
	e.frame = m_frame;
	e.sender = sender;
	e.type = m.getType();
	e.ok = m_logic.dispatch(m, sender, &e.error);
	m_executed.push_back(e);
}

bool StrategicSession::step()
{
	m_network->update(m_frame, m_local);
	// (Sol wotr2m8s2b2) the others dropped this client (or its own connection failed): ZH processDisconnectPlayer, "being dropped ourselves ends our game":
	// no further frame runs, not even the drop's frame (the others run it without this player: running it here would be a war of its own)
	if (m_network->quitRequested())
	{
		return false;
	}
	if (!m_network->allLoaded() || m_network->pausedBefore(m_frame) || !m_network->isFrameReady(m_frame) || !m_network->allowedToContinue(m_frame))
	{
		return false;
	}
	CommandList commands;
	m_network->relayCommands(m_frame, commands);
	for (const GameMessage &m : commands.messages())
	{
		if (m.getType() == MSG_LOGIC_CRC)
		{
			continue; // compared by the Network when the frame was relayed
		}
		if (m.getType() == MSG_SELF_DESTRUCT)
		{
			// the disconnect path dropped the player: its 0x6B8 (the next packet router's, RW 0x8D8735), from the player itself
			const int player = m.getPlayerIndex();
			if (player >= 0 && player < (int)m_logic.players().size() && !m_logic.players()[(size_t)player].defeated)
			{
				GameMessage surrender(MSG_LW_PLAYER_SURRENDERED, player);
				surrender.appendIntegerArgument(player);
				run(surrender, player);
			}
			continue;
		}
		run(m, m.getPlayerIndex());
	}
	m_logic.update();
	// UpdateTurnPhase RW 0x6BE283: the timer ran out: the local player's DONE_PHASE through the end-phase gate RW 0x6B6ACC, once per phase (L + 0x10A)
	if (m_logic.phaseTimerExpired() && m_logic.mayEndPhase(m_localPlayer) && !(m_doneSentTurn == m_logic.turn() && m_doneSentPhase == m_logic.phase()))
	{
		GameMessage done(MSG_LW_DONE_PHASE, m_localPlayer);
		done.appendIntegerArgument(m_logic.turn());
		done.appendIntegerArgument(m_logic.phase());
		done.appendIntegerArgument(m_localPlayer);
		post(done);
	}
	m_logic.advanceFrame(); // L + 0x100++ (RW 0x6BE50E's last step)
	++m_frame;
	FrameCompletion c;
	c.batchFrame = m_frame - 1;
	c.frame = m_frame;
	c.hashed = true;
	c.hash = m_logic.stateHash();
	c.breakdown = true;
	GameLogic::StateHashSection section;
	section.name = "LivingWorldLogic";
	section.value = c.hash;
	section.chainAfter = c.hash;
	c.sections.push_back(section);
	m_hashes.push_back(c.hash);
	m_network->frameCompleted(c, m_local);
	m_network->update(m_frame, m_local);
	return true;
}

void StrategicSession::leave()
{
	// the quit menu on the map (U2's 0x6B8 sender a): the local player's surrender goes out before the leave, so it runs on every peer
	if (m_localPlayer >= 0)
	{
		GameMessage surrender(MSG_LW_PLAYER_SURRENDERED, m_localPlayer);
		surrender.appendIntegerArgument(m_localPlayer);
		post(surrender);
		m_network->update(m_frame, m_local);
	}
	m_network->leave(m_frame);
}

std::vector<std::string> StrategicSession::errors() const
{
	std::vector<std::string> out = m_errors;
	for (const std::string &e : m_network->errors())
	{
		out.push_back("network: " + e);
	}
	return out;
}
