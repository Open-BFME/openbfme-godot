// OpenBFME. GPL-3.0.
// See GameClient/GameRecords.h (lane ONLINEHUB-1).

#include "GameClient/GameRecords.h"

#include "Common/MiniJson.h"
#include "GameNetwork/PlatformApiClient.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace
{
bool fail(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}

bool idText(const std::string &s, std::size_t maxBytes)
{
	if (s.empty() || s.size() > maxBytes)
	{
		return false;
	}
	for (char c : s)
	{
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
		{
			return false;
		}
	}
	return true;
}

bool displayText(const std::string &s, std::size_t maxBytes)
{
	if (s.empty() || s.size() > maxBytes)
	{
		return false;
	}
	for (unsigned char c : s)
	{
		if (c < 0x20 || c == 0x7F)
		{
			return false;
		}
	}
	return true;
}

bool hex64(const std::string &s)
{
	if (s.size() != 64)
	{
		return false;
	}
	for (char c : s)
	{
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return false;
		}
	}
	return true;
}

bool getInt(const JsonValue &o, const char *key, std::int64_t lo, std::int64_t hi, std::int64_t &out, std::string *error, const std::string &where)
{
	const JsonValue *v = o.get(key);
	if (!v || !v->isNumber() || std::floor(v->number) != v->number || v->number < (double)lo || v->number > (double)hi)
	{
		return fail(error, where + key + " is missing or not a whole number in " + std::to_string(lo) + " .. " + std::to_string(hi));
	}
	out = (std::int64_t)v->number;
	return true;
}

bool getStr(const JsonValue &o, const char *key, std::string &out, std::string *error, const std::string &where)
{
	const JsonValue *v = o.get(key);
	if (!v || !v->isString())
	{
		return fail(error, where + key + " is missing or not a string");
	}
	out = v->string;
	return true;
}

constexpr std::int64_t kMaxTime = (std::int64_t)1 << 40; // unix seconds far beyond any real date, below a double's exact range
constexpr std::int64_t kMaxDuration = 7 * 24 * 3600;
} // namespace

const GameRecord::Player *GameRecord::me() const
{
	for (const Player &p : players)
	{
		if (p.slot == meSlot)
		{
			return &p;
		}
	}
	return nullptr;
}

bool GameRecord::won() const
{
	const Player *m = me();
	return m && winnerTeam >= 0 && m->team == winnerTeam;
}

bool GameRecord::vsComputerOnly() const
{
	int humans = 0;
	for (const Player &p : players)
	{
		humans += p.human ? 1 : 0;
	}
	return humans == 1;
}

// display only (the STATS pages): never simulation state
double PlayerStats::Faction::winRate() const
{
	return picks ? (double)wins / (double)picks : 0.0;
}

double PlayerStats::winRate() const
{
	return games ? (double)wins / (double)games : 0.0;
}

const PlayerStats::Faction *PlayerStats::faction(const std::string &name) const
{
	for (const Faction &f : factions)
	{
		if (f.faction == name)
		{
			return &f;
		}
	}
	return nullptr;
}

namespace GameRecords
{

void normaliseTeams(std::vector<GameRecord::Player> &players)
{
	std::set<int> used;
	for (const GameRecord::Player &p : players)
	{
		if (p.team >= 0)
		{
			used.insert(p.team);
		}
	}
	int next = 0;
	for (GameRecord::Player &p : players)
	{
		if (p.team < 0)
		{
			while (used.count(next))
			{
				++next;
			}
			p.team = next;
			used.insert(next);
		}
	}
}

bool parse(const std::string &json, GameRecord &out, std::string *error)
{
	if (json.size() > kMaxLineBytes)
	{
		return fail(error, "the record is longer than " + std::to_string(kMaxLineBytes) + " bytes");
	}
	JsonValue root;
	std::string why;
	if (!JsonValue::parse(json, root, &why))
	{
		return fail(error, "not JSON: " + why);
	}
	return parse(root, out, error);
}

bool parse(const JsonValue &root, GameRecord &out, std::string *error)
{
	if (!root.isObject())
	{
		return fail(error, "the record is not an object");
	}
	const JsonValue *v = root.get("v");
	if (!v || !v->isNumber() || v->number != 1)
	{
		return fail(error, "v is not 1");
	}
	GameRecord r;
	std::int64_t meSlot = 0, winner = 0;
	if (!getStr(root, "id", r.id, error, "") || !getStr(root, "kind", r.kind, error, "") || !getStr(root, "mode", r.mode, error, "")
		|| !getStr(root, "map", r.map, error, "") || !getInt(root, "start", 0, kMaxTime, r.start, error, "")
		|| !getInt(root, "end", 0, kMaxTime, r.end, error, "") || !getInt(root, "duration_s", 0, kMaxDuration, r.durationS, error, "")
		|| !getInt(root, "me", 0, 7, meSlot, error, "") || !getInt(root, "winner_team", -1, 7, winner, error, ""))
	{
		return false;
	}
	if (!idText(r.id, PlatformApiClient::kMaxIdBytes))
	{
		return fail(error, "id is not an id ([A-Za-z0-9_-], 1 .. 64)");
	}
	if (r.kind != "skirmish" && r.kind != "lan" && r.kind != "online")
	{
		return fail(error, "kind \"" + r.kind.substr(0, 16) + "\" is not skirmish / lan / online");
	}
	if (r.mode != "faithful" && r.mode != "uber")
	{
		return fail(error, "mode \"" + r.mode.substr(0, 16) + "\" is not faithful / uber");
	}
	if (!displayText(r.map, PlatformApiClient::kMaxMapBytes))
	{
		return fail(error, "map is empty, longer than 200 bytes or has control characters");
	}
	if (r.end < r.start)
	{
		return fail(error, "end is before start");
	}
	const JsonValue *hash = root.get("replay_hash");
	if (hash && hash->type != JsonValue::NUL)
	{
		if (!hash->isString() || !hex64(hash->string))
		{
			return fail(error, "replay_hash is not 64 hex digits");
		}
		r.replayHash = hash->string;
	}
	const JsonValue *list = root.get("players");
	if (!list || !list->isArray() || list->array.empty() || list->array.size() > 8)
	{
		return fail(error, "players is missing or does not have 1 .. 8 entries");
	}
	std::set<int> slots;
	for (std::size_t i = 0; i < list->array.size(); ++i)
	{
		const JsonValue &p = list->array[i];
		const std::string at = "players[" + std::to_string(i) + "].";
		if (!p.isObject())
		{
			return fail(error, at + " is not an object");
		}
		GameRecord::Player pl;
		std::int64_t slot = 0, team = 0;
		if (!getInt(p, "slot", 0, 7, slot, error, at) || !getStr(p, "nick", pl.nick, error, at) || !getStr(p, "faction", pl.faction, error, at)
			|| !getInt(p, "team", 0, 7, team, error, at))
		{
			return false;
		}
		const JsonValue *human = p.get("human");
		if (!human || human->type != JsonValue::BOOL)
		{
			return fail(error, at + "human is missing or not true / false");
		}
		if (!displayText(pl.nick, PlatformApiClient::kMaxNickBytes))
		{
			return fail(error, at + "nick is empty, longer than 24 bytes or has control characters");
		}
		if (!idText(pl.faction, PlatformApiClient::kMaxFactionBytes))
		{
			return fail(error, at + "faction is not a faction name");
		}
		if (!slots.insert((int)slot).second)
		{
			return fail(error, at + "slot " + std::to_string(slot) + " appears twice");
		}
		pl.slot = (int)slot;
		pl.team = (int)team;
		pl.human = human->boolean;
		r.players.push_back(pl);
	}
	r.meSlot = (int)meSlot;
	const GameRecord::Player *mine = r.me();
	if (!mine)
	{
		return fail(error, "me (slot " + std::to_string(meSlot) + ") is not one of the players");
	}
	if (!mine->human)
	{
		return fail(error, "me (slot " + std::to_string(meSlot) + ") is not a human player");
	}
	if (winner >= 0)
	{
		bool named = false;
		for (const GameRecord::Player &p : r.players)
		{
			named = named || p.team == winner;
		}
		if (!named)
		{
			return fail(error, "winner_team " + std::to_string(winner) + " is no player's team");
		}
	}
	r.winnerTeam = (int)winner;
	// lane CHEAT-1: "cheat_mode" (optional, absent: false; anything but true / false is refused)
	if (const JsonValue *cheat = root.get("cheat_mode"); cheat && cheat->type != JsonValue::NUL)
	{
		if (cheat->type != JsonValue::BOOL)
		{
			return fail(error, "cheat_mode is not true / false");
		}
		r.cheatMode = cheat->boolean;
	}
	std::sort(r.players.begin(), r.players.end(), [](const GameRecord::Player &a, const GameRecord::Player &b) { return a.slot < b.slot; });
	out = std::move(r);
	return true;
}

std::string toJson(const GameRecord &r)
{
	auto q = [](const std::string &s) { return PlatformApiClient::jsonQuote(s); };
	std::string out = "{\"v\":1,\"id\":" + q(r.id) + ",\"kind\":" + q(r.kind) + ",\"mode\":" + q(r.mode) + ",\"map\":" + q(r.map)
		+ ",\"start\":" + std::to_string(r.start) + ",\"end\":" + std::to_string(r.end) + ",\"duration_s\":" + std::to_string(r.durationS)
		+ ",\"players\":[";
	for (std::size_t i = 0; i < r.players.size(); ++i)
	{
		const GameRecord::Player &p = r.players[i];
		out += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(p.slot) + ",\"nick\":" + q(p.nick) + ",\"faction\":" + q(p.faction)
			+ ",\"team\":" + std::to_string(p.team) + ",\"human\":" + (p.human ? "true" : "false") + "}";
	}
	out += "],\"me\":" + std::to_string(r.meSlot) + ",\"winner_team\":" + std::to_string(r.winnerTeam);
	if (!r.replayHash.empty())
	{
		out += ",\"replay_hash\":" + q(r.replayHash);
	}
	if (r.cheatMode)
	{
		out += ",\"cheat_mode\":true"; // lane CHEAT-1 (a record without it reads as it did before)
	}
	return out + "}";
}

std::vector<GameRecord> load(const std::string &text, std::vector<std::string> *rejected)
{
	std::vector<GameRecord> out;
	std::set<std::string> ids;
	const std::size_t size = std::min(text.size(), kMaxFileBytes);
	if (text.size() > kMaxFileBytes && rejected)
	{
		rejected->push_back("the file is larger than " + std::to_string(kMaxFileBytes) + " bytes: the rest is not read");
	}
	std::size_t pos = 0;
	int line = 0;
	while (pos < size)
	{
		std::size_t eol = text.find('\n', pos);
		if (eol == std::string::npos || eol > size)
		{
			eol = size;
		}
		++line;
		std::string row = text.substr(pos, eol - pos);
		pos = eol + 1;
		if (!row.empty() && row.back() == '\r')
		{
			row.pop_back();
		}
		if (row.empty())
		{
			continue;
		}
		GameRecord r;
		std::string why;
		if (!parse(row, r, &why))
		{
			if (rejected)
			{
				rejected->push_back("line " + std::to_string(line) + ": " + why);
			}
			continue;
		}
		if (!ids.insert(r.id).second)
		{
			if (rejected)
			{
				rejected->push_back("line " + std::to_string(line) + ": game " + r.id + " is recorded already");
			}
			continue;
		}
		out.push_back(std::move(r));
	}
	if (out.size() > kMaxRecords)
	{
		out.erase(out.begin(), out.begin() + (std::ptrdiff_t)(out.size() - kMaxRecords));
	}
	return out;
}

PlayerStats summarise(const std::vector<GameRecord> &records, const std::string &kind)
{
	PlayerStats s;
	std::vector<const GameRecord *> games;
	for (const GameRecord &r : records)
	{
		if ((kind.empty() || r.kind == kind) && !r.cheatMode) // lane CHEAT-1: a Cheat Mode game never counts
		{
			games.push_back(&r);
		}
	}
	// in the order they ended (ties: the file's order): the streaks
	std::stable_sort(games.begin(), games.end(), [](const GameRecord *a, const GameRecord *b) { return a->end < b->end; });
	std::map<std::string, PlayerStats::Faction> factions;
	std::map<std::string, int> maps;
	int streak = 0;
	for (const GameRecord *r : games)
	{
		const GameRecord::Player *me = r->me();
		++s.games;
		PlayerStats::Faction &f = factions[me->faction];
		f.faction = me->faction;
		++f.picks;
		++maps[r->map];
		if (r->winnerTeam < 0)
		{
			++s.undecided;
			streak = 0;
		}
		else if (r->won())
		{
			++s.wins;
			++f.wins;
			streak = streak > 0 ? streak + 1 : 1;
		}
		else
		{
			++s.losses;
			streak = streak < 0 ? streak - 1 : -1;
		}
		s.longestWinStreak = std::max(s.longestWinStreak, streak);
	}
	s.currentStreak = streak;
	for (const auto &kv : factions)
	{
		s.factions.push_back(kv.second);
	}
	std::stable_sort(s.factions.begin(), s.factions.end(), [](const PlayerStats::Faction &a, const PlayerStats::Faction &b) {
		if (a.picks != b.picks)
		{
			return a.picks > b.picks;
		}
		if (a.wins != b.wins)
		{
			return a.wins > b.wins;
		}
		return a.faction < b.faction;
	});
	if (!s.factions.empty())
	{
		s.favouriteFaction = s.factions.front().faction;
	}
	for (const auto &kv : maps) // name order: the first with the most games wins a tie
	{
		if (kv.second > s.favouriteMapGames)
		{
			s.favouriteMap = kv.first;
			s.favouriteMapGames = kv.second;
		}
	}
	for (std::size_t i = games.size(); i > 0 && s.last.size() < kRecentGames; --i)
	{
		const GameRecord *r = games[i - 1];
		PlayerStats::Recent g;
		g.id = r->id;
		g.kind = r->kind;
		g.map = r->map;
		g.faction = r->me()->faction;
		g.result = r->winnerTeam < 0 ? -1 : (r->won() ? 1 : 0);
		g.end = r->end;
		g.durationS = r->durationS;
		s.last.push_back(g);
	}
	return s;
}

std::string resultBody(const GameRecord &r, const std::string &install, const std::string &nick, const std::string &clientPlatform,
	const std::string &clientVersion, std::string *error)
{
	if (r.kind != "lan" && r.kind != "online")
	{
		fail(error, "a " + r.kind + " game is not reported (single player)");
		return std::string();
	}
	if (r.cheatMode)
	{
		fail(error, "a Cheat Mode game is not reported (it never counts)"); // lane CHEAT-1
		return std::string();
	}
	if (r.players.size() < 2)
	{
		fail(error, "a game with fewer than two players is not reported (the service takes 2 .. 8)");
		return std::string();
	}
	// lane SOLO-1: an online game with a single human is never reported, with AI slots or none (a custom game may start solo; a
	// LAN game's one human still reports, its "vs_ai" bucket)
	int humans = 0;
	for (const GameRecord::Player &p : r.players)
	{
		humans += p.human ? 1 : 0;
	}
	if (r.kind == "online" && humans < 2)
	{
		fail(error, "an online game with a single human is not reported (a custom game may start solo)");
		return std::string();
	}
	if (!PlatformApiClient::validInstall(install))
	{
		fail(error, "the install id is not 32 hex digits");
		return std::string();
	}
	if (!displayText(nick, PlatformApiClient::kMaxNickBytes))
	{
		fail(error, "the nickname is empty, longer than 24 bytes or has control characters");
		return std::string();
	}
	std::string clientWhy;
	if (!PlatformApiClient::validClient(clientPlatform, clientVersion, &clientWhy))
	{
		fail(error, "the client metadata is not usable: " + clientWhy);
		return std::string();
	}
	auto q = [](const std::string &s) { return PlatformApiClient::jsonQuote(s); };
	std::string players;
	for (std::size_t i = 0; i < r.players.size(); ++i)
	{
		const GameRecord::Player &p = r.players[i];
		players += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(p.slot) + ",\"nick\":" + q(p.nick) + ",\"faction\":" + q(p.faction)
			+ ",\"team\":" + std::to_string(p.team) + ",\"human\":" + (p.human ? "true" : "false") + "}";
	}
	std::string game = "{\"id\":" + q(r.id) + ",\"mode\":" + q(r.mode) + ",\"map\":" + q(r.map) + ",\"players\":[" + players + "],\"start\":"
		+ std::to_string(r.start) + ",\"end\":" + std::to_string(r.end) + ",\"duration_s\":" + std::to_string(r.durationS) + ",\"winner_team\":"
		+ (r.winnerTeam < 0 ? std::string("null") : std::to_string(r.winnerTeam));
	if (!r.replayHash.empty())
	{
		game += ",\"replay_hash\":" + q(r.replayHash);
	}
	game += "}";
	return "{\"v\":1,\"install\":" + q(install) + ",\"nick\":" + q(nick) + ",\"client\":{\"platform\":" + q(clientPlatform) + ",\"version\":" + q(clientVersion)
		+ "},\"game\":" + game + ",\"me\":{\"slot\":" + std::to_string(r.meSlot) + "}}";
}

} // namespace GameRecords
