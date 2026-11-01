// OpenBFME. GPL-3.0.
//
// RelayProtocol (lane ONLINE-2, docs/ONLINE.md phase O1, "forwarding relay"): the datagrams between a game client's Transport and the online relay
// (services/online/relay), and the session ticket the lobby service signs. OpenBFME's own protocol (no retail counterpart: RotWK's online play went through
// GameSpy / EA's FESL servers, which are gone; docs/ONLINE.md section 5).
//
// Every client opens ONE UDP flow to the relay; the relay forwards each transport packet to the slot it names. The end-to-end Transport (acks, resends,
// fragments; GameNetwork/Transport.h) is unchanged inside the envelope, so the relay keeps no reliability state of its own.
//
// Envelope (little-endian): u16 magic 0x5242 (bytes 'B' 'R'), u8 version 2, u8 kind, then by kind
//   HELLO      client -> relay   a signed ticket, then the 16-byte session token of an earlier WELCOME when the client has one. A slot's first HELLO
//                                binds the sender's address to (game, slot); once bound, only a HELLO that carries the slot's session token moves the
//                                slot to another address (a NAT rebinding): the ticket alone does not (ONLINE-2 r2, Sol's takeover). Sent until WELCOME,
//                                and again after NOT_BOUND
//   WELCOME    relay -> client   u64 game id, u8 player slot or 0x80 + spectator index, the 16-byte session token (random, chosen by the relay at first bind)
//   DATA       client -> relay   u8 target slot, the transport packet (its sender byte must be the bound slot: the relay drops it otherwise); lane
//                                ONLINE-3: the client's own slot as the target is its log-only stream (logged, acknowledged by the relay, forwarded nowhere)
//   DATA_FROM  relay -> client   u8 source slot, the transport packet (DATA rewritten in place: same size)
//   NOT_BOUND  relay -> client   (empty) the relay does not know the sender's address (a NAT rebinding, a relay restart): HELLO again
//   REFUSED    relay -> client   u8 reason (RefuseReason)
//   BYE        client -> relay   (empty) the client's game is over (the relay closes the game's log once every bound slot said BYE, or after silence)
// The envelope costs 5 bytes per datagram (4 + the slot byte).
// Lane ONLINE-3 (envelope version 3: reconnect, docs/ONLINE.md "Phase O3"):
//   STATUS     relay -> client   u8 n, then n x (u8 slot, u8 flags (bit 0 silent: nothing from the slot for Options::silentMs, or not bound; bit 1 bound),
//                                u16 generation): every slot that ever bound, sent once a second while one is silent or has rejoined (generation > 0)
//   LOG_REQUEST client -> relay  u32 offset: the game's command log from that byte (a bound client of the game only)
//   LOG_CHUNK  relay -> client   u32 offset, u32 the log's size now, then at most kLogChunkBytes bytes of it
// A spectator ticket (version 3/6, 34/35 bytes) binds a read-only client: it may ask for the game's log (LOG_REQUEST: served up to its delay behind the players)
// and nothing else; its DATA is REFUSED (REFUSE_SPECTATOR), never logged or forwarded; WELCOME names it as 0x80 + its index.
// A rejoin ticket (version 2/5, 34/35 bytes) carries a generation: a HELLO whose ticket names a higher generation than the slot's binds the slot to the new
// address without the session token (the lobby issued it to the slot's player after its game restarted); the relay then starts the slot's streams afresh
// and tells the others (STATUS), whose transports start theirs afresh when their Network takes the slot back. A lower generation is REFUSED (stale).
//
// Tickets 1/4 are 32/33 bytes; tickets 2/3/5/6 are 34/35 bytes. Layout: u8 version, u8 key id, u64 game id, u8 slot, u8 low mask byte, optional u8 high
// mask byte in versions 4..6, u32 expiry, optional u16 generation/delay, then the first 16 bytes of HMAC-SHA256.
// Lane NET-SYNC (envelope version 4, ticket version 7): the canonical game's mode is a property of the GAME, decided by the lobby and signed into the
// START ticket (TicketMode bits: canonical, equal-delay policy). Version 7 is the version-4 layout plus a u8 mode byte: 34 bytes. A HELLO on the v4
// envelope appends the client's capability bits (u8: CAP_CANONICAL), after the optional session token. The relay binds a game with the first player
// START ticket's mode, refuses a HELLO whose declared capabilities lack the bound mode (REFUSE_CAPABILITY), and refuses a player ticket whose mode
// disagrees with the game's (REFUSE_MODE_MIXED). Rejoin and spectator tickets carry no mode: they join whatever the game already is. Tickets without
// the flag take the old (non-canonical) path unchanged.
// The lobby holds the keys and signs; the relay holds the same keys and checks offline (no call to the lobby during a game).
//
// Not simulation code: it decides where bytes go, never what a frame contains.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace RelayProtocol
{
constexpr std::uint8_t kMagic0 = 'B', kMagic1 = 'R';
constexpr std::uint8_t kVersion = 4; // 2 (ONLINE-2 r2): the session token in WELCOME / HELLO; 3 (ONLINE-3): STATUS, LOG_REQUEST / LOG_CHUNK, rejoin tickets; 4 (NET-SYNC): HELLO capability bits, mode-flagged tickets
constexpr std::uint8_t kVersionAuth = 5; // lane AUTH-1: the authenticated envelope (counter + tag; GameNetwork/RelayAuth.h)
// lane ONLINE16-FIX: the previous release's envelope (v0.4.11 and before): version 3, the same kinds and layouts but a HELLO without the
// capability byte. The relay keeps answering it in kind (every reply and forward to such a binding carries 3), so legacy games of older
// clients play on; a client without a macKey drops to it when a version-3-only relay refuses its version 4
constexpr std::uint8_t kVersionLegacy = 3;
constexpr size_t kSessionTokenBytes = 16;
constexpr size_t kHeaderBytes = 4;
constexpr size_t kTicketBytes = 32;
constexpr size_t kTicketBytesV2 = 34; ///< lane ONLINE-3: the rejoin ticket (+ u16 generation before the MAC)
constexpr size_t kTicketBytes16 = 33; ///< P16: version 4, a u16 slot mask instead of u8
constexpr size_t kTicketBytesV2_16 = 35; ///< P16: versions 5 (rejoin) and 6 (spectator)
constexpr size_t kTicketBytesFlags = 36; ///< NET-SYNC: version 7 (player START + u8 mode flags + u16 frameMs; ROUTER-1)
// ticket version 8 is reserved (not used by this build)
constexpr size_t kLogChunkBytes = 1024; ///< lane ONLINE-3: the log bytes of one LOG_CHUNK
constexpr int kMaxSpectators = 16;      ///< lane ONLINE-3: spectator indices 0 .. 15 per game
constexpr std::uint8_t kSpectatorWelcome = 0x80; ///< lane ONLINE-3: WELCOME's slot byte of spectator i is 0x80 + i
constexpr size_t kTicketMacBytes = 16;
constexpr int kMaxSlots = 16;

enum Kind : std::uint8_t
{
	HELLO = 1,
	WELCOME = 2,
	DATA = 3,
	DATA_FROM = 4,
	NOT_BOUND = 5,
	REFUSED = 6,
	BYE = 7,
	STATUS = 8,      ///< lane ONLINE-3
	LOG_REQUEST = 9, ///< lane ONLINE-3
	LOG_CHUNK = 10   ///< lane ONLINE-3
};

enum RefuseReason : std::uint8_t
{
	REFUSE_BAD_TICKET = 1, ///< malformed, unknown key id or a wrong MAC
	REFUSE_EXPIRED = 2,
	REFUSE_GAME_CLOSED = 3, ///< the game's log is closed (every slot said BYE, or it went silent)
	REFUSE_FULL = 4,        ///< the relay holds its maximum number of games
	REFUSE_VERSION = 5,     ///< another envelope version
	REFUSE_SLOT_TAKEN = 6,  ///< the slot is bound to another address and the HELLO lacks its session token (ONLINE-2 r2)
	REFUSE_STALE = 7,       ///< lane ONLINE-3: the ticket's generation is below the slot's (a newer rejoin ticket was used)
	REFUSE_NO_LOG = 8,      ///< lane ONLINE-3: LOG_REQUEST for a game the relay keeps no log of
	REFUSE_SPECTATOR = 9,   ///< lane ONLINE-3: DATA from a spectator (read-only: it sends no commands)
	REFUSE_REJOIN_BEFORE_BIND = 10, ///< the original process never joined the relay; there is no incarnation to resume
	REFUSE_CAPABILITY = 11,         ///< lane NET-SYNC: the HELLO's capability bits lack the mode the game's ticket bound
	REFUSE_MODE_MIXED = 12,         ///< lane NET-SYNC: a player ticket whose mode disagrees with the game's bound mode
	REFUSE_AUTH_REQUIRED = 13       ///< lane AUTH-1: the game's ticket requires authenticated (v5) datagrams
};
const char *refuseReasonName(int reason);

// lane NET-SYNC: mode bits signed into a version-7 START ticket (the lobby decides the game's mode; nobody can change it alone)
enum TicketMode : std::uint8_t
{
	TICKET_MODE_CANONICAL = 1,   ///< the relay keeps the beat: canonical bundles replace client-chosen frames
	TICKET_MODE_EQUAL_DELAY = 2, ///< canonical late-order policy A (equal delay for everyone); absent: per-player NextFrame (the default)
	TICKET_MODE_AUTH = 4         ///< lane AUTH-1: the lobby requires authenticated datagrams for this game (REFUSE_AUTH_REQUIRED otherwise)
};

// lane NET-SYNC: capability bits a v4-envelope HELLO appends (what the client's build can play)
enum HelloCapability : std::uint8_t
{
	CAP_CANONICAL = 1, ///< the client speaks the canonical input path (OBCI ingress, canonical bundle consumption)
	CAP_AUTH = 2       ///< lane AUTH-1: the client speaks the authenticated envelope (version 5; its HELLO is already v5)
};

struct Ticket
{
	std::uint8_t keyId = 0;
	std::uint64_t gameId = 0;
	std::uint8_t slot = 0;
	std::uint16_t slotMask = 0;
	std::uint32_t expires = 0;
	std::uint16_t generation = 0; ///< lane ONLINE-3: 0 the START's ticket (version 1/4); 1 .. the n-th rejoin ticket of the slot (version 2/5)
	bool spectator = false;       ///< lane ONLINE-3: a spectator ticket (version 3/6): `slot` is the spectator index, no player slot
	std::uint16_t delaySeconds = 0; ///< lane ONLINE-3, spectator: the relay serves the game's log this many seconds of game time behind the players
	std::uint8_t mode = 0;        ///< lane NET-SYNC: TicketMode bits (version 7 START tickets; zero on every earlier version)
	std::uint16_t frameMs = 0;    ///< ROUTER-1: the game's frame period in ms (version 7+ START tickets; 0 = the 200 ms default)
};

// the signing keys by id (the lobby's and the relay's key ring)
using KeyRing = std::map<std::uint8_t, std::vector<std::uint8_t>>;

// P16 (OpenBFME): masks above 0xff use versions 4/5/6, otherwise versions 1/2/3 stay byte-identical.
std::vector<std::uint8_t> signTicket(const Ticket &t, const std::vector<std::uint8_t> &key);
// lane ONLINE-3: versions 2/5 (34/35 bytes) add u16 generation (>= 1) before HMAC-SHA256.
std::vector<std::uint8_t> signRejoinTicket(const Ticket &t, const std::vector<std::uint8_t> &key);
// lane ONLINE-3: a spectator ticket, version 3/6 (34/35 bytes), with spectator index (0 .. 15), mask, expiry, delay, then HMAC
std::vector<std::uint8_t> signSpectatorTicket(const Ticket &t, const std::vector<std::uint8_t> &key);
size_t ticketSize(std::uint8_t version); ///< zero for an unknown version; excludes the optional HELLO session token
// Structural validation only, NOT authentication. Clients use this to compare START fields; the relay must use verifyTicket.
bool readTicketFields(const std::uint8_t *bytes, size_t size, Ticket &out, std::string *error);
// false + *error (and *reason) on a bad length / version / key id / MAC, an expired ticket (nowUnix > expires), a slot outside the mask; versions 2/5
// (34/35 bytes) must carry a generation of at least 1
bool verifyTicket(const std::uint8_t *bytes, size_t size, const KeyRing &keys, std::uint32_t nowUnix, Ticket &out, std::string *error,
	RefuseReason *reason = nullptr);

// "keyId:hex" lines (a key file: '#' comments, blank lines); false + *error on a malformed line, a duplicate id, a key under 16 bytes
bool parseKeyRing(const std::string &text, KeyRing &out, std::string *error);

std::string base64UrlEncode(const std::uint8_t *data, size_t size);
bool base64UrlDecode(const std::string &text, std::vector<std::uint8_t> &out); ///< strict: the url alphabet, no padding, no other characters
bool base64Decode(const std::string &text, std::vector<std::uint8_t> &out);    ///< the standard alphabet with '=' padding (the lobby's setup blobs)
std::string base64Encode(const std::uint8_t *data, size_t size);

bool isRelayDatagram(const std::uint8_t *d, size_t size);
inline bool isRelayDatagram(const std::vector<std::uint8_t> &d) { return isRelayDatagram(d.data(), d.size()); }
void putHeader(std::vector<std::uint8_t> &out, Kind kind);
// `sessionToken`: empty (a first HELLO) or the 16 bytes of an earlier WELCOME; `caps`: the client's HelloCapability bits (NET-SYNC, v4 envelope)
std::vector<std::uint8_t> encodeHello(const std::vector<std::uint8_t> &ticket, const std::vector<std::uint8_t> &sessionToken = {}, std::uint8_t caps = 0);
// `epoch`: lane NET-SYNC, nonzero only for canonical games: the commit epoch the client builds its channel with
std::vector<std::uint8_t> encodeWelcome(std::uint64_t gameId, std::uint8_t slot,
	const std::array<std::uint8_t, kSessionTokenBytes> &sessionToken, std::uint64_t epoch = 0);
std::vector<std::uint8_t> encodeRefused(RefuseReason reason);
std::vector<std::uint8_t> encodeEmpty(Kind kind); ///< NOT_BOUND, BYE
// DATA: the envelope (5 bytes) in front of a transport packet
std::vector<std::uint8_t> encodeData(std::uint8_t targetSlot, const std::vector<std::uint8_t> &packet);
// lane ONLINE-3
struct SlotStatus
{
	std::uint8_t slot = 0;
	bool silent = false, bound = false;
	std::uint16_t generation = 0;
};
std::vector<std::uint8_t> encodeStatus(const std::vector<SlotStatus> &slots);
bool decodeStatus(const std::uint8_t *d, size_t size, std::vector<SlotStatus> &out); ///< the whole datagram; false when malformed
std::vector<std::uint8_t> encodeLogRequest(std::uint32_t offset);
std::vector<std::uint8_t> encodeLogChunk(std::uint32_t offset, std::uint32_t total, const std::uint8_t *bytes, size_t size);
} // namespace RelayProtocol
