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
	m_routerHistory.push_back({ 0u, m_packetRouter });
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
	int r = -1;
	for (const auto &e : m_routerHistory)
	{
		if (e.first <= frame)
		{
			r = e.second;
		}
	}
	return r;
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
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		// a slot whose leave arrived is retired: nothing more goes to it (it runs no frame after its leave frame)
		if (s != m_config.localSlot && m_slots[(size_t)s].active && !hasLeft(s))
		{
			std::string why;
			if (!m_transport.sendTo(s, bytes, &why))
			{
				m_errors.push_back("cannot send " + std::string(NetCommandTypeName(c.type)) + " to slot " + std::to_string(s) + ": " + why);
			}
		}
	}
	file(c, m_config.localSlot); // the local frame data (ZH sendLocalCommand files it for the local slot too)
}

void Network::update(UnsignedInt logicFrame, CommandList &pending)
{
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
	// lane MP-2: ConnectionManager::update -> DisconnectManager::update (RW 0x8D77B5 -> 0x8D8A90), once every player has loaded
	if (m_config.disconnectPath && allLoaded() && !m_leaving)
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
		if (m_slots[(size_t)from].disconnected)
		{
			continue; // lane MP-2: a dropped player's late traffic (its connection is retired)
		}
		m_slots[(size_t)from].lastHeard = now();
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
		else if (c.type == NETCOMMANDTYPE_DESTROYPLAYER)
		{
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
		if (fromSlot != m_config.localSlot && m_config.disconnectPath)
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
		}
		return;
	case NETCOMMANDTYPE_LOADCOMPLETE:
		sd.loaded = true;
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
	return sd.active && frame < sd.leaveFrame;
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
		if (sd.active && !sd.loaded)
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
	if (worst < 0 || m_config.localSlot != m_packetRouter)
	{
		return; // only the packet router decides (ZH: the router's RUNAHEAD); the others take its value when its frame runs
	}
	if (m_runAheadChangePending && (!m_relayedAny || m_lastRelayed < m_runAheadChangeFrame))
	{
		return; // the last change has not run yet
	}
	m_runAheadChangePending = false;
	const long long fps = 5;                                       // LOGICFRAMES_PER_SECOND (RW 0xD9F608)
	long long frames = (worst * fps + 1999) / 2000;                // one way, rounded up
	frames += frames * (long long)m_config.settings.runAheadSlack / 100;
	long long target = frames + 1;
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
	std::uint64_t last = std::max(sd.lastHeard, m_transport.lastHeardFrom(slot));
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
		if (!hasLeft(s) && !m_slots[(size_t)s].disconnected)
		{
			return false; // a predecessor this peer holds connected is the one to decide
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
		if (!hasLeft(s) && !m_slots[(size_t)s].disconnected)
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
	m_transport.retire(slot);
	if (slot == m_packetRouter)
	{
		m_packetRouter = nextPacketRouterSlot(slot);
		m_routerHistory.push_back({ frame, m_packetRouter }); // lane MP-2 (review): the role passes on at the drop frame (routerAt)
	}
	m_routerOrder.erase(std::remove(m_routerOrder.begin(), m_routerOrder.end(), slot), m_routerOrder.end());
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

std::vector<std::string> Network::stopLines()
{
	return {
		"[S-720] run-ahead: RotWK paces a game by a packet router (RW 0x65DD0F: a 200 ms period from the performance counter; RW 0x8D3FD9: the router "
		"stalls only when a peer's latest frame is NetworkRunAheadSlack frames behind, 3 before frame 6) whose command delay is the network's; that "
		"protocol is not ported: OpenBFME keeps MP-1's symmetric lockstep and (lane MP-2) the packet router adapts the common command delay to its measured "
		"round trip (ZH ConnectionManager::updateRunAhead's formula and RUNAHEAD frame command, at least the lobby's run-ahead)",
		"[S-721] packet encoding: the net commands and packets use OpenBFME's own encoding (enhanced profile); retail's packet layout (cross-play gate 1) is not "
		"reproduced",
		"[S-722] replays: OpenBFME replays only (enhanced profile); retail .BfME2Replay playback through the command pipeline is not ported",
		"[S-723] disconnects: lane MP-2 ports RotWK's DisconnectManager (GameNetwork/DisconnectManager.h: silence, the disconnect screen, votes, the drop at "
		"one frame, DESTROYPLAYER -> MSG_SELF_DESTRUCT); what stays open is S-1121 / S-1122",
		"[S-724] lobby: lane MP-2 ports ZH LANAPI's state machine on RotWK's lobby ports and LanLobby.apt (AptLanLobby); the messages are OpenBFME's encoding, "
		"not retail's LANMessage; the scenario description and Load Game are not ported (the countdown, the chat gadgets, the message boxes and the "
		"Options button: lane UI-1, S-1261)",
		"[S-725] CRC: the exchanged CRC is OpenBFME's state hash (GameLogic::computeStateHash), not retail's xfer CRC (PLAN rule 5)",
	};
}
