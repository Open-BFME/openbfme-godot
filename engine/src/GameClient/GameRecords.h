// OpenBFME. GPL-3.0.
//
// GameRecords (lane ONLINEHUB-1): the games this installation played, kept by the game itself (one JSON line per game, user://game_records.jsonl), and
// the player's own statistics computed from them ("Your stats": the Profile / Stats pages, GameClient/GUI/AptScreens/AptOnlineHub.h). NOT RETAIL:
// RotWK kept a skirmish profile's statistics and GameSpy's persistent online statistics (the Stats.apt page shows them, AptStats' registration RW 0x9F021E,
// BFME2 0x5DD22B); neither store is ported: OpenBFME records each game when its score screen opens (skirmish against the computer, LAN and online
// alike, online or not), and the same record is the body of the platform's POST /v1/results (docs/PLATFORM_API.md) for LAN and online games.
//
// A record is read from outside (the file can be edited by hand; the game hands the record over as JSON): every field is checked against its bound and
// its closed set before anything is kept, a bad line is refused by name (it never counts), and the file is bounded (kMaxRecords, kMaxLineBytes).
// Display only: nothing here reaches the game's logic.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class JsonValue;

struct GameRecord
{
	struct Player
	{
		int slot = 0;            ///< the game slot (0 .. 7)
		std::string nick;        ///< UTF-8, at most 24 bytes
		std::string faction;     ///< the PlayerTemplate's name (as the game's setup names it; a mod's own names are kept as they are)
		int team = 0;            ///< 0 .. 7 (the platform's range): a player without a team has a team of its own (GameRecords::normaliseTeams)
		bool human = true;
	};
	std::string id;              ///< the lobby's game id, the LAN game's or a local game's id ([A-Za-z0-9_-], at most 64)
	std::string kind;            ///< "skirmish" / "lan" / "online"
	std::string mode;            ///< "faithful" / "uber" (the simulation profile)
	std::string map;             ///< the map's key (at most 200 bytes)
	std::int64_t start = 0;      ///< unix seconds
	std::int64_t end = 0;
	std::int64_t durationS = 0;  ///< the game's own time (logic frames / 5)
	std::vector<Player> players; ///< 1 .. 8, by slot, each slot once
	int meSlot = 0;              ///< the local player's slot (one of players)
	int winnerTeam = -1;         ///< the team that won; -1: none (a draw, an abandoned game, the player left before the end)
	std::string replayHash;      ///< optional (64 hex digits)
	bool cheatMode = false;      ///< lane CHEAT-1: a Cheat Mode game ("cheat_mode": true; absent false): kept in the file, never counted in the statistics nor reported

	const Player *me() const;
	bool won() const;            ///< the local player's team won
	bool vsComputerOnly() const; ///< the local player is the only human
};

// The player's own statistics over a set of records
struct PlayerStats
{
	struct Faction
	{
		std::string faction;
		int picks = 0, wins = 0;
		double winRate() const; ///< 0 .. 1 (display only; GameRecords.cpp)
	};
	struct Recent
	{
		std::string id, kind, map, faction;
		int result = 0; ///< 1 won, 0 lost, -1 no winner
		std::int64_t end = 0, durationS = 0;
	};
	int games = 0, wins = 0, losses = 0, undecided = 0;
	std::vector<Faction> factions;   ///< by picks (most first), then by name
	std::string favouriteFaction;    ///< most picks (ties: more wins, then the name); "" without games
	std::string favouriteMap;        ///< most games (ties: the name)
	int favouriteMapGames = 0;
	int longestWinStreak = 0;
	int currentStreak = 0;           ///< > 0 wins in a row up to the last game, < 0 losses, 0 none
	std::vector<Recent> last;        ///< the last 20 games, newest first
	double winRate() const; ///< 0 .. 1 (display only; GameRecords.cpp)
	const Faction *faction(const std::string &name) const;
};

namespace GameRecords
{
constexpr std::size_t kMaxLineBytes = 4096;
constexpr std::size_t kMaxRecords = 5000;  ///< the file keeps the newest 5000 games (about 1.5 MB)
constexpr std::size_t kMaxFileBytes = kMaxLineBytes * kMaxRecords;
constexpr std::size_t kRecentGames = 20;

// One record from its JSON (a line of the file, or what the game hands over); false + *error names the field refused (`out` untouched then)
bool parse(const std::string &json, GameRecord &out, std::string *error);
bool parse(const JsonValue &root, GameRecord &out, std::string *error);
// the record as one JSON line (no newline), the form parse() reads back
std::string toJson(const GameRecord &r);
// The file's text: the records in it, in order; a line that is not a record is skipped and named in *rejected ("line <n>: <why>"). A text larger than
// kMaxFileBytes is read up to that bound (the rest is named); more than kMaxRecords records keep the newest. Duplicate ids: the first is kept.
std::vector<GameRecord> load(const std::string &text, std::vector<std::string> *rejected);
// players without a team (team < 0 in the game's setup) each get a team of their own: the lowest team numbers 0 .. 7 no other player uses (at most 8
// players, so there are always enough), so "the winning team" names them alone
void normaliseTeams(std::vector<GameRecord::Player> &players);

// The player's statistics; `kind` "" counts every game, else only that kind's ("skirmish" / "lan" / "online")
PlayerStats summarise(const std::vector<GameRecord> &records, const std::string &kind = "");

// The body of POST /v1/results for this record (docs/PLATFORM_API.md); "" + *error when the record is not one to report: a skirmish game (single
// player), a game with fewer than two players (the service takes 2 .. 8), an install id that is not 32 hex digits, a nickname the service would refuse,
// or client metadata (platform windows / linux / macos, a 1 .. 64 character version) the service would refuse
std::string resultBody(const GameRecord &r, const std::string &install, const std::string &nick, const std::string &clientPlatform,
	const std::string &clientVersion, std::string *error);
} // namespace GameRecords
