// OpenBFME. GPL-3.0.
//
// OnlineLobbyClient (lane ONLINE-4, docs/ONLINE.md section 5, phase O2): the client of the OpenBFME lobby service (services/online/lobby/src/lobby.mjs: JSON
// over HTTP(S), "v": 1) that the online screens (GameClient/GUI/AptScreens/AptOnlineShell.h) are fed from, in the place retail's GameSpy peer / buddy
// queues held (TheGameSpyInfo, TheGameSpyPeerMessageQueue). It is the online counterpart of LANAPI: requests in (sign in, list, create, join, slot and setup
// changes, ready, chat, kick, start, leave), state and events out (the game list, the staging room's slots, chat, the START).
//
// The client does no I/O. It queues HTTP requests; the host sends them (the Godot game: scripts/online_http.gd with Godot's HTTPRequest, TLS included, no
// redirects; the tests: a plain HTTP/1.1 socket client) and hands every answer back with deliver(). One request is in flight at a time, in the order they
// were made, so a slot change is never overtaken by a later one. While the player is in a game the client polls it (GET /v1/games/<id>?since=<seq>)
// every Options::pollMs.
//
// Every answer is checked: a transport error, an answer that is not lobby version 1, a refused request (the lobby's error code and message) or a field
// out of range reaches an event (never a silent default). The lobby's game view becomes the SkirmishGameInfo the MpGameSetup gadgets show: "faction" is
// the player template index (as OnlineStart reads it), "team" the 0-based team, -1 random / none.
//
// Every answer is bounded (Sol r1): its body, its strings and its lists have the lobby's own limits (lobby.mjs: names 24 / 48 bytes, the map 200, a chat
// line 200, the message of the day 2000, 200 listed games, 200 chat lines kept); an answer beyond them is refused ("protocol"), never kept.
// Credentials never reach a log or a screen through the client (Sol r1): a refusal's text is redacted (redact()).
//
// Not simulation code: the game it produces starts from the lobby's START (GameNetwork/OnlineStart.h), exactly as ONLINE-2's command-line path does.

#pragma once

#include "GameNetwork/GameInfo.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

class JsonValue;

class OnlineLobbyClient
{
public:
	struct Options
	{
		std::string profileDigest;   ///< the profile identity's MD5 (ProfileIdentity::digest): the lobby compares it at the join
		int playerTemplateCount = 0; ///< factions are indices below this (or -1 random); required
		int colorCount = 0;          ///< colours likewise; required
		std::uint32_t pollMs = 1000; ///< the staging room's poll interval (a lobby that does not hold polls; the pause after a poll that failed)
		// lane ONLINE-5: long polling. Above 0 the client asks a lobby that holds polls (its poll answers carry "wait": lobby.mjs MAX_WAIT_S) to keep
		// each poll until the game changes, at most this many seconds: a chat line or a ready tick arrives one round trip after it happened, and an
		// idle room costs one request per player every `longPollS` seconds, not one a second. A held poll travels beside the ordinary requests
		// (HttpRequest::held: its own connection), so a chat line typed meanwhile is not kept waiting. 0: plain polls every pollMs (the tests' blocking
		// transport; a lobby without "wait" is polled that way whatever this says)
		std::uint32_t longPollS = 0;
	};
	struct HttpRequest
	{
		int id = 0;
		std::string method; ///< "GET" / "POST"
		std::string path;   ///< "/v1/..." (the host prefixes the lobby URL)
		std::string body;   ///< JSON ("" for a GET)
		std::string token;  ///< the bearer token ("" before the sign-in)
		bool held = false;  ///< lane ONLINE-5: a poll the lobby may keep for up to holdSeconds: sent on a connection of its own, with a longer timeout
		int holdSeconds = 0;
	};
	// One game of the server browser (lobby.mjs publicGame)
	struct Listing
	{
		std::string id;          ///< 16 hex digits
		std::u16string name;     ///< the game's name
		std::u16string host;     ///< the host's name
		std::string map;         ///< the map cache key
		int players = 0;         ///< human players in it
		int openSlots = 0;
		bool password = false;
		std::string state;       ///< "open", or "started" (refreshList(true): a running game, to watch; mine(): the player's own, to rejoin)
		int slot = -1;           ///< mine(): the player's slot in the running game
	};
	struct ChatLine
	{
		std::uint32_t seq = 0;
		bool system = false;     ///< the lobby's own line ("x joined", "the host changed the game options" ...): from is null
		std::u16string from, text;
	};
	struct Event
	{
		enum Kind
		{
			SignedIn,     ///< text = the account's name
			GameList,     ///< games() changed
			Motd,         ///< the lobby's message of the day arrived (hasMotd() may be false: the lobby has none)
			GameCreated,  ///< we host currentGame()
			GameJoined,   ///< we joined currentGame() (slot = our slot)
			GameUpdated,  ///< the staging room's view changed (slots, setup, ready flags)
			Chat,         ///< a chat line (from, text, flag = system)
			Removed,      ///< we are no longer in the game: code "kicked", "closed" (the host left) or "left"
			GameStart,    ///< the START arrived: startJson() (lane ONLINE-4: also a rejoin START or a spectator START)
			Mine,         ///< lane ONLINE-4: mine() changed (the player's running games, GET /v1/mine)
			Refused,      ///< a request was refused or failed: request = what was asked ("signin", "list", "create", "join", "slot", "setup", "ready",
			              ///< "chat", "kick", "start", "leave", "poll", "motd"), code = the lobby's error code (or "transport", "protocol"), text = the message
		} kind;
		std::string request, code;
		std::u16string text, from;
		int slot = -1;
		bool flag = false;
	};
	// A lobby slot as the lobby holds it (lobby.mjs gameView), next to its SkirmishGameSlot
	struct SlotView
	{
		std::string state;  ///< open / closed / player / easy / medium / hard / brutal
		bool ready = false;
		bool you = false;
	};

	// the bounds of an answer (lobby.mjs's own limits; Sol r1: a hostile lobby must not exhaust memory). The transport stops reading a body at
	// kMaxBodyBytes (scripts/online_http.gd: HTTPRequest.body_size_limit); deliver() refuses a larger one too
	static constexpr std::size_t kMaxBodyBytes = 256 * 1024;   ///< the largest answer: a list of 200 games is about 60 KiB
	static constexpr std::size_t kMaxPlayerNameBytes = 24;     ///< lobby.mjs: a guest's / an AI slot's name
	static constexpr std::size_t kMaxGameNameBytes = 48;       ///< lobby.mjs create: the game's name
	static constexpr std::size_t kMaxMapBytes = 200;           ///< lobby.mjs setup.map
	static constexpr std::size_t kMaxChatBytes = 256;          ///< a chat line (players' 200 bytes, the lobby's own lines name a player)
	static constexpr std::size_t kMaxMotdBytes = 2000;         ///< lobby.mjs motdFromEnv
	static constexpr std::size_t kMaxTokenBytes = 1024;        ///< a session token (base64url(JSON).base64url(HMAC): about 150 bytes)
	static constexpr std::size_t kMaxStartBytes = 16 * 1024;   ///< a START (8 slots, no Create-a-Hero records: about 1.5 KiB)
	static constexpr std::size_t kMaxRefusalBytes = 512;       ///< a refusal's message ("not ready: " and up to 8 names)
	static constexpr std::size_t kMaxGames = 200;              ///< lobby.mjs list: .slice(0, 200)
	static constexpr std::size_t kMaxMine = 32;                ///< the player's running games
	static constexpr std::size_t kMaxChatLines = 200;          ///< lobby.mjs keeps the last 200 lines of a game
	static constexpr int kMaxHoldSeconds = 60;                 ///< lane ONLINE-5: the longest hold a lobby may announce ("wait"; lobby.mjs MAX_WAIT_S is 25)

	// Sol r1: the credentials a text may carry, masked for the logs and the screens: every run of 40 or more base64url characters (a relay ticket,
	// a rejoin or spectator ticket, a session token's HMAC; with its dot-joined parts: the whole token) becomes "<redacted>". The lobby's ids (16 hex
	// digits) and the profile digest (32) stay
	static std::string redact(const std::string &text);

	explicit OnlineLobbyClient(const Options &options);

	// ---- requests ----
	// POST /v1/auth/guest: a guest identity (the lobby has only guest sign-in today)
	void signInGuest(const std::u16string &name);
	void refreshList(bool running = false);               // GET /v1/games (lane ONLINE-4: ?running=1 also lists the started games)
	// lane ONLINE-4 (ONLINE-3's rejoin and spectators in the server browser)
	// a lobby session kept from an earlier run (ONLINE-3: user://online_session.txt): it is checked with GET /v1/mine; valid -> SignedIn (the account's
	// name read from the token's own payload) and Mine; refused -> Refused "session" (the caller signs in as a new guest)
	void useSession(const std::string &token);
	void fetchMine();                                     // GET /v1/mine: the player's running games
	void rejoinGame(const std::string &id);               // POST /v1/games/<id>/rejoin: a rejoin START (GameStart)
	void spectateGame(const std::string &id);             // POST /v1/games/<id>/spectate: a spectator START (GameStart)
	const std::string &sessionToken() const { return m_token; }
	const std::vector<Listing> &mine() const { return m_mine; }
	// the account name a session token names (its payload, lobby.mjs makeToken: base64url(JSON).base64url(HMAC)); "" when it is not one
	static std::u16string sessionName(const std::string &token);
	void fetchMotd();                                     // GET /v1/motd
	// POST /v1/games: host a game; `setup` gives the map, the cash, the rules and the slot states (the host's slot must be SLOT_PLAYER, the other human
	// slots open). `password` "" = none
	void createGame(const std::u16string &name, const std::string &password, const SkirmishGameInfo &setup, int hostSlot);
	void joinGame(const std::string &id, const std::string &password); // POST /v1/games/<id>/join
	void leaveGame();                                     // POST .../leave; the client is out of the game at once (Removed "left")
	// the local player's own slot (POST .../slot); kKeep leaves a field
	static constexpr int kKeep = -1000;
	void setOwnSlot(int faction, int color, int startPos, int team);
	// host: a slot that is not a player's (its state open / closed / an AI level, and the AI's choices)
	void setOtherSlot(int slot, SlotState state, int faction, int color, int startPos, int team);
	// host: the game options (POST .../setup)
	void setSetup(const SkirmishGameInfo &setup);
	void setReady(bool ready);
	void chat(const std::u16string &text);
	void kick(int slot);
	void start();                                         // host: POST .../start (the lobby builds every player's START)

	// moves the client on: schedules the staging room's poll
	void update(std::uint64_t nowMs);
	// the next request to send (at most one in flight): empty when none is due
	std::vector<HttpRequest> takeRequests();
	// the answer to request `id`: the HTTP status and body, or a transport error ("" when the request reached the lobby)
	void deliver(int id, int status, const std::string &body, const std::string &transportError);
	std::vector<Event> takeEvents();

	// ---- state ----
	bool signedIn() const { return !m_token.empty(); }
	const std::u16string &accountName() const { return m_accountName; }
	const std::vector<Listing> &games() const { return m_games; }
	bool motdKnown() const { return m_motdKnown; }  ///< the lobby answered /v1/motd
	bool hasMotd() const { return m_hasMotd; }      ///< and had a message
	const std::u16string &motd() const { return m_motd; }
	bool inGame() const { return !m_gameId.empty(); }
	const std::string &gameId() const { return m_gameId; }
	const std::u16string &gameName() const { return m_gameName; }
	bool amIHost() const { return inGame() && m_localSlot >= 0 && m_localSlot == m_hostSlot; }
	int localSlot() const { return m_localSlot; }
	int hostSlot() const { return m_hostSlot; }
	const SkirmishGameInfo &gameInfo() const { return m_info; }
	const std::array<SlotView, MAX_SLOTS> &slotViews() const { return m_slots; }
	const std::string &startJson() const { return m_startJson; }
	// Sol r1: the START could not be played (OnlineStart refused it, or the host program could not open the relay socket): the client forgets it and
	// the game it was for, as far as this client goes (no leave is sent: the player keeps the slot and can rejoin the running game)
	void dropStart();
	const Options &options() const { return m_options; }
	std::uint32_t viewSeq() const { return m_seq; } ///< the game view's seq (moves with every change the lobby made)
	bool busy() const { return m_inFlight != 0 || !m_queue.empty(); }
	// lane ONLINE-5: the seconds the lobby holds a poll (its "wait"; 0: it does not, or no poll answered yet), and whether a held poll is out
	int holdSeconds() const { return m_holdCap; }
	bool pollHeld() const { return m_heldInFlight != 0; }
	// the number of the requests sent of a kind ("slot", "setup" ...), for the tests
	int sentCount(const std::string &kind) const
	{
		auto it = m_sent.find(kind);
		return it == m_sent.end() ? 0 : it->second;
	}

	// helpers (also the screens')
	static std::string utf16ToUtf8(const std::u16string &in);
	static std::string jsonQuote(const std::string &utf8);
	static const char *slotStateName(SlotState s);

private:
	struct Pending
	{
		std::string kind;
		HttpRequest request;
	};
	void queue(const std::string &kind, const std::string &method, const std::string &path, const std::string &body);
	void refuse(const std::string &request, const std::string &code, const std::string &message);
	// `fresh`: the view of a create / join (taken whatever its seq); otherwise a view older than the one held (an answer overtaken by a held poll's, or
	// the other way round) is checked like any other and then left aside
	bool readGameView(const JsonValue &game, std::string *error, bool fresh = false);
	void leftGame(const std::string &why);
	void handle(const Pending &p, int status, const JsonValue &body);

	Options m_options;
	std::string m_token;
	std::u16string m_accountName;
	std::vector<Listing> m_games;
	std::vector<Listing> m_mine;
	bool m_motdKnown = false, m_hasMotd = false;
	std::u16string m_motd;
	// the game we are in
	std::string m_gameId;
	std::u16string m_gameName;
	int m_localSlot = -1, m_hostSlot = -1;
	SkirmishGameInfo m_info;
	std::array<SlotView, MAX_SLOTS> m_slots{};
	std::uint32_t m_seq = 0;      ///< the game view's seq
	std::uint32_t m_chatSince = 0; ///< the last chat line read
	std::string m_startJson;
	std::uint64_t m_nextPoll = 0;
	bool m_pollQueued = false;
	// the requests
	int m_nextId = 1;
	int m_inFlight = 0;
	Pending m_current;
	std::deque<Pending> m_queue;
	std::vector<Event> m_events;
	std::map<std::string, int> m_sent;
	// lane ONLINE-5: long polling and the sender's own chat lines
	int m_holdCap = 0;              ///< the lobby's "wait" (seconds); 0 until a poll answer carried one
	int m_heldInFlight = 0;         ///< the id of the held poll that is out (0: none)
	bool m_heldDue = false;         ///< a held poll is to be sent (takeRequests)
	std::uint64_t m_now = 0;        ///< the last update()'s clock
	std::uint64_t m_heldSentAt = 0;
	std::uint32_t m_heldSeq = 0, m_heldSince = 0; ///< what the held poll that is out asked from
	int m_backoffS = 0;             ///< Sol r1: the last poll answer's "backoff" (seconds before the next poll; 0: none)
	int m_ownEcho = 0;              ///< chat lines shown when they were sent whose copy from the lobby ("you") is still to come
};
