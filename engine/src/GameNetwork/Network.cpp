// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameNetwork/Network.h.

#include "GameNetwork/Network.h"

#include "GameNetwork/Transport.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace
{
constexpr size_t kMaxReportObjects = 4000; // the objects of one DESYNCREPORT (a UDP datagram stays below 64 KiB)
constexpr size_t kKeptHalves = 8;          // the CRC frames whose breakdown is kept for a report
constexpr UnsignedInt kFramesToKeep = 65;  // lane MP-2: FRAMES_TO_KEEP (RW 0xDB824C), the frames sendFrameDataToPlayer can relay

std::string hex32(std::uint32_t v)
{
	char b[16];
	std::snprintf(b, sizeof(b), "0x%08X", v);
	return b;
}
} // namespace

// ---- DesyncHalf / DesyncReport ---------------------------------------------------------------------------------------------
std::vector<std::uint8_t> DesyncHalf::encode() const
{
	NetByteWriter w;
	w.i32(slot);
	w.u32(frame);
	w.u32(hash);
	w.u32((std::uint32_t)sections.size());
	for (const GameLogic::StateHashSection &s : sections)
	{
		w.str(s.name);
		w.u32(s.value);
	}
	const size_t n = std::min(objects.size(), kMaxReportObjects);
	w.u32((std::uint32_t)n);
	for (size_t i = 0; i < n; ++i)
	{
		w.u32(objects[i].id);
		w.str(objects[i].templateName);
		w.u32(objects[i].value);
	}
	return w.take();
}

bool DesyncHalf::decode(const std::vector<std::uint8_t> &bytes, DesyncHalf &out, std::string *error)
{
	NetByteReader r(bytes);
	out = DesyncHalf();
	out.slot = r.i32();
	out.frame = r.u32();
	out.hash = r.u32();
	const std::uint32_t ns = r.u32();
	for (std::uint32_t i = 0; i < ns && !r.failed(); ++i)
	{
		GameLogic::StateHashSection s;
		s.name = r.str();
		s.value = r.u32();
		out.sections.push_back(s);
	}
	const std::uint32_t no = r.u32();
	for (std::uint32_t i = 0; i < no && !r.failed(); ++i)
	{
		GameLogic::ObjectStateHash o;
		o.id = r.u32();
		o.templateName = r.str();
		o.value = r.u32();
		out.objects.push_back(o);
	}
	if (r.failed() || !r.atEnd())
	{
		if (error)
		{
			*error = "malformed desync report half";
		}
		return false;
	}
	return true;
}

std::vector<std::string> DesyncReport::differingSections() const
{
	std::vector<std::string> out;
	if (halves.size() < 2)
	{
		return out;
	}
	const DesyncHalf &first = halves.begin()->second;
	for (size_t i = 0; i < first.sections.size(); ++i)
	{
		for (const auto &kv : halves)
		{
			const DesyncHalf &h = kv.second;
			if (i >= h.sections.size() || h.sections[i].name != first.sections[i].name || h.sections[i].value != first.sections[i].value)
			{
				out.push_back(first.sections[i].name);
				break;
			}
		}
	}
	return out;
}

std::string DesyncReport::text() const
{
	std::ostringstream o;
	o << "DESYNC: the state hash of logic frame " << frame << " differs (CRCs compared on frame " << checkedOnFrame << ")\n";
	for (const auto &kv : crcs)
	{
		o << "  slot " << kv.first << ": " << hex32(kv.second) << "\n";
	}
	if (halves.size() < 2)
	{
		o << "  per-subsystem breakdown: " << halves.size() << " of " << crcs.size() << " halves arrived\n";
		return o.str();
	}
	o << "  per-subsystem hashes:\n";
	o << "    " << "subsystem";
	for (const auto &kv : halves)
	{
		o << "  slot " << kv.first;
	}
	o << "\n";
	const DesyncHalf &first = halves.begin()->second;
	const std::vector<std::string> diff = differingSections();
	for (size_t i = 0; i < first.sections.size(); ++i)
	{
		o << "    " << first.sections[i].name;
		for (const auto &kv : halves)
		{
			o << "  " << (i < kv.second.sections.size() ? hex32(kv.second.sections[i].value) : std::string("-"));
		}
		if (std::find(diff.begin(), diff.end(), first.sections[i].name) != diff.end())
		{
			o << "  <== DIFFERS";
		}
		o << "\n";
	}
	// the objects: by id, the first few that differ or exist on one side only
	std::map<ObjectID, std::map<int, const GameLogic::ObjectStateHash *>> byId;
	for (const auto &kv : halves)
	{
		for (const GameLogic::ObjectStateHash &x : kv.second.objects)
		{
			byId[x.id][kv.first] = &x;
		}
	}
	int shown = 0, total = 0;
	for (const auto &kv : byId)
	{
		bool differs = kv.second.size() != halves.size();
		const GameLogic::ObjectStateHash *ref = kv.second.begin()->second;
		for (const auto &e : kv.second)
		{
			differs = differs || e.second->value != ref->value;
		}
		if (!differs)
		{
			continue;
		}
		++total;
		if (shown < 20)
		{
			++shown;
			o << "    object " << kv.first << " (" << ref->templateName << "):";
			for (const auto &h : halves)
			{
				auto it = kv.second.find(h.first);
				o << "  slot " << h.first << " " << (it != kv.second.end() ? hex32(it->second->value) : std::string("missing"));
			}
			o << "\n";
		}
	}
	o << "  objects that differ: " << total << "\n";
	return o.str();
}

// ---- Network ------------------------------------------------------------------------------------------------------------------
Network::Network(const NetworkConfig &config, NetTransport &transport)
	: m_config(config)
	, m_transport(transport)
{
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		m_slots[(size_t)s].active = config.slotPlayerIndex[(size_t)s] >= 0;
	}
	if (config.localSlot < 0 || config.localSlot >= MAX_SLOTS || !m_slots[(size_t)config.localSlot].active)
	{
		m_errors.push_back("the local slot " + std::to_string(config.localSlot) + " is not a network player");
	}
	if (config.runAhead < 1)
	{
		m_errors.push_back("run-ahead " + std::to_string(config.runAhead) + " (at least 1)");
		m_config.runAhead = 1;
	}
	// lane MP-2: the packet router fallback order and the disconnect path
	m_routerOrder = config.packetRouterOrder;
	if (m_routerOrder.empty())
	{
		for (int s = 0; s < MAX_SLOTS; ++s)
		{
			if (m_slots[(size_t)s].active)
			{
				m_routerOrder.push_back(s);
			}
		}
	}
	m_packetRouter = m_routerOrder.empty() ? -1 : m_routerOrder.front();
	m_agreedRouterOrder = m_routerOrder;
	m_disconnect.init(config.settings);
	m_minRunAhead = m_config.runAhead;
	m_baseRunAhead = m_config.runAhead;
}

int Network::runAheadAt(UnsignedInt logicFrame) const
{
	int ra = m_baseRunAhead;
	for (const auto &kv : m_runAheadHistory)
	{
		if (logicFrame == 0 || kv.first > logicFrame - 1)
		{
			break;
		}
		ra = kv.second;
	}
	return ra;
}

int Network::routerAt(UnsignedInt frame) const
{
	// lane MP-3 (review r3): the first slot of the agreed fallback order still in the game at `frame` (its departure, a leave or a drop, is later). Every
	// peer knows every departure at or before a frame before it can run that frame (the leaver's / the dropped slot's frame data is waited for until the
	// leave or the drop arrives), so all peers agree whatever order the departures arrived in
	for (int s : m_agreedRouterOrder)
	{
		if (m_departureFrame[(size_t)s] > frame)
		{
			return s;
		}
	}
	return -1;
}

std::uint64_t Network::now() const
{
	return m_clock ? m_clock() : NetMilliseconds();
}

UnsignedInt Network::executionFrame(UnsignedInt logicFrame)
{
	// ZH Network::getExecutionFrame. lane MP-2 (review): with the run-ahead agreed for this protocol frame (runAheadAt), so a command issued on frame L
	// (the CRC of frame L above all) gets the same execution frame on every peer however far its logic worker has acquired batches
	const UnsignedInt f = logicFrame + (UnsignedInt)runAheadAt(logicFrame);
	if (f > m_lastExecutionFrame)
	{
		m_lastExecutionFrame = f;
	}
	return m_lastExecutionFrame;
}

void Network::send(const NetCommandMsg &c)
{
	std::string error;
	const std::vector<std::uint8_t> bytes = NetPacket::encodeCommand(c, &error);
	if (bytes.empty())
	{
		m_errors.push_back("cannot encode " + std::string(NetCommandTypeName(c.type)) + ": " + error);
		return;
	}
	bool reachedLive = false;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		// a slot whose leave arrived is retired: nothing more goes to it (it runs no frame after its leave frame)
		if (s != m_config.localSlot && m_slots[(size_t)s].active && !hasLeft(s))
		{
			std::string why;
			reachedLive = reachedLive || reachesLivePeer(s);
			if (!m_transport.sendTo(s, bytes, &why))
			{
				m_errors.push_back("cannot send " + std::string(NetCommandTypeName(c.type)) + " to slot " + std::to_string(s) + ": " + why);
			}
		}
	}
	// lane ONLINE-3: a frame command no live player receives (a 1v1 whose other player is gone or held, the last player in) still reaches the relay's log;
	// the exact bytes of the recent ones are kept for the moment this peer finds itself alone (logAloneFrames)
	if (c.type == NETCOMMANDTYPE_GAMECOMMAND || c.type == NETCOMMANDTYPE_FRAMEINFO || c.type == NETCOMMANDTYPE_PLAYERLEAVE || c.type == NETCOMMANDTYPE_DESTROYPLAYER
		|| c.type == NETCOMMANDTYPE_RUNAHEAD || c.type == NETCOMMANDTYPE_REJOIN)
	{
		// (Sol r1 of ONLINE-3: the first command sent alone went to the log before the history it follows, so the log read 2, 1, 2 for 1, 2): the
		// history not yet on the log-only stream goes first, then this command; none goes there twice, so the relay's count of identical bytes per
		// frame on that stream is the sender's own (every stream carries a sender's commands in its send order)
		RecentFrameCommand rc;
		rc.frame = c.executionFrame;
		rc.bytes = bytes;
		if (!reachedLive)
		{
			logRecentFrameCommands();
			m_wasAlone = true;
			rc.onLogStream = m_transport.sendToLog(bytes);
		}
		m_recentFrameCommands.push_back(std::move(rc));
		while (!m_recentFrameCommands.empty() && m_relayedAny && m_recentFrameCommands.front().frame + kFramesToKeep < m_lastRelayed)
		{
			m_recentFrameCommands.pop_front();
		}
	}
	file(c, m_config.localSlot); // the local frame data (ZH sendLocalCommand files it for the local slot too)
}

void Network::update(UnsignedInt logicFrame, CommandList &pending)
{
	if (m_rejoining)
	{
		// lane ONLINE-3: a rejoining client sends no frame data before beginLive (the catch-up runs the game from the relay's log); it files what the
		// others send it once they took it back, and asks for that every second once its catch-up is near the live edge
		m_logicFrame = logicFrame;
		m_transport.service();
		receiveAll();
		const std::uint64_t t = now();
		if (m_rejoinReady && (m_lastRejoinRequest == 0 || t - m_lastRejoinRequest >= 1000))
		{
			m_lastRejoinRequest = t;
			NetCommandMsg c;
			c.type = NETCOMMANDTYPE_REJOINREQUEST;
			c.slot = (std::uint8_t)m_config.localSlot;
			c.frame = (std::uint32_t)m_rejoinGeneration;
			sendDirect(c);
		}
		return;
	}
	if (m_config.adaptiveRunAhead && !m_leaving && allLoaded())
	{
		updateRunAhead(now()); // lane MP-2: before the local commands get their execution frame
	}
	if (!m_leaving)
	{
		// (1) ZH GetCommandsFromCommandList / sendLocalGameMessage
		const UnsignedInt exec = executionFrame(logicFrame);
		for (const GameMessage &m : pending.messages())
		{
			NetCommandMsg c;
			c.type = NETCOMMANDTYPE_GAMECOMMAND;
			c.slot = (std::uint8_t)m_config.localSlot;
			c.executionFrame = exec;
			auto msg = std::make_shared<GameMessage>(m);
			c.message = msg;
			if (m.getType() == MSG_LOGIC_CRC && !m_pendingCrcFrames.empty())
			{
				m_crcFrameOfExecution[exec] = m_pendingCrcFrames.front();
				m_pendingCrcFrames.pop_front();
			}
			send(c);
		}
		// (2) ZH processCommand / processFrameTick: the counts of every frame no later command can reach
		for (long long f = m_lastFrameAnnounced + 1; f < (long long)exec; ++f)
		{
			NetCommandMsg c;
			c.type = NETCOMMANDTYPE_FRAMEINFO;
			c.slot = (std::uint8_t)m_config.localSlot;
			c.executionFrame = (UnsignedInt)f;
			const auto &frames = m_slots[(size_t)m_config.localSlot].frames;
			const auto it = frames.find((UnsignedInt)f);
			c.commandCount = (std::uint16_t)(it == frames.end() ? 0 : it->second.entries());
			send(c);
			m_lastFrameAnnounced = f;
		}
	}
	pending.reset();
	m_logicFrame = logicFrame;
	// (3)
	m_transport.service();
	receiveAll();
	checkRelayStatus(); // lane ONLINE-3
	logAloneFrames();
	if (m_config.holdForRejoin && !m_leaving && allLoaded())
	{
		routerRejoinDuties();
	}
	// lane MP-2: ConnectionManager::update -> DisconnectManager::update (RW 0x8D77B5 -> 0x8D8A90). lane MP-DROP-1: also at the load barrier (the others
	// still loading): RotWK's update runs then too; its screen waits for the multiplayer load screen to go, which GameLogic's loading update (RW
	// 0x62B385) keeps while no slot is silent and the frame is below 6 (DisconnectManager::update models that gate). GameLogic's testTimeOut (RW
	// 0x625C92, the only caller of isProgressComplete RW 0x625C38) has no caller in RotWK, and nothing sends NETCOMMANDTYPE_TIMEOUTSTART
	if (m_config.disconnectPath && !m_leaving)
	{
		m_disconnect.update(*this, now(), logicFrame);
	}
}

void Network::receiveAll()
{
	int from = -1;
	std::vector<std::uint8_t> bytes;
	while (m_transport.receive(from, bytes))
	{
		NetCommandMsg c;
		std::string error;
		if (from < 0 || from >= MAX_SLOTS || !m_slots[(size_t)from].active || from == m_config.localSlot)
		{
			m_errors.push_back("a net command from slot " + std::to_string(from) + ", which is not a remote network player");
			continue;
		}
		if (m_slots[(size_t)from].disconnected && !newIncarnationTalking(from))
		{
			continue; // lane MP-2: a dropped player's late traffic (its connection is retired)
		}
		if (!NetPacket::decodeCommand(bytes, c, &error))
		{
			m_errors.push_back("slot " + std::to_string(from) + ": " + error);
			continue;
		}
		if (c.slot != from)
		{
			m_errors.push_back("slot " + std::to_string(from) + " sent a command claiming slot " + std::to_string(c.slot));
			continue;
		}
		SlotData &sd = m_slots[(size_t)from];
		if (sd.disconnected)
		{
			// lane ONLINE-3: the new incarnation of a held slot (its transport was started afresh for it): its request to be taken back, and the frame
			// data it sends once it runs in lockstep (frames after the REJOIN, kept until the REJOIN runs here); nothing else counts before it is a member
			if (c.type == NETCOMMANDTYPE_REJOINREQUEST)
			{
				if (sd.held && !sd.handedOver && (int)c.frame == sd.seenGeneration && !sd.rejoinRequested)
				{
					sd.rejoinRequested = true;
					m_rejoinLog.push_back("slot " + std::to_string(from) + " asked to rejoin (generation " + std::to_string(c.frame) + ")");
				}
			}
			else if (c.type == NETCOMMANDTYPE_GAMECOMMAND || c.type == NETCOMMANDTYPE_FRAMEINFO)
			{
				file(c, from);
			}
			continue;
		}
		sd.lastHeard = now();
		file(c, from);
	}
}

void Network::file(const NetCommandMsg &c, int fromSlot)
{
	SlotData &sd = m_slots[(size_t)fromSlot];
	switch (c.type)
	{
	case NETCOMMANDTYPE_GAMECOMMAND:
	case NETCOMMANDTYPE_FRAMEINFO:
	case NETCOMMANDTYPE_DESTROYPLAYER:
	case NETCOMMANDTYPE_RUNAHEAD:
	case NETCOMMANDTYPE_REJOIN: // lane ONLINE-3
	{
		if (m_relayedAny && c.executionFrame <= m_lastRelayed)
		{
			if (m_history.count(c.executionFrame) && m_history[c.executionFrame].resent[(size_t)fromSlot])
			{
				return; // lane MP-2: the frame ran with this slot's commands relayed by another peer (sendFrameDataToPlayer); its own copy came late
			}
			m_errors.push_back(std::string(NetCommandTypeName(c.type)) + " of slot " + std::to_string(fromSlot) + " for frame " + std::to_string(c.executionFrame)
				+ ", which already ran");
			return;
		}
		FrameData &fd = sd.frames[c.executionFrame];
		if (fd.fromResend)
		{
			return; // lane MP-2: already filled whole by a FRAMERESEND
		}
		if (c.type == NETCOMMANDTYPE_FRAMEINFO)
		{
			fd.commandCount = c.commandCount;
		}
		else if (c.type == NETCOMMANDTYPE_RUNAHEAD)
		{
			// lane MP-2: taken when the frame runs (relayCommands) if the sender is that frame's packet router. The value must lie in the configured bounds;
			// out of them it is an error and is filed as 0 (it keeps its place in the frame's count, nothing changes)
			const long long v = (long long)c.frame;
			const long long top = std::max(m_minRunAhead, m_config.maxRunAhead);
			if (v < m_minRunAhead || v > top)
			{
				m_errors.push_back("RUNAHEAD " + std::to_string(v) + " from slot " + std::to_string(fromSlot) + " for frame " + std::to_string(c.executionFrame)
					+ " is outside " + std::to_string(m_minRunAhead) + " .. " + std::to_string(top) + ": ignored");
				fd.runAheadChange = 0;
			}
			else
			{
				fd.runAheadChange = (int)v;
			}
		}
		else if (c.type == NETCOMMANDTYPE_REJOIN)
		{
			// lane ONLINE-3: taken when the frame runs (relayCommands) if the sender is that frame's packet router; counted in the frame info
			fd.rejoins.push_back((int)c.targetSlot);
		}
		else if (c.type == NETCOMMANDTYPE_DESTROYPLAYER)
		{
			if (c.targetSlot < MAX_SLOTS && m_slots[(size_t)c.targetSlot].held && !m_slots[(size_t)c.targetSlot].handedOver)
			{
				m_slots[(size_t)c.targetSlot].handedOver = true; // lane ONLINE-3: the held player's army goes to an ally: no rejoin any more
				m_rejoinLog.push_back("slot " + std::to_string(c.targetSlot) + " handed over by slot " + std::to_string(fromSlot) + " at frame "
					+ std::to_string(c.executionFrame));
			}
			// lane MP-2: ZH Network::processDestroyPlayerCommand / RotWK RW 0x65E234: MSG_SELF_DESTRUCT { boolean TRUE } of the slot's player (RotWK finds it by
			// the game slot's player name, RW 0x800B55 + 0x34; ZH passed FALSE), in the frame's command list where the sender's command stands
			const int victim = c.targetSlot < MAX_SLOTS ? m_config.slotPlayerIndex[(size_t)c.targetSlot] : -1;
			if (victim < 0)
			{
				m_errors.push_back("DESTROYPLAYER of slot " + std::to_string(c.targetSlot) + ", which has no player");
				fd.commands.push_back(std::make_shared<GameMessage>(MSG_SELF_DESTRUCT, m_config.slotPlayerIndex[(size_t)fromSlot])); // keeps the count
				return;
			}
			auto m = std::make_shared<GameMessage>(MSG_SELF_DESTRUCT, victim);
			m->appendBooleanArgument(true);
			fd.commands.push_back(std::move(m));
		}
		else
		{
			// ZH NetGameCommandMsg::constructGameMessage: the message belongs to the sender's player
			auto m = std::make_shared<GameMessage>(*c.message);
			m->friend_setPlayerIndex(m_config.slotPlayerIndex[(size_t)fromSlot]);
			fd.commands.push_back(std::move(m));
		}
		return;
	}
	case NETCOMMANDTYPE_FRAMERESEND:
	{
		// lane MP-2 (RW 0x8D4761 on the sender): another peer relays the commands one slot ran a frame with
		const int owner = c.targetSlot;
		if (owner == m_config.localSlot || owner >= MAX_SLOTS || !m_slots[(size_t)owner].active || (m_relayedAny && c.frame <= m_lastRelayed))
		{
			return;
		}
		if (c.frame >= m_slots[(size_t)owner].leaveFrame)
		{
			return; // lane MP-2 (review): the owner's frames from its leave / drop frame on do not run
		}
		// lane MP-2 (review): the relayed commands are the owner's (its player, as file() sets it); a MSG_SELF_DESTRUCT of another player only stands in
		// the frame of the packet router that sent the DESTROYPLAYER; the run-ahead change keeps the bounds file() checks
		std::vector<std::shared_ptr<GameMessage>> messages;
		for (const std::shared_ptr<GameMessage> &m : c.messages)
		{
			if (!m)
			{
				continue;
			}
			auto copy = std::make_shared<GameMessage>(*m);
			const int ownerPlayer = m_config.slotPlayerIndex[(size_t)owner];
			if (copy->getType() == MSG_SELF_DESTRUCT && copy->getPlayerIndex() != ownerPlayer && owner != routerAt(c.frame))
			{
				m_errors.push_back("slot " + std::to_string(fromSlot) + " resent a MSG_SELF_DESTRUCT of player " + std::to_string(copy->getPlayerIndex()) + " in slot "
					+ std::to_string(owner) + "'s frame " + std::to_string(c.frame) + ", which is not the packet router's: refused");
				return;
			}
			if (copy->getType() != MSG_SELF_DESTRUCT)
			{
				copy->friend_setPlayerIndex(ownerPlayer);
			}
			messages.push_back(std::move(copy));
		}
		if (c.runAheadChange > std::max(m_minRunAhead, m_config.maxRunAhead) || (c.runAheadChange >= 1 && c.runAheadChange < m_minRunAhead) || c.runAheadChange < -1)
		{
			m_errors.push_back("slot " + std::to_string(fromSlot) + " resent the run-ahead change " + std::to_string(c.runAheadChange) + ": refused");
			return;
		}
		FrameData &fd = m_slots[(size_t)owner].frames[c.frame];
		if (fd.fromResend || (fd.commandCount >= 0 && (size_t)fd.commandCount == fd.entries()))
		{
			return; // complete already
		}
		fd.commands = std::move(messages);
		fd.runAheadChange = c.runAheadChange;
		fd.rejoins.clear();
		for (int t = 0; t < MAX_SLOTS; ++t)
		{
			if (c.rejoinMask & (1u << t))
			{
				fd.rejoins.push_back(t); // lane ONLINE-3
			}
		}
		fd.commandCount = (int)fd.entries();
		fd.fromResend = true;
		++m_framesFilled;
		return;
	}
	case NETCOMMANDTYPE_DISCONNECTKEEPALIVE:
	case NETCOMMANDTYPE_DISCONNECTPLAYER:
	case NETCOMMANDTYPE_DISCONNECTVOTE:
	case NETCOMMANDTYPE_DISCONNECTFRAME:
	case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
		if (fromSlot != m_config.localSlot && m_config.disconnectPath && !m_rejoining) // lane ONLINE-3: a rejoining client is not in the game yet
		{
			m_disconnect.processDisconnectCommand(*this, c, now(), m_logicFrame);
		}
		return;
	case NETCOMMANDTYPE_PLAYERLEAVE:
		sd.leaveFrame = std::min(sd.leaveFrame, c.executionFrame);
		if (fromSlot != m_config.localSlot)
		{
			// the leaver announced every frame up to its leave before this (in-order delivery) and runs no later frame: its connection is retired
			m_transport.retire(fromSlot);
			// lane MP-3 (review r1): a graceful leave changes the membership like a drop does (disconnectPlayer below, RW 0x8D57D8: the next packet router
			// when it was the router, the fallback order without it), at the frame it advertised; no self destruct is issued here (the leaver's own
			// MSG_SELF_DESTRUCT, the quit menu's Exit, travels in its frame commands)
			passRouterRole(fromSlot, sd.leaveFrame);
		}
		return;
	case NETCOMMANDTYPE_LOADCOMPLETE:
		sd.loaded = true;
		return;
	case NETCOMMANDTYPE_REJOINREQUEST:
		return; // lane ONLINE-3: from a member slot it means nothing (receiveAll takes it from a held one)
	case NETCOMMANDTYPE_PAUSE:
	{
		// lane ONLINE-3: every peer stops before frame c.frame (the sender's next execution frame: its frame info for it follows this command, so no peer
		// can have run it); a RESUME that overtook it (from another player) cancels it
		if (m_resumedPauses.count(c.frame))
		{
			return;
		}
		if (m_relayedAny && c.frame <= m_lastRelayed)
		{
			m_errors.push_back("PAUSE of slot " + std::to_string(fromSlot) + " before frame " + std::to_string(c.frame) + ", which already ran here");
			return;
		}
		if (!m_pauses.count(c.frame))
		{
			m_pauses[c.frame] = fromSlot;
			++m_pausesSeen;
		}
		return;
	}
	case NETCOMMANDTYPE_RESUME:
		m_pauses.erase(c.frame); // lane ONLINE-3: any player may resume
		m_resumedPauses.insert(c.frame);
		return;
	case NETCOMMANDTYPE_KEEPALIVE:
		return;
	case NETCOMMANDTYPE_DESYNCREPORT:
	{
		if (fromSlot == m_config.localSlot)
		{
			return; // our own half is already in the report
		}
		DesyncHalf half;
		std::string error;
		if (!DesyncHalf::decode(c.payload, half, &error))
		{
			m_errors.push_back("slot " + std::to_string(fromSlot) + ": " + error);
			return;
		}
		half.slot = fromSlot;
		for (DesyncReport &r : m_desyncs)
		{
			if (r.frame == half.frame)
			{
				r.halves[fromSlot] = std::move(half);
				return;
			}
		}
		m_orphanHalves.push_back(std::move(half)); // the sender ran the frame before us: attached when our report exists
		return;
	}
	}
}

bool Network::slotNeeded(int slot, UnsignedInt frame) const
{
	const SlotData &sd = m_slots[(size_t)slot];
	return sd.active && frame < sd.leaveFrame && frame >= sd.joinFrame; // lane ONLINE-3: a rejoined incarnation from its join frame
}

bool Network::isFrameReady(UnsignedInt frame) const
{
	return waitingFor(frame).empty();
}

std::vector<int> Network::waitingFor(UnsignedInt frame) const
{
	std::vector<int> out;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (!slotNeeded(s, frame))
		{
			continue;
		}
		const auto &frames = m_slots[(size_t)s].frames;
		const auto it = frames.find(frame);
		// ZH FrameData::allCommandsReady: the count arrived and equals the commands received
		if (it == frames.end() || it->second.commandCount < 0 || (size_t)it->second.commandCount != it->second.entries())
		{
			out.push_back(s);
		}
	}
	return out;
}

void Network::relayCommands(UnsignedInt frame, CommandList &out)
{
	int newRunAhead = -1;
	std::vector<int> rejoined; // lane ONLINE-3: the slots the frame's packet router takes back (from frame + 1)
	std::map<int, std::uint32_t> crcs;
	bool anyCRC = false;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		SlotData &sd = m_slots[(size_t)s];
		if (!sd.active)
		{
			continue;
		}
		const auto it = sd.frames.find(frame);
		if (it == sd.frames.end())
		{
			continue;
		}
		if (!slotNeeded(s, frame))
		{
			// lane MP-2 (review): a slot that left or was dropped runs no frame from its leave / drop frame on: what it sent for them is discarded on every
			// surviving peer alike (some peers may have received it, others not)
			sd.frames.erase(it);
			continue;
		}
		if (it->second.commandCount < 0 || (size_t)it->second.commandCount != it->second.entries())
		{
			m_errors.push_back("frame " + std::to_string(frame) + " relayed before slot " + std::to_string(s) + "'s commands were complete");
		}
		{
			RelayedFrame &h = m_history[frame]; // lane MP-2: kept for sendFrameDataToPlayer
			h.present[(size_t)s] = true;
			h.resent[(size_t)s] = it->second.fromResend;
			h.commands[(size_t)s] = it->second.commands;
			h.runAheadChange[(size_t)s] = it->second.runAheadChange;
			for (int t : it->second.rejoins)
			{
				// lane ONLINE-3: only the frame's packet router takes a slot back, and only a held one (deterministic: the same membership on every peer)
				h.rejoinMask[(size_t)s] |= (std::uint8_t)(1u << (t & 7));
				if (s != routerAt(frame))
				{
					m_errors.push_back("REJOIN of slot " + std::to_string(t) + " from slot " + std::to_string(s) + " for frame " + std::to_string(frame)
						+ ", which is not the packet router's: ignored");
				}
				else if (t < 0 || t >= MAX_SLOTS || t == s || !m_slots[(size_t)t].active || !m_slots[(size_t)t].held || m_slots[(size_t)t].handedOver)
				{
					m_errors.push_back("REJOIN of slot " + std::to_string(t) + " for frame " + std::to_string(frame) + ", which is not held for a rejoin: ignored");
				}
				else
				{
					rejoined.push_back(t);
				}
			}
			if (it->second.runAheadChange >= 1)
			{
				// lane MP-2: the router's RUNAHEAD, on every peer at this frame; from another slot it is an error (only the packet router of the frame decides),
				// and a fall of more than one frame is refused (the router lowers one frame per period: the execution frames stay monotonic)
				const int inEffect = m_config.runAhead; // the value the frames before this one agreed on (relayed in order on every peer)
				if (s != routerAt(frame))
				{
					m_errors.push_back("RUNAHEAD " + std::to_string(it->second.runAheadChange) + " of slot " + std::to_string(s) + " for frame " + std::to_string(frame)
						+ ", which is not the packet router's (slot " + std::to_string(routerAt(frame)) + "): ignored");
				}
				else if (it->second.runAheadChange < inEffect - 1)
				{
					m_errors.push_back("RUNAHEAD " + std::to_string(it->second.runAheadChange) + " for frame " + std::to_string(frame) + " falls more than one frame below "
						+ std::to_string(inEffect) + ": ignored");
				}
				else
				{
					newRunAhead = it->second.runAheadChange;
				}
			}
		}
		for (const std::shared_ptr<GameMessage> &m : it->second.commands)
		{
			if (m->getType() == MSG_LOGIC_CRC)
			{
				anyCRC = true;
				const GameMessageArgument *a = m->getArgument(0);
				crcs[s] = a ? (std::uint32_t)a->integer : 0u;
			}
			out.append(*m);
			++m_commandsRelayed;
		}
		sd.frames.erase(it);
	}
	// frame data older than this frame can never be used again
	for (SlotData &sd : m_slots)
	{
		while (!sd.frames.empty() && sd.frames.begin()->first < frame)
		{
			sd.frames.erase(sd.frames.begin());
		}
	}
	m_history[frame]; // a frame whose slots sent nothing is still a frame that ran
	while (!m_history.empty() && m_history.begin()->first + kFramesToKeep <= frame)
	{
		m_history.erase(m_history.begin());
	}
	m_lastRelayed = frame;
	m_relayedAny = true;
	for (int t : rejoined)
	{
		applyRejoin(t, frame + 1);
	}
	if (newRunAhead >= 1 && newRunAhead != m_config.runAhead)
	{
		m_config.runAhead = newRunAhead;
		m_runAheadHistory[frame] = newRunAhead; // protocol frames after this one execute with it (runAheadAt)
		++m_runAheadChanges;
	}
	// changes older than every frame still in play fold into the base
	while (!m_runAheadHistory.empty() && m_runAheadHistory.begin()->first + kFramesToKeep <= frame)
	{
		m_baseRunAhead = m_runAheadHistory.begin()->second;
		m_runAheadHistory.erase(m_runAheadHistory.begin());
	}
	if (anyCRC)
	{
		checkCRCs(frame, crcs);
	}
	m_crcFrameOfExecution.erase(m_crcFrameOfExecution.begin(), m_crcFrameOfExecution.upper_bound(frame));
}

void Network::checkCRCs(UnsignedInt frame, const std::map<int, std::uint32_t> &crcs)
{
	// ZH GameLogic::processCommandList: every connected player's CRC must be there and equal
	bool mismatch = false;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (slotNeeded(s, frame) && crcs.find(s) == crcs.end())
		{
			mismatch = true; // ZH "Not enough CRCs!"
		}
	}
	for (const auto &kv : crcs)
	{
		mismatch = mismatch || kv.second != crcs.begin()->second;
	}
	if (!mismatch)
	{
		++m_crcChecksPassed;
		return;
	}
	DesyncReport r;
	r.checkedOnFrame = frame;
	r.crcs = crcs;
	const auto described = m_crcFrameOfExecution.find(frame);
	if (described == m_crcFrameOfExecution.end())
	{
		m_errors.push_back("CRC mismatch on frame " + std::to_string(frame) + " without a local CRC for it");
	}
	else
	{
		r.frame = described->second;
		const auto half = m_localHalves.find(r.frame);
		if (half != m_localHalves.end())
		{
			r.halves[m_config.localSlot] = half->second;
			NetCommandMsg c;
			c.type = NETCOMMANDTYPE_DESYNCREPORT;
			c.slot = (std::uint8_t)m_config.localSlot;
			c.executionFrame = frame;
			c.payload = half->second.encode();
			send(c);
		}
	}
	for (size_t i = 0; i < m_orphanHalves.size();)
	{
		if (m_orphanHalves[i].frame == r.frame)
		{
			r.halves[m_orphanHalves[i].slot] = m_orphanHalves[i];
			m_orphanHalves.erase(m_orphanHalves.begin() + (long)i);
		}
		else
		{
			++i;
		}
	}
	m_desyncs.push_back(std::move(r));
}

void Network::frameCompleted(const FrameCompletion &c, CommandList &pending)
{
	const UnsignedInt frameNow = c.frame;
	if (m_config.crcInterval <= 0 || (frameNow % (UnsignedInt)m_config.crcInterval) != 0 || m_leaving)
	{
		return;
	}
	if (!c.breakdown)
	{
		m_errors.push_back("the completion of CRC frame " + std::to_string(frameNow) + " carries no hash breakdown");
		return;
	}
	DesyncHalf half;
	half.slot = m_config.localSlot;
	half.frame = frameNow;
	half.hash = c.hash;
	half.sections = c.sections;
	half.objects = c.objects;
	// GameLogic.cpp:3666: MSG_LOGIC_CRC { integer CRC, boolean playback }
	GameMessage m(MSG_LOGIC_CRC, m_config.slotPlayerIndex[(size_t)m_config.localSlot]);
	m.appendIntegerArgument((int)half.hash);
	m.appendBooleanArgument(false);
	pending.append(m);
	m_pendingCrcFrames.push_back(frameNow);
	m_localHalves[frameNow] = std::move(half);
	while (m_localHalves.size() > kKeptHalves)
	{
		m_localHalves.erase(m_localHalves.begin());
	}
}

void Network::leave(UnsignedInt logicFrame)
{
	if (m_leaving)
	{
		return;
	}
	// ZH handleLocalPlayerLeaving: the counts up to the execution frame, then the leave on the frame after it
	const UnsignedInt exec = executionFrame(logicFrame);
	for (long long f = m_lastFrameAnnounced + 1; f <= (long long)exec; ++f)
	{
		NetCommandMsg c;
		c.type = NETCOMMANDTYPE_FRAMEINFO;
		c.slot = (std::uint8_t)m_config.localSlot;
		c.executionFrame = (UnsignedInt)f;
		const auto &frames = m_slots[(size_t)m_config.localSlot].frames;
		const auto it = frames.find((UnsignedInt)f);
		c.commandCount = (std::uint16_t)(it == frames.end() ? 0 : it->second.entries());
		send(c);
		m_lastFrameAnnounced = f;
	}
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_PLAYERLEAVE;
	c.slot = (std::uint8_t)m_config.localSlot;
	c.executionFrame = exec + 1;
	send(c);
	m_leaving = true;
}

bool Network::hasLeft(int slot) const
{
	return slot >= 0 && slot < MAX_SLOTS && m_slots[(size_t)slot].leaveFrame != 0xFFFFFFFFu;
}

void Network::sendLoadComplete()
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_LOADCOMPLETE;
	c.slot = (std::uint8_t)m_config.localSlot;
	send(c);
}

bool Network::allLoaded() const
{
	for (const SlotData &sd : m_slots)
	{
		// lane MP-DROP-1: a slot dropped (or gone) before it finished loading does not hold the others at the load barrier
		if (sd.active && !sd.loaded && !sd.disconnected && sd.leaveFrame == 0xFFFFFFFFu)
		{
			return false;
		}
	}
	return true;
}

// ---- lane MP-2: the adaptive run-ahead -------------------------------------------------------------------------------------------
void Network::updateRunAhead(std::uint64_t nowMs)
{
	// every NetworkRunAheadMetricsTime ms (GameData, GlobalData + 0xC10) the packet router computes the run-ahead from the worst measured latency to a
	// connected peer with ZH ConnectionManager::updateRunAhead's formula (DONOR: (latency / 2) * fps, plus NetworkRunAheadSlack percent, at least the
	// minimum) and sends it as ZH's NETCOMMANDTYPE_RUNAHEAD, a frame command (RotWK has no such command, RW 0x989449: S-720); every peer takes the new
	// value when that frame runs (relayCommands), so all peers keep one run-ahead and their MSG_LOGIC_CRCs one execution frame. OpenBFME choices: the
	// latency is the transport's smoothed round trip plus four times its variation (RFC 6298's timeout), one frame is added for the frame a command is
	// issued in, a lower target lowers the value by one frame per period, and no new change is sent before the last one ran.
	if (m_lastRunAheadUpdate != 0 && nowMs - m_lastRunAheadUpdate < (std::uint64_t)m_config.settings.runAheadMetricsTime)
	{
		return;
	}
	m_lastRunAheadUpdate = nowMs;
	long long worst = -1;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (!hasConnection(s))
		{
			continue;
		}
		const int rtt = m_transport.roundTripMs(s);
		const int var = m_transport.roundTripVarMs(s);
		if (rtt >= 0)
		{
			worst = std::max(worst, (long long)rtt + 4LL * (long long)std::max(0, var));
		}
	}
	m_lastMaxRtt = (int)worst;
	// only the packet router decides (ZH: the router's RUNAHEAD); the others take its value when its frame runs. Lane MP-3 (review r1): the router of the
	// frame the RUNAHEAD would execute on (a successor after a leave decides from the leave frame on: its earlier RUNAHEAD would be refused)
	const UnsignedInt nextExecution = std::max<UnsignedInt>(m_lastExecutionFrame, m_logicFrame + (UnsignedInt)runAheadAt(m_logicFrame));
	if (worst < 0 || m_config.localSlot != m_packetRouter || routerAt(nextExecution) != m_config.localSlot)
	{
		return;
	}
	if (m_runAheadChangePending && (!m_relayedAny || m_lastRelayed < m_runAheadChangeFrame))
	{
		return; // the last change has not run yet
	}
	m_runAheadChangePending = false;
	// lane NETLAG-1: the frames a command needs to reach every peer before the frame it executes on. A peer sends its FRAMEINFO of frame P + run-ahead - 1
	// when it completes frame P - 1, and the other peer runs that frame (run-ahead - 1) periods after its next 200 ms tick; so a one-way latency (with
	// NetworkRunAheadSlack percent) below half a period keeps the run-ahead 1, RotWK's command delay (the packet router stamps a command with the current
	// frame + 1, RW 0x8D4301 / 0x8D3AF1), and each further 200 ms adds a frame. OpenBFME choice (INFERENCE, S-720): the other half period is kept for
	// the simulation time of a frame and the phase between the peers' 5 Hz clocks. MP-2's formula rounded the one-way latency up to whole frames and added
	// one, so every real link (a 1 ms LAN included) ran at 2 frames: 400 to 600 ms from a click to its effect
	const long long fps = 5;                                       // LOGICFRAMES_PER_SECOND (RW 0xD9F608)
	const long long periodMs = 1000 / fps;
	long long oneWayMs = (worst + 1) / 2;                          // rounded up
	oneWayMs += oneWayMs * (long long)m_config.settings.runAheadSlack / 100;
	long long target = (oneWayMs + periodMs / 2) / periodMs + 1;
	target = std::max<long long>(target, m_minRunAhead);
	target = std::min<long long>(target, std::max(m_minRunAhead, m_config.maxRunAhead));
	int next = m_config.runAhead;
	if (target > m_config.runAhead)
	{
		next = (int)target;
	}
	else if (target < m_config.runAhead)
	{
		next = m_config.runAhead - 1;
	}
	if (next == m_config.runAhead)
	{
		return;
	}
	// ZH updateRunAhead: the command executes on the current execution frame (never before frames already announced); every peer switches when it runs
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_RUNAHEAD;
	c.slot = (std::uint8_t)m_config.localSlot;
	c.executionFrame = executionFrame(m_logicFrame);
	c.frame = (std::uint32_t)next;
	send(c);
	m_runAheadChangeFrame = c.executionFrame;
	m_runAheadChangePending = true;
}

// ---- lane MP-2: the disconnect path ----------------------------------------------------------------------------------------------
bool Network::allowedToContinue(UnsignedInt frame)
{
	if (pausedBefore(frame))
	{
		return false; // lane ONLINE-3: a pause
	}
	return !m_config.disconnectPath || m_disconnect.allowedToContinue(*this, frame);
}

void Network::voteForPlayerDisconnect(int slot)
{
	m_disconnect.voteForPlayerDisconnect(*this, slot, m_logicFrame);
}

void Network::quitFromDisconnectScreen()
{
	// RW 0x9195E1: the chat "Network:PlayerLeftGame" (not ported: no disconnect chat), a vote for every slot that is not the local one, then quit
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (s != m_config.localSlot && isPlayerConnected(s))
		{
			m_disconnect.voteForPlayerDisconnect(*this, s, m_logicFrame);
		}
	}
	m_quitFromScreen = true;
}

bool Network::hasConnection(int slot) const
{
	if (slot < 0 || slot >= MAX_SLOTS || slot == m_config.localSlot)
	{
		return false;
	}
	const SlotData &sd = m_slots[(size_t)slot];
	return sd.active && !sd.disconnected && sd.leaveFrame == 0xFFFFFFFFu;
}

bool Network::isPlayerConnected(int slot) const
{
	return slot == m_config.localSlot || hasConnection(slot);
}

bool Network::isLeftState(int slot) const
{
	// RotWK ConnectionManager + 0x12080 in 1 .. 3 and connected: a left or disconnected slot has no connection any more here, so the test reduces to false
	// for every connected slot (state 1 / 2 writers not located, S-1121)
	(void)slot;
	return false;
}

int Network::numConnected() const
{
	int n = 0;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		n += isPlayerConnected(s) ? 1 : 0;
	}
	return n;
}

std::uint64_t Network::lastHeard(int slot, std::uint64_t nowMs)
{
	SlotData &sd = m_slots[(size_t)slot];
	// lane ONLINE-3: once the slot's game restarted, its transport hears the new incarnation, not the member: only the member's commands count. The same
	// while the relay reports the slot silent (STATUS): a restarted process's first datagrams reach the transport before the relay's next STATUS names
	// its new generation, and must not end the disconnect screen of the old one (the screen would come back on the same frame, where retail's rule
	// ignores the repeated disconnect frame, RW 0x8D836B, and the drop could never be decided)
	const bool memberOnly = sd.restarted || m_transport.silentAtRelay(slot);
	std::uint64_t last = memberOnly ? sd.lastHeard : std::max(sd.lastHeard, m_transport.lastHeardFrom(slot));
	if (last == 0)
	{
		if (sd.firstQuery == 0)
		{
			sd.firstQuery = nowMs; // RW 0x8D33BA: the first query of a connection stores the current time
		}
		last = sd.firstQuery;
	}
	return last;
}

bool Network::gameRunning() const
{
	return !m_leaving;
}

int Network::nextPacketRouterSlot(int slot) const
{
	// RW 0x8D3ECD: the entry after `slot` in the fallback order (past the end: none)
	for (size_t i = 0; i < m_routerOrder.size(); ++i)
	{
		if (m_routerOrder[i] == slot)
		{
			return i + 1 < m_routerOrder.size() ? m_routerOrder[i + 1] : MAX_SLOTS;
		}
	}
	return MAX_SLOTS;
}

bool Network::dropClaims(int sender, int target, std::uint8_t &gone) const
{
	gone = 0;
	if (sender < 0 || sender >= MAX_SLOTS || target < 0 || target >= MAX_SLOTS || sender == target || !m_slots[(size_t)target].active
		|| m_slots[(size_t)target].disconnected || m_slots[(size_t)sender].disconnected)
	{
		return false;
	}
	for (int s : m_agreedRouterOrder)
	{
		if (s == sender)
		{
			return true;
		}
		if (s == target)
		{
			continue;
		}
		if (!hasLeft(s) && !m_slots[(size_t)s].disconnected && m_departureFrame[(size_t)s] == 0xFFFFFFFFu)
		{
			return false; // a predecessor this peer holds connected is the one to decide (lane ONLINE-3: a rejoined slot is never a decider again)
		}
		gone |= (std::uint8_t)(1u << s);
	}
	return false;
}

int Network::verifyDropClaims(int sender, int target, std::uint8_t gone) const
{
	if (sender < 0 || sender >= MAX_SLOTS || target < 0 || target >= MAX_SLOTS || sender == target || !m_slots[(size_t)target].active
		|| !m_slots[(size_t)sender].active)
	{
		return -1;
	}
	bool pending = false;
	for (int s : m_agreedRouterOrder)
	{
		if (s == sender)
		{
			return pending ? 0 : 1;
		}
		if (s == target)
		{
			continue;
		}
		if (!(gone & (1u << s)))
		{
			return -1; // the sender holds a predecessor connected: not the one to decide (judged on the message and the agreed order alone)
		}
		if (!hasLeft(s) && !m_slots[(size_t)s].disconnected && m_departureFrame[(size_t)s] == 0xFFFFFFFFu)
		{
			pending = true; // the claimed leave / drop has not reached this peer yet
		}
	}
	return -1;
}

void Network::sendDirect(const NetCommandMsg &c)
{
	std::string error;
	const std::vector<std::uint8_t> bytes = NetPacket::encodeCommand(c, &error);
	if (bytes.empty())
	{
		m_errors.push_back("cannot encode " + std::string(NetCommandTypeName(c.type)) + ": " + error);
		return;
	}
	bool reachedLive = false;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		reachedLive = reachedLive || (hasConnection(s) && reachesLivePeer(s));
	}
	if (!reachedLive && c.type == NETCOMMANDTYPE_DISCONNECTPLAYER)
	{
		m_transport.sendToLog(bytes); // lane ONLINE-3: the drop frame reaches the relay's log when only the dropped player would have received it
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (hasConnection(s))
		{
			std::string why;
			if (!m_transport.sendTo(s, bytes, &why))
			{
				m_errors.push_back("cannot send " + std::string(NetCommandTypeName(c.type)) + " to slot " + std::to_string(s) + ": " + why);
			}
		}
	}
}

void Network::sendDirectTo(int slot, const NetCommandMsg &c)
{
	if (!hasConnection(slot))
	{
		return;
	}
	std::string error;
	const std::vector<std::uint8_t> bytes = NetPacket::encodeCommand(c, &error);
	std::string why;
	if (bytes.empty() || !m_transport.sendTo(slot, bytes, &why))
	{
		m_errors.push_back("cannot send " + std::string(NetCommandTypeName(c.type)) + " to slot " + std::to_string(slot) + ": " + (bytes.empty() ? error : why));
	}
}

void Network::sendPlayerDestruct(int slot)
{
	// ZH DisconnectManager::sendPlayerDestruct: a frame command of the local slot at TheNetwork->getExecutionFrame() + 1
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_DESTROYPLAYER;
	c.slot = (std::uint8_t)m_config.localSlot;
	c.executionFrame = executionFrame(m_logicFrame) + 1;
	c.targetSlot = (std::uint8_t)slot;
	send(c);
}

void Network::disconnectPlayer(int slot, UnsignedInt frame)
{
	// RW 0x8D57D8: the game slot's disconnect frame, the "Network:PlayerLeftGame" notice, the frame data and the connection released, the state 3, the next
	// packet router when it was the router, the fallback order without it
	if (slot < 0 || slot >= MAX_SLOTS || slot == m_config.localSlot)
	{
		return;
	}
	SlotData &sd = m_slots[(size_t)slot];
	if (sd.disconnected)
	{
		return;
	}
	if (m_relayedAny && frame <= m_lastRelayed)
	{
		m_errors.push_back("slot " + std::to_string(slot) + " disconnected at frame " + std::to_string(frame) + " after this peer ran frame " + std::to_string(m_lastRelayed)
			+ " with its commands");
	}
	sd.disconnected = true;
	sd.leaveFrame = std::min(sd.leaveFrame, frame);
	m_leftNotices.push_back(playerName(slot));
	if (!m_reconstructing)
	{
		m_transport.retire(slot); // lane ONLINE-3 r2: beginLive retires the slots gone at its end
	}
	passRouterRole(slot, frame);
	if (m_config.holdForRejoin && !sd.handedOver)
	{
		// lane ONLINE-3: an online drop holds the slot (its army stays its own) until the packet router hands it over or takes it back
		sd.held = true;
		sd.heldSince = now();
		sd.restarted = false;
		sd.rejoinRequested = false;
		sd.rejoinIssued = false;
		m_rejoinLog.push_back("slot " + std::to_string(slot) + " held for a rejoin from frame " + std::to_string(frame));
	}
}

void Network::passRouterRole(int slot, UnsignedInt frame)
{
	// lane MP-3 (review r3): the router of a frame is a function of the departure frames alone (routerAt), so it does not depend on the order in which
	// the leaves and drops arrived; the departure keeps the earliest frame (a leave and a drop of one slot)
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return;
	}
	m_departureFrame[(size_t)slot] = std::min(m_departureFrame[(size_t)slot], frame);
	m_routerOrder.erase(std::remove(m_routerOrder.begin(), m_routerOrder.end(), slot), m_routerOrder.end());
	m_packetRouter = m_routerOrder.empty() ? MAX_SLOTS : m_routerOrder.front(); // the next router of the fallback order (RW 0x8D3ECD: 8 or more = none)
}
void Network::sendFrameDataToPlayer(int slot, UnsignedInt startFrame, UnsignedInt endFrame)
{
	// RW 0x8D4761: the frames startFrame .. min(endFrame, the current frame) (at most FRAMES_TO_KEEP back), every slot's commands of each to `slot`. Only
	// frames this peer ran are complete; those are the ones relayed (INFERENCE: RotWK walks every frame data manager's list of the frame)
	if (!hasConnection(slot) || !m_relayedAny)
	{
		return;
	}
	UnsignedInt end = std::min(endFrame, m_lastRelayed);
	if (startFrame + kFramesToKeep < end)
	{
		startFrame = end - kFramesToKeep;
	}
	for (UnsignedInt f = startFrame; f <= end; ++f)
	{
		const auto h = m_history.find(f);
		if (h == m_history.end())
		{
			continue;
		}
		for (int owner = 0; owner < MAX_SLOTS; ++owner)
		{
			if (owner == slot || !h->second.present[(size_t)owner])
			{
				continue;
			}
			NetCommandMsg c;
			c.type = NETCOMMANDTYPE_FRAMERESEND;
			c.slot = (std::uint8_t)m_config.localSlot;
			c.targetSlot = (std::uint8_t)owner;
			c.frame = f;
			c.messages = h->second.commands[(size_t)owner];
			c.runAheadChange = h->second.runAheadChange[(size_t)owner];
			c.rejoinMask = h->second.rejoinMask[(size_t)owner]; // lane ONLINE-3
			std::string error;
			const std::vector<std::uint8_t> bytes = NetPacket::encodeCommand(c, &error);
			std::string why;
			if (bytes.empty() || !m_transport.sendTo(slot, bytes, &why))
			{
				m_errors.push_back("cannot resend frame " + std::to_string(f) + " to slot " + std::to_string(slot) + ": " + (bytes.empty() ? error : why));
				return;
			}
		}
		++m_framesResent;
	}
}

void Network::notePlayerLatestFrame(int slot, UnsignedInt frame)
{
	if (slot >= 0 && slot < MAX_SLOTS)
	{
		m_slots[(size_t)slot].latestFrame = std::max(m_slots[(size_t)slot].latestFrame, frame);
	}
}

// ---- lane ONLINE-3: hold, rejoin and pause ---------------------------------------------------------------------------------------------
bool Network::pingTest() const
{
	// RW 0x8D7C78 (RotWK's ping test, GameSpy's pinger in an internet game; no pinger in a LAN game: it passes): with the relay, the local link failed
	// when nothing came from the relay for NetworkDisconnectTime (every client hears its peers' keep-alives through it every 100 ms)
	return m_transport.localLinkAlive(m_config.settings.disconnectTime);
}

bool Network::reachesLivePeer(int slot) const
{
	// lane ONLINE-3: what is sent to the slot reaches a running process (and so the relay's log through its stream): not when the relay reports the slot
	// silent or its game restarted (the relay starts its streams afresh for the new process: the old stream's records are not logged)
	return !m_transport.silentAtRelay(slot) && !m_slots[(size_t)slot].restarted;
}

void Network::logAloneFrames()
{
	// lane ONLINE-3 (relay mode): when no live player receives this peer's frame commands any more (the last other one left, was dropped, went silent at
	// the relay or restarted), its recent ones go to the relay's log-only stream once more: those in flight to a player whose stream the relay just
	// started afresh (its restarted game bound) were never logged. The relay logs each command once (it counts identical bytes per frame and stream,
	// and none reaches the log-only stream twice); a LAN transport has no log stream
	bool alone = true;
	for (int s = 0; s < MAX_SLOTS && alone; ++s)
	{
		alone = !(s != m_config.localSlot && m_slots[(size_t)s].active && !hasLeft(s) && reachesLivePeer(s));
	}
	if (alone && !m_wasAlone)
	{
		logRecentFrameCommands();
	}
	m_wasAlone = alone;
}

void Network::logRecentFrameCommands()
{
	for (RecentFrameCommand &rc : m_recentFrameCommands)
	{
		if (rc.onLogStream)
		{
			continue;
		}
		if (!m_transport.sendToLog(rc.bytes))
		{
			break;
		}
		rc.onLogStream = true;
		++m_frameCommandsRelogged;
	}
}

bool Network::newIncarnationTalking(int slot) const
{
	// a held slot whose new incarnation (a newer generation at the relay) this peer's transport was started afresh for
	const SlotData &sd = m_slots[(size_t)slot];
	return sd.held && !sd.handedOver && sd.resetGeneration > sd.memberGeneration && sd.resetGeneration == sd.seenGeneration;
}

void Network::checkRelayStatus()
{
	// the relay's STATUS (Transport relay mode; a LAN transport reports generation 0 for every slot): a newer generation of a slot means its game
	// restarted and its player has a fresh ticket. While the slot is a member the old incarnation is gone (the disconnect path drops it without waiting
	// for its timeout); once it is held, its transport streams start afresh so the new incarnation's REJOINREQUEST and frame data get through
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		SlotData &sd = m_slots[(size_t)s];
		if (s == m_config.localSlot || !sd.active)
		{
			continue;
		}
		const int gen = m_transport.peerGeneration(s);
		if (gen > sd.seenGeneration)
		{
			sd.seenGeneration = gen;
			if (!sd.disconnected && !hasLeft(s) && gen > sd.memberGeneration)
			{
				sd.restarted = true;
				m_rejoinLog.push_back("slot " + std::to_string(s) + " restarted (generation " + std::to_string(gen) + ") while in the game");
			}
		}
		if (sd.held && !sd.handedOver && sd.seenGeneration > sd.memberGeneration && sd.resetGeneration < sd.seenGeneration)
		{
			sd.resetGeneration = sd.seenGeneration;
			m_transport.resetPeer(s);
		}
	}
}

void Network::routerRejoinDuties()
{
	// only the packet router of the frame its command would execute on decides (as for RUNAHEAD): it takes back a held slot that asked, or hands its army
	// over (DESTROYPLAYER, retail's MSG_SELF_DESTRUCT { TRUE }) once the rejoin window has passed
	const UnsignedInt nextExecution = std::max<UnsignedInt>(m_lastExecutionFrame, m_logicFrame + (UnsignedInt)runAheadAt(m_logicFrame));
	if (m_config.localSlot != m_packetRouter || routerAt(nextExecution) != m_config.localSlot)
	{
		return;
	}
	const std::uint64_t t = now();
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		SlotData &sd = m_slots[(size_t)s];
		if (!sd.held || sd.handedOver || sd.rejoinIssued || s == m_config.localSlot)
		{
			continue;
		}
		if (sd.rejoinRequested)
		{
			NetCommandMsg c;
			c.type = NETCOMMANDTYPE_REJOIN;
			c.slot = (std::uint8_t)m_config.localSlot;
			c.executionFrame = executionFrame(m_logicFrame);
			c.targetSlot = (std::uint8_t)s;
			send(c);
			sd.rejoinIssued = true;
			m_rejoinLog.push_back("took slot " + std::to_string(s) + " back at frame " + std::to_string(c.executionFrame) + " (packet router)");
		}
		else if (t - sd.heldSince >= (std::uint64_t)m_config.rejoinWindowMs)
		{
			m_rejoinLog.push_back("the rejoin window of slot " + std::to_string(s) + " ended: its army is handed over (packet router)");
			sendPlayerDestruct(s);
		}
	}
}

void Network::applyRejoin(int slot, UnsignedInt joinFrame)
{
	// the REJOIN ran on joinFrame - 1 on every peer alike: the slot's new incarnation is waited for from joinFrame on. It never becomes packet router or
	// decider again (its departure frame stays: routerAt, dropClaims)
	SlotData &sd = m_slots[(size_t)slot];
	sd.disconnected = false;
	sd.held = false;
	sd.restarted = false;
	sd.leaveFrame = 0xFFFFFFFFu;
	sd.joinFrame = joinFrame;
	sd.loaded = true;
	sd.memberGeneration = std::max(sd.memberGeneration, sd.seenGeneration);
	sd.rejoinRequested = false;
	sd.rejoinIssued = false;
	sd.lastHeard = now();
	while (!sd.frames.empty() && sd.frames.begin()->first < joinFrame)
	{
		sd.frames.erase(sd.frames.begin()); // what the old incarnation sent for frames it no longer runs
	}
	m_disconnect.slotRejoined(*this, slot);
	m_rejoinLog.push_back("slot " + std::to_string(slot) + " rejoined at frame " + std::to_string(joinFrame));
}

void Network::requestPause()
{
	if (m_leaving || m_rejoining)
	{
		return;
	}
	// the pause frame is this peer's next execution frame: its frame info for that frame is sent after this command (in order on every link), so no peer
	// can have run it, and every peer stops before it
	const UnsignedInt p = executionFrame(m_logicFrame);
	if (m_pauses.count(p))
	{
		return;
	}
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_PAUSE;
	c.slot = (std::uint8_t)m_config.localSlot;
	c.frame = p;
	sendDirect(c);
	file(c, m_config.localSlot);
}

void Network::requestResume()
{
	if (m_pauses.empty())
	{
		return;
	}
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_RESUME;
	c.slot = (std::uint8_t)m_config.localSlot;
	c.frame = m_pauses.begin()->first;
	sendDirect(c);
	file(c, m_config.localSlot);
}

void Network::setRejoining(int generation)
{
	m_rejoining = true;
	m_rejoinGeneration = generation;
	m_slots[(size_t)m_config.localSlot].memberGeneration = generation;
}

void Network::beginLive(const LiveStart &start)
{
	// the membership of the game so far, in the order the relay logged it (the order every peer applied it in: a drop, a leave and a rejoin are each
	// decided on agreed data); then this client is a member from start.frame
	const int local = m_config.localSlot;
	// lane ONLINE-3 r2 (Sol r2 of ONLINE-3): the history changes membership and generations only, never the transport: this client's streams with the
	// others are its current incarnation's (the relay started them afresh when it bound) and already carried the REJOINREQUEST and the frames before
	// the switch; a reset or retire of a slot that is a member now would drop their queues and restart only this end's sequence numbers. Once the
	// history is applied, the slots that are gone now (left, or dropped and not back) are retired, as a peer that saw it live has them
	m_reconstructing = true;
	for (const MembershipEvent &e : start.membership)
	{
		if (e.slot < 0 || e.slot >= MAX_SLOTS || !m_slots[(size_t)e.slot].active)
		{
			m_errors.push_back("a membership change of slot " + std::to_string(e.slot) + " in the log, which is not a network slot");
			continue;
		}
		SlotData &sd = m_slots[(size_t)e.slot];
		switch (e.kind)
		{
		case MembershipEvent::DROP:
			if (e.slot == local)
			{
				passRouterRole(local, e.frame); // this slot's earlier incarnation was dropped: never router again
			}
			else if (!sd.disconnected)
			{
				disconnectPlayer(e.slot, e.frame);
			}
			break;
		case MembershipEvent::LEAVE:
			sd.leaveFrame = std::min(sd.leaveFrame, e.frame);
			passRouterRole(e.slot, sd.leaveFrame);
			break;
		case MembershipEvent::REJOIN:
			if (e.slot != local)
			{
				// as applyRejoin after checkRelayStatus on a peer that saw it live (Sol r1 of ONLINE-3: its generation unknown, the relay's STATUS
				// made it look restarted again): this incarnation's generation is the member's, the seen and the reset one
				sd.disconnected = false;
				sd.held = false;
				sd.restarted = false;
				sd.leaveFrame = 0xFFFFFFFFu;
				sd.joinFrame = e.frame + 1;
				sd.memberGeneration = std::max(sd.memberGeneration, e.generation);
				sd.seenGeneration = std::max(sd.seenGeneration, e.generation);
				sd.resetGeneration = std::max(sd.resetGeneration, e.generation);
			}
			break;
		case MembershipEvent::DESTROY:
			sd.handedOver = true;
			break;
		}
	}
	m_reconstructing = false;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		const SlotData &sd = m_slots[(size_t)s];
		if (s != local && sd.active && (sd.disconnected || sd.leaveFrame != 0xFFFFFFFFu))
		{
			m_transport.retire(s); // gone now: nothing more goes to it
		}
	}
	SlotData &me = m_slots[(size_t)local];
	me.joinFrame = start.frame;
	me.leaveFrame = 0xFFFFFFFFu;
	me.disconnected = false;
	for (SlotData &sd : m_slots)
	{
		sd.loaded = sd.active;
		while (!sd.frames.empty() && sd.frames.begin()->first < start.frame)
		{
			sd.frames.erase(sd.frames.begin());
		}
	}
	m_lastRelayed = start.frame - 1;
	m_relayedAny = true;
	m_lastFrameAnnounced = (long long)start.frame - 1;
	m_config.runAhead = start.runAhead;
	m_baseRunAhead = start.runAhead;
	m_runAheadHistory.clear();
	m_lastExecutionFrame = 0;
	m_logicFrame = start.frame;
	m_rejoining = false;
	// this slot's CRCs that execute in its frames (the others' for the same CRC frames are in theirs): the frame data and the reports
	for (const LiveStart::Crc &crc : start.crcs)
	{
		GameMessage m(MSG_LOGIC_CRC, m_config.slotPlayerIndex[(size_t)local]);
		m.appendIntegerArgument((int)crc.hash);
		m.appendBooleanArgument(false);
		NetCommandMsg c;
		c.type = NETCOMMANDTYPE_GAMECOMMAND;
		c.slot = (std::uint8_t)local;
		c.executionFrame = crc.executionFrame;
		c.message = std::make_shared<GameMessage>(m);
		m_crcFrameOfExecution[crc.executionFrame] = crc.frame;
		m_localHalves[crc.frame] = crc.half;
		send(c);
		m_lastExecutionFrame = std::max(m_lastExecutionFrame, crc.executionFrame);
	}
	m_disconnect.init(m_config.settings);
	m_rejoinLog.push_back("rejoined: lockstep from frame " + std::to_string(start.frame) + " with run-ahead " + std::to_string(start.runAhead));
}

void Network::fillFromLog(int slot, UnsignedInt frame, const std::vector<std::shared_ptr<GameMessage>> &commands, int runAheadChange, const std::vector<int> &rejoins)
{
	if (slot < 0 || slot >= MAX_SLOTS || slot == m_config.localSlot || !m_slots[(size_t)slot].active || (m_relayedAny && frame <= m_lastRelayed))
	{
		return;
	}
	FrameData &fd = m_slots[(size_t)slot].frames[frame];
	if (fd.fromResend || (fd.commandCount >= 0 && (size_t)fd.commandCount == fd.entries()))
	{
		return; // complete already (it came live)
	}
	fd.commands.clear();
	for (const std::shared_ptr<GameMessage> &m : commands)
	{
		auto copy = std::make_shared<GameMessage>(*m);
		if (copy->getType() != MSG_SELF_DESTRUCT)
		{
			copy->friend_setPlayerIndex(m_config.slotPlayerIndex[(size_t)slot]);
		}
		fd.commands.push_back(std::move(copy));
	}
	fd.runAheadChange = runAheadChange;
	fd.rejoins = rejoins;
	fd.commandCount = (int)fd.entries();
	fd.fromResend = true;
	++m_framesFilled;
}

std::vector<std::string> Network::onlineStopLines()
{
	return {
		"[S-3900] reconnect: OpenBFME addition (RotWK 2.01 has none: a dropped player is gone, RW 0x8D57D8; RW 0x989449 lists no rejoin command): in an "
		"online game a player the disconnect path drops after its timeout is held for a rejoin window of OpenBFME's choosing (5 minutes) and handed to an "
		"ally (retail's MSG_SELF_DESTRUCT) by the packet router when it ends; a vote hands over at once; a restarted client rejoins by re-simulating the "
		"relay's command log from frame 0 (no snapshots: a long game's catch-up grows with its length) and is never packet router again; the relay is "
		"RotWK's ping test (RW 0x8D7C78) of an online game; LAN games keep retail's behaviour",
		"[S-3901] multiplayer pause: OpenBFME addition: RotWK refuses to pause a multiplayer game (GameLogic::setGamePaused RW 0x625AF1 returns when "
		"isInMultiplayerGame RW 0x441B7C (game mode + 0x110 is 1 or 5) or RW 0x610A21 holds; RW 0x989449 lists no pause command): any player pauses every "
		"peer before one frame (PAUSE, direct) and any player resumes (RESUME); no per-player limit",
		"[S-3902] spectators: retail's observer is a lobby slot (PLAYERTEMPLATE_OBSERVER) that plays in the lockstep; ported: with no slot of its own the "
		"spectator's local player is the ReplayObserver side (replay mode, RW 0x6282F0), whose map is revealed (RW 0x62F91A), and observeNextPlayer "
		"(RW 0x6A8D2B: the next human's view, RW 0xB4F440; inference: Player + 0x5C == 0 is a human). OpenBFME additions: an online spectator is no slot: "
		"a spectator ticket (version 3) reads the relay's log (LOG_REQUEST: the log file is its buffer, nothing is queued per spectator, one silent for "
		"30 s is dropped, nobody waits for one) from frame 0 and follows it, its CRCs compared with the players'; the host's delay holds the log back "
		"by game frames (5 a second) behind the latest logged frame; the observed player's resources are a line on screen and Tab / Shift+Tab step "
		"(the Palantir's ObserverStuff buttons, SetObserverStuffState RW 0x8003F6, are not driven; retail's key for the command was not located)",
	};
}

std::vector<std::string> Network::stopLines()
{
	return {
		"[S-720] run-ahead: RotWK paces a game by a packet router (RW 0x65DD0F: a 200 ms period from the performance counter; RW 0x8D3FD9: the router "
		"stalls only when a peer's latest frame is NetworkRunAheadSlack frames behind, 3 before frame 6) that stamps every command with its current frame + 1 "
		"(RW 0x8D4301 / 0x8D3AF1): the router's player waits one frame on any link, the others one frame plus their round trip to the router; that "
		"protocol is not ported: OpenBFME keeps MP-1's symmetric lockstep with the same one frame on a LAN (lane NETLAG-1: the lobby's run-ahead 1) and "
		"(lane MP-2) the packet router adapts the common command delay to its measured round trip (ZH ConnectionManager::updateRunAhead's RUNAHEAD frame "
		"command, at least the lobby's run-ahead), so on a slow link every player waits alike and a slow peer stalls the game at once",
		"[S-721] packet encoding: the net commands and packets use OpenBFME's own encoding (enhanced profile); retail's packet layout (cross-play gate 1) is not "
		"reproduced",
		"[S-722] replays: OpenBFME replays only (enhanced profile); retail .BfME2Replay playback through the command pipeline is not ported",
		"[S-723] disconnects: lane MP-2 ports RotWK's DisconnectManager (GameNetwork/DisconnectManager.h: silence, the disconnect screen, votes, the drop at "
		"one frame, DESTROYPLAYER -> MSG_SELF_DESTRUCT); what stays open is S-1121 / S-1122",
		"[S-724] lobby: lane MP-2 ports ZH LANAPI's state machine on RotWK's lobby ports and LanLobby.apt (AptLanLobby); the messages are OpenBFME's encoding, "
		"not retail's LANMessage; the scenario description and Load Game are not ported (the countdown, the chat gadgets, the message boxes and the "
		"Options button: lane UI-1, S-1261)",
		"[S-725] CRC: the exchanged CRC is OpenBFME's state hash (GameLogic::computeStateHash), not retail's xfer CRC (PLAN rule 5)",
		"[S-4530] adaptive run-ahead (lane NETLAG-1, OpenBFME choice, no retail counterpart: S-720): the packet router's target is the one-way latency (half "
		"the round trip plus four times its variation) plus NetworkRunAheadSlack percent plus half a 200 ms period (INFERENCE: room for a frame's "
		"simulation and the phase between the peers' clocks), in whole periods, + 1; a one-way latency under about 90 ms keeps RotWK's one frame",
	};
}
