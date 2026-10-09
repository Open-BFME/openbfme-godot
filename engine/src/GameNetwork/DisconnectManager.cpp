// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameNetwork/DisconnectManager.h.

#include "GameNetwork/DisconnectManager.h"

#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Network.h"

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
			c.releasedMask |= (std::uint8_t)(1u << i);
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
		// the GameSpy ping test (RW 0x8D7C78) passes without pings: the spell is the silent slot's own
		if (!m_silentFlag[(size_t)i])
		{
			m_silentFlag[(size_t)i] = true;
			++m_silentEpisodes[(size_t)i];
			note("slot " + std::to_string(i) + " silent at frame " + std::to_string(logicFrame) + " (spell " + std::to_string(m_silentEpisodes[(size_t)i]) + ")");
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
		if (net.gameRunning() && m_state == SCREENOFF)
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
		if ((remaining < timeout / 3 || isPlayerVotedOut(net, t, now, logicFrame)) && (!m_notifiedValid || logicFrame != m_lastNotifiedFrame))
		{
			notifyOthersOfCurrentFrame(net, logicFrame);
			m_lastNotifiedFrame = logicFrame;
			m_notifiedValid = true;
		}
		if ((std::int32_t)remaining < 0 || isPlayerVotedOut(net, t, now, logicFrame) || m_silentEpisodes[(size_t)i] > kMaxSilentEpisodes - 1)
		{
			remaining = 0;
			std::uint8_t gone = 0;
			if (allOnSameFrame(net, now, logicFrame) && isLocalPlayerNextPacketRouter(net, now, logicFrame)
				&& (!net.isLeftState(i) || i == net.packetRouterSlot()) && net.dropClaims(local, i, gone))
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
				note("dropping slot " + std::to_string(i) + " at frame " + std::to_string(frame) + " (next packet router)");
				m_dropped.push_back(i);
				disconnectPlayer(net, i, frame);
				net.sendPlayerDestruct(i);
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
		const int verdict = net.verifyDropClaims(from, c.targetSlot, c.releasedMask);
		if (verdict < 0)
		{
			net.reportError("DISCONNECTPLAYER of slot " + std::to_string(c.targetSlot) + " from slot " + std::to_string(from)
				+ ", which may not decide it (a slot before it in the fallback order is connected for it): refused");
			return;
		}
		m_pendingDrops.push_back(PendingDrop{ from, (int)c.targetSlot, c.frame, c.releasedMask });
		applyPendingDrops(net, logicFrame);
		return;
	}
	case NETCOMMANDTYPE_DISCONNECTFRAME:
	{
		// RW 0x8D836B
		if (from < 0 || from >= MAX_SLOTS || m_disconnectFrames[(size_t)from] >= c.frame)
		{
			return;
		}
		resetPlayersVotes(net, from, c.frame - 1);
		m_disconnectFrames[(size_t)from] = c.frame;
		m_disconnectFramesReceived[(size_t)from] = true;
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
			// OpenBFME (S-1121): the next packet router with its screen off releases the announcement at once
			if (m_state == SCREENOFF && nextPacketRouter(net, now, logicFrame) == local)
			{
				NetCommandMsg r;
				r.type = NETCOMMANDTYPE_DISCONNECTSCREENOFF;
				r.slot = (std::uint8_t)local;
				r.frame = logicFrame;
				r.releasedFrames.assign(MAX_SLOTS, 0u);
				r.releasedMask = (std::uint8_t)(1u << from);
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
		const int verdict = net.verifyDropClaims(d.sender, d.target, d.gone);
		if (verdict == 0)
		{
			++k; // a claimed leave / drop is still on its way
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
		"(none here but OpenBFME's announced-frame rule), the disconnect chat",
		"[S-1122] self destruct: MSG_SELF_DESTRUCT (RW 0x77CA3D) and transferAssetsFromThat (RW 0x6AF598) are ported without the contain interface's "
		"occupant hand-over (vslot 0x54), the RW 0xDE4AB8 lists, the end of a defection (RW 0x69ABA7) and the Living World branch; the objects change "
		"owner in object list order (RotWK: team, then member order); a horde member follows its container (inferred)",
		"[S-1123] disconnect screen: DisconnectScreen.apt gets RotWK's texts, colour providers, SetBarPercent / Show / HideKickButton and its Kick / Quit "
		"commands (RW 0x91988F); not ported: the chat (InitGadgets, Chat::OnBttnEnterText, Quit's disconnect chat), the in-game UI calls of show "
		"(RW 0x9190F0), the level (the first free one)",
		"[S-1124] desync dump: RW 0x6290C7's DESYNC-Frame<n>-<exe>-<player>.txt holds OpenBFME's per-subsystem breakdown and object hashes (not "
		"retail's CRC text lines), the replay is copied next to it (retail embeds it), no BIN_DESYNC; the message box (GUI:DesyncTitle / "
		"GUI:DesyncText) is drawn by the presentation layer, RW 0x77D251 / 0x602FFE after it are not ported",
		"[S-1125] replays: every skirmish and LAN game is recorded (OpenBFME replays, S-722) to the user data's Replays/<GUI:LastReplay>.replay and plays back "
		"from Load Replay with every frame's hash compared; retail's Last Replay copy-on-save, Save Replay on the score screen and the replay camera "
		"(MSG_SET_REPLAY_CAMERA, UseCameraInReplays) are not ported",
		"[S-1126] load screen: SaveLoad.apt's replay page is ported (AptSaveLoad, RW 0x81874E / 0x818D41: the providers, the two lists' columns, Load); "
		"the saved-game pages, Save, Delete and the confirmation box are not; the list's date and time are the files' (OpenBFME replays carry none), "
		"the gadget names are compared after their '~' prefix (inferred)",
	};
}
