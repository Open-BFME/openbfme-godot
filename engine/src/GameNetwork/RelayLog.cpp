// OpenBFME. GPL-3.0.
// See GameNetwork/RelayLog.h.

#include "GameNetwork/RelayLog.h"
#include "Common/PathIO.h"

#include "GameNetwork/RelayFrameBundle.h"
#include "GameNetwork/RelayMembership.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>

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
	std::ifstream f(PathIO::native(path), std::ios::binary);
	if (!f)
	{
		return fail(error, "cannot read " + path);
	}
	const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (!parse(bytes, out, error))
	{
		return false;
	}
	// online batch r4: a version-2 log's INPUT records in its sidecar (RelayCore.h "INPUT sidecar"), when the relay wrote one
	std::ifstream side(path + ".input", std::ios::binary | std::ios::ate);
	if (out.version >= 4 || !side)
	{
		return true;
	}
	// online batch r6 (Sol): bounded before it is buffered: a valid sidecar of an eight-slot game is at most 527 bytes
	const std::streamoff sideSize = side.tellg();
	if (sideSize < 0 || sideSize > (std::streamoff)kMaxInputSidecarBytes)
	{
		return fail(error, "the INPUT sidecar of " + path + " is " + std::to_string((long long)sideSize) + " bytes (at most " +
			std::to_string(kMaxInputSidecarBytes) + ")");
	}
	side.seekg(0);
	const std::vector<std::uint8_t> sideBytes((std::istreambuf_iterator<char>(side)), std::istreambuf_iterator<char>());
	return parseInputSidecar(sideBytes, path.substr(path.find_last_of("/\\") + 1), out, error);
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
	if (version != 1 && version != 2 && version != 4)
	{
		return fail(error, "log version " + std::to_string(version) + " (this build reads 1, 2 and 4)");
	}
	l.version = (int)version;
	l.gameId = u64(r);
	l.slotMask = r.u8();
	if (version == 4)
	{
		l.slotMask = (std::uint16_t)(l.slotMask | ((std::uint16_t)r.u8() << 8));
	}
	l.startUnix = r.u32();
	if (r.failed())
	{
		return fail(error, "truncated header");
	}
	if (l.slotMask == 0 || (version != 4 && (l.slotMask & 0xff00u)))
	{
		return fail(error, "invalid slot mask");
	}
	static const char *kEvents[] = { "?", "bound", "rebound", "bye" };
	// lane ONLINE-3: the record sizes, to tell a record cut at the end of a log still being written (partialTail) from a malformed one
	auto cutHere = [&](size_t need) { return partialTail && r.remaining() < need; };
	while (!r.atEnd())
	{
		const size_t remainingBefore = r.remaining();
		const std::uint8_t kind = bytes[bytes.size() - remainingBefore];
		const size_t wireSlots = l.slotMask <= 0xff ? 8 : RelayProtocol::kMaxSlots;
		const size_t fixed = kind == 1 ? 10 : kind == 2 ? 7 : kind == 3 ? 8 : kind == 4 ? 6 + wireSlots * 24 : kind == 5 ? 8 : kind == 6 ? 5 : kind == 7 ? 3 + 7 * 8 : 1;
		if (cutHere(fixed))
		{
			break;
		}
		if ((kind == 1 || kind == 6) && partialTail)
		{
			const size_t at = bytes.size() - remainingBefore;
			const size_t lenAt = at + (kind == 1 ? 6 : 1);
			const std::uint32_t len = (std::uint32_t)bytes[lenAt] | ((std::uint32_t)bytes[lenAt + 1] << 8) | ((std::uint32_t)bytes[lenAt + 2] << 16)
				| ((std::uint32_t)bytes[lenAt + 3] << 24);
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
			if (r.failed() || len > r.remaining() || c.sender >= wireSlots || !(l.slotMask & (std::uint16_t(1) << c.sender)))
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
			if (r.failed() || slot >= wireSlots || event < 1 || event > 3 || !(l.slotMask & (std::uint16_t(1) << slot)))
			{
				return fail(error, "invalid EVENT slot " + std::to_string(slot));
			}
			l.events.push_back(std::to_string(ms) + " slot " + std::to_string(slot) + " " + kEvents[event]);
		}
		else if (kind == 3)
		{
			const int sender = r.u8(), target = r.u8();
			const std::uint32_t ms = r.u32();
			const int reason = r.u8();
			if (r.failed() || sender >= wireSlots || target >= wireSlots || !(l.slotMask & (std::uint16_t(1) << sender))
				|| !(l.slotMask & (std::uint16_t(1) << target)))
			{
				return fail(error, "invalid GAP slot");
			}
			if (reason != 1 && reason != 2)
			{
				// lane NETDIAG-1 (from a contributor's parser hardening): RelayCore::gap writes reason 1 (reorder buffer) or 2 (malformed) only
				return fail(error, "invalid GAP reason " + std::to_string(reason));
			}
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
			if (r.failed() || j.slot >= wireSlots || !(l.slotMask & (std::uint16_t(1) << j.slot)))
			{
				return fail(error, "a REJOINED record of slot " + std::to_string(j.slot));
			}
			l.rejoins.push_back(j);
			l.events.push_back(std::to_string(ms) + " slot " + std::to_string(j.slot) + " rejoined generation " + std::to_string(j.generation));
		}
		else if (kind == 6)
		{
			// lane ROUTER-1: a canonical game's committed OBRC record (authority append): u32 length, then the record bytes
			const std::uint32_t len = r.u32();
			if (r.failed() || len > r.remaining() || len > 64 * 1024 * 1024)
			{
				return fail(error, "truncated or malformed CANONICAL record");
			}
			const std::vector<std::uint8_t> rec(bytes.end() - (long)r.remaining(), bytes.end() - (long)r.remaining() + (long)len);
			for (std::uint32_t i = 0; i < len; ++i)
			{
				r.u8();
			}
			RelayCommit::Record decoded;
			std::string why;
			if (!RelayCommit::decode(rec, rec.size(), decoded, why))
			{
				return fail(error, "a CANONICAL record does not decode: " + why);
			}
			l.canonical.push_back(std::move(decoded));
		}
		else if (kind == 4)
		{
			r.u32();
			l.endReason = r.u8();
			if (!r.failed() && (l.endReason < 1 || l.endReason > 3))
			{
				// lane NETDIAG-1 (from a contributor's parser hardening): RelayCore::close writes 1 (every BYE), 2 (silence) or 3 (shutdown) only
				return fail(error, "invalid END reason " + std::to_string(l.endReason));
			}
			for (size_t s = 0; s < wireSlots; ++s)
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
		else if (kind == 7 && version >= 4)
		{
			// lane ANTICHEAT-1: INPUT: a slot's c2r datagrams as AUTH-1 verified them (RelayCore.h "u8 7 INPUT")
			RelayLogFile::Input in;
			in.slot = r.u8();
			in.hadKey = r.u8() != 0;
			in.signedIn = u64(r);
			in.plainIn = u64(r);
			in.badTag = u64(r);
			in.replayed = u64(r);
			in.unsignedRefused = u64(r);
			in.slotMismatch = u64(r);
			in.malformed = u64(r);
			if (r.failed() || in.slot >= (int)wireSlots)
			{
				return fail(error, "an INPUT record of slot " + std::to_string(in.slot));
			}
			l.inputs.push_back(in);
		}
		else if (kind == 5)
		{
			return fail(error, "a REJOINED record in a log of version " + std::to_string(version));
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

bool RelayLogFile::parseInputSidecar(const std::vector<std::uint8_t> &bytes, const std::string &logName, RelayLogFile &io, std::string *error)
{
	static const char kMagic[8] = { 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'I' };
	if (bytes.size() < 8 || std::string(bytes.begin(), bytes.begin() + 8) != std::string(kMagic, 8))
	{
		return fail(error, "not an INPUT sidecar (magic)");
	}
	NetByteReader r(bytes.data() + 8, bytes.size() - 8);
	if (r.u32() != 1)
	{
		return fail(error, "INPUT sidecar version (this build reads 1)");
	}
	const std::uint64_t gameId = u64(r);
	const int flags = r.u8();
	const int opener = r.u8();
	const std::uint32_t hellos = r.u32();
	const std::uint32_t nameBytes = r.u16();
	std::string name;
	for (std::uint32_t i = 0; i < nameBytes && !r.failed(); ++i)
	{
		name += (char)r.u8();
	}
	const int count = r.u8();
	if (r.failed() || gameId != io.gameId || name != logName || (flags & ~1) != 0 || opener >= (int)RelayProtocol::kMaxSlots)
	{
		return fail(error, "the INPUT sidecar (" + name + ") is not this log's (" + logName + ") or is malformed");
	}
	std::vector<RelayLogFile::Input> inputs;
	for (int i = 0; i < count; ++i)
	{
		RelayLogFile::Input in;
		const int kind = r.u8();
		in.slot = r.u8();
		in.hadKey = r.u8() != 0;
		in.signedIn = u64(r);
		in.plainIn = u64(r);
		in.badTag = u64(r);
		in.replayed = u64(r);
		in.unsignedRefused = u64(r);
		in.slotMismatch = u64(r);
		in.malformed = u64(r);
		if (r.failed() || kind != 7 || in.slot >= (int)RelayProtocol::kMaxSlots || !(io.slotMask & (1u << in.slot)))
		{
			return fail(error, "the INPUT sidecar's record " + std::to_string(i) + " is malformed");
		}
		inputs.push_back(in);
	}
	if (!r.atEnd())
	{
		return fail(error, "bytes after the INPUT sidecar's records");
	}
	io.inputs = std::move(inputs);
	io.inputSidecar = true;
	io.legacyAuthAnomaly = (flags & 1) != 0;
	io.openerSlot = opener;
	io.authenticatedHellos = hellos;
	return true;
}

std::string RelayLog::packetIntegrityReport(const RelayLogFile &log, const NetworkConfig &config, const Assembled &assembled, const std::string &gameId)
{
	auto js = [](const std::string &t) {
		std::string o = "\"";
		for (unsigned char c : t)
		{
			if (c == '"' || c == '\\')
			{
				o += '\\';
				o += (char)c;
			}
			else if (c >= 0x20)
			{
				o += (char)c;
			}
		}
		return o + "\"";
	};
	std::map<int, int> malformedRecords; // the stream gaps a malformed record caused, by sender
	for (const std::string &gap : log.gaps)
	{
		if (gap.find("a malformed record") != std::string::npos && gap.rfind("sender ", 0) == 0)
		{
			++malformedRecords[std::atoi(gap.c_str() + 7)];
		}
	}
	// online batch r4: a version-2 log's records come from its sidecar; a player that authenticated under a version-2 log is a finding: the slot whose
	// HELLO chose version 2 (a previous-release HELLO, which kept every INPUT record out of the log) is the one to review
	const bool haveInputs = log.version >= 4 || log.inputSidecar;
	const std::string anomaly = log.legacyAuthAnomaly ? "a version-2 (previous-release) relay log, yet " + std::to_string(log.authenticatedHellos) +
		" authenticated player HELLO(s): one release per game is the honest case; slot " + std::to_string(log.openerSlot) +
		"'s HELLO opened the game in the previous release's envelope" : std::string();
	std::string j = "{\"v\":1,\"detector\":\"D7-packet-integrity\",\"detector_version\":1,\"game_id\":" + (gameId.empty() ? std::string("null") : js(gameId)) +
		",\"status\":\"" + std::string(haveInputs ? "ok" : "abstained") + "\",\"abstain_reason\":" +
		(haveInputs ? std::string("null") : js("the relay log (version " + std::to_string(log.version) + ") has no INPUT records" +
			(log.version == 2 ? " and no INPUT sidecar" : ""))) +
		",\"anomaly\":" + (anomaly.empty() ? std::string("null") : js(anomaly)) +
		",\"frames\":" + std::to_string(assembled.lastCompleteFrame) + ",\"crc_parity\":\"" + (assembled.crcDisagreements ? "mismatch" : "match") +
		"\",\"settings\":{},\"players\":[";
	bool first = true;
	for (const RelayLogFile::Input &in : log.inputs)
	{
		const unsigned long long refused = in.badTag + in.replayed + in.unsignedRefused + in.slotMismatch + in.malformed +
			(unsigned long long)malformedRecords[in.slot];
		const char *input = in.signedIn && !in.plainIn ? "signed" : in.plainIn && !in.signedIn ? "plain" : in.signedIn ? "mixed" : "none";
		const bool opened = !anomaly.empty() && in.slot == log.openerSlot;
		const bool review = refused || opened;
		j += (first ? "" : ",") + std::string("{\"player_index\":") + std::to_string(config.slotPlayerIndex[(size_t)in.slot]) + ",\"slot\":" +
			 std::to_string(in.slot) + ",\"name\":\"\",\"band\":\"" + (!haveInputs ? "abstained" : review ? "review" : "none") +
			 "\",\"score\":" + (review ? "1.000000" : "0.000000") + ",\"encounters\":" + (review ? "1" : "0") + ",\"input\":\"" + input +
			 "\",\"signed_datagrams\":" + std::to_string(in.signedIn) + ",\"plain_datagrams\":" + std::to_string(in.plainIn) +
			 ",\"bad_tag\":" + std::to_string(in.badTag) + ",\"replayed\":" + std::to_string(in.replayed) + ",\"unsigned_refused\":" +
			 std::to_string(in.unsignedRefused) + ",\"slot_mismatch\":" + std::to_string(in.slotMismatch) + ",\"malformed\":" +
			 std::to_string(in.malformed) + ",\"malformed_records\":" + std::to_string(malformedRecords[in.slot]) + ",\"anomaly\":" +
			 (opened ? js(anomaly) : std::string("null")) + ",\"events\":[]}";
		first = false;
	}
	j += "]}\n";
	return j;
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

// S-1064: replay the native relay's DROP / REJOIN flags for score presentation (RW 0x90313E), not simulation. assemble appends REJOIN events after
// collecting drops, so vector order is not execution order. A rejoin completes on its frame; a drop excludes the slot from the start of its frame.
std::uint16_t RelayLog::Assembled::disconnectedSlotsAt(UnsignedInt nextFrame) const
{
	// ONLINE16-FIX: sixteen slots wide (the u8 mask ignored the drops of slots 8 .. 15)
	std::array<std::uint64_t, RelayProtocol::kMaxSlots> latest{};
	std::uint16_t disconnected = 0;
	for (const Network::MembershipEvent &e : membership)
	{
		if (e.slot < 0 || e.slot >= RelayProtocol::kMaxSlots || (e.kind != Network::MembershipEvent::DROP && e.kind != Network::MembershipEvent::REJOIN))
		{
			continue;
		}
		const bool drop = e.kind == Network::MembershipEvent::DROP;
		const std::uint64_t boundary = (std::uint64_t)e.frame + (drop ? 0 : 1);
		// At an equal boundary a drop of the new incarnation wins over its completed rejoin, matching assemble's applyDrops after REJOIN.
		const std::uint64_t order = boundary * 2 + (drop ? 1 : 0);
		if (boundary > nextFrame || order < latest[(size_t)e.slot])
		{
			continue;
		}
		latest[(size_t)e.slot] = order;
		const auto bit = (std::uint16_t)(1u << e.slot);
		disconnected = drop ? (std::uint16_t)(disconnected | bit) : (std::uint16_t)(disconnected & ~bit);
	}
	return disconnected;
}

// lane ROUTER-1: a canonical game's log is its committed OBRC stream: each Frame record's bundle is the frame's command
// list in order (no FRAMEINFO to announce and no incarnations to interleave), Membership records stage the membership
// snapshots the frames ran with, and End closes it. The rejoining client feeds the same records to its canonical cursor.
static bool assembleCanonical(const RelayLogFile &log, const NetworkConfig &config, RelayLog::Assembled &a, std::string *error)
{
	RelayMembership::Snapshot prev;
	std::array<int, 16> crcCount{};
	std::map<UnsignedInt, std::map<int, std::uint32_t>> crcsOf;
	for (const RelayCommit::Record &rec : log.canonical)
	{
		if (rec.stamp.game != log.gameId)
		{
			return fail(error, "a CANONICAL record of game " + std::to_string(rec.stamp.game) + " in the log of game " + std::to_string(log.gameId));
		}
		if (rec.kind == RelayCommit::Kind::Frame)
		{
			RelayFrameBundle::Frame bundle;
			std::string why;
			if (!RelayFrameBundle::decode(rec.payload, bundle, why))
			{
				return fail(error, "frame " + std::to_string(rec.stamp.frame) + "'s CANONICAL record does not decode: " + why);
			}
			CommandList &list = a.frames[rec.stamp.frame];
			for (const RelayFrameBundle::Order &o : bundle.orders)
			{
				if (o.slot >= 16 || !(log.slotMask & (1u << o.slot)))
				{
					return fail(error, "frame " + std::to_string(rec.stamp.frame) + " carries an order of slot " + std::to_string(o.slot)
						+ ", which is not a network slot");
				}
				NetByteReader r(o.bytes.data(), o.bytes.size());
				std::unique_ptr<GameMessage> msg = NetPacket::readGameMessage(r, &why);
				if (!msg)
				{
					return fail(error, "frame " + std::to_string(rec.stamp.frame) + " order of slot " + std::to_string(o.slot) + ": " + why);
				}
				// NetGameCommandMsg::constructGameMessage: the sender's player — ONLINE16-FIX: the validated sixteen-entry mapping for every
				// network slot (slots 8 .. 15 got -1, so their orders ran as nobody's on rejoin, spectating and --relay-log)
				const int player = config.slotPlayerIndex[(size_t)o.slot];
				if (player < 0)
				{
					return fail(error, "network slot " + std::to_string(o.slot) + " has no player in the loaded game");
				}
				msg->friend_setPlayerIndex(player);
				if (msg->getType() == MSG_LOGIC_CRC && config.crcInterval > 0)
				{
					const GameMessageArgument *arg = msg->getArgument(0);
					const UnsignedInt described = (UnsignedInt)(++crcCount[(size_t)o.slot]) * (UnsignedInt)config.crcInterval;
					crcsOf[described][(int)o.slot] = arg ? (std::uint32_t)arg->integer : 0u;
				}
				list.append(*msg);
			}
			for (const RelayFrameBundle::Deferral &d : bundle.deferrals)
			{
				a.problems.push_back("frame " + std::to_string(rec.stamp.frame) + ": the relay moved slot " + std::to_string(d.slot)
					+ "'s order " + std::to_string(d.id) + " to frame " + std::to_string(d.toFrame) + " (budget)");
			}
			for (const RelayFrameBundle::Refusal &o : bundle.refusals)
			{
				a.problems.push_back("frame " + std::to_string(rec.stamp.frame) + ": the relay refused slot " + std::to_string(o.slot)
					+ "'s order " + std::to_string(o.id) + " (" + RelayFrameBundle::refusalMessage(o.code) + ")");
			}
			a.anyComplete = true;
			a.lastCompleteFrame = std::max(a.lastCompleteFrame, rec.stamp.frame);
		}
		else if (rec.kind == RelayCommit::Kind::Membership)
		{
			RelayMembership::Snapshot snap;
			std::string why;
			if (!RelayMembership::decode(rec.payload, snap, why))
			{
				return fail(error, "membership record of frame " + std::to_string(rec.stamp.frame) + " does not decode: " + why);
			}
			for (int s = 0; s < 16; ++s)
			{
				const std::uint16_t bit = (std::uint16_t)(1u << s);
				if (!(snap.slotMask & bit))
				{
					continue;
				}
				// the slot's incarnation count is its ticket generation + 1 (the generation a rejoin ticket bumps)
				a.incarnations[(size_t)s] = 1 + (int)snap.ticketGenerations[(size_t)s];
				if (snap.ticketGenerations[(size_t)s] != prev.ticketGenerations[(size_t)s] && (snap.active & bit))
				{
					// a new incarnation went live with this membership (a rejoin)
					a.membership.push_back({ Network::MembershipEvent::REJOIN, s, rec.stamp.frame });
					a.joinFrame[(size_t)s] = rec.stamp.frame;
				}
				if (!(prev.departed & bit) && (snap.departed & bit))
				{
					a.membership.push_back({ Network::MembershipEvent::DROP, s, rec.stamp.frame });
					a.leaveFrame[(size_t)s] = rec.stamp.frame;
				}
			}
			prev = snap;
		}
		else if (rec.kind == RelayCommit::Kind::End)
		{
			// the committed end: the replay runs to the last frame recorded (a problem-free tail is what separates it from a cut log)
			continue;
		}
	}
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
	return true;
}

bool RelayLog::assemble(const RelayLogFile &log, const NetworkConfig &config, Assembled &out, std::string *error, bool live)
{
	const std::uint32_t currentMask = MAX_SLOTS >= 16 ? 0xffffu : ((std::uint32_t(1) << MAX_SLOTS) - 1);
	if (log.slotMask == 0 || log.slotMask > currentMask)
	{
		return fail(error, "relay log has slots beyond this game's capacity");
	}
	Assembled a;
	a.leaveFrame.fill(0xFFFFFFFFu);
	a.joinFrame.fill(0);
	if (!log.canonical.empty())
	{
		if (!assembleCanonical(log, config, a, error))
		{
			return false;
		}
		out = std::move(a);
		return true;
	}
	// lane CONTRIB-1 (GitHub #23): static, so the local structs below may name it; MSVC 19.44 refused the automatic one in a default member initializer (C2065)
	static constexpr UnsignedInt kNever = 0xFFFFFFFFu;
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
	std::array<std::vector<Incarnation>, MAX_SLOTS> inc;
	// lane SOLO-1: a one-human room's log has exactly one network slot; only such a log takes the solo paths below (a held slot's own REJOIN,
	// the incarnation ended by its successor, its own CRC count). Every other log is assembled exactly as lane NETDIAG-1 did
	const bool soloRoom = log.slotMask != 0 && (log.slotMask & (log.slotMask - 1)) == 0;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if ((log.slotMask & (1u << s)) && config.slotPlayerIndex[(size_t)s] < 0)
		{
			return fail(error, "network slot " + std::to_string(s) + " has no player in the loaded game");
		}
		inc[(size_t)s].resize(1);
	}
	std::array<std::vector<UnsignedInt>, MAX_SLOTS> rejoinRan; // per slot, the frames its REJOINs ran on, in log order
	std::vector<std::pair<int, UnsignedInt>> drops;     // (slot, frame) in log order
	size_t nextRejoin = 0;
	for (size_t ci = 0; ci < log.commands.size(); ++ci)
	{
		while (nextRejoin < log.rejoins.size() && log.rejoins[nextRejoin].firstCommand <= ci)
		{
			const int rs = log.rejoins[nextRejoin].slot;
			inc[(size_t)rs].emplace_back();
			// lane SOLO-1: a rejoined incarnation proves the old one is over (its process restarted; the relay bound a newer-generation
			// ticket): end it at its last logged frame so the slot is held from there on. Without it a dropped-and-restarted slot nobody
			// logged a DROP for (the packet router was the dead slot itself: a solo game) stays a member forever and its first missing
			// FRAMEINFO stops the walk before the new incarnation's REJOIN is reached
			if (soloRoom)
			{
				inc[(size_t)rs][inc[(size_t)rs].size() - 2].drop = std::min<UnsignedInt>(
					inc[(size_t)rs][inc[(size_t)rs].size() - 2].drop,
					inc[(size_t)rs][inc[(size_t)rs].size() - 2].frames.empty() ? 0 : inc[(size_t)rs][inc[(size_t)rs].size() - 2].frames.rbegin()->first + 1);
			}
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
			if (m.targetSlot < MAX_SLOTS && (log.slotMask & (1u << m.targetSlot)))
			{
				drops.push_back({ (int)m.targetSlot, m.frame });
				a.membership.push_back({ Network::MembershipEvent::DROP, (int)m.targetSlot, m.frame });
			}
			continue;
		}
		if (m.type == NETCOMMANDTYPE_VIEW)
		{
			// lane ANTICHEAT-1: the player's reported view (its log-only stream): kept for the replay, never a frame entry
			ReplayFile::View v;
			v.frame = m.executionFrame;
			v.slot = (std::uint8_t)s;
			v.cameraX = m.view[0];
			v.cameraY = m.view[1];
			v.cursorX = m.view[2];
			v.cursorY = m.view[3];
			v.flags = m.viewFlags;
			a.views.push_back(v);
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
			const int victim = m.targetSlot < MAX_SLOTS ? config.slotPlayerIndex[m.targetSlot] : -1;
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
		if (soloRoom) // lane SOLO-1: as above
		{
			inc[(size_t)rs][inc[(size_t)rs].size() - 2].drop = std::min<UnsignedInt>(
				inc[(size_t)rs][inc[(size_t)rs].size() - 2].drop,
				inc[(size_t)rs][inc[(size_t)rs].size() - 2].frames.empty() ? 0 : inc[(size_t)rs][inc[(size_t)rs].size() - 2].frames.rbegin()->first + 1);
		}
		inc[(size_t)rs].back().join = kNever;
		++nextRejoin;
	}
	// the frames, in order: every needed incarnation announced, and the entries it announced are all here. A REJOIN of a held slot by the frame's packet
	// router (Network::relayCommands: here the sender is taken as it, the peers checked it) starts the slot's next incarnation at the frame after; a drop
	// ends the incarnation it falls in
	// every slot's incarnations are a joined prefix then a pending suffix (a REJOIN joins the first pending one): firstPending is where the
	// suffix starts, so neither a drop, the member search nor a REJOIN walks the pending incarnations (they cannot match: the same result)
	std::array<size_t, MAX_SLOTS> firstPending{};
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		while (firstPending[(size_t)s] < inc[(size_t)s].size() && inc[(size_t)s][firstPending[(size_t)s]].join != kNever)
		{
			++firstPending[(size_t)s];
		}
	}
	auto applyDrops = [&](int s) {
		for (const auto &d : drops)
		{
			if (d.first != s)
			{
				continue;
			}
			for (size_t i = firstPending[(size_t)s]; i-- > 0;)
			{
				Incarnation &in = inc[(size_t)s][i];
				if (in.join <= d.second)
				{
					in.drop = std::min(in.drop, d.second);
					break;
				}
			}
		}
	};
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		applyDrops(s);
	}
	// for the walk: per slot, its drop frames sorted (an incarnation joined during the walk takes the first drop at or after its join: every
	// earlier one is already applied, applyDrops again would change nothing else), and its joined incarnations not known to be over, oldest
	// first (an incarnation whose end has passed never runs again: frames only grow and an end only moves earlier)
	std::array<std::vector<UnsignedInt>, MAX_SLOTS> dropFrames;
	for (const auto &d : drops)
	{
		dropFrames[(size_t)d.first].push_back(d.second);
	}
	std::array<std::vector<size_t>, MAX_SLOTS> joined;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		std::sort(dropFrames[(size_t)s].begin(), dropFrames[(size_t)s].end());
		for (size_t i = 0; i < firstPending[(size_t)s]; ++i)
		{
			joined[(size_t)s].push_back(i);
		}
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
	std::array<bool, MAX_SLOTS> gone{};
	std::array<int, MAX_SLOTS> crcCount{};            // per slot, the CRCs its incarnations sent so far (the n-th describes frame (n + 1) x interval)
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
		bool anyMember = false; // lane NETDIAG-1: a slot ran this frame (an incarnation it is a member of)
		std::vector<int> selfAdmits; ///< lane SOLO-1: pending incarnations' own REJOINs of themselves on this frame
		for (int s = 0; s < MAX_SLOTS; ++s)
		{
			if (!(log.slotMask & (1u << s)) || gone[(size_t)s])
			{
				continue;
			}
			// the newest incarnation that runs this frame (joined, its join frame reached, its end not)
			int k = -1;
			std::vector<size_t> &candidates = joined[(size_t)s];
			while (!candidates.empty() && frame >= inc[(size_t)s][candidates.back()].end())
			{
				candidates.pop_back();
			}
			if (!candidates.empty() && inc[(size_t)s][candidates.back()].join <= frame)
			{
				k = (int)candidates.back();
			}
			if (k < 0)
			{
				// not a member at this frame: left / dropped, or held until a later REJOIN (a held slot keeps the game going)
				const Incarnation &lastInc = inc[(size_t)s].back();
				anyLive = anyLive || (lastInc.join == kNever || frame < lastInc.end());
				if (soloRoom && lastInc.join == kNever)
				{
					// lane SOLO-1: a held slot's own REJOIN of itself stands in for the router's once nobody at all runs
					// this frame (a solo game's rejoined player has nobody left to send it; Network::relayCommands: the same)
					const Incarnation &pending = inc[(size_t)s][firstPending[(size_t)s]];
					const auto self = pending.frames.find(frame);
					if (self != pending.frames.end())
					{
						for (int t : self->second.rejoins)
						{
							if (t == s)
							{
								selfAdmits.push_back(t);
							}
						}
					}
				}
				continue;
			}
			anyLive = true;
			anyMember = true;
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
		if (soloRoom && !anyMember)
		{
			// lane SOLO-1: no member could have routed a REJOIN on this frame: a held slot's own REJOIN of itself admits it
			rejoinedNow.insert(rejoinedNow.end(), selfAdmits.begin(), selfAdmits.end());
		}
		// a rejoined incarnation's CRCs: the CRC frame the first incarnations' CRCs of the same execution frame describe (every peer stamps the CRC of
		// frame C with the same execution frame). lane SOLO-1: with no first incarnation beside it (a solo game's own rejoined slot) its own CRC
		// sequence continues the count and names the frame instead
		for (const LateCrc &lc : lateCrcs)
		{
			const auto described = crcFrameOfExecution.find(frame);
			if (described == crcFrameOfExecution.end())
			{
				if (log.slotMask == (1u << lc.slot))
				{
					crcsOf[(UnsignedInt)(++crcCount[(size_t)lc.slot]) * (UnsignedInt)config.crcInterval][lc.slot] = lc.value;
				}
				else
				{
					a.problems.push_back("slot " + std::to_string(lc.slot) + "'s CRC on frame " + std::to_string(frame)
						+ " (a rejoined incarnation) has no CRC of a first incarnation beside it to name its frame");
				}
				continue;
			}
			crcsOf[described->second][lc.slot] = lc.value;
		}
		lateCrcs.clear();
		for (int t : rejoinedNow)
		{
			// the slot's next incarnation without a join frame yet starts at the frame after this one
			if (t < 0 || t >= MAX_SLOTS)
			{
				continue;
			}
			if (firstPending[(size_t)t] < inc[(size_t)t].size())
			{
				const size_t joinedIndex = firstPending[(size_t)t];
				Incarnation &in = inc[(size_t)t][joinedIndex];
				in.join = frame + 1;
				a.membership.push_back({ Network::MembershipEvent::REJOIN, t, frame, in.generation });
				if (in.join == kNever)
				{
					continue; // a REJOIN on frame 0xFFFFFFFE: its join frame is the "not joined" value, so the incarnation stays the first pending one
				}
				++firstPending[(size_t)t];
				// a drop of the new incarnation logged before its REJOIN ran (applyDrops: the first drop at or after its join is the one it takes)
				const auto d = std::lower_bound(dropFrames[(size_t)t].begin(), dropFrames[(size_t)t].end(), in.join);
				if (d != dropFrames[(size_t)t].end())
				{
					in.drop = std::min(in.drop, *d);
				}
				joined[(size_t)t].push_back(joinedIndex);
			}
		}
		if (!list.messages().empty())
		{
			a.frames[frame] = std::move(list);
		}
		a.lastCompleteFrame = frame;
		a.anyComplete = true;
		if (soloRoom ? ((!anyMember && rejoinedNow.empty()) || frame == last) : (!anyMember || frame == last))
		{
			// lane NETDIAG-1 (a fuzzer's log: every slot gone but one held for a REJOIN, a command naming frame 0xFFFFFFFE: four billion empty frames,
			// and `frame <= last` never ends at 0xFFFFFFFF): with no member this frame no REJOIN can come (only a member's frame carries one) and
			// nothing changes any more, so every frame up to `last` is this one again: the walk ends there at once, with the same result.
			// lane SOLO-1: in a one-human room the held slot's own REJOIN is the exception (see selfAdmits): only the first pending incarnation
			// can be admitted, so the walk goes straight to its next frame that carries one (it is admitted there: each incarnation's frames are
			// looked at once), else ends as above
			if (soloRoom && frame != last)
			{
				int solo = 0;
				while (!(log.slotMask & (1u << solo)))
				{
					++solo;
				}
				UnsignedInt next = kNever;
				if (firstPending[(size_t)solo] < inc[(size_t)solo].size())
				{
					const Incarnation &pending = inc[(size_t)solo][firstPending[(size_t)solo]];
					for (auto f = pending.frames.upper_bound(frame); f != pending.frames.end(); ++f)
					{
						if (std::find(f->second.rejoins.begin(), f->second.rejoins.end(), solo) != f->second.rejoins.end())
						{
							next = f->first;
							break;
						}
					}
				}
				if (next != kNever && next <= last)
				{
					frame = next - 1;
					continue;
				}
			}
			a.lastCompleteFrame = last;
			break;
		}
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
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
	for (const ReplayFile::View &v : a.views)
	{
		w.recordView(v); // lane ANTICHEAT-1
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
