// OpenBFME online relay. GPL-3.0.
//
// RelayCore (lane ONLINE-2, docs/ONLINE.md phase O1): the forwarding relay's logic without sockets, so the tests drive it datagram by datagram. The process
// around it (relay_main.cpp) owns one UDP socket and one thread: receive a batch, handle() each datagram, send what comes out, tick() once a second.
//
// What it does (the envelope and the ticket: engine/src/GameNetwork/RelayProtocol.h):
//   * HELLO with a valid ticket binds the sender's address to (game, slot): the game is created on its first HELLO; a slot HELLOing from a new address is
//     rebound (a NAT rebinding); an address bound elsewhere moves. WELCOME answers.
//   * DATA from a bound address goes to the target slot's address as DATA_FROM, rewritten in place (same size, no copy). The relay checks that the transport
//     packet's sender byte is the bound slot (the slot check by session: a client cannot speak for another slot), that the target is another network slot of
//     the same game, and a per-slot byte budget (token bucket). DATA from an unknown address is answered NOT_BOUND (4 bytes: no amplification).
//   * The command log: the relay follows every (sender, target) transport stream (records in sequence order, fragments joined, duplicates and resends
//     dropped) and writes each sender's frame commands (FRAMEINFO, GAMECOMMAND, PLAYERLEAVE, DESTROYPLAYER, RUNAHEAD: NetPacket's encoding, which names its
//     execution frame) ONCE: a command is logged when one stream has carried it (lane ONLINE-3: the same bytes, counted per frame) more often than it
//     was logged, so the streams to every target cover each other (a target that leaves early, a stream whose reorder buffer overflowed). Other commands (keep-alives, disconnect votes, desync reports,
//     resends of another slot's frame) are not logged. A frame command whose header names another slot than the sender is not logged and is counted.
//   * A game closes when every slot that bound said BYE, or after `idleMs` of silence; its log gets an END record and is renamed from .part to .obfmerlog
//     (the uploader, upload_logs.py, sends finished logs to the lobby). A closed game's id is remembered for `tombstoneMs` and its HELLOs are REFUSED.
//
// Lane ONLINE-3 (reconnect, docs/ONLINE.md "Phase O3"):
//   * every slot's last datagram is timed; once a second (tick) every bound slot of a game gets a STATUS (each slot that ever bound: silent = nothing from
//     it for `silentMs` or not bound, its generation) while one slot is silent or has rejoined: the clients' disconnect path learns whose link failed;
//   * a HELLO with a rejoin ticket (version 2) of a higher generation than the slot's binds the slot to its new address without the session token: the
//     slot's game restarted (a crash, a new process) and the lobby issued its player a fresh ticket. The slot gets a new session token, its streams (both
//     directions) and its logged frame counts start afresh, and the log gets a REJOINED record; a lower generation is REFUSED (REFUSE_STALE);
//   * LOG_REQUEST from a bound client of the game answers one LOG_CHUNK of the game's log (the rejoining client re-simulates the game from it), within a
//     per-slot log budget (its own token bucket, separate from the data budget); a game without a log answers REFUSED (REFUSE_NO_LOG);
//   * spectators (a version 3 or 6 ticket): read-only clients. A spectator's HELLO binds its address to (game, spectator index); LOG_REQUEST is served up to
//     its delay behind the players (the delay in game frames, 5 a second, behind the highest execution frame logged: a pause stops it); its DATA is
//     REFUSED (REFUSE_SPECTATOR), never logged or forwarded; it is never in STATUS, the streams or the game's end; one that asks for nothing for
//     `spectatorIdleMs` is dropped. When the players are done, a game with spectators stays (its log readable, frames going on at 5 a second for
//     the delay) until they had their delay plus `spectatorGraceMs`.
//   * the decider's DISCONNECTPLAYER (a direct command: the frame a dropped player's frames end) is logged once (identical copies to every target are one),
//     and so is the packet router's REJOIN (a frame command): the log alone gives every frame's membership;
//   * DATA addressed to the sender's own slot is its log-only stream (frame commands that reach no other player: a 1v1 whose other player is held):
//     followed and logged like any stream, forwarded nowhere, and answered with an ack-only transport packet as that slot (DATA_FROM).
//
// Log format (version 2, little-endian; version 1 had no REJOINED record; version 4 uses a u16 slot mask): "OBFMERLG", u32 version, u64 game id, slot mask, u32 start (Unix seconds);
// then records
//   u8 1 COMMAND  u8 sender, u32 ms since the game's first HELLO, u32 length, the encoded command (NetPacket::encodeCommand)
//   u8 2 EVENT    u8 slot, u8 event (1 bound, 2 rebound, 3 bye), u32 ms
//   u8 3 GAP      u8 sender, u8 target, u32 ms, u8 reason (1 the stream's reorder buffer overflowed, 2 a malformed record): that stream stops being logged
//   u8 4 END      u32 ms, u8 reason (1 every slot said BYE, 2 silence, 3 relay shutdown), then per slot u64 datagrams in, u64 bytes in, u64 dropped
//   u8 5 REJOINED u8 slot, u16 generation, u32 ms (lane ONLINE-3): the slot's later COMMANDs are its new incarnation's
//   u8 6 CANONICAL u32 length, the OBRC record's bytes (lane ROUTER-1): a canonical game's committed frame bundle,
//          membership or end record — the game's whole input (replay and rejoin reproduce it); no COMMAND records
// engine/src/GameNetwork/RelayLog.h reads it and assembles the replay (openbfme_peer --relay-log).
//
//   u8 7 INPUT    (version 4, lane ANTICHEAT-1 with ONLINE16-FIX) u8 slot, u8 authenticated (the slot's binding verified under AUTH-1), then u64 each,
//                 what the slot SENT (c2r): DATA verified, DATA accepted unauthenticated, wrong tags, counters outside / already in the window,
//                 unauthenticated datagrams to an authenticated binding, packets naming another slot as sender, malformed datagrams / packets; one per
//                 slot that ever bound, just before END (the integrity check D7: openbfme_peer --relay-log --packet-check)
// Version 4 is written for every current game (the u16 slot mask and the INPUT records), eight slots or fewer too; a game the previous release
// opened (a player's version-3 envelope HELLO, eight slots; a spectator HELLO opens no game: NOT_BOUND) keeps version 2 (no INPUT records):
// its clients' rejoin catch-up and spectating read 1 and 2 only.
// INPUT sidecar (online batch r4): a version-2 log's INPUT records go next to it, <log>.input (written before the log is renamed into place):
//   "OBFMERLI", u32 version 1, u64 game id, u8 flags (bit 0: a player authenticated (AUTH-1) in this game: honest games never mix releases, so
//   the integrity check D7 reports it), u8 the slot whose HELLO opened the game, u32 authenticated player HELLOs, u16 length + the log's file name,
//   u8 count, then that many INPUT records in the kind-7 layout (u8 7, u8 slot, u8 authenticated, 7 x u64). upload_logs.py posts it after its log
//   (/v1/relay/logs/<id>/input, ADMINFIX-1) and moves both to uploaded/ once both are stored.
// The transport packet layout it follows is Transport.h's version 2 (u32 CRC, u16 magic 0x4642, u8 version 2, u8 sender slot, u32 ack, u16 record count,
// records of u32 seq, u16 fragment index, u16 fragment count, u32 length, bytes); engine/tests/test_online_relay.cpp runs real Transports through the relay
// so a layout change fails there.
//
// Lane NETDIAG-1: passive health metrics (RelayHealth.h, healthJson; docs/ONLINE.md "Reading the relay health"): per game, the ACK turnaround and
// its jitter, resends, late frames and refused DATA of the unwrapped transport packets the relay validated; Options::metrics = false collects none.
//
// Not simulation code.

#pragma once

#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/RelayAuth.h"
#include "RelayHealth.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include "GameNetwork/RelayCommit.h"

#include <cstdio>
#include <deque>
#include <memory>
#include <random>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class RelayCanonicalAuthority;

struct RelayAddress
{
	std::uint32_t ip = 0; ///< host order
	std::uint16_t port = 0;
	std::uint64_t key() const { return ((std::uint64_t)ip << 16) | port; }
	bool operator==(const RelayAddress &o) const { return ip == o.ip && port == o.port; }
	std::string text() const;
};

class RelayCore
{
public:
	struct Options
	{
		RelayProtocol::KeyRing keys;
		bool metrics = true;               ///< lane NETDIAG-1: passive health collection (RelayHealth, healthJson); false = none (--no-metrics)
		std::string logDir;                ///< "" = no command log
		std::uint64_t idleMs = 60000;      ///< a game silent this long closes
		std::uint64_t tombstoneMs = 6 * 3600 * 1000ull;
		size_t maxGames = 4096;
		std::uint32_t bytesPerSecond = 32768; ///< per slot (the measured need is about 4 KB/s up in an 8-player game: docs/ONLINE.md section 2)
		std::uint32_t burstBytes = 262144;    ///< per slot (a big selection or a Create-a-Hero goes out as a burst of fragments)
		size_t maxStreamBuffer = 1u << 20;    ///< per-stream admission quota: logical command counts + early records + the command being joined
		size_t maxSlotBuffer = 4u << 20;      ///< ONLINE-2 r2: the same over all of one sender's streams
		size_t maxGameBuffer = 16u << 20;     ///< ONLINE-2 r2: the same over a game
		size_t maxTotalBuffer = 256u << 20;   ///< ONLINE-2 r2: the same over the relay
		size_t maxPacketBytes = 1200;         ///< a transport packet beyond this is refused (Transport::Options::packetBytes: every client's receive ceiling)
		std::uint32_t streamWindow = 16384;   ///< records accepted ahead of the next expected one (Transport::Options::window)
		std::uint64_t silentMs = 3000;        ///< lane ONLINE-3: a slot not heard this long is reported silent (STATUS)
		std::uint32_t logBytesPerSecond = 4u << 20; ///< lane ONLINE-3: the log a slot may fetch (LOG_CHUNK bytes), per slot
		std::uint32_t logBurstBytes = 4u << 20;
		std::uint64_t spectatorIdleMs = 30000; ///< lane ONLINE-3: a spectator that asked for nothing this long is dropped (it never holds anyone up)
		std::uint64_t spectatorGraceMs = 120000; ///< lane ONLINE-3: a finished game's log stays readable to its spectators this long after their delay
		// lane ONLINE-3 (Sol r1 of ONLINE-3): a game's log stops at this size (an hour of an 8-player game is about 2 MB), and the LOG_CHUNK bytes the
		// relay reads for one game and in all are budgeted like a client's (token buckets, checked before the disk is touched)
		std::uint64_t maxLogBytes = 64ull << 20;
		std::uint32_t logGameBytesPerSecond = 16u << 20, logGameBurstBytes = 16u << 20;
		std::uint32_t logTotalBytesPerSecond = 64u << 20, logTotalBurstBytes = 64u << 20;
	};
	struct Action
	{
		enum Kind
		{
			NONE,
			FORWARD, ///< send the (rewritten) input datagram to `to`
			REPLY    ///< send `reply` to `to`
		} kind = NONE;
		RelayAddress to;
		std::vector<std::uint8_t> reply;
	};
	struct Stats
	{
		unsigned long long datagrams = 0, bytesIn = 0, forwarded = 0, bytesForwarded = 0, hellos = 0, welcomes = 0, refused = 0, notBound = 0;
		unsigned long long badDatagrams = 0, slotMismatches = 0, badTargets = 0, unboundTargets = 0, rateLimited = 0, gamesOpened = 0, gamesClosed = 0;
		/// online batch r4: player HELLOs authenticated (AUTH-1) into a game whose log is version 2 (a previous-release HELLO opened it): an anomaly
		unsigned long long legacyLogAuthenticated = 0;
		unsigned long long commandsLogged = 0, logGaps = 0, logErrors = 0, foreignFrameCommands = 0;
		unsigned long long takeoversRefused = 0, invalidPackets = 0, peakBuffered = 0; ///< ONLINE-2 r2
		unsigned long long rejoins = 0, staleTickets = 0, statusesSent = 0, logChunksSent = 0, logBytesSent = 0, logRefused = 0, logRateLimited = 0; ///< ONLINE-3
		unsigned long long logStreamPackets = 0; ///< ONLINE-3: packets of the log-only streams (DATA to the sender's own slot)
		unsigned long long spectatorsBound = 0, spectatorDataRefused = 0, spectatorsDropped = 0, spectatorLogBytes = 0; ///< ONLINE-3
		unsigned long long accountingErrors = 0; ///< ONLINE-3 r3: a release larger than a held-bytes counter (a bug: the tests require 0)
		unsigned long long canonicalRecords = 0; ///< ROUTER-1: OBRC records appended to canonical games' logs
		unsigned long long authDropped = 0;      ///< lane AUTH-1: datagrams dropped on a bad tag / replayed counter (silent: no reply, no amplification)
	};

	static constexpr size_t kMetaNodeBytes = 64; ///< lane ONLINE-3: conservative per-node base estimate, not allocator or RSS measurement

	explicit RelayCore(const Options &options);
	~RelayCore(); ///< closes every open game (END reason 3)
	RelayCore(const RelayCore &) = delete;
	RelayCore &operator=(const RelayCore &) = delete;

	// one datagram in (`d` may be rewritten in place for a FORWARD)
	Action handle(const RelayAddress &from, std::uint8_t *d, size_t size, std::uint64_t nowMs, std::uint32_t nowUnix);
	// lane NET-SYNC: actions one datagram's processing emitted beyond its own reply (canonical ACKs, retries, record
	// fanout); the caller drains them right after handle() and after tick()
	void takePending(std::vector<Action> &out);
	// closes silent games, forgets old tombstones; call about once a second. Lane ONLINE-3: `out` gets the STATUS datagrams to send (null: none built)
	void tick(std::uint64_t nowMs, std::vector<Action> *out = nullptr);
	void shutdown(std::uint64_t nowMs); ///< closes every game (END reason 3)

	const Stats &stats() const { return m_stats; }
	// lane NETDIAG-1: the relay_health JSON snapshot (game ids in hex and counters only: never an address, a name or a payload byte); built at
	// reporting intervals only, never while a datagram is handled. Rates are over the time since the previous call. Empty when metrics are off.
	// residentBytes 0 = unknown (null).
	std::string healthJson(std::uint64_t uptimeMs, std::uint64_t residentBytes = 0);
	// lane NETDIAG-1: the socket layer's results: datagrams the kernel accepted / refused, and a datagram truncated on receive (never handled)
	void noteSend(size_t sent, size_t failed)
	{
		if (m_options.metrics)
		{
			m_packetsOut += sent;
			m_sendDrops += failed;
		}
	}
	void noteTruncated()
	{
		if (m_options.metrics)
		{
			++m_stats.datagrams;
			++m_stats.badDatagrams;
		}
	}
	size_t openGames() const { return m_games.size(); }
	size_t openLogHandles() const
	{
		size_t count = 0;
		for (const auto &kv : m_games)
		{
			count += kv.second.log ? 1 : 0;
			count += kv.second.logReader ? 1 : 0;
		}
		return count;
	}
	// ONLINE-2 r3: conservative held-storage estimates used by caps and retainedBytes; not allocator or RSS measurements
	size_t heldBytes() const { return m_buffered; }
	size_t retainedBytes() const;
	// lane ONLINE-3: the entries of a game's delay frontier (0: no such game); the tests read it
	size_t frontierEntries(std::uint64_t game) const
	{
		const auto it = m_games.find(game);
		return it == m_games.end() ? 0 : it->second.frontier.size();
	}
	// the finished logs (paths), in closing order (the last 1,000); the tests read them
	const std::vector<std::string> &closedLogs() const { return m_closedLogs; }
	// problems writing logs (at most 100 waiting); takeErrors hands them over and clears them
	const std::vector<std::string> &errors() const { return m_errors; }
	std::vector<std::string> takeErrors() { return std::move(m_errors); }

private:
	struct Stream
	{
		std::uint32_t expected = 0;
		std::map<std::uint32_t, std::pair<std::pair<std::uint16_t, std::uint16_t>, std::vector<std::uint8_t>>> early; ///< seq -> ((index, count), bytes)
		size_t earlyBytes = 0;
		std::vector<std::uint8_t> partial;
		std::uint16_t partialNext = 0, partialCount = 0;
		size_t metaBytes = 0; ///< lane ONLINE-3: logical per-stream quota for the shared command counts (not physical held bytes)
		bool carried = false; ///< lane ONLINE-3 r2: it carried a frame command of its sender (its sender's log follows it)
		bool broken = false;
		std::uint64_t highest = 0; ///< ONLINE-2 r2: the highest sequence number seen + 1 (records start at 0): the reverse direction's ack check
	};
	// P16-NET: the per-target columns of the shared count tables are split at the legacy eight-slot boundary. The second
	// half (targets 8..15) is allocated only when a game's traffic reaches those slots, so a game of eight or fewer
	// players holds -- and is charged -- exactly the bytes it was before the sixteen-slot support.
	static constexpr size_t kCountColumns = 8;
	struct CommandCounts
	{
		std::uint32_t logged = 0;
		std::array<std::uint32_t, kCountColumns> carried{};
	};
	struct FrameCounts
	{
		std::array<std::uint32_t, kCountColumns> distinctByTarget{};
		std::map<std::uint64_t, CommandCounts> commands;
	};
	using SenderFrameCounts = std::map<std::uint32_t, FrameCounts>;
	// each shared-table node is charged the price of the two nodes it consolidates (the per-stream perFrame node and the
	// sender's logged node, kMetaNodeBytes each): a (frame, hash) entry in one half costs the old 4 x kMetaNodeBytes the
	// per-stream model charged for a delivery, and a game's cost grows only through the second half's entries (never
	// allocated while no target >= 8 carries a command). The stream's own metaBytes stay a logical maxStreamBuffer quota.
	static constexpr size_t kFrameCountsNodeBytes = 2 * kMetaNodeBytes;
	static constexpr size_t kCommandCountsNodeBytes = 2 * kMetaNodeBytes;
	// lane AUTH-1: a binding's authenticated-envelope state (RelayAuth.h): the direction keys derived at bind time
	// from the ticket's k, the outgoing counter, and the incoming replay window
	struct PeerAuth
	{
		bool authed = false;
		RelayAuth::Keys keys;
		std::uint16_t generation = 0;    ///< the ticket generation the keys were derived with
		std::uint64_t sendCounter = 0;   ///< the next r2c counter value
		RelayAuth::Window recv;          ///< the c2r replay window
	};
	struct Slot
	{
		bool bound = false, everBound = false, bye = false;
		RelayAddress address;
		std::uint64_t budgetMilli = 0; ///< ONLINE-2 r2: the token bucket in milli-bytes (integers); set at the first bind only
		std::uint64_t lastRefill = 0;
		std::array<std::uint8_t, RelayProtocol::kSessionTokenBytes> session{}; ///< ONLINE-2 r2: the token a rebinding HELLO must carry
		size_t buffered = 0; ///< ONLINE-2 r2: bytes this sender's streams hold for the log
		unsigned long long datagramsIn = 0, bytesIn = 0, dropped = 0;
		std::uint64_t lastHeard = 0;   ///< lane ONLINE-3: the slot's last datagram (STATUS: silent)
		std::uint16_t generation = 0;  ///< lane ONLINE-3: the generation of the ticket it bound with (a rejoin raises it)
		std::uint64_t logBudgetMilli = 0, logRefill = 0; ///< lane ONLINE-3: the LOG_CHUNK budget (milli-bytes)
		PeerAuth auth;                       ///< lane AUTH-1
		std::uint8_t envelope = RelayProtocol::kVersion; ///< lane ONLINE16-FIX: the envelope version of its HELLO (3: the previous release)
		// lane ANTICHEAT-1: what this slot SENT (c2r) as AUTH-1's verification saw it, for the log's INPUT record (kind 7) and the integrity check D7:
		// DATA verified under AUTH-1, DATA accepted unauthenticated, wrong tags, counters outside / already in the window, unauthenticated datagrams
		// to an authenticated binding, packets naming another slot as sender, malformed datagrams or packets
		unsigned long long inVerified = 0, inPlain = 0, inBadTag = 0, inReplayed = 0, inUnauthRefused = 0, inSlotMismatch = 0, inMalformed = 0;
	};
	struct Spectator // lane ONLINE-3: a read-only client of the game (its log, delayed by `delayFrames`)
	{
		bool bound = false, everBound = false;
		RelayAddress address;
		std::array<std::uint8_t, RelayProtocol::kSessionTokenBytes> session{};
		std::uint32_t delayFrames = 0;
		std::uint64_t lastHeard = 0, logBudgetMilli = 0, logRefill = 0;
		PeerAuth auth; ///< lane AUTH-1: only relay->spectator is authenticated (its DATA is refused anyway)
		std::uint8_t envelope = RelayProtocol::kVersion; ///< lane ONLINE16-FIX
	};
	struct Game
	{
		std::unique_ptr<RelayHealth> health; ///< lane NETDIAG-1: null when metrics are off (and once a watched game closed)
		std::uint64_t id = 0;
		std::uint16_t mask = 0;
		std::uint64_t startMs = 0, lastHeard = 0;
		std::array<Slot, RelayProtocol::kMaxSlots> slots;
		std::array<std::array<Stream, RelayProtocol::kMaxSlots>, RelayProtocol::kMaxSlots> streams; ///< [sender][target]
		///< one shared execution-frame/hash map per sender and half of the target space ([target / kCountColumns])
		std::array<std::array<SenderFrameCounts, RelayProtocol::kMaxSlots / kCountColumns>, RelayProtocol::kMaxSlots> commandCounts;
		std::FILE *log = nullptr;
		std::string logPath;
		bool logRenamed = false; ///< DEVPORT-1: the .part became logPath; a failed rename leaves the .part as the log spectators read
		size_t buffered = 0; ///< ONLINE-2 r2
		std::FILE *logReader = nullptr;          ///< lane ONLINE-3: LOG_REQUESTs read the log being written through this
		std::uint64_t logBytes = 0;              ///< lane ONLINE-3: bytes written to the log so far
		std::vector<std::vector<std::uint8_t>> membershipLogged; ///< lane ONLINE-3: the DISCONNECTPLAYERs logged (each once)
		// lane ONLINE-3: the spectators, and for their delay the log's frontier: (bytes of the log, the highest execution frame logged up to there), one
		// entry per value of that frame (its last offset); the players' latest announced frame is the last one
		std::array<Spectator, RelayProtocol::kMaxSpectators> spectators;
		std::vector<std::pair<std::uint64_t, std::uint32_t>> frontier; ///< at most kMaxFrontier entries (thinned: a delay's cut-off only moves earlier)
		size_t frontierBytes = 0;                ///< its allocation, held by the game (Sol r1 of ONLINE-3)
		std::array<bool, RelayProtocol::kMaxSlots> senderStopped{};     ///< lane ONLINE-3 r2: every stream of the sender that carried its commands stopped (its log ended)
		size_t membershipBytes = 0;              ///< lane ONLINE-3 r2: membershipLogged, held by the game (kMetaNodeBytes + the bytes per record)
		std::uint64_t logReadBudgetMilli = 0, logReadRefill = 0; ///< the LOG_CHUNK bytes read for this game (Options::logGameBytesPerSecond)
		std::uint32_t maxFrame = 0;
		std::uint64_t headerBytes = 0;
		std::uint8_t mode = 0;              ///< lane NET-SYNC: TicketMode bits the first player START ticket bound
		bool modeDeclared = false;          ///< a player START ticket already fixed `mode` (spectator tickets never do)
		std::unique_ptr<RelayCanonicalAuthority> canonical; ///< lane NET-SYNC: non-null iff the game is canonical
		bool logOpened = false;
		bool legacyLog = false;   ///< online batch: opened by the previous release (version-3 envelope): a version-2 log, no INPUT records
		// online batch r4: a version-2 log's INPUT records go to its sidecar; a player authenticating under it is the anomaly the sidecar names
		bool legacyAuthAnomaly = false;
		int openerSlot = -1;                 ///< the player slot whose HELLO opened the game (and so chose the log's version)
		std::uint32_t authenticatedHellos = 0; ///< in a version-2 log game: the authenticated player HELLOs (binds, rebinds, rejoins)
		bool closed = false;      ///< the players are done; kept while spectators still read the log
		std::uint64_t closedMs = 0;
	};
	// lane NETDIAG-1: the health streams cover exactly the relay's slot space (no second literal: both come from RelayProtocol::kMaxSlots)
	static_assert(std::tuple_size<decltype(RelayHealth::streams)>::value == std::tuple_size<decltype(Game::streams)>::value
		&& std::tuple_size<decltype(RelayHealth::streams)::value_type>::value == std::tuple_size<decltype(Game::streams)::value_type>::value
		&& std::tuple_size<decltype(Game::slots)>::value == (size_t)RelayProtocol::kMaxSlots, "RelayHealth::streams must be indexed by every relay slot");
	struct Binding
	{
		std::uint64_t game;
		std::uint8_t slot;
		bool spectator = false; ///< lane ONLINE-3: spectator identity is separated by this binding flag, not by a disjoint slot index
	};
	std::uint64_t spectatorLimit(const Game &g, const Spectator &sp, std::uint64_t nowMs) const; ///< lane ONLINE-3: the log bytes it may read now
	bool spectatorExpired(const Game &g, const Spectator &sp, std::uint64_t nowMs) const; ///< ADMINFIX-1: a finished game's spectator had its own delay + grace
	Action spectatorHello(Game &g, const RelayProtocol::Ticket &t, const RelayAddress &from, const std::uint8_t *token, std::uint64_t nowMs,
		std::uint8_t envelope);
	void unbindSpectator(Game &g, int index);
	void accountMeta(Game &g, int sender, std::ptrdiff_t delta); ///< lane ONLINE-3: metadata bytes held by a sender (-1: by the game alone)
	void noteFrontier(Game &g);                                  ///< lane ONLINE-3: the frontier after a log write (thinned, accounted)
	void eraseBinding(const RelayAddress &address, const Binding &which);  ///< lane ONLINE-3: only while the address is still bound to `which`
	void releaseEndpoint(const RelayAddress &from, const Binding &keep);   ///< lane ONLINE-3: the address leaves its other binding, by kind

	Action hello(const RelayAddress &from, const std::uint8_t *d, size_t size, std::uint64_t nowMs, std::uint32_t nowUnix);
	// lane NET-SYNC: DATA_FROM datagrams for a canonical game's emitted (slot, channel packet) pairs
	void emitCanonical(const Game &g, const std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> &emissions,
		std::vector<Action> &out);
	// lane AUTH-1: the version-5 envelope of one datagram: verifies the tag (a HELLO always with the keys of ITS OWN
	// ticket, whoever sends it; any other kind with the c2r key of the sender's binding), the replay window (a HELLO's:
	// the window of the slot its ticket names, once that slot authenticated with the same keys), then compacts `d` to
	// its v4 view and returns its size; 0 to drop silently, (size_t)-1 to answer NOT_BOUND (a v5 datagram from an
	// address the relay forgot)
	size_t unwrapAuth(const RelayAddress &from, std::uint8_t *d, size_t size);
	// lane AUTH-1: the keys of a verified-but-yet-unbound HELLO, for sealing its own answer (and installing at bind)
	struct HelloAuth
	{
		bool active = false;
		RelayAuth::Keys keys;
		std::uint64_t counter = 0; // the counter this HELLO arrived with (installed into the slot's window at bind)
		bool windowed = false;     // ONLINE16-FIX: the named slot's window (same keys) admitted it: a fresh tag proves ownership
	};
	// lane AUTH-1: installs the verified HELLO's keys on a binding; a generation or key change resets counters and window
	void installAuth(PeerAuth &auth, const RelayAuth::Keys &keys, std::uint16_t generation, std::uint64_t seenCounter);
	// ONLINE16-FIX: the authentication state of a binding (player slot or spectator place)
	PeerAuth &bindingAuth(const Binding &b);
	// ONLINE16-FIX (AUTH-1 review 5): a send counter start no earlier instance of the relay can have reached: wall-clock
	// microseconds (a binding sends far fewer than a million datagrams a second), strictly increasing in this process
	std::uint64_t nextCounterBase();
	std::uint64_t m_counterBase = 0;
	// lane AUTH-1: seals a REPLY action for an authenticated destination (binding lookup, else the HELLO context);
	// FORWARD never reaches it: an authenticated target's copy is built as REPLY by data(). ONLINE16-FIX: an
	// unauthenticated destination's reply is stamped with its envelope version (3 for the previous release)
	void sealAction(Action &a);
	Action data(const RelayAddress &from, std::uint8_t *d, size_t size, std::uint64_t nowMs);
	void bye(const RelayAddress &from, std::uint64_t nowMs);
	Action logRequest(const RelayAddress &from, const std::uint8_t *d, size_t size, std::uint64_t nowMs); ///< lane ONLINE-3
	void clearTargetCounts(Game &g, int sender, int target, Stream &st);
	void clearSenderLogged(Game &g, int sender);
	void clearSenderCounts(Game &g, int sender);
	void trimSenderCounts(Game &g, int sender);
	void resetStream(Game &g, int sender, int target);                                                   ///< lane ONLINE-3
	// false: the receiver would refuse the packet whole (it is then neither logged nor forwarded)
	bool follow(Game &g, int sender, int target, const std::uint8_t *packet, size_t size, std::uint64_t nowMs);
	// lane NETDIAG-1: the health sample of one validated, unwrapped transport packet (its records in m_records); O(records), no allocation
	void observePacket(Game &g, int sender, int target, const std::uint8_t *packet, std::uint64_t nowMs);
	void noteDataDrop(Game &g, Slot &s); ///< a DATA the relay refused: the slot's `dropped` and the health counters
	void account(Game &g, int sender, size_t before, const Stream &st);
	static size_t held(const Stream &st);
	void newSessionToken(std::array<std::uint8_t, RelayProtocol::kSessionTokenBytes> &out);
	// false: the command's count nodes do not fit under the caps (the caller stops the stream's logging)
	bool command(Game &g, int sender, int target, Stream &st, std::vector<std::uint8_t> &&bytes, std::uint64_t nowMs);
	bool admit(const Game &g, int sender, const Stream *st, size_t streamGrow, size_t senderGrow, size_t gameGrow, size_t optionalMetaGrow = 0) const; ///< lane ONLINE-3 r2
	void stopLog(Game &g, const std::string &why);
	void noteAuthenticated(Game &g);   ///< online batch r4: a player slot of `g` authenticated (the anomaly when its log is version 2)
	void writeInputSidecar(Game &g);   ///< online batch r4: a version-2 log's INPUT records, at its close ///< lane ONLINE-3 r2: the game's log ends; everything held for it is released
	void gap(Game &g, int sender, int target, Stream &st, std::uint8_t reason, std::uint64_t nowMs);
	void close(Game &g, std::uint8_t reason, std::uint64_t nowMs);
	void write(Game &g, const std::vector<std::uint8_t> &bytes);
	// ROUTER-1: a canonical game's durable append — OBRC record -> the game's OBFMERLG log (record kind 6)
	bool canonicalAppend(Game &g, const RelayCommit::Record &record, std::string &error);
	void error(const std::string &e);
	static Action reply(const RelayAddress &to, std::vector<std::uint8_t> &&bytes);

	Options m_options;
	std::map<std::uint64_t, Game> m_games;
	std::map<std::uint64_t, std::uint64_t> m_tombstones; ///< closed game id -> when
	std::deque<std::pair<std::uint64_t, std::uint64_t>> m_tombstoneOrder; ///< (when, id) in closing order
	std::unordered_map<std::uint64_t, Binding> m_bindings; ///< address key -> (game, slot): lookups only, never iterated
	Stats m_stats;
	std::uint64_t m_packetsOut = 0, m_sendDrops = 0, m_dataDrops = 0, m_resends = 0, m_lateFrames = 0; ///< lane NETDIAG-1
	std::uint64_t m_healthUptime = 0, m_healthIn = 0, m_healthOut = 0;                                  ///< lane NETDIAG-1: the previous snapshot
	std::vector<std::string> m_closedLogs, m_errors;
	std::deque<Action> m_pending;                  ///< lane NET-SYNC: emits beyond handle()'s single Action
	size_t m_buffered = 0;   ///< ONLINE-2 r2: bytes held for the logs, every game
	std::uint64_t m_logReadBudgetMilli = 0, m_logReadRefill = 0; ///< lane ONLINE-3: LOG_CHUNK bytes read, every game (Options::logTotalBytesPerSecond)
	std::random_device m_random; ///< ONLINE-2 r2: session tokens (the OS generator per word: tokens are unpredictable; not simulation code)
	struct Record
	{
		std::uint32_t seq;
		std::uint16_t index, frags;
		size_t at, len;
	};
	std::vector<Record> m_records;
	HelloAuth m_helloAuth;      ///< lane AUTH-1: the current datagram's HELLO key context (valid inside handle())
	bool m_inAuthed = false;    ///< lane AUTH-1: the current datagram arrived as a verified version-5 envelope
	std::uint8_t m_inVersion = RelayProtocol::kVersion; ///< ONLINE16-FIX: the current datagram's envelope version (5 reads as 4 once opened)

	// lane CHAT-1: the structural packet check `follow` runs (magic / CRC / record layout / exact end), without the
	// stream model or the log — the canonical game's direct-command path
	bool validatePacket(const std::uint8_t *p, size_t n, std::vector<Record> &records);
};
