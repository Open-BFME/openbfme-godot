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

#include "Common/SimulationProfile.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/JoinBuild.h"
#include "GameNetwork/PlatformKind.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class JsonValue;

class OnlineLobbyClient
{
public:
	struct Options
	{
		std::string installId, deviceHash; ///< supplied by the host; never part of simulation state
		std::string clientPlatform, clientVersion;
		// lane JOINVER-1: the rest of the build a create / join tells (lobby.mjs checkBuild; the version is clientVersion): the build day and whether
		// this is an official release (1), not (0), not told (-1)
		std::string buildDate;
		int buildOfficial = -1;
		std::string profileDigest;   ///< the profile identity's MD5 (ProfileIdentity::digest): the lobby compares it at the join
		// lane UBERJOIN-1: the simulation profile that digest is of (the player's performance mode): the room made and the room joined carry it as their
		// "mode" ("normal" / "uber"); a join of a room in the other mode is the lobby's "mode" refusal. setProfile changes both together
		SimulationProfile profile = SimulationProfile::Faithful;
		int playerTemplateCount = 0; ///< factions are indices below this (or -1 random); required
		int colorCount = 0;          ///< colours likewise; required
		std::uint32_t pollMs = 1000; ///< the staging room's poll interval (a lobby that does not hold polls; the pause after a poll that failed)
		// lane ONLINE-5: long polling. Above 0 the client asks a lobby that holds polls (its poll answers carry "wait": lobby.mjs MAX_WAIT_S) to keep
		// each poll until the game changes, at most this many seconds: a chat line or a ready tick arrives one round trip after it happened, and an
		// idle room costs one request per player every `longPollS` seconds, not one a second. A held poll travels beside the ordinary requests
		// (HttpRequest::held: its own connection), so a chat line typed meanwhile is not kept waiting. 0: plain polls every pollMs (the tests' blocking
		// transport; a lobby without "wait" is polled that way whatever this says)
		std::uint32_t longPollS = 0;
		// lane PLATFORMICON-1: the one value this client tells the lobby about itself (the create / join body's "platform"); None: nothing is sent
		PlatformKind platform = PlatformKind::None; // the game's glue sets localPlatform()
		bool enableSocial = false; ///< SOCIAL-1: the client also runs the social calls (friends, presence, invites, the inbox poll)
	};
	struct HttpRequest
	{
		enum Channel { Ordinary, Room, Social };
		int id = 0;
		std::string method; ///< "GET" / "POST"
		std::string path;   ///< "/v1/..." (the host prefixes the lobby URL)
		std::string body;   ///< JSON ("" for a GET)
		std::string token;  ///< the bearer token ("" before the sign-in)
		bool held = false;  ///< lane ONLINE-5: a poll the lobby may keep for up to holdSeconds: sent on a connection of its own, with a longer timeout
		int holdSeconds = 0;
		int maxSlots = MAX_SLOTS; ///< X-OpenBFME-Max-Slots: absent on legacy clients, which support eight slots
		Channel channel = Ordinary;
	};
	struct Player
	{
		std::string pid, code;
		std::u16string nick;
	};
	struct Presence
	{
		std::string pid, state, game, map, mode;
		bool hasGame = false, joinable = false, hasMap = false, hasMode = false;
		std::uint64_t since = 0;
	};
	struct Request
	{
		Player from, to;
		std::string id;
		std::uint64_t at = 0;
	};
	struct Invite
	{
		Player from, to;
		std::string id, game, kind, mode, note;
		bool hasMode = false;
		std::uint64_t expires = 0;
	};
	struct Message
	{
		std::uint64_t id = 0, at = 0;
		std::string from, to, clientId, text;
	};
	struct Privacy
	{
		std::string invites = "friends_or_room";
		std::string challenges = "friends_or_room";
		std::string requests = "anyone";
		std::string presence = "friends";
	};
	struct Me
	{
		std::string pid, code;
		std::u16string nick;
		Privacy privacy;
		bool hidden = false;
	};
	struct Notice
	{
		std::string id, kind, invite;
		bool hasInvite = false;
		std::uint64_t at = 0;
	};
	struct RecentPlayer
	{
		Player player;
		std::string target;
	};
	struct RecentGame
	{
		std::string game;
		std::uint64_t at = 0;
		std::vector<RecentPlayer> players;
	};
	struct Block
	{
		Player player;
		std::string note;
		std::uint64_t at = 0;
	};
	struct ChatHead
	{
		std::string pid;
		std::uint64_t lastId = 0, readId = 0;
	};
	struct Thread
	{
		std::string pid, code;
		std::u16string nick;
		std::uint64_t lastId = 0, readId = 0;
	};
	struct Conversation
	{
		std::vector<Message> messages;
		std::uint64_t lastId = 0, readId = 0;
		std::uint64_t historyThrough = 0;
		bool hasNext = false;
		std::uint64_t next = 0;
	};
	struct SocialStats
	{
		std::uint64_t players = 0, gamesOpen = 0, gamesRunning = 0, asOf = 0;
	};
	struct SocialState
	{
		bool available = false;
		/// ONLINEMENU-1 (BATCH-P3 live-lobby check): the lobby has no social service: its /v1/health lists no "social" feature (the deployed
		/// lobby before the social routes), or it answered not_found for /v1/me or the inbox. Nothing social is asked; the screens say so
		bool unsupported = false;
		Me me;
		std::vector<Player> friends;
		std::vector<Request> requests;
		std::vector<Invite> invites;
		std::vector<Presence> presence;
		std::vector<Notice> notices;
		std::vector<RecentGame> recent;
		std::vector<Block> blocks;
		std::vector<ChatHead> chatHeads;
		std::vector<Thread> threads;
		std::map<std::string, Conversation> conversations;
		std::string seq, blocksNext, threadsNext;
		bool hasBlocksNext = false, hasThreadsNext = false;
		SocialStats stats;
	};
	struct SocialEvent
	{
		enum Kind { Changed, Chat, Recovery, NoticeEvent, Error } kind = Changed;
		std::string request, code, pid, id, text;
	};
	// One game of the server browser (lobby.mjs publicGame)
	struct Listing
	{
		std::string id;          ///< 16 hex digits
		std::u16string name;     ///< the game's name
		std::u16string host;     ///< the host's name
		PlatformKind hostPlatform = PlatformKind::None; ///< lane PLATFORMICON-1: the host's system (null / absent / anything but the three names: None)
		std::string map;         ///< the map cache key
		int players = 0;         ///< human players in it
		int openSlots = 0;
		int mapStartCount = LEGACY_MAX_SLOTS;
		bool password = false;
		SimulationProfile mode = SimulationProfile::Faithful; ///< lane UBERJOIN-1: the game's performance mode ("mode": absent = "normal", a room of an old client)
		bool cheatMode = false;  ///< lane CHEAT-1: the host turned Cheat Mode on ("cheatMode": absent = off, a lobby before CHEAT-1)
		std::string state;       ///< "open", or "started" (refreshList(true): a running game, to watch; mine(): the player's own, to rejoin)
		int slot = -1;           ///< mine(): the player's slot in the running game
		// lane JOINVER-1: the game's profile id and its host's build (an older lobby: both empty); a profile other than this client's cannot be joined
		std::string profile;
		JoinBuild::Build build;
	};
	// lane JOINVER-1: the lobby's 409 "profile" (a join refused for a different build): both sides' builds and profile ids (an older lobby: none)
	struct ProfileRefusal
	{
		bool valid = false;
		JoinBuild::Build host, yours;
		std::string hostProfile, yourProfile;
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
			Notice,       ///< text = a pending sign-in notice
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
		std::string playerId; ///< opaque reporting target, never an install or device identifier
		// lane ONLINEMENU-1: the slot's Create-a-Hero record (lobby.mjs gameView `cah`) could not be read (why); the slot is shown without a hero and
		// the host's start is refused by name until it is changed (a START carrying it would fail on every client, OnlineStart's "cah" check)
		std::string heroProblem;
		std::string pid;      ///< empty when the personalized view has no social pid
		bool blocked = false;
		// lane ONLINEMENU-1: the player's link to the room's relay as their client reported it (lobby.mjs gameView `link`, the Connections screen):
		// linkKnown false = not reported yet; rttMs -1 = the relay did not answer them at all (loss 100)
		bool linkKnown = false;
		int rttMs = -1, lossPct = 100;
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
	// a START: 8 slots are about 1.5 KiB; lane ONLINEMENU-1: with up to 8 Create-a-Hero records of kMaxCreateAHeroBytes (5,464 bytes of base64 each)
	static constexpr std::size_t kMaxStartBytes = 64 * 1024;
	static constexpr std::size_t kMaxRefusalBytes = 512;       ///< a refusal's message ("not ready: " and up to 8 names)
	static constexpr std::size_t kMaxStatusBytes = 8192;
	static constexpr std::size_t kMaxNoticeBytes = 4000;
	static constexpr std::size_t kMaxGames = 200;              ///< lobby.mjs list: .slice(0, 200)
	static constexpr std::size_t kMaxMine = 32;                ///< the player's running games
	static constexpr std::size_t kMaxChatLines = 200;          ///< lobby.mjs keeps the last 200 lines of a game
	static constexpr int kMaxHoldSeconds = 60;                 ///< lane ONLINE-5: the longest hold a lobby may announce ("wait"; lobby.mjs MAX_WAIT_S is 25)

	// Sol r1: the credentials a text may carry, masked for the logs and the screens: every run of 40 or more base64url characters (a relay ticket,
	// a rejoin or spectator ticket, a session token's HMAC; with its dot-joined parts: the whole token) becomes "<redacted>". The lobby's ids (16 hex
	// digits) and the profile digest (32) stay
	static std::string redact(const std::string &text);
	std::string platformField() const;
	std::string buildField() const; ///< lane JOINVER-1: ",\"build\":{...}" for create / join bodies, or "" (an odd version is not told) // lane PLATFORMICON-1: ,"platform":"<name>" or nothing

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
	// lane ONLINEUX-1 r2 (Sol 4): the started games the signed-in account left, as the lobby's /v1/mine names them ("left": absent from an older lobby,
	// then empty): a game left in an earlier run of the program is neither the player's to rejoin nor to watch (the lobby refuses a player as its spectator)
	const std::vector<std::string> &mineLeft() const { return m_mineLeft; }
	// the account name a session token names (its payload, lobby.mjs makeToken: base64url(JSON).base64url(HMAC)); "" when it is not one
	static std::u16string sessionName(const std::string &token);
	// DEVINPR-1, ONLINEMENU-1: whether a session token was issued to this install. The token no longer shows the install (PR #8 review finding 2:
	// it is the credential that signs in as the player); it carries a public tag of it (lobby.mjs installTag: the first 16 bytes of HMAC-SHA256 keyed
	// with "openbfme-install-tag-v1" over the install id, hex), compared here. False for a token without one (older lobbies) or not a token
	static bool sessionMatchesInstall(const std::string &token, const std::string &installId);
	void fetchMotd();                                     // GET /v1/motd
	// POST /v1/games: host a game; `setup` gives the map, the cash, the rules and the slot states (the host's slot must be SLOT_PLAYER, the other human
	// slots open). `password` "" = none
	// lane ONLINEHUB-1: `roomCode` (not "") makes a private room named by that code (lobby.mjs ROOM_CODE: a Quick Match or tournament match room; not
	// listed, no password)
	void createGame(const std::u16string &name, const std::string &password, const SkirmishGameInfo &setup, int hostSlot, const std::string &roomCode = "");
	void joinGame(const std::string &id, const std::string &password); // POST /v1/games/<id>/join
	void joinByCode(const std::string &roomCode);                      // lane ONLINEHUB-1: POST /v1/rooms/<code>/join (GameJoined, or Refused "join")
	void leaveGame();                                     // POST .../leave; the client is out of the game at once (Removed "left")
	// the local player's own slot (POST .../slot); kKeep leaves a field
	static constexpr int kKeep = -1000;
	// lane ONLINEMENU-1: `handicap` the slot's GameSlot + 0x20 (0, -5 .. -95: SkirmishGameSlot::handicapValid); kKeep leaves it
	void setOwnSlot(int faction, int color, int startPos, int team, int handicap = kKeep);
	// host: a slot that is not a player's (its state open / closed / an AI level, and the AI's choices)
	void setOtherSlot(int slot, SlotState state, int faction, int color, int startPos, int team, int handicap = kKeep);
	// lane ONLINEMENU-1: the local player's Create-a-Hero for its own slot (POST .../slot {"cah": base64 of its wire form | null}); false + *error when the
	// record is over kMaxCreateAHeroBytes (nothing is sent)
	bool setOwnCreateAHero(const CreateAHeroHero *hero, std::string *error);
	// lane ONLINEMENU-1: the local player's link to the room's relay (POST .../link {rtt, loss}, rttMs -1: {rtt: null}); display data for the Connections
	// screen, it unreadies nobody. Out of range values are refused here (nothing sent)
	void reportLink(int rttMs, int lossPct);
	const std::string &roomRelay() const { return m_roomRelay; } ///< lane ONLINEMENU-1: the relay the room's START will name ("" none)
	/// lane JOINVER-1: whether the lobby's game view names its relay at all (false: an older lobby, without the Connections screen's data)
	bool roomRelayTold() const { return m_roomRelayTold; }
	// lane ONLINEMENU-1 (owner, 2026-10-20: "ping to server"): the lobby's response time, the fastest of the last kLobbyRttSamples answers to ordinary
	// requests (held polls excluded: the lobby keeps them on purpose), in ms from the request handed to the host to its answer; -1 before the first
	int lobbyRttMs() const;
	static constexpr std::size_t kLobbyRttSamples = 8;
	// lane ONLINEMENU-1: the local player's latest measured link to the relay (the room's, else the lobby's: lobbyRelay), every measurement, not
	// only the reported steps; display data (the corner's ping). ownRelayKnown false: not measured yet; ownRelayRttMs -1 with it: no answer
	void noteOwnLink(int rttMs, int lossPct)
	{
		m_ownRelayKnown = true;
		m_ownRelayRtt = rttMs;
		m_ownRelayLoss = lossPct;
	}
	void forgetOwnLink()
	{
		m_ownRelayKnown = false;
		m_ownRelayRtt = -1;
		m_ownRelayLoss = 100;
	}
	bool ownRelayKnown() const { return m_ownRelayKnown; }
	int ownRelayRttMs() const { return m_ownRelayRtt; }
	// lane ONLINEMENU-1: the relay the lobby names to anyone (GET /v1/health "relays", the first: the one a room's START names), so the player's ping
	// to the game server is known outside a room too. Display data: a failure is kept in relayLookupError() (the screens note it), never a box
	void fetchRelays();
	bool relaysAsked() const { return m_relaysAsked; }
	/// lane JOINVER-1: the last join refused for a different build (valid only when the lobby named the builds)
	const ProfileRefusal &profileRefusal() const { return m_profileRefusal; }
	/// lane JOINVER-1: this client's build as the lobby is told it
	JoinBuild::Build ownBuild() const;
	const std::string &profileDigest() const { return m_options.profileDigest; }
	/// ONLINEMENU-1: a feature the lobby's /v1/health `features` names ("social", "oauth", "email"), asked before each guest sign-in
	bool lobbyHasFeature(const std::string &name) const { return m_lobbyFeatures.count(name) != 0; }
	const std::string &lobbyRelay() const { return m_lobbyRelay; }
	const std::string &relayLookupError() const { return m_relayLookupError; }
	// host: the game options (POST .../setup)
	void setSetup(const SkirmishGameInfo &setup);
	void setReady(bool ready);
	void chat(const std::u16string &text);
	void kick(int slot);
	void start();                                         // host: POST .../start (the lobby builds every player's START)
	void refreshSocial();
	bool setPrivacy(const Privacy &privacy);
	bool setPresence(const std::string &state, bool hasHidden = false, bool hidden = false);
	bool requestFriendCode(const std::string &code);
	bool requestFriend(const std::string &pid);
	bool acceptFriend(const std::string &requestId);
	bool declineFriend(const std::string &requestId);
	bool removeFriend(const std::string &pid);
	bool blockPlayer(const std::string &pid, const std::string &note = "");
	bool unblockPlayer(const std::string &pid);
	bool fetchBlocks(const std::string &after = "");
	bool sendInvite(const std::string &to, const std::string &kind, const std::string &mode = "", const std::string &note = "");
	bool acceptInvite(const std::string &id);
	bool acceptInviteAfterLeave(const std::string &id);
	bool declineInvite(const std::string &id, bool busy = false);
	bool fetchRecent();
	bool fetchThreads(const std::string &after = "");
	bool fetchMessages(const std::string &pid, std::uint64_t after = 0);
	bool sendMessage(const std::string &pid, const std::string &clientId, const std::string &text);
	bool markRead(const std::string &pid, std::uint64_t through);
	bool generateRecovery();
	std::string takeRecoveryCode();

	// moves the client on: schedules the staging room's poll
	void update(std::uint64_t nowMs);
	// the next request to send (at most one in flight): empty when none is due
	std::vector<HttpRequest> takeRequests();
	// the answer to request `id`: the HTTP status and body, or a transport error ("" when the request reached the lobby)
	void deliver(int id, int status, const std::string &body, const std::string &transportError);
	std::vector<Event> takeEvents();
	std::vector<SocialEvent> takeSocialEvents();

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
	// the game view's "private" (lobby.mjs: a room-code room is a Quick Match's or a tournament's, ONLINEHUB-1): SOLO-1's
	// one-human start is for a custom game only; a private room keeps the two-human minimum; lane CHEAT-1: and has no Cheat Mode
	bool privateRoom() const { return m_privateRoom; }
	int localSlot() const { return m_localSlot; }
	int hostSlot() const { return m_hostSlot; }
	const SkirmishGameInfo &gameInfo() const { return m_info; }
	// lane PLATFORMICON-1: the room's slots whose "platform" was neither absent, null nor one of windows / linux / macos: shown without an icon, and named
	// here ("slot 2: platform \"beos\" is not windows, linux or macos"; the last kMaxPlatformNotes)
	const std::vector<std::string> &platformNotes() const { return m_platformNotes; }
	static constexpr std::size_t kMaxPlatformNotes = 16;
	const std::array<SlotView, MAX_SLOTS> &slotViews() const { return m_slots; }
	const std::string &startJson() const { return m_startJson; }
	// lane ONLINEUX-1: the lobby game this player played: the screens set it when they hand a START on (the room's game, or the rejoined one; a
	// spectator's is not the player's). finishGame: the played game is over for this player (the online screens are back after it): the lobby is told
	// (POST /v1/games/<id>/leave: the lobby keeps a started game's slot, and offers it for a rejoin, until its player leaves) and the client forgets it
	// (also out of mine()); returns the id, "" when none was played
	void setPlayedGame(const std::string &id) { m_playedGame = id; }
	const std::string &playedGame() const { return m_playedGame; }
	std::string finishGame();
	// lane ONLINEUX-1 r2 (Claude F1, Sol): the played game could not go on on this side (a rejoin or a watch that failed before it was live): the client
	// forgets it and the staging room it came from without telling the lobby, so the player keeps the slot and the lobby keeps offering the game for a
	// rejoin (only a real leave ends it; the room page's teardown sends none for a room this client no longer is in)
	void forgetPlayedGame();
	// Sol r1: the START could not be played (OnlineStart refused it, or the host program could not open the relay socket): the client forgets it and
	// the game it was for, as far as this client goes (no leave is sent: the player keeps the slot and can rejoin the running game)
	void dropStart();
	const Options &options() const { return m_options; }
	// lane UBERJOIN-1: the performance mode this client's games run, the one function a later matcher (Quick Match) asks. setProfile switches it and the
	// digest the lobby compares (the caller's: the profile identity of that mode); refused (false, `why` named) while in a room: nobody changes mode inside one
	SimulationProfile profile() const { return m_options.profile; }
	bool setProfile(SimulationProfile profile, const std::string &digest, std::string *why);
	// the room's mode (the host's: it can't differ inside a room): the view's "mode", Faithful before a room
	SimulationProfile gameMode() const { return m_info.simulationProfile; }
	const SocialState &social() const { return m_social; }
	const std::string &lastSocialError() const { return m_lastSocialError; }
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
	// lane UBERJOIN-1: the lobby's "mode" names: "normal" (Faithful) and "uber"
	static const char *modeName(SimulationProfile p) { return p == SimulationProfile::Uber ? "uber" : "normal"; }
	static bool parseMode(const std::string &name, SimulationProfile *out);

private:
	std::string joinIdentityFields() const;
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
	void handleSocial(const Pending &p, const JsonValue &body);
	void startSocial();
	void resetSocialSession();
	void stopSocial(const std::string &code, const std::string &request = "social");
	void markSocialUnsupported(); ///< ONLINEMENU-1: the lobby has no social service (SocialState::unsupported)
	void socialRefuse(const std::string &request, const std::string &code);
	void socialQueue(const std::string &kind, const std::string &method, const std::string &path, const std::string &body = "");
	bool socialReady(const std::string &request);

	Options m_options;
	std::string m_token;
	std::string m_lastNotice;
	std::u16string m_accountName;
	std::vector<Listing> m_games;
	std::vector<Listing> m_mine;
	bool m_motdKnown = false, m_hasMotd = false;
	std::u16string m_motd;
	// the game we are in
	std::string m_gameId;
	std::u16string m_gameName;
	bool m_privateRoom = false;
	int m_localSlot = -1, m_hostSlot = -1;
	SkirmishGameInfo m_info;
	std::vector<std::string> m_platformNotes; // lane PLATFORMICON-1
	std::array<SlotView, MAX_SLOTS> m_slots{};
	std::string m_roomRelay; // lane ONLINEMENU-1
	std::uint32_t m_seq = 0;      ///< the game view's seq
	std::uint32_t m_chatSince = 0; ///< the last chat line read
	std::string m_startJson;
	std::string m_playedGame; // lane ONLINEUX-1
	std::vector<std::string> m_mineLeft; // lane ONLINEUX-1 r2
	std::uint64_t m_nextPoll = 0;
	bool m_pollQueued = false;
	// the requests
	int m_nextId = 1;
	int m_inFlight = 0;
	Pending m_current;
	std::deque<Pending> m_queue;
	std::vector<Event> m_events;
	SocialState m_social;
	std::vector<SocialEvent> m_socialEvents;
	std::string m_lastSocialError, m_recoveryCode, m_pendingSocialAcceptInvite;
	std::set<std::string> m_seenSocialNotices;
	bool m_socialStarted = false, m_socialStopped = false, m_socialDue = false, m_socialFirstInbox = true;
	int m_socialHeldInFlight = 0;
	Pending m_socialHeld;
	std::uint64_t m_socialNextPoll = 0, m_socialBackoffUntil = 0, m_socialSentAt = 0;
	std::string m_socialAskedSeq;
	bool m_gamePasswordProtected = false;
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
	std::uint64_t m_inFlightSentAt = 0; ///< lane ONLINEMENU-1: when the ordinary request in flight was handed to the host
	std::deque<int> m_lobbyRtts;        ///< its last answers' times (lobbyRttMs)
	int m_ownRelayRtt = -1, m_ownRelayLoss = 100; ///< noteOwnLink
	bool m_ownRelayKnown = false;
	bool m_relaysAsked = false;
	bool m_roomRelayTold = false; ///< lane JOINVER-1
	ProfileRefusal m_profileRefusal; ///< lane JOINVER-1
	std::set<std::string> m_lobbyFeatures; ///< ONLINEMENU-1: /v1/health `features` (empty: an older lobby, or the answer was unreadable)
	std::string m_lobbyRelay, m_relayLookupError; ///< fetchRelays
};
