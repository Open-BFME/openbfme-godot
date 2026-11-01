// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// LANAPI (lane MP-2): the LAN lobby: finding the games on the local network, creating one, joining one, the game options of the setup screen (slots,
// factions, colours, teams, start positions, the map, the starting cash), accepting, chat, kicking, and the start that hands every player the same game
// (LobbyStart, GameNetwork/LANLobby.h) for its NetGameSession.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the lobby socket (RW 0x84C844): port 8086 + atoi(getenv("_EA_RTS_HEADLESS") or "0"), the next one while the bind fails, up to 8093; a message
//     goes out to every port 8086 .. 8093 (RW 0x84C264 / 0x84C2DF: the broadcast address, the eight ports, 0x1D8 = 472 bytes per message);
//   * the message types (the LANMessage::MSG_* names RW 0xC54E08 .. 0xC55308): REQUEST_LOCATIONS, GAME_ANNOUNCE, LOBBY_ANNOUNCE, REQUEST_JOIN,
//     JOIN_ACCEPT, JOIN_DENY, REQUEST_GAME_LEAVE, REQUEST_LOBBY_LEAVE, REQUEST_HOST_LEAVE, SET_ACCEPT, MAP_AVAILABILITY, CHAT, ENABLE_MPSETUP_UI,
//     GAME_START, GAME_START_TIMER, GAME_OPTIONS, INACTIVE, REQUEST_GAME_INFO, GAME_OPTIONS_PACKED (the numbers below are their order in the binary's
//     string table, INFERENCE);
//   * the screens: LanLobby.apt (AptLanLobby) hosts LanOpenPlay.swf (the CustomGamesList / NameEntry gadgets) and MpGameSetup.swf (the setup screen).
// DONOR FACTS (ZH GameNetwork/LANAPI.cpp, LANAPIhandlers.cpp, LANAPICallbacks.cpp): the state machine: every s_resendDelta = 10 s the lobby players
//   announce themselves (LOBBY_ANNOUNCE) and the host its game (GAME_ANNOUNCE with the options); a lobby player or a game not heard for two of those is
//   forgotten; a joiner not heard from the host for 16 of them leaves ("LAN:HostNotResponding"), the host drops a player not heard for 8
//   ("LAN:PlayerDropped"); a join (REQUEST_JOIN -> JOIN_ACCEPT with the slot / JOIN_DENY with the reason) or a leave waits m_actionTimeout = 5 s; the host
//   owns the options: a change unaccepts every player, a player's own changes are requests the host checks (a colour or start position another slot
//   holds is refused); the host starts when every human slot accepted.
// OpenBFME DIFFERENCES (stop S-724, narrowed by MP-2): the encoding is OpenBFME's (magic 0x4E4C, the fields below), not retail's 472-byte LANMessage;
//   a message also goes to 127.0.0.1 on the eight ports (several games on one machine find each other without broadcast loopback); the profile identity
//   (GameNetwork/ProfileIdentity.h) is compared at the join instead of the exe / INI CRCs; GAME_START carries the whole start (the NewGameMessage, every
//   slot's game socket address, run-ahead, CRC interval) and is repeated until every player answered it.
// lane HERO-2: a human slot carries its player's whole Create-a-Hero record (the GameInfo's slot hero, requestSlotCreateAHero; checked with
//   Options::validateCreateAHero by the host and by every player that receives the options) into the start (stop S-1226).
//
// Not simulation code: what it produces is the NewGameMessage every peer starts from.

#pragma once

#include "GameNetwork/LANLobby.h"
#include "GameNetwork/PlatformKind.h"
#include "GameNetwork/ProfileIdentity.h"
#include "GameNetwork/Transport.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace LANMessageType
{
enum : std::uint8_t
{
	REQUEST_LOCATIONS = 0,
	GAME_ANNOUNCE = 1,
	LOBBY_ANNOUNCE = 2,
	REQUEST_JOIN = 3,
	JOIN_ACCEPT = 4,
	JOIN_DENY = 5,
	REQUEST_GAME_LEAVE = 6,
	REQUEST_LOBBY_LEAVE = 7,
	REQUEST_HOST_LEAVE = 8,
	SET_ACCEPT = 9,
	MAP_AVAILABILITY = 10,
	CHAT = 11,
	ENABLE_MPSETUP_UI = 12,
	GAME_START = 13,
	GAME_START_TIMER = 14,
	GAME_OPTIONS = 15,
	INACTIVE = 16,
	REQUEST_GAME_INFO = 17,
	GAME_OPTIONS_PACKED = 18,
	GAME_START_ACK = 0x80, ///< OpenBFME: a player received the start
	// lane PLATFORMICON-1 (OpenBFME): a player of the game tells the others its system (one byte, PlatformKind 0 .. 3). A NEW message type, not a field of
	// an existing one: a build without it answers with its "unhandled LAN message type" note and nothing else changes; the display data never enters the
	// game options, the start or the profile identity
	PLATFORM_INFO = 0x81
};
}

// One game of the list / the game the local player is in (ZH LANGameInfo)
struct LANGame
{
	std::u16string name;                          ///< the game's name (the host's name)
	NetAddress host;                              ///< the host's lobby address
	std::uint32_t hostNonce = 0;                  ///< process identity shared by announcements on different interfaces
	bool inProgress = false;
	SkirmishGameInfo info;                        ///< the slots (names, states, factions, colours, teams, start positions), the map, the cash, the seed
	std::array<NetAddress, MAX_SLOTS> lobby{};    ///< every human slot's lobby address
	std::array<NetAddress, MAX_SLOTS> game{};     ///< every human slot's game socket (the NetGameSession's)
	std::array<bool, MAX_SLOTS> accepted{};       ///< SET_ACCEPT (the ready flag)
	std::array<std::uint64_t, MAX_SLOTS> lastHeard{};
	std::string profileDigest;                    ///< the host's profile identity digest (the list shows another profile's game)
	std::uint64_t lastHeardHost = 0;
	int humanSlots() const;
};

class LANAPI
{
public:
	// the lobby messages' version (a message of another version is refused by name): 2 (lane HERO-2): the GameInfo slots' Create-a-Hero records,
	// OPT_SLOT_CREATE_A_HERO; 3 (lane OWNERFIX-2): the GameInfo slots' handicaps and the slot request's handicap; 4 (lane CHEAT-1): the GameInfo's Cheat Mode;
	// 5 (WOTR2 milestone 8): the GameInfo's game mode and scenario (a War of the Ring's LanStrategic game; RotWK sends it as type 14, MSG_GAME_START + 1);
	// 6 (step 4): a resumed war's save identity and state hash (the list's resume icon, Continue); 7 (Sol wotr2m8s4 follow-up 1): a LAN war's save answer
	// and leave vote name their save request (MSG_LW_OPENBFME_SAVE / MSG_LW_REQUEST_ENDGAME): a build without it is kept out of the lobby
	static constexpr std::uint8_t kProtocolVersion = 7;
	enum ReturnType // ZH LANAPIInterface::ReturnType
	{
		RET_OK,
		RET_TIMEOUT,
		RET_GAME_FULL,
		RET_DUPLICATE_NAME,
		RET_CRC_MISMATCH, ///< here: another profile identity
		RET_SERIAL_DUPE,
		RET_GAME_STARTED,
		RET_GAME_EXISTS,
		RET_GAME_GONE,
		RET_BUSY,
		RET_UNKNOWN
	};
	struct Options
	{
		std::uint16_t lobbyPortBase = 8086; ///< RW 0x84C881 (plus _EA_RTS_HEADLESS)
		int lobbyPorts = 8;                 ///< 8086 .. 8093
		std::uint32_t resendMs = 10000;     ///< ZH s_resendDelta
		std::uint32_t actionTimeoutMs = 5000; ///< ZH m_actionTimeout
		std::uint32_t startResendMs = 300;  ///< OpenBFME: GAME_START until answered
		bool broadcast = true;              ///< send to 255.255.255.255 (and to 127.0.0.1)
		std::vector<std::uint32_t> extraTargets; ///< more addresses to send the lobby's messages to (tests: 127.0.0.1 only)
		int runAhead = 1;                   ///< the started game's (lane NETLAG-1: RotWK's frame + 1, was 2)
		int crcInterval = 100;
		// lane PLATFORMICON-1: the one value this player tells the others of its game (PLATFORM_INFO); None: nothing is sent
		PlatformKind platform = PlatformKind::None; // the game's glue sets localPlatform()
		// lane MP-2 (review): the ranges a slot's fields are checked against before a request or the host's options are applied (-1 is random / none;
		// teams 0 .. MAX_SLOTS / 2 - 1 as the lobby's Team entries, start positions 0 .. MAX_SLOTS - 1). Required: open() fails without them
		int playerTemplateCount = 0; ///< the PlayerTemplate store's count
		int colorCount = 0;          ///< MultiplayerSettings' colours
		// lane HERO-2: a slot's Create-a-Hero record is checked with this before it is applied (a request, the host's options, a player's copy of them):
		// CreateAHeroSystem::validateHero of the world. A lobby without one refuses every record
		std::function<bool(const CreateAHeroHero &, std::string *)> validateCreateAHero;
		// WOTR2 milestone 8: a War of the Ring's start positions are region indices (RW 0x9759BB): the scenario's region count (-1: no such scenario).
		// A lobby without it refuses every War of the Ring game
		std::function<int(const std::string &scenario)> warRegionCount;
		// WOTR2 milestone 8 (step 4): a resumed LAN war (GameInfo::resumeSave): "" when this player has the save of that identity and it loads with that
		// state hash (RotWK's joiners look their copy up by the MD5, RW 0x81869B), else why not (without the player's name: the host adds it). A lobby
		// without it answers every resumed war "this game cannot look for saved wars"
		std::function<std::string(const std::string &save, std::uint32_t stateHash)> resumeCheck;
	};
	// what happened, for the screens (ZH LANAPI's On* callbacks)
	struct Event
	{
		enum Kind
		{
			GameList,     ///< the game list changed
			PlayerList,   ///< the lobby's players changed
			GameJoin,     ///< our join was answered (ret)
			GameCreate,
			PlayerJoin,   ///< someone joined our game (slot, name)
			PlayerLeave,  ///< someone left our game (name)
			HostLeave,    ///< the host left: we are back in the lobby
			Kicked,       ///< the host removed us
			Accept,       ///< a slot's accept flag changed (slot, flag)
			Chat,         ///< (name, text, system)
			GameOptions,  ///< the options changed
			GameStart,    ///< the game starts: start() holds it
			NameChange
		} kind;
		ReturnType ret = RET_OK;
		int slot = -1;
		bool flag = false;
		std::u16string name, text;
	};

	LANAPI(const ProfileIdentity &profile, const std::u16string &name, const Options &options);
	~LANAPI();

	// RW 0x84C844: binds the lobby socket (the first free port of 8086 + _EA_RTS_HEADLESS .. 8093) and a game socket (any free port)
	bool open(std::string *error);
	std::uint16_t lobbyPort() const { return m_lobbyPort; }
	UDP &gameSocket() { return *m_gameSocket; }
	// the started game takes the game socket (NetGameSession runs on it); the lobby keeps answering late GAME_STARTs on its own socket
	std::unique_ptr<UDP> takeGameSocket() { return std::move(m_gameSocket); }
	// moves the lobby on (call often): reads, answers, re-announces, times out (ZH LANAPI::update)
	void update(std::uint64_t now);

	// ---- requests (ZH Request*) ----
	void requestLocations();
	void requestSetName(const std::u16string &name);
	// ZH RequestGameCreate: the local player hosts `game` (its slot 0 becomes the host; open slots wait for players)
	void requestGameCreate(const SkirmishGameInfo &game);
	// ZH RequestGameJoin: the game at `index` of games()
	void requestGameJoin(int index);
	// ZH RequestGameJoinDirectConnect: a game at an address (its lobby port)
	void requestGameJoinDirect(const NetAddress &host);
	void requestGameLeave();
	void requestLobbyLeave();
	void requestAccept(bool accepted);
	// `system` (lane UI-1): RotWK's RequestChat with the SYSTEM type (TheLAN slot 0x54 with 3, RW 0x847508): the countdown's lines
	void requestChat(const std::u16string &text, bool system = false);
	// a player's own slot (ZH LanGameOptionsMenu: the host checks and re-broadcasts); kKeep leaves a field as it is (-1 is random / none)
	static constexpr int kKeep = -1000;
	// lane OWNERFIX-2: `handicap` is the slot's GameSlot + 0x20 (0, -5 .. -95), refused by the host outside that set
	void requestSlotOptions(int playerTemplate, int color, int team, int startPos, int handicap = kKeep);
	// lane HERO-2: the local player's Create-a-Hero for its own slot (nullptr: none), the whole record: the host checks it (Options::validateCreateAHero) and
	// re-broadcasts it in the options (every player's accept is cleared when it changed); false + *error when it is refused here already
	bool requestSlotCreateAHero(const CreateAHeroHero *hero, std::string *error);
	// host only: the whole setup (slot states, AI factions, the map, the cash), re-broadcast; every player's accept is cleared (ZH); the human slots keep
	// their names and Create-a-Heroes
	bool hostSetOptions(const SkirmishGameInfo &info);
	// host only: the slot's player is removed (its slot opens) and told so
	bool hostKick(int slot);
	// host only: every human slot accepted: GAME_START to everyone (repeated until answered); false + reason otherwise
	bool hostStartGame(std::string *reason);
	// WOTR2 milestone 8 (step 4): host only: the saved war's human seats other than the host's (slot, the saved player's name), reopened for the joiners
	// (RotWK RW 0x840F3F); the start is refused while one is open or a joiner's copy of the save is missing or differs (every PC sees the line)
	// (Sol wotr2m8s4 #4) each saved human's seat: its slot, its name and its saved slot (the START gives the seat its saved faction, colour, team, start and
	// handicap back; the seat's player must be the saved one)
	struct ResumeSeat
	{
		int slot = -1;
		std::u16string name;
		SkirmishGameSlot saved;
	};
	// `rules` the saved war's rules: the start is refused while the setup's differ (the joiners checked their copies under them)
	void hostSetResumeSeats(const std::vector<ResumeSeat> &seats, const GameRules &rules);
	// the check this player sent for the resumed war ("" ok, else the reason; for the screens and the tests)
	const std::string &resumeCheckResult() const { return m_checkedWhy; }

	// ---- state ----
	const std::vector<LANGame> &games() const { return m_games; }
	const LANGame *currentGame() const { return m_inGame ? &m_current : nullptr; }
	// lane UBER-1: the player switched the simulation profile (the OpenBFME option): the lobby announces the new identity from now on; false (nothing
	// changes) while in a game
	bool setProfile(const ProfileIdentity &profile)
	{
		if (m_inGame)
		{
			return false;
		}
		m_profile = profile;
		return true;
	}
	const ProfileIdentity &profile() const { return m_profile; }
	bool amIHost() const { return m_inGame && m_host; }
	int localSlot() const { return m_localSlot; }
	const std::u16string &name() const { return m_name; }
	bool inLobby() const { return !m_inGame; }
	// the start every player got (after a GameStart event)
	bool started() const { return m_started; }
	const LobbyStart &start() const { return m_start; }
	std::vector<Event> takeEvents();
	const std::vector<std::string> &errors() const { return m_errors; }
	const std::vector<std::string> &log() const { return m_log; }
	// the lobby players heard (ZH m_lobbyPlayers): name and address
	struct LobbyPlayer
	{
		std::u16string name;
		NetAddress address;
		std::uint64_t lastHeard = 0;
		std::uint32_t nonce = 0;
	};
	const std::vector<LobbyPlayer> &lobbyPlayers() const { return m_lobbyPlayers; }

private:
	enum PendingAction
	{
		ACT_NONE,
		ACT_JOIN,
		ACT_LEAVE
	};
	void sendToAll(const std::vector<std::uint8_t> &bytes);
	void sendTo(const NetAddress &to, const std::vector<std::uint8_t> &bytes);
	void sendToGame(const std::vector<std::uint8_t> &bytes); ///< every other human of the current game
	std::vector<std::uint8_t> header(std::uint8_t type) const;
	std::vector<std::uint8_t> gameAnnounce() const;
	void broadcastOptions();
	void handle(const NetAddress &from, const std::vector<std::uint8_t> &bytes, std::uint64_t now);
	bool handleGameAnnounce(NetByteReader &r, std::uint32_t nonce, const NetAddress &from, std::uint64_t now);
	bool handleRequestJoin(NetByteReader &r, const std::u16string &sender, const NetAddress &from, std::uint64_t now);
	bool handleGameOptions(NetByteReader &r, const NetAddress &from, std::uint64_t now);
	bool handleSlotRequest(NetByteReader &r, const NetAddress &from);
	bool handleSlotCreateAHero(NetByteReader &r, const NetAddress &from); ///< lane HERO-2
	void applyCreateAHero(int slot, const SkirmishGameSlot &from);
	bool createAHeroValid(const SkirmishGameSlot &s, std::string *why) const;
	bool handleGameStart(NetByteReader &r, const NetAddress &from);
	void leaveToLobby();
	// lane PLATFORMICON-1: tell the other humans of the game our system (to each new address at once, to all again every resend interval), and copy the
	// ones told into the current game's slots (display data: a slot whose address changed since it told shows none)
	void sendPlatform(bool all);
	void syncPlatforms();
	void resetPlatforms();
	static void applySlotRequest(SkirmishGameInfo &info, int slot, int playerTemplate, int color, int team, int startPos, int handicap);
	// lane MP-2 (review): a field of a slot request is kKeep, -1 or within its range; a GameInfo's every slot field is -1 or within its range
	bool slotRequestValid(const SkirmishGameInfo &info, int playerTemplate, int color, int team, int startPos, int handicap, std::string *why) const;
	bool gameInfoValid(const SkirmishGameInfo &info, std::string *why) const;
	int slotOf(const NetAddress &lobbyAddress) const;
	void event(Event e) { m_events.push_back(std::move(e)); }
	void note(const std::string &s);

	ProfileIdentity m_profile;
	std::u16string m_name;
	Options m_options;
	std::unique_ptr<UDP> m_lobbySocket, m_gameSocket;
	std::uint16_t m_lobbyPort = 0, m_gamePort = 0;
	std::uint32_t m_nonce = 0;
	std::uint64_t m_now = 0, m_lastResend = 0, m_expiration = 0, m_lastStartSend = 0;
	PendingAction m_pending = ACT_NONE;
	NetAddress m_joinTarget;
	std::vector<LANGame> m_games;
	std::vector<LobbyPlayer> m_lobbyPlayers;
	bool m_inGame = false, m_host = false;
	LANGame m_current;
	int m_localSlot = -1;
	bool m_started = false;
	LobbyStart m_start;
	std::array<bool, MAX_SLOTS> m_startAcked{};
	// WOTR2 milestone 8 (step 4): the joiners' answers to the resumed war's save (host) and this player's own check (joiner)
	struct SaveCheck
	{
		bool heard = false;
		bool ok = false;
		std::string save;
		std::string why;
		std::u16string player;
	};
	std::array<SaveCheck, MAX_SLOTS> m_saveChecks{};
	std::vector<ResumeSeat> m_resumeSeats;
	GameRules m_resumeRules = MpGameRules::none();
	std::string m_checkedSave;
	std::uint32_t m_checkedHash = 0;
	bool m_checkedDone = false;
	std::string m_checkedWhy;
	void sendSaveAvailability();
	bool handleSaveAvailability(NetByteReader &r, const NetAddress &from);
	bool m_starting = false;
	std::vector<Event> m_events;
	std::vector<std::string> m_errors, m_log;
	std::array<PlatformKind, MAX_SLOTS> m_peerPlatform{};       // lane PLATFORMICON-1: what each slot's player told
	std::array<NetAddress, MAX_SLOTS> m_peerPlatformAddr{};     // ... and from which lobby address
	std::array<NetAddress, MAX_SLOTS> m_platformSentTo{};       // the address each slot's player was last sent ours at
};
