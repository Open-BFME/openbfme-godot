// OpenBFME. GPL-3.0.
// See GameNetwork/RelayLog.h.

#include "GameNetwork/RelayLog.h"

#include <fstream>
#include <iterator>

namespace
{
bool fail(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = "relay log: " + why;
	}
	return false;
}
std::uint64_t u64(NetByteReader &r)
{
	const std::uint64_t lo = r.u32();
	return lo | ((std::uint64_t)r.u32() << 32);
}
} // namespace

bool RelayLogFile::load(const std::string &path, RelayLogFile &out, std::string *error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
	{
		return fail(error, "cannot read " + path);
	}
	const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return parse(bytes, out, error);
}

bool RelayLogFile::parse(const std::vector<std::uint8_t> &bytes, RelayLogFile &out, std::string *error, bool partialTail)
{
	static const char kMagic[8] = { 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'G' };
	if (bytes.size() < 8 || std::string(bytes.begin(), bytes.begin() + 8) != std::string(kMagic, 8))
	{
		return fail(error, "not an OpenBFME relay log (magic)");
	}
	NetByteReader r(bytes.data() + 8, bytes.size() - 8);
	RelayLogFile l;
	const std::uint32_t version = r.u32();
	if (version != 1 && version != 2)
	{
		return fail(error, "log version " + std::to_string(version) + " (this build reads 1 and 2)");
	}
	l.version = (int)version;
	l.gameId = u64(r);
	l.slotMask = r.u8();
	l.startUnix = r.u32();
	if (r.failed())
	{
		return fail(error, "truncated header");
	}
	static const char *kEvents[] = { "?", "bound", "rebound", "bye" };
	// lane ONLINE-3: the record sizes, to tell a record cut at the end of a log still being written (partialTail) from a malformed one
	auto cutHere = [&](size_t need) { return partialTail && r.remaining() < need; };
	while (!r.atEnd())
	{
		const size_t remainingBefore = r.remaining();
		const std::uint8_t kind = bytes[bytes.size() - remainingBefore];
		const size_t fixed = kind == 1 ? 10 : kind == 2 ? 7 : kind == 3 ? 8 : kind == 4 ? 6 + 8 * 24 : kind == 5 ? 8 : 1;
		if (cutHere(fixed))
		{
			break;
		}
		if (kind == 1 && partialTail)
		{
			const size_t at = bytes.size() - remainingBefore;
			const std::uint32_t len = (std::uint32_t)bytes[at + 6] | ((std::uint32_t)bytes[at + 7] << 8) | ((std::uint32_t)bytes[at + 8] << 16) | ((std::uint32_t)bytes[at + 9] << 24);
			if (cutHere(fixed + (size_t)len))
			{
				break;
			}
		}
		r.u8();
		if (kind == 1)
		{
			Command c;
			c.sender = r.u8();
			c.ms = r.u32();
			const std::uint32_t len = r.u32();
			if (r.failed() || len > r.remaining() || c.sender >= 8)
			{
				return fail(error, "truncated or malformed COMMAND record");
			}
			const std::vector<std::uint8_t> cmd(bytes.end() - (long)r.remaining(), bytes.end() - (long)r.remaining() + (long)len);
			for (std::uint32_t i = 0; i < len; ++i)
			{
				r.u8();
			}
			std::string why;
			if (!NetPacket::decodeCommand(cmd, c.msg, &why))
			{
				return fail(error, "a command of slot " + std::to_string(c.sender) + " at " + std::to_string(c.ms) + " ms does not decode: " + why);
			}
			l.commands.push_back(std::move(c));
		}
		else if (kind == 2)
		{
			const int slot = r.u8(), event = r.u8();
			const std::uint32_t ms = r.u32();
			l.events.push_back(std::to_string(ms) + " slot " + std::to_string(slot) + " " + kEvents[event >= 1 && event <= 3 ? event : 0]);
		}
		else if (kind == 3)
		{
			const int sender = r.u8(), target = r.u8();
			const std::uint32_t ms = r.u32();
			const int reason = r.u8();
			l.gaps.push_back("sender " + std::to_string(sender) + " target " + std::to_string(target) + " at " + std::to_string(ms) + " ms: "
				+ (reason == 1 ? "the reorder buffer overflowed" : "a malformed record"));
		}
		else if (kind == 5 && version >= 2)
		{
			// lane ONLINE-3: REJOINED: the slot's new incarnation (its restarted game) bound with a rejoin ticket
			RelayLogFile::Rejoined j;
			j.slot = r.u8();
			j.generation = r.u16();
			const std::uint32_t ms = r.u32();
			j.firstCommand = l.commands.size();
			if (j.slot >= 8)
			{
				return fail(error, "a REJOINED record of slot " + std::to_string(j.slot));
			}
			l.rejoins.push_back(j);
			l.events.push_back(std::to_string(ms) + " slot " + std::to_string(j.slot) + " rejoined generation " + std::to_string(j.generation));
		}
		else if (kind == 4)
		{
			r.u32();
			l.endReason = r.u8();
			for (int s = 0; s < 8; ++s)
			{
				l.datagramsIn[(size_t)s] = u64(r);
				l.bytesIn[(size_t)s] = u64(r);
				l.dropped[(size_t)s] = u64(r);
			}
			l.complete = true;
			if (!r.atEnd())
			{
				return fail(error, "bytes after the END record");
			}
		}
		else
		{
			return fail(error, "unknown record kind " + std::to_string(kind));
		}
		if (r.failed())
		{
			return fail(error, "truncated record");
		}
	}
	out = std::move(l);
	return true;
}

int RelayLog::Assembled::runAheadAfter(UnsignedInt frame, int initial) const
{
	int ra = initial;
	for (const auto &kv : runAheadChanges)
	{
		if (kv.first > frame)
		{
			break;
		}
		ra = kv.second;
	}
	return ra;
}

bool RelayLog::assemble(const RelayLogFile &log, const NetworkConfig &config, Assembled &out, std::string *error, bool live)
{
	Assembled a;
	a.leaveFrame.fill(0xFFFFFFFFu);
	a.joinFrame.fill(0);
	constexpr UnsignedInt kNever = 0xFFFFFFFFu;
	struct FrameOfSlot
	{
		int count = -1; ///< FRAMEINFO
		std::vector<std::shared_ptr<GameMessage>> commands;
		int runAhead = -1; ///< the packet router's RUNAHEAD of the frame: one of the FRAMEINFO's entries (Network.h FrameData::entries), no message
		std::vector<int> rejoins; ///< lane ONLINE-3: the REJOINs of the frame (entries too)
		size_t entries() const { return commands.size() + (runAhead >= 0 ? 1 : 0) + rejoins.size(); }
	};
	// lane ONLINE-3: every slot's incarnations: the first one from frame 0, each later one (a REJOINED record) from the frame after the packet router's
	// REJOIN of it ran, each until its leave or its drop
	struct Incarnation
	{
		std::map<UnsignedInt, FrameOfSlot> frames;
		UnsignedInt join = 0, leave = kNever, drop = kNever;
		int generation = 0; ///< lane ONLINE-3: the REJOINED record's generation (0: the first incarnation)
		UnsignedInt end() const { return std::min(leave, drop); }
	};
	std::array<std::vector<Incarnation>, 8> inc;
	for (int s = 0; s < 8; ++s)
	{
		if ((log.slotMask & (1u << s)) && config.slotPlayerIndex[(size_t)s] < 0)
		{
			return fail(error, "network slot " + std::to_string(s) + " has no player in the loaded game");
		}
		inc[(size_t)s].resize(1);
	}
	std::array<std::vector<UnsignedInt>, 8> rejoinRan; // per slot, the frames its REJOINs ran on, in log order
	std::vector<std::pair<int, UnsignedInt>> drops;     // (slot, frame) in log order
	size_t nextRejoin = 0;
	for (size_t ci = 0; ci < log.commands.size(); ++ci)
	{
		while (nextRejoin < log.rejoins.size() && log.rejoins[nextRejoin].firstCommand <= ci)
		{
			const int rs = log.rejoins[nextRejoin].slot;
			inc[(size_t)rs].emplace_back();
			inc[(size_t)rs].back().join = kNever; // until its REJOIN is seen
			inc[(size_t)rs].back().generation = log.rejoins[nextRejoin].generation;
			++nextRejoin;
		}
		const RelayLogFile::Command &c = log.commands[ci];
		const NetCommandMsg &m = c.msg;
		const int s = c.sender;
		if (!(log.slotMask & (1u << s)))
		{
			return fail(error, "a command of slot " + std::to_string(s) + ", which is not a network slot");
		}
		Incarnation &in = inc[(size_t)s].back();
		if (m.type == NETCOMMANDTYPE_DISCONNECTPLAYER)
		{
			// lane ONLINE-3: the decider's drop (logged once by the relay): the target runs no frame from m.frame on
			if (m.targetSlot < 8 && (log.slotMask & (1u << m.targetSlot)))
			{
				drops.push_back({ (int)m.targetSlot, m.frame });
				a.membership.push_back({ Network::MembershipEvent::DROP, (int)m.targetSlot, m.frame });
			}
			continue;
		}
		if (m.executionFrame >= in.leave)
		{
			continue; // Network::relayCommands discards a leaver's frames from its leave on
		}
		FrameOfSlot &f = in.frames[m.executionFrame];
		switch (m.type)
		{
		case NETCOMMANDTYPE_FRAMEINFO:
			f.count = m.commandCount;
			break;
		case NETCOMMANDTYPE_GAMECOMMAND:
		{
			auto msg = std::make_shared<GameMessage>(*m.message);
			msg->friend_setPlayerIndex(config.slotPlayerIndex[(size_t)s]); // NetGameCommandMsg::constructGameMessage: the sender's player
			f.commands.push_back(std::move(msg));
			break;
		}
		case NETCOMMANDTYPE_DESTROYPLAYER:
		{
			const int victim = m.targetSlot < 8 ? config.slotPlayerIndex[m.targetSlot] : -1;
			auto msg = std::make_shared<GameMessage>(MSG_SELF_DESTRUCT, victim >= 0 ? victim : config.slotPlayerIndex[(size_t)s]);
			if (victim >= 0)
			{
				msg->appendBooleanArgument(true);
				a.membership.push_back({ Network::MembershipEvent::DESTROY, (int)m.targetSlot, m.executionFrame });
			}
			else
			{
				a.problems.push_back("DESTROYPLAYER of slot " + std::to_string(m.targetSlot) + ", which has no player");
			}
			f.commands.push_back(std::move(msg));
			break;
		}
		case NETCOMMANDTYPE_PLAYERLEAVE:
			in.leave = std::min<UnsignedInt>(in.leave, m.executionFrame);
			a.membership.push_back({ Network::MembershipEvent::LEAVE, s, m.executionFrame });
			break;
		case NETCOMMANDTYPE_RUNAHEAD:
			f.runAhead = (int)m.frame; // no message of the frame (the run-ahead only decides when later commands execute), but counted by the FRAMEINFO
			break;
		case NETCOMMANDTYPE_REJOIN:
			f.rejoins.push_back((int)m.targetSlot); // lane ONLINE-3: counted by the FRAMEINFO; the membership is decided below, in frame order
			break;
		default:
			break;
		}
	}
	while (nextRejoin < log.rejoins.size())
	{
		const int rs = log.rejoins[nextRejoin].slot;
		inc[(size_t)rs].emplace_back();
		inc[(size_t)rs].back().join = kNever;
		++nextRejoin;
	}
	// the frames, in order: every needed incarnation announced, and the entries it announced are all here. A REJOIN of a held slot by the frame's packet
	// router (Network::relayCommands: here the sender is taken as it, the peers checked it) starts the slot's next incarnation at the frame after; a drop
	// ends the incarnation it falls in
	auto applyDrops = [&](int s) {
		for (const auto &d : drops)
		{
			if (d.first != s)
			{
				continue;
			}
			for (auto it = inc[(size_t)s].rbegin(); it != inc[(size_t)s].rend(); ++it)
			{
				if (it->join != kNever && it->join <= d.second)
				{
					it->drop = std::min(it->drop, d.second);
					break;
				}
			}
		}
	};
	for (int s = 0; s < 8; ++s)
	{
		applyDrops(s);
	}
	UnsignedInt last = 0;
	for (const auto &sl : inc)
	{
		for (const Incarnation &in : sl)
		{
			if (!in.frames.empty())
			{
				last = std::max(last, in.frames.rbegin()->first);
			}
		}
	}
	std::array<bool, 8> gone{};
	std::array<int, 8> crcCount{};            // per slot, the CRCs its first incarnation sent (the n-th describes frame (n + 1) x interval)
	std::map<UnsignedInt, UnsignedInt> crcFrameOfExecution;
	std::map<UnsignedInt, std::map<int, std::uint32_t>> crcsOf; // CRC frame -> slot -> value
	struct LateCrc
	{
		int slot;
		std::uint32_t value;
	};
	std::vector<LateCrc> lateCrcs; // this frame's CRCs of rejoined incarnations, mapped once the frame's first-incarnation CRCs named the frame
	for (UnsignedInt frame = 0; frame <= last; ++frame)
	{
		CommandList list;
		bool complete = true;
		std::vector<int> rejoinedNow;
		bool anyLive = false;
		for (int s = 0; s < 8; ++s)
		{
			if (!(log.slotMask & (1u << s)) || gone[(size_t)s])
			{
				continue;
			}
			int k = -1;
			for (int i = (int)inc[(size_t)s].size() - 1; i >= 0; --i)
			{
				const Incarnation &in = inc[(size_t)s][(size_t)i];
				if (in.join != kNever && in.join <= frame && frame < in.end())
				{
					k = i;
					break;
				}
			}
			if (k < 0)
			{
				// not a member at this frame: left / dropped, or held until a later REJOIN (a held slot keeps the game going)
				const Incarnation &lastInc = inc[(size_t)s].back();
				anyLive = anyLive || (lastInc.join == kNever || frame < lastInc.end());
				continue;
			}
			anyLive = true;
			Incarnation &in = inc[(size_t)s][(size_t)k];
			const auto it = in.frames.find(frame);
			if (it == in.frames.end() || it->second.count < 0)
			{
				if (live)
				{
					complete = false; // the game goes on: this member's frame info has not reached the log yet
					continue;
				}
				// no FRAMEINFO: the incarnation's log ends here. Gone from this frame on (the end of the game, or a crash nobody dropped)
				gone[(size_t)s] = true;
				bool later = false;
				for (const auto &kv : in.frames)
				{
					later = later || (kv.first > frame && kv.second.count >= 0);
				}
				if (later)
				{
					a.problems.push_back("slot " + std::to_string(s) + " has no frame info for frame " + std::to_string(frame) + " but has some later: a log gap");
					complete = false;
				}
				continue;
			}
			if ((size_t)it->second.count != it->second.entries())
			{
				if (!live)
				{
					a.problems.push_back("frame " + std::to_string(frame) + " slot " + std::to_string(s) + ": " + std::to_string(it->second.entries())
						+ " commands in the log, the frame info announced " + std::to_string(it->second.count));
				}
				complete = false;
				continue;
			}
			for (const auto &m : it->second.commands)
			{
				if (m->getType() == MSG_LOGIC_CRC)
				{
					const GameMessageArgument *arg = m->getArgument(0);
					const std::uint32_t v = arg ? (std::uint32_t)arg->integer : 0u;
					if (k == 0 && config.crcInterval > 0)
					{
						const UnsignedInt described = (UnsignedInt)(++crcCount[(size_t)s]) * (UnsignedInt)config.crcInterval;
						crcFrameOfExecution[frame] = described;
						crcsOf[described][s] = v;
					}
					else if (config.crcInterval > 0)
					{
						lateCrcs.push_back({ s, v });
					}
				}
				list.append(*m);
			}
			if (it->second.runAhead >= 1)
			{
				a.runAheadChanges[frame] = it->second.runAhead;
			}
			for (int t : it->second.rejoins)
			{
				rejoinedNow.push_back(t);
			}
		}
		if (!anyLive)
		{
			break;
		}
		if (!complete)
		{
			break; // nothing after an incomplete frame can be trusted
		}
		// a rejoined incarnation's CRCs: the CRC frame the first incarnations' CRCs of the same execution frame describe (every peer stamps the CRC of
		// frame C with the same execution frame)
		for (const LateCrc &lc : lateCrcs)
		{
			const auto described = crcFrameOfExecution.find(frame);
			if (described == crcFrameOfExecution.end())
			{
				a.problems.push_back("slot " + std::to_string(lc.slot) + "'s CRC on frame " + std::to_string(frame)
					+ " (a rejoined incarnation) has no CRC of a first incarnation beside it to name its frame");
				continue;
			}
			crcsOf[described->second][lc.slot] = lc.value;
		}
		lateCrcs.clear();
		for (int t : rejoinedNow)
		{
			// the slot's next incarnation without a join frame yet starts at the frame after this one
			if (t < 0 || t >= 8)
			{
				continue;
			}
			for (Incarnation &in : inc[(size_t)t])
			{
				if (in.join == kNever)
				{
					in.join = frame + 1;
					a.membership.push_back({ Network::MembershipEvent::REJOIN, t, frame, in.generation });
					break;
				}
			}
			applyDrops(t); // a drop of the new incarnation logged before its REJOIN ran
		}
		if (!list.messages().empty())
		{
			a.frames[frame] = std::move(list);
		}
		a.lastCompleteFrame = frame;
		a.anyComplete = true;
	}
	for (int s = 0; s < 8; ++s)
	{
		if (!(log.slotMask & (1u << s)))
		{
			continue;
		}
		const Incarnation &lastInc = inc[(size_t)s].back();
		a.incarnations[(size_t)s] = (int)inc[(size_t)s].size();
		a.joinFrame[(size_t)s] = lastInc.join;
		a.leaveFrame[(size_t)s] = lastInc.end();
		for (const auto &kv : lastInc.frames)
		{
			if (kv.second.count >= 0 && (size_t)kv.second.count == kv.second.entries() && kv.first < lastInc.end())
			{
				Assembled::SlotFrame sf;
				sf.commands = kv.second.commands;
				sf.runAheadChange = kv.second.runAhead;
				sf.rejoins = kv.second.rejoins;
				a.latestFrames[(size_t)s][kv.first] = std::move(sf);
			}
		}
		if (!live && gone[(size_t)s] && lastInc.leave == kNever && lastInc.drop == kNever && log.endReason != 1)
		{
			a.problems.push_back("slot " + std::to_string(s) + "'s frames end without a leave or a logged drop and the game did not end with every BYE: a crash "
								 "at the end (S-3710)");
		}
	}
	// the hash records: every CRC frame the complete frames carried, the slots' values agreed or not
	for (const auto &kv : crcsOf)
	{
		if (kv.first > a.lastCompleteFrame + 1 || kv.second.empty())
		{
			continue;
		}
		bool agree = true;
		const std::uint32_t value = kv.second.begin()->second;
		for (const auto &sv : kv.second)
		{
			agree = agree && sv.second == value;
		}
		++a.crcFrames;
		if (!agree)
		{
			++a.crcDisagreements;
			a.problems.push_back("the players' CRCs of frame " + std::to_string(kv.first) + " differ: the game desynced there");
			continue;
		}
		a.hashes[kv.first] = value;
	}
	out = std::move(a);
	return true;
}

bool RelayLog::writeReplay(const std::string &path, const ReplayHeader &header, const Assembled &a, std::string *error)
{
	ReplayWriter w;
	if (!w.open(path, header, error))
	{
		return false;
	}
	// in frame order, the hash of frame F (the state after frame F - 1 ran) after frame F - 1's commands, as LockstepDriver records them
	auto h = a.hashes.begin();
	for (const auto &f : a.frames)
	{
		while (h != a.hashes.end() && h->first <= f.first)
		{
			w.recordHash(h->first, h->second);
			++h;
		}
		w.recordFrame(f.first, f.second);
	}
	for (; h != a.hashes.end(); ++h)
	{
		w.recordHash(h->first, h->second);
	}
	if (!w.errors().empty())
	{
		return fail(error, w.errors().front());
	}
	return true;
}

std::vector<std::string> RelayLog::stopLines()
{
	return {
		"[S-3710] relay log replay: the relay logs each player's own frame commands, the players' CRCs and (lane ONLINE-3) the drops and rejoins; a replay "
		"made from it has a hash every CRC interval (not every frame) and no end record; a player whose frames end with neither a leave nor a logged drop "
		"(a crash at the end of the game) is replayed as gone from its first missing frame",
		"[S-3711] relay trust (phase O1): tickets are HMAC-SHA256 with one key ring shared by the lobby and every relay (a relay could mint tickets); the "
		"envelope is not encrypted, so an on-path observer who reads a slot's WELCOME (its session token) could rebind that slot until the game ends; IPv4 "
		"only; no per-hop relay (docs/ONLINE.md phase O3); a rejoin ticket of a newer generation binds a slot without its session token (S-3900)",
	};
}
