// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameNetwork/DisconnectManager.h.

#include "GameNetwork/DisconnectManager.h"

#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Network.h"

#include <algorithm>
#include <string>

namespace
{
constexpr std::uint32_t kLaggingMs = 5000;          // RW 0x8D8AD0: FUN_008d33c7(slot, 5000)
constexpr std::uint32_t kStartupFrames = 6;         // RW 0xBFD298: the silence limit is four times as long below this frame
constexpr std::uint32_t kKeepAliveMs = 500;         // RW 0x8D7CF0
constexpr int kMaxSilentEpisodes = 5;               // RW 0x8D8C0B (< 5) / 0x8D8828 (> 4)
constexpr int kKickShowBelow = 0x5A;                // RW 0x8D8963: below 90 % ShowKickButton
constexpr int kKickHideAbove = 0x5F;                // RW 0x8D8977: above 95 % HideKickButton
constexpr std::uint64_t kReannounceMs = 500;        // OpenBFME (S-1121): a released announcement is repeated at most this often
} // namespace

DisconnectManager::DisconnectManager()
{
	init(NetworkSettings());
}

void DisconnectManager::init(const NetworkSettings &settings)
{
	m_settings = settings;
	m_state = SCREENOFF;
	m_lastFrame = 0;
	m_lastFrameTime = -1;
	m_lastKeepAliveSendTime = -1;
	m_playerTimeouts.fill(0);
	for (auto &row : m_playerVotes)
	{
		for (Vote &v : row)
		{
			v = Vote();
		}
	}
	m_disconnectFrames.fill(0);
	m_disconnectFramesReceived.fill(false);
	m_announcedBy.fill(false);
	m_announcedByFrame.fill(0);
	m_lastNotifiedFrame = 0;
	m_notifiedValid = false;
	m_timeOfDisconnectScreenOn = 0;
	m_anySilent = false;
	m_silentEpisodes.fill(0);
	m_silentFlag.fill(false);
	m_announced = false;
	m_announcementReleased = false;
	m_screen = Screen();
	m_quitReason.clear();
	m_dropWait.fill(std::string());
	m_announcedEver = false;
	m_loadScreenUp = true;
	m_decided.clear();
}

int DisconnectManager::translatedSlotPosition(int slot, int localSlot)
{
	// RW 0x8D7D7E
	if (localSlot <= slot)
	{
		if (slot == localSlot)
		{
			return -1;
		}
		return slot - 1;
	}
	return slot;
}

int DisconnectManager::untranslatedSlotPosition(int slot, int localSlot)
{
	// RW 0x8D7D90
	if (slot == -1)
	{
		return localSlot;
	}
	return localSlot <= slot ? slot + 1 : slot;
}

void DisconnectManager::note(const std::string &line)
{
	if (m_log.size() < 200)
	{
		m_log.push_back(line);
	}
}

bool DisconnectManager::heardWithin(Network &net, int slot, std::uint32_t ms, std::uint64_t now) const
{
	// RW 0x8D33C7 / 0x8D335D: the local slot and a slot without a connection count as heard; the first query of a connection starts its clock
	if (slot == net.config().localSlot || !net.hasConnection(slot))
	{
		return true;
	}
	const std::uint64_t last = net.lastHeard(slot, now);
	return now - last <= (std::uint64_t)ms;
}

bool DisconnectManager::heard(Network &net, int slot, std::uint64_t now, std::uint32_t logicFrame) const
{
	std::uint32_t limit = m_settings.disconnectTime;
	if (logicFrame < kStartupFrames)
	{
		limit <<= 2;
	}
	return heardWithin(net, slot, limit, now);
}

bool DisconnectManager::voterAbsent(Network &net, int slot, std::uint64_t now, std::uint32_t logicFrame) const
{
	// RW 0x8D7F1E (with a connection manager): not (connected and heard)
	return !(slot >= 0 && slot < MAX_SLOTS && net.isPlayerConnected(slot) && heard(net, slot, now, logicFrame));
}

int DisconnectManager::countVotes(Network &net, int slot, std::uint64_t now, std::uint32_t logicFrame) const
{
	// RW 0x8D7F77
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return 0;
	}
	int n = 0;
	for (int v = 0; v < MAX_SLOTS; ++v)
	{
		if (m_playerVotes[slot][v].vote && !voterAbsent(net, v, now, logicFrame) && !net.isLeftState(v))
		{
			++n;
		}
	}
	return n;
}

int DisconnectManager::countVotesForPlayer(Network &net, int slot, std::uint64_t now) const
{
	return countVotes(net, slot, now, m_lastFrame);
}

bool DisconnectManager::isPlayerVotedOut(Network &net, int t, std::uint64_t now, std::uint32_t logicFrame) const
{
	// RW 0x8D826F with the eligible voters of RW 0x8D8233 (left state 1 is never set here)
	if (t == -1)
	{
		return false;
	}
	const int slot = untranslatedSlotPosition(t, net.config().localSlot);
	const int votes = countVotes(net, slot, now, logicFrame);
	int eligible = 0;
	for (int v = 0; v < MAX_SLOTS; ++v)
	{
		if (v != slot && !voterAbsent(net, v, now, logicFrame))
		{
			++eligible;
		}
	}
	return eligible <= votes;
}

bool DisconnectManager::hasPlayerTimedOut(int t, std::uint64_t now) const
{
	// RW 0x8D7E52
	if (t == -1)
	{
		return false;
	}
	return now - m_playerTimeouts[(size_t)t] >= (std::uint64_t)m_settings.playerTimeoutTime;
}

bool DisconnectManager::isPlayerInGame(Network &net, int t, std::uint64_t now, std::uint32_t logicFrame) const
{
	// RW 0x8D82BB
	const int slot = untranslatedSlotPosition(t, net.config().localSlot);
	return slot >= 0 && slot < MAX_SLOTS && net.isPlayerConnected(slot) && !isPlayerVotedOut(net, t, now, logicFrame) && !hasPlayerTimedOut(t, now);
}

bool DisconnectManager::allOnSameFrame(Network &net, std::uint64_t now, std::uint32_t logicFrame) const
{
	// RW 0x8D861A
	const int local = net.config().localSlot;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const int t = translatedSlotPosition(i, local);
		if (t == -1 || !net.isPlayerConnected(i) || !isPlayerInGame(net, t, now, logicFrame) || net.isLeftState(i) || !heard(net, i, now, logicFrame))
		{
			continue;
		}
		if (!m_disconnectFramesReceived[(size_t)i])
		{
			return false;
		}
		if (m_disconnectFrames[(size_t)local] != m_disconnectFrames[(size_t)i])
		{
			return false;
		}
	}
	return true;
}

int DisconnectManager::nextPacketRouter(Network &net, std::uint64_t now, std::uint32_t logicFrame) const
{
	// RW 0x8D86D0: from the current router along the fallback order (RW 0x8D3ECD), the first slot that is local, or in the game and not in a left state
	const int local = net.config().localSlot;
	int r = net.packetRouterSlot();
	for (int guard = 0; guard <= MAX_SLOTS; ++guard)
	{
		if (r < 0 || r >= MAX_SLOTS)
		{
			return -1;
		}
		const int t = translatedSlotPosition(r, local);
		if (t == -1 || (isPlayerInGame(net, t, now, logicFrame) && !net.isLeftState(r)))
		{
			return r;
		}
		r = net.nextPacketRouterSlot(r);
	}
	return -1;
}

bool DisconnectManager::isLocalPlayerNextPacketRouter(Network &net, std::uint64_t now, std::uint32_t logicFrame) const
{
	return nextPacketRouter(net, now, logicFrame) == net.config().localSlot;
}

void DisconnectManager::resetPlayerTimeouts(Network &net, std::uint64_t now)
{
	// RW 0x8D80CB
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const int t = translatedSlotPosition(i, net.config().localSlot);
		if (t != -1)
		{
			m_playerTimeouts[(size_t)t] = now;
		}
	}
}

void DisconnectManager::populateScreen(Network &net)
{
	// RW 0x8D847B
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const int t = translatedSlotPosition(i, net.config().localSlot);
		if (t == -1)
		{
			continue;
		}
		Screen::Row &row = m_screen.rows[(size_t)t];
		row.slot = i;
		row.name = net.playerName(i);
		row.used = !row.name.empty();
		row.votes = countVotes(net, i, m_lastFrameTime < 0 ? 0 : (std::uint64_t)m_lastFrameTime, m_lastFrame);
	}
}

void DisconnectManager::turnOnScreen(Network &net, std::uint64_t now)
{
	// RW 0x8D8516
	m_screen.visible = true;
	m_state = SCREENON;
	m_lastKeepAliveSendTime = -1;
	populateScreen(net);
	resetPlayerTimeouts(net, now);
	m_lastNotifiedFrame = 0;
	m_notifiedValid = false;
	m_timeOfDisconnectScreenOn = now;
	note("disconnect screen on at frame " + std::to_string(m_lastFrame));
}

void DisconnectManager::turnOffScreen(Network &net, std::uint32_t logicFrame)
{
	// RW 0x8D7CA6: hide, SCREENOFF, the local player's votes cleared, + 0x25C = 0
	if (m_state == SCREENOFF)
	{
		return;
	}
	m_screen.visible = false;
	m_state = SCREENOFF;
	const int local = net.config().localSlot;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		m_playerVotes[i][local].vote = false;
	}
	m_timeOfDisconnectScreenOn = 0;
	// ZH notifyOthersOfNewFrame (S-1121), carrying the announcements this peer releases; they are then forgotten (a drop needs fresh ones)
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_DISCONNECTSCREENOFF;
	c.slot = (std::uint8_t)local;
	c.frame = logicFrame;
	c.releasedFrames.assign(MAX_SLOTS, 0u);
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (m_disconnectFramesReceived[(size_t)i])
		{
			c.releasedMask |= (std::uint16_t)(1u << i);
			c.releasedFrames[(size_t)i] = m_disconnectFrames[(size_t)i];
		}
		m_disconnectFramesReceived[(size_t)i] = false;
	}
	net.sendDirect(c);
	if (m_announced && nextPacketRouter(net, net.now(), logicFrame) == local)
	{
		m_announcementReleased = true; // the decider released its own announcement by turning off
	}
	note("disconnect screen off at frame " + std::to_string(logicFrame));
}

void DisconnectManager::sendKeepAlive(Network &net, std::uint64_t now)
{
	// RW 0x8D7CE2: every 500 ms to every other slot
	if (m_lastKeepAliveSendTime == -1 || now - (std::uint64_t)m_lastKeepAliveSendTime > kKeepAliveMs)
	{
		NetCommandMsg c;
		c.type = NETCOMMANDTYPE_DISCONNECTKEEPALIVE;
		c.slot = (std::uint8_t)net.config().localSlot;
		net.sendDirect(c);
		m_lastKeepAliveSendTime = (long long)now;
	}
}

void DisconnectManager::notifyOthersOfCurrentFrame(Network &net, std::uint32_t logicFrame)
{
	// RW 0x8D3DEC: DISCONNECTFRAME of the current frame to every other slot, and processed locally (RW 0x8D8A43 -> processDisconnectFrame)
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_DISCONNECTFRAME;
	c.slot = (std::uint8_t)net.config().localSlot;
	c.frame = logicFrame;
	net.sendDirect(c);
	note("announced frame " + std::to_string(logicFrame));
	processDisconnectCommand(net, c, net.now(), logicFrame);
	m_announced = true;
	m_announcedEver = true;
	m_announcedFrame = logicFrame;
	m_announcementReleased = false;
	m_lastReannounce = net.now();
}

void DisconnectManager::applyDisconnectVote(Network &net, int slot, std::uint32_t frame, int fromSlot)
{
	// RW 0x8D8078
	if (slot < 0 || slot >= MAX_SLOTS || fromSlot < 0 || fromSlot >= MAX_SLOTS)
	{
		return;
	}
	m_playerVotes[slot][fromSlot].vote = true;
	m_playerVotes[slot][fromSlot].frame = frame;
	const int t = translatedSlotPosition(slot, net.config().localSlot);
	if (t != -1)
	{
		m_screen.rows[(size_t)t].votes = countVotes(net, slot, net.now(), m_lastFrame);
	}
	note("vote of slot " + std::to_string(fromSlot) + " to drop slot " + std::to_string(slot) + " (frame " + std::to_string(frame) + ")");
}

void DisconnectManager::resetPlayersVotes(Network &net, int playerID, std::uint32_t frame)
{
	// RW 0x8D7FD2: the votes CAST by playerID on or before `frame`
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (m_playerVotes[i][playerID].frame <= frame)
		{
			m_playerVotes[i][playerID].vote = false;
		}
	}
	const int t = translatedSlotPosition(playerID, net.config().localSlot);
	if (t != -1)
	{
		m_screen.rows[(size_t)t].votes = countVotes(net, playerID, net.now(), m_lastFrame);
	}
}

void DisconnectManager::voteForPlayerDisconnect(Network &net, int slot, std::uint32_t logicFrame)
{
	// RW 0x8D8194: once per slot; the vote command (RW 0x8D7DBB) carries TheGameLogic's frame
	const int local = net.config().localSlot;
	if (slot < 0 || slot >= MAX_SLOTS || slot == local || m_playerVotes[slot][local].vote)
	{
		return;
	}
	m_playerVotes[slot][local].vote = true;
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_DISCONNECTVOTE;
	c.slot = (std::uint8_t)local;
	c.targetSlot = (std::uint8_t)slot;
	c.frame = logicFrame;
	net.sendDirect(c);
	applyDisconnectVote(net, slot, logicFrame, local);
}

std::uint32_t DisconnectManager::getMaxDisconnectFrame() const
{
	std::uint32_t r = 0;
	for (std::uint32_t f : m_disconnectFrames)
	{
		r = f > r ? f : r;
	}
	return r;
}

void DisconnectManager::disconnectPlayer(Network &net, int slot, std::uint32_t frame)
{
	// RW 0x8D8553: the game slot marked, the recorder's note, the screen row removed, ConnectionManager::disconnectPlayer RW 0x8D57D8
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return;
	}
	const int t = translatedSlotPosition(slot, net.config().localSlot);
	if (t == -1)
	{
		return; // ZH: a disconnect of ourselves is ignored here (the Network handles being dropped)
	}
	m_screen.rows[(size_t)t].removed = true;
	note("slot " + std::to_string(slot) + " disconnected at frame " + std::to_string(frame));
	net.disconnectPlayer(slot, frame);
}

void DisconnectManager::playerHasAdvancedAFrame(int slot, std::uint32_t frame)
{
	if (slot >= 0 && slot < MAX_SLOTS && frame >= m_disconnectFrames[(size_t)slot])
	{
		m_disconnectFrames[(size_t)slot] = frame;
		m_disconnectFramesReceived[(size_t)slot] = false;
	}
}

std::string DisconnectManager::frameLine(Network &net, std::uint64_t now, std::uint32_t logicFrame) const
{
	// lane MP-DROP-1 (diagnostics): the disconnect frame each player still counted by allOnSameFrame announced ("-" none received)
	std::string line;
	const int local = net.config().localSlot;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const int t = translatedSlotPosition(i, local);
		if (t != -1 && (!net.isPlayerConnected(i) || !isPlayerInGame(net, t, now, logicFrame) || net.isLeftState(i) || !heard(net, i, now, logicFrame)))
		{
			continue;
		}
		line += (line.empty() ? "" : ", ") + std::string("slot ") + std::to_string(i) + " "
			+ (m_disconnectFramesReceived[(size_t)i] ? std::to_string(m_disconnectFrames[(size_t)i]) : std::string("-"));
	}
	return line;
}

void DisconnectManager::update(Network &net, std::uint64_t now, std::uint32_t logicFrame)
{
	const int local = net.config().localSlot;
	applyPendingDrops(net, logicFrame); // lane MP-2 (review): a held drop whose claimed leave / drop arrived since
	if (m_announced && logicFrame > m_announcedFrame)
	{
		m_announced = false; // the announced frame ran
	}
	int silent = 0;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (heard(net, i, now, logicFrame) || net.isLeftState(i) || !net.isPlayerConnected(i))
		{
			if (i != local)
			{
				m_silentFlag[(size_t)i] = false;
			}
			const int t = translatedSlotPosition(i, local);
			if (t != -1)
			{
				m_playerTimeouts[(size_t)t] = now; // RW 0x8D7DA6
			}
			continue;
		}
		++silent;
		// the ping test (RW 0x8D7C78(0xBD83D4)): passed, the spell is the silent slot's own; failed, it is the LOCAL slot's (the local link failed). A LAN
		// game has no pinger (it passes); lane ONLINE-3: through the relay it fails when nothing came from the relay for NetworkDisconnectTime
		if (!m_silentFlag[(size_t)i])
		{
			m_silentFlag[(size_t)i] = true;
			const int blamed = net.pingTest() ? i : local;
			++m_silentEpisodes[(size_t)blamed];
			note("slot " + std::to_string(i) + " silent at frame " + std::to_string(logicFrame) + " (spell " + std::to_string(m_silentEpisodes[(size_t)blamed])
				+ (blamed == local ? " of the local link: the ping test failed)" : ")"));
		}
	}
	if (silent == 0)
	{
		m_silentFlag[(size_t)local] = false;
	}
	if (silent < 1)
	{
		m_anySilent = false;
		turnOffScreen(net, logicFrame);
	}
	else
	{
		m_anySilent = true;
		// RW 0x8D8B82: the screen opens (and the local player quits, below) only once TheGameLogic + 0x120, the multiplayer load screen, is gone
		if (net.gameRunning() && m_state == SCREENOFF && !m_loadScreenUp)
		{
			bool quit = true;
			if (m_silentEpisodes[(size_t)local] < kMaxSilentEpisodes)
			{
				// RW 0x8D8BAF: more than one connected slot, and (in a game of kind 3) the game not decided for the local player (VictoryConditions
				// vslots 0x4C / 0x48 / 0x54)
				if (net.numConnected() > 1 && !net.gameDecidedForLocalPlayer())
				{
					turnOnScreen(net, now);
					quit = false;
				}
			}
			if (quit && m_quitReason.empty())
			{
				m_quitReason = m_silentEpisodes[(size_t)local] >= kMaxSilentEpisodes ? "the local connection failed five times"
					: net.numConnected() <= 1                                         ? "no other player is connected"
																					   : "the game is decided for the local player";
				note("quit: " + m_quitReason);
			}
		}
		sendKeepAlive(net, now);
	}
	// lane MP-DROP-1: RotWK's multiplayer load screen. GameLogic's loading update (RW 0x62B385) keeps it (only its update, RW 0x62550D) while TheNetwork
	// vslot 0xD0 (RW 0x65DA20: this manager's + 0x270, a slot is silent) is false and the logic frame is below 6, and deletes it (RW 0x625524)
	// otherwise. So a player that never finishes loading takes the others off the load screen once it is silent (four times NetworkDisconnectTime below
	// frame 6, RW 0x8D335D) and the screen above opens on the next update; a loader that is heard keeps every player on the load screen. Here the gate
	// is modelled in the manager (the Godot client draws its own loading screen: S-4811)
	if (m_loadScreenUp && (m_anySilent || logicFrame >= kStartupFrames))
	{
		m_loadScreenUp = false;
		note("load screen gone at frame " + std::to_string(logicFrame) + (m_anySilent ? " (a slot is silent)" : ""));
	}
	if (m_lastFrameTime == -1 || m_lastFrame != logicFrame)
	{
		m_lastFrame = logicFrame;
		m_lastFrameTime = (long long)now;
	}
	if (m_state != SCREENOFF)
	{
		updateDisconnectStatus(net, now, logicFrame);
	}
	// OpenBFME (S-1121): a released announcement is repeated while the screen stays on (a fresh one the router can act on)
	if (m_state == SCREENON && m_announced && m_announcementReleased && now - m_lastReannounce >= kReannounceMs)
	{
		m_notifiedValid = false;
	}
	(void)kLaggingMs;
}

void DisconnectManager::updateDisconnectStatus(Network &net, std::uint64_t now, std::uint32_t logicFrame)
{
	const int local = net.config().localSlot;
	const std::uint32_t timeout = m_settings.playerTimeoutTime;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (!net.isPlayerConnected(i))
		{
			continue;
		}
		const int t = translatedSlotPosition(i, local);
		if (t == -1)
		{
			continue;
		}
		std::uint32_t remaining = (std::uint32_t)(m_playerTimeouts[(size_t)t] - now) + timeout; // unsigned 32-bit (RW 0x8D8785)
		// lane ONLINE-3: a restarted slot is announced at once too (its drop does not wait for the bar)
		if ((remaining < timeout / 3 || isPlayerVotedOut(net, t, now, logicFrame) || net.slotRestarted(i)) && (!m_notifiedValid || logicFrame != m_lastNotifiedFrame))
		{
			notifyOthersOfCurrentFrame(net, logicFrame);
			m_lastNotifiedFrame = logicFrame;
			m_notifiedValid = true;
		}
		// lane ONLINE-3: a slot whose game restarted (a newer incarnation at the relay) is gone at once, as if its timeout had run out. A KICK (the
		// players voted it out with the Kick button) is a vote that carries it while more than a tenth of this decider's bar remains: the votes the
		// timeout casts come at 0 % (below), so a drop at the end of the bar is a timeout whoever's vote completed it (OpenBFME's rule, S-3900)
		const bool votedOut = isPlayerVotedOut(net, t, now, logicFrame);
		const bool kicked = votedOut && (std::int32_t)remaining > 0 && (std::uint64_t)remaining * 10u > (std::uint64_t)timeout && !net.slotRestarted(i);
		if ((std::int32_t)remaining < 0 || votedOut || m_silentEpisodes[(size_t)i] > kMaxSilentEpisodes - 1 || net.slotRestarted(i))
		{
			remaining = 0;
			std::uint16_t gone = 0;
			const bool sameFrame = allOnSameFrame(net, now, logicFrame);
			const int router = nextPacketRouter(net, now, logicFrame);
			const bool claims = sameFrame && router == local && net.dropClaims(local, i, gone);
			if (!(sameFrame && router == local && claims))
			{
				// lane MP-DROP-1: why a due drop waits (logged once per reason): the frames of the players still in, or another decider
				std::string why = !sameFrame ? "not every player is on one frame (" + frameLine(net, now, logicFrame) + ")"
					: router != local      ? "slot " + std::to_string(router) + " is the next packet router"
										   : "a slot before this one in the fallback order is connected here";
				why = "the drop of slot " + std::to_string(i) + " waits: " + why;
				if (why != m_dropWait[(size_t)i])
				{
					m_dropWait[(size_t)i] = why;
					note(why);
				}
			}
			if (sameFrame && router == local && claims)
			{
				notifyOthersOfCurrentFrame(net, logicFrame);
				const std::uint32_t frame = getMaxDisconnectFrame();
				NetCommandMsg c; // sendDisconnectCommand RW 0x8D80FC
				c.type = NETCOMMANDTYPE_DISCONNECTPLAYER;
				c.slot = (std::uint8_t)local;
				c.targetSlot = (std::uint8_t)i;
				c.frame = frame;
				c.releasedMask = gone; // lane MP-2 (review): the predecessors this decider holds gone
				net.sendDirect(c);
				if (m_decided.size() < 16)
				{
					m_decided.push_back(c); // lane MP-DROP-1: sent again to a player that announces a frame at or before it (it refused it: not announced yet)
				}
				note("dropping slot " + std::to_string(i) + " at frame " + std::to_string(frame) + " (next packet router)");
				m_dropped.push_back(i);
				disconnectPlayer(net, i, frame);
				if (net.config().holdForRejoin && !kicked)
				{
					// lane ONLINE-3 (online games): a timed-out player is held for a rejoin (its army stays its own); the packet router hands it over when
					// the rejoin window ends (Network::routerRejoinDuties). A vote drops it as retail does
					note("holding slot " + std::to_string(i) + " for a rejoin (no hand-over yet)");
				}
				else
				{
					net.sendPlayerDestruct(i);
				}
				m_announcementReleased = true;
			}
		}
		if (m_screen.visible)
		{
			const int pct = timeout == 0 ? 0 : (int)((std::uint64_t)remaining * 100u / timeout);
			Screen::Row &row = m_screen.rows[(size_t)t];
			row.barPercent = pct;
			if (pct < kKickShowBelow)
			{
				row.kickShown = true;
			}
			else if (pct > kKickHideAbove && row.kickShown)
			{
				row.kickShown = false;
			}
			if (pct == 0 && !isPlayerVotedOut(net, t, now, logicFrame) && net.isPlayerConnected(i))
			{
				if (!m_playerVotes[i][local].vote)
				{
					note("automatic vote for slot " + std::to_string(i) + " (its timeout ran out)");
				}
				voteForPlayerDisconnect(net, i, logicFrame); // TheNetwork vslot 0x98
			}
		}
	}
}

bool DisconnectManager::validateDrop(Network &net, const NetCommandMsg &c, int from, std::string &why) const
{
	// lane MP-DROP-1 (hostile input, OpenBFME's own, S-4812: RotWK's processDisconnectPlayer RW 0x8D89D7 disconnects the named slot with no check of its
	// sender or frame): the whole decision is checked
	// against what this peer knows, before anything is queued or changed. A decider sends DISCONNECTPLAYER only after announcing its own frame
	// (notifyOthersOfCurrentFrame right before it) and only when every in-game player it hears announced that same frame (RW 0x8D861A), the drop frame;
	// the decision lists the predecessors it holds gone (lane MP-2's review)
	const int local = net.config().localSlot;
	const int target = c.targetSlot;
	if (!net.isGameSlot(target) || target == from)
	{
		why = target == from ? "the sender drops itself" : "slot " + std::to_string(target) + " is not a player of the game";
		return false;
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (!(c.releasedMask & (1u << s)))
		{
			continue;
		}
		if (!net.isGameSlot(s) || s == target || s == from)
		{
			why = "its list of departed players names slot " + std::to_string(s) + (!net.isGameSlot(s) ? ", which is not a player of the game"
				: s == target ? ", the player it drops" : ", its sender");
			return false;
		}
		if (s == local && !net.hasDeparted(local))
		{
			why = "it claims this player is gone"; // a rejoined player is gone in the agreed order for good: that claim is true
			return false;
		}
	}
	// the sender's ACTUAL announcements (Sol r2: the zero a slot's record starts with, or a frame set by a DISCONNECTSCREENOFF, is not one)
	if (!m_announcedBy[(size_t)from] || m_announcedByFrame[(size_t)from] < c.frame)
	{
		why = "its sender never announced a disconnect frame at or after it to this player";
		return false;
	}
	if (target != local && (!m_announcedEver || m_announcedFrame != c.frame))
	{
		// this player is heard by the decider, or the decision could not count it: in both cases it announced the drop frame first. Refused here, the
		// decider sends the drop again when this player announces a frame at or before it (DISCONNECTFRAME above)
		why = "this player did not announce that frame (its last announcement: " + (m_announcedEver ? std::to_string(m_announcedFrame) : std::string("none")) + ")";
		return false;
	}
	return true;
}

void DisconnectManager::processDisconnectCommand(Network &net, const NetCommandMsg &c, std::uint64_t now, std::uint32_t logicFrame)
{
	const int local = net.config().localSlot;
	const int from = c.slot;
	switch (c.type)
	{
	case NETCOMMANDTYPE_DISCONNECTKEEPALIVE:
	{
		// ZH processDisconnectKeepAlive
		const int t = translatedSlotPosition(from, local);
		if (t != -1)
		{
			m_playerTimeouts[(size_t)t] = now;
		}
		return;
	}
	case NETCOMMANDTYPE_DISCONNECTVOTE:
	{
		// lane MP-DROP-1 (hostile input): a vote for the sender itself or for a slot that is not a network player of the game is refused by name
		// (RW 0x8D8314 checks only the sender; our own Kick never sends either: voteForPlayerDisconnect skips the local slot)
		if (c.targetSlot == from)
		{
			net.reportError("DISCONNECTVOTE of slot " + std::to_string(from) + " for itself: refused");
			return;
		}
		if (!net.isGameSlot(c.targetSlot))
		{
			net.reportError("DISCONNECTVOTE of slot " + std::to_string(from) + " for slot " + std::to_string(c.targetSlot) + ", which is not a player of the game: refused");
			return;
		}
		// RW 0x8D8314: only from an in-game sender
		const int t = translatedSlotPosition(from, local);
		if (isPlayerInGame(net, t, now, logicFrame))
		{
			applyDisconnectVote(net, c.targetSlot, c.frame, from);
		}
		return;
	}
	case NETCOMMANDTYPE_DISCONNECTPLAYER:
	{
		// lane MP-2 (review): only the decider of the agreed fallback order may drop a player (Network::verifyDropClaims): refused at once when the
		// message itself shows the sender is not it; held while a claimed leave / drop has not reached this peer (the frames from the drop frame on wait,
		// allowedToContinue); applied before the drop frame runs, else refused
		if (net.isGameSlot(c.targetSlot) && net.isDisconnected(c.targetSlot))
		{
			return; // lane MP-DROP-1: a drop this peer applied already (the decider sends it again to a player that announces an earlier frame)
		}
		{
			std::string why;
			if (!validateDrop(net, c, from, why))
			{
				net.reportError("DISCONNECTPLAYER of slot " + std::to_string(c.targetSlot) + " from slot " + std::to_string(from) + " at frame "
					+ std::to_string(c.frame) + ": " + why + ": refused");
				return;
			}
		}
		const int verdict = net.verifyDropClaims(from, c.targetSlot, c.releasedMask);
		if (verdict < 0)
		{
			net.reportError("DISCONNECTPLAYER of slot " + std::to_string(c.targetSlot) + " from slot " + std::to_string(from)
				+ ", which may not decide it (a slot before it in the fallback order is connected for it): refused");
			return;
		}
		m_pendingDrops.push_back(PendingDrop{ from, (int)c.targetSlot, c.frame, c.releasedMask, now });
		applyPendingDrops(net, logicFrame);
		return;
	}
	case NETCOMMANDTYPE_DISCONNECTFRAME:
	{
		// Why an equal frame here is always a new announcement of the sender (Sol r1): the record is closed (received false) only by the sender's own
		// DISCONNECTSCREENOFF of a frame at or after it, this peer's release of it, this peer's screen going off, or the slot's rejoin; a sender
		// announces a frame again only after its notify state was reset (a new screen spell, turnOnScreen, or OpenBFME's repeat after a release); each
		// sender's stream is reliable, ordered and deduplicated (Transport) and nobody forwards another player's DISCONNECTFRAME. So no duplicate or stale
		// copy can arrive after the record closed.
		// RW 0x8D836B ignores an announcement at or below the sender's known frame. lane MP-DROP-1 (S-4810): except the same frame announced again after
		// the sender's DISCONNECTSCREENOFF of that frame released it (received cleared): here a screen goes off when the silence ends, not when a frame
		// runs (S-1121), so a moment of traffic from the silent slot turns a screen off and on at one frame; with retail's rule the second announcement
		// was lost and "every player on one frame" (RW 0x8D861A) never held again: the game froze with every vote cast (ONLINE-5's S-4556); the first
		// announcement of frame 0 counts too (a player that dies on the load screen stalls the others at frame 0 here: retail's first frames need no commands)
		if (from < 0 || from >= MAX_SLOTS || m_disconnectFrames[(size_t)from] > c.frame
			|| (m_disconnectFrames[(size_t)from] == c.frame && m_disconnectFramesReceived[(size_t)from]))
		{
			return;
		}
		resetPlayersVotes(net, from, c.frame - 1);
		m_disconnectFrames[(size_t)from] = c.frame;
		m_disconnectFramesReceived[(size_t)from] = true;
		m_announcedBy[(size_t)from] = true; // lane MP-DROP-1 (Sol r2): an actual DISCONNECTFRAME of this sender (frame 0 counts), unlike the zero a slot starts with
		m_announcedByFrame[(size_t)from] = std::max(m_announcedByFrame[(size_t)from], c.frame);
		net.notePlayerLatestFrame(from, c.frame); // RW 0x8D4884
		if (from == local)
		{
			for (int i = 0; i < MAX_SLOTS; ++i)
			{
				if (i == from)
				{
					continue;
				}
				const int t = translatedSlotPosition(i, local);
				if (isPlayerInGame(net, t, now, logicFrame) && m_disconnectFrames[(size_t)i] < m_disconnectFrames[(size_t)from] && m_disconnectFramesReceived[(size_t)i])
				{
					net.sendFrameDataToPlayer(i, m_disconnectFrames[(size_t)i], logicFrame);
				}
			}
		}
		else
		{
			if (m_disconnectFrames[(size_t)from] < m_disconnectFrames[(size_t)local] && m_disconnectFramesReceived[(size_t)from])
			{
				net.sendFrameDataToPlayer(from, m_disconnectFrames[(size_t)from], logicFrame);
			}
			// lane MP-DROP-1: a player announcing a frame at or before a drop this peer decided has not applied it (it refuses a drop for a frame it
			// had not announced itself, validateDrop): the drop goes to it again, now that its announcement can match
			for (const NetCommandMsg &d : m_decided)
			{
				if (c.frame <= d.frame && from != (int)d.targetSlot)
				{
					net.sendDirectTo(from, d);
					note("sent the drop of slot " + std::to_string(d.targetSlot) + " at frame " + std::to_string(d.frame) + " again to slot " + std::to_string(from));
				}
			}
			// OpenBFME (S-1121): the next packet router with its screen off releases the announcement at once
			if (m_state == SCREENOFF && nextPacketRouter(net, now, logicFrame) == local)
			{
				NetCommandMsg r;
				r.type = NETCOMMANDTYPE_DISCONNECTSCREENOFF;
				r.slot = (std::uint8_t)local;
				r.frame = logicFrame;
				r.releasedFrames.assign(MAX_SLOTS, 0u);
				r.releasedMask = (std::uint16_t)(1u << from);
				r.releasedFrames[(size_t)from] = c.frame;
				m_disconnectFramesReceived[(size_t)from] = false;
				net.sendDirect(r);
			}
		}
		return;
	}
	case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
	{
		// ZH processDisconnectScreenOff
		if (from < 0 || from >= MAX_SLOTS)
		{
			return;
		}
		if (c.frame >= m_disconnectFrames[(size_t)from])
		{
			m_disconnectFramesReceived[(size_t)from] = false;
			m_disconnectFrames[(size_t)from] = c.frame;
			resetPlayersVotes(net, from, c.frame);
		}
		// OpenBFME (S-1121): the next packet router released our announcement
		if (m_announced && (c.releasedMask & (1u << local)) && (size_t)local < c.releasedFrames.size() && c.releasedFrames[(size_t)local] == m_announcedFrame
			&& nextPacketRouter(net, now, logicFrame) == from)
		{
			m_announcementReleased = true;
		}
		return;
	}
	default:
		return;
	}
}

void DisconnectManager::applyPendingDrops(Network &net, std::uint32_t logicFrame)
{
	const int local = net.config().localSlot;
	for (size_t k = 0; k < m_pendingDrops.size();)
	{
		const PendingDrop d = m_pendingDrops[k];
		if (net.isDisconnected(d.target))
		{
			// lane MP-DROP-1: the target was dropped meanwhile (by the decider of the agreed order): this held one decides nothing any more and must not
			// hold the frame
			m_pendingDrops.erase(m_pendingDrops.begin() + (std::ptrdiff_t)k);
			note("the drop of slot " + std::to_string(d.target) + " from slot " + std::to_string(d.sender) + " was discarded: the slot was dropped meanwhile");
			continue;
		}
		const int verdict = net.verifyDropClaims(d.sender, d.target, d.gone);
		if (verdict == 0)
		{
			// lane MP-DROP-1 (hostile input): a claimed departure of a player this peer still hears is false (forged): held, it froze this peer's
			// game for good with every player heard (no screen, no drop). Refused once a player's timeout has passed with a claimed-gone slot still
			// heard; a departure that is only late (its leave cut off on the way, lane MP-2's review case) is held as before
			bool falseClaim = false;
			if (net.now() - d.arrived > (std::uint64_t)m_settings.playerTimeoutTime)
			{
				for (int s = 0; s < MAX_SLOTS; ++s)
				{
					// a slot this peer never saw depart (a rejoined slot's departure is a fact: it may be heard again)
					falseClaim = falseClaim || ((d.gone & (1u << s)) && !net.hasDeparted(s) && net.hasConnection(s) && heard(net, s, net.now(), logicFrame));
				}
			}
			if (!falseClaim)
			{
				++k; // a claimed leave / drop is still on its way
				continue;
			}
			m_pendingDrops.erase(m_pendingDrops.begin() + (std::ptrdiff_t)k);
			net.reportError("DISCONNECTPLAYER of slot " + std::to_string(d.target) + " from slot " + std::to_string(d.sender)
				+ ": it claims a player is gone that this peer still hears after NetworkPlayerTimeoutTime: refused");
			continue;
		}
		m_pendingDrops.erase(m_pendingDrops.begin() + (std::ptrdiff_t)k);
		if (verdict < 0)
		{
			net.reportError("DISCONNECTPLAYER of slot " + std::to_string(d.target) + " from slot " + std::to_string(d.sender) + ": refused");
			continue;
		}
		if (!net.frameStillOpen(d.frame))
		{
			net.reportError("DISCONNECTPLAYER of slot " + std::to_string(d.target) + " for frame " + std::to_string(d.frame) + ", which this peer already ran: refused");
			continue;
		}
		// ZH processDisconnectPlayer; being dropped ourselves ends our game
		if (d.target == local)
		{
			if (m_quitReason.empty())
			{
				m_quitReason = "dropped by the other players (slot " + std::to_string(d.sender) + ")";
				note("quit: " + m_quitReason);
			}
			continue;
		}
		if (logicFrame != d.frame)
		{
			note("DISCONNECTPLAYER of slot " + std::to_string(d.target) + " for frame " + std::to_string(d.frame) + " arrived on frame " + std::to_string(logicFrame));
		}
		disconnectPlayer(net, d.target, d.frame);
		m_announcementReleased = true;
	}
}

void DisconnectManager::slotRejoined(Network &net, int slot)
{
	// lane ONLINE-3: the slot is a member again (Network::applyRejoin): its votes, disconnect frame, silent spells and screen row start afresh
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return;
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		m_playerVotes[slot][i] = Vote();
		m_playerVotes[i][slot] = Vote();
	}
	m_disconnectFrames[(size_t)slot] = 0;
	m_disconnectFramesReceived[(size_t)slot] = false;
	m_announcedBy[(size_t)slot] = false;
	m_announcedByFrame[(size_t)slot] = 0;
	m_silentFlag[(size_t)slot] = false;
	m_silentEpisodes[(size_t)slot] = 0;
	m_dropWait[(size_t)slot].clear();
	const int t = translatedSlotPosition(slot, net.config().localSlot);
	if (t != -1)
	{
		m_screen.rows[(size_t)t].removed = false;
		m_screen.rows[(size_t)t].votes = 0;
		m_screen.rows[(size_t)t].barPercent = 100;
		m_screen.rows[(size_t)t].kickShown = false;
		m_playerTimeouts[(size_t)t] = net.now();
	}
	m_dropped.erase(std::remove(m_dropped.begin(), m_dropped.end(), slot), m_dropped.end());
	note("slot " + std::to_string(slot) + " rejoined");
}

bool DisconnectManager::allowedToContinue(Network &net, std::uint32_t frame) const
{
	for (const PendingDrop &d : m_pendingDrops)
	{
		if (d.frame <= frame)
		{
			return false; // lane MP-2 (review): a drop decided for this frame or an earlier one waits for a claimed leave / drop
		}
	}
	// no screen gate (see the header: RotWK's screen follows silence, not the frame; a peer that is behind must run the frames relayed to it)
	if (!m_announced || m_announcementReleased || frame != m_announcedFrame)
	{
		return true;
	}
	// an in-game peer already ran the announced frame (its announcement is later): a drop can only come at a later frame
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (i != net.config().localSlot && m_disconnectFrames[(size_t)i] > frame && net.isPlayerConnected(i))
		{
			return true;
		}
	}
	return false;
}

std::vector<std::string> DisconnectManager::stopLines()
{
	return {
		"[S-1121] disconnect path: RotWK's DisconnectManager is ported (RW 0x8D7C78 .. 0x8D8DDC); not located or not ported: the sender of "
		"DISCONNECTSCREENOFF (type 29 inferred, ZH's notifyOthersOfNewFrame kept), the GameSpy ping test (no pinger in a LAN game), TheGameLogic + 0x2A4, "
		"the left state 1 (ConnectionManager + 0x12080), the router fallback order's filler (here the network slots in slot order), RotWK's frame gate "
		"(none here but OpenBFME's announced-frame rule)",
		"[S-1122] self destruct: MSG_SELF_DESTRUCT (RW 0x77CA3D) and transferAssetsFromThat (RW 0x6AF598) are ported without the contain interface's "
		"occupant hand-over (vslot 0x54), the RW 0xDE4AB8 lists, the end of a defection (RW 0x69ABA7) and the Living World branch; the objects change "
		"owner in object list order (RotWK: team, then member order); a horde member follows its container (inferred)",
		"[S-1123] disconnect screen: DisconnectScreen.apt gets RotWK's texts, colour providers, SetBarPercent / Show / HideKickButton and its Kick / Quit "
		"commands (RW 0x91988F) and its chat (lane CHAT-1: DISCONNECTCHAT 13); not ported: the in-game UI calls of show "
		"(RW 0x9190F0), the level (the first free one)",
		"[S-1124] desync dump: RW 0x6290C7's DESYNC-Frame<n>-<exe>-<player>.txt holds OpenBFME's per-subsystem breakdown and object hashes (not "
		"retail's CRC text lines), the replay is copied next to it (retail embeds it), no BIN_DESYNC; the message box (GUI:DesyncTitle / "
		"GUI:DesyncText) is drawn by the presentation layer, RW 0x77D251 / 0x602FFE after it are not ported",
		"[S-1125] replays: every skirmish and LAN game is recorded (OpenBFME replays, S-722) to the user data's Replays/<GUI:LastReplay>.replay and plays back "
		"from Load Replay with every frame's hash compared; retail's Last Replay copy-on-save, Save Replay on the score screen and the replay camera "
		"(MSG_SET_REPLAY_CAMERA, UseCameraInReplays) are not ported",
		"[S-1126] load screen: SaveLoad.apt's replay page is ported (AptSaveLoad, RW 0x81874E / 0x818D41: the providers, the two lists' columns, Load); "
		"the saved-game pages, Save, Delete and the confirmation box are lane SAVE-2's (S-4003); the list's date and time are the files' (OpenBFME replays carry none), "
		"the gadget names are compared after their '~' prefix (inferred)",
		"[S-4810] repeated disconnect frame: OpenBFME counts a player's announcement of the frame it announced before once that announcement was closed (its "
		"DISCONNECTSCREENOFF, this peer's release or screen-off; RW 0x8D836B ignores any announcement at or below the known frame): here a screen goes off "
		"when the silence ends, not when a frame runs, and with retail's rule a crash could freeze the game for good; a game whose packet router runs a "
		"client without this rule still freezes that way (every player must update)",
		"[S-4811] loading: OpenBFME's client sends nothing while it loads (RotWK's handles NETCOMMANDTYPE_PROGRESS), so a player still loading four times "
		"NetworkDisconnectTime after the others reach the start is silent to them: they leave the load screen (RW 0x62B385), the disconnect screen "
		"opens and its bar can drop it",
		"[S-4812] drop validation: OpenBFME refuses a DISCONNECTPLAYER the whole decision does not support (its target, its departed list, a frame its "
		"sender announced and this player announced; RotWK's RW 0x8D89D7 takes it on its sender's word); the decider sends a refused one again when that player "
		"announces the frame",
	};
}
