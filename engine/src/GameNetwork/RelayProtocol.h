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
//   HELLO      client -> relay   the 32-byte ticket, then the 16-byte session token of an earlier WELCOME when the client has one. A slot's first HELLO
//                                binds the sender's address to (game, slot); once bound, only a HELLO that carries the slot's session token moves the
//                                slot to another address (a NAT rebinding): the ticket alone does not (ONLINE-2 r2, Sol's takeover). Sent until WELCOME,
//                                and again after NOT_BOUND
//   WELCOME    relay -> client   u64 game id, u8 slot, the slot's 16-byte session token (random, chosen by the relay at the first bind)
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
// A spectator ticket (version 3, 34 bytes) binds a read-only client: it may ask for the game's log (LOG_REQUEST: served up to its delay behind the players)
// and nothing else; its DATA is REFUSED (REFUSE_SPECTATOR), never logged or forwarded; WELCOME names it as 0x80 + its index.
// A rejoin ticket (version 2, 34 bytes) carries a generation: a HELLO whose ticket names a higher generation than the slot's binds the slot to the new
// address without the session token (the lobby issued it to the slot's player after its game restarted); the relay then starts the slot's streams afresh
// and tells the others (STATUS), whose transports start theirs afresh when their Network takes the slot back. A lower generation is REFUSED (stale).
//
// Ticket (32 bytes): u8 ticket version 1, u8 key id, u64 game id, u8 slot, u8 slot mask (the game's network slots), u32 expiry (Unix seconds),
// then the first 16 bytes of HMAC-SHA256(key[key id], the 16 bytes before). The lobby holds the keys and signs; the relay holds the same keys and checks
// offline (no call to the lobby during a game: a lobby outage does not stop running games). Text form: base64url without padding (43 characters).
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
constexpr std::uint8_t kVersion = 3; // 2 (ONLINE-2 r2): the session token in WELCOME / HELLO; 3 (ONLINE-3): STATUS, LOG_REQUEST / LOG_CHUNK, rejoin tickets
constexpr size_t kSessionTokenBytes = 16;
constexpr size_t kHeaderBytes = 4;
constexpr size_t kTicketBytes = 32;
constexpr size_t kTicketBytesV2 = 34; ///< lane ONLINE-3: the rejoin ticket (+ u16 generation before the MAC)
constexpr size_t kLogChunkBytes = 1024; ///< lane ONLINE-3: the log bytes of one LOG_CHUNK
constexpr int kMaxSpectators = 16;      ///< lane ONLINE-3: spectator indices 0 .. 15 per game
constexpr std::uint8_t kSpectatorWelcome = 0x80; ///< lane ONLINE-3: WELCOME's slot byte of spectator i is 0x80 + i
constexpr size_t kTicketMacBytes = 16;
constexpr int kMaxSlots = 8;

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
	REFUSE_SPECTATOR = 9    ///< lane ONLINE-3: DATA from a spectator (read-only: it sends no commands)
};
const char *refuseReasonName(int reason);

struct Ticket
{
	std::uint8_t keyId = 0;
	std::uint64_t gameId = 0;
	std::uint8_t slot = 0;
	std::uint8_t slotMask = 0;
	std::uint32_t expires = 0;
	std::uint16_t generation = 0; ///< lane ONLINE-3: 0 the START's ticket (version 1); 1 .. the n-th rejoin ticket of the slot (version 2)
	bool spectator = false;       ///< lane ONLINE-3: a spectator ticket (version 3): `slot` is the spectator index, no player slot
	std::uint16_t delaySeconds = 0; ///< lane ONLINE-3, spectator: the relay serves the game's log this many seconds of game time behind the players
};

// the signing keys by id (the lobby's and the relay's key ring)
using KeyRing = std::map<std::uint8_t, std::vector<std::uint8_t>>;

std::array<std::uint8_t, kTicketBytes> signTicket(const Ticket &t, const std::vector<std::uint8_t> &key);
// lane ONLINE-3: version 2 (34 bytes): u8 2, the same fields, u16 generation (>= 1), then the first 16 bytes of HMAC-SHA256 over the 18 bytes before
std::vector<std::uint8_t> signRejoinTicket(const Ticket &t, const std::vector<std::uint8_t> &key);
// lane ONLINE-3: a spectator ticket, version 3 (34 bytes): u8 3, key id, game id, u8 spectator index (0 .. 15), u8 the game's slot mask, u32 expiry,
// u16 the spectator delay in seconds, then the first 16 bytes of HMAC-SHA256 over the 18 bytes before
std::vector<std::uint8_t> signSpectatorTicket(const Ticket &t, const std::vector<std::uint8_t> &key);
// false + *error (and *reason) on a bad length / version / key id / MAC, an expired ticket (nowUnix > expires), a slot outside the mask; a version 2 ticket
// (34 bytes) must carry a generation of at least 1
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
// `sessionToken`: empty (a first HELLO) or the 16 bytes of an earlier WELCOME
std::vector<std::uint8_t> encodeHello(const std::vector<std::uint8_t> &ticket, const std::vector<std::uint8_t> &sessionToken = {});
std::vector<std::uint8_t> encodeWelcome(std::uint64_t gameId, std::uint8_t slot, const std::array<std::uint8_t, kSessionTokenBytes> &sessionToken);
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
