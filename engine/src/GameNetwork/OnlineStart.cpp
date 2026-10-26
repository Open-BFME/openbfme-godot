// OpenBFME. GPL-3.0.
// See GameNetwork/OnlineStart.h.

#include "GameNetwork/OnlineStart.h"

#include "Common/MiniJson.h"
#include "GameNetwork/RelayAuth.h"
#include "GameNetwork/RelayProtocol.h"

#include <cmath>

namespace OnlineStart
{
namespace
{
bool fail(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = "online START: " + why;
	}
	return false;
}

// an integer field in [lo, hi] (the helpers write the bare reason; the caller prefixes it once)
bool intField(const JsonValue &o, const char *key, long long lo, long long hi, long long &out, std::string *error)
{
	const JsonValue *v = o.get(key);
	if (!v || !v->isNumber() || std::floor(v->number) != v->number || v->number < (double)lo || v->number > (double)hi)
	{
		*error = std::string("\"") + key + "\" must be an integer in " + std::to_string(lo) + " .. " + std::to_string(hi);
		return false;
	}
	out = (long long)v->number;
	return true;
}

bool stringField(const JsonValue &o, const char *key, std::string &out, std::string *error)
{
	const JsonValue *v = o.get(key);
	if (!v || !v->isString())
	{
		*error = std::string("\"") + key + "\" must be a string";
		return false;
	}
	out = v->string;
	return true;
}
} // namespace

bool utf8ToUtf16(const std::string &in, std::u16string &out)
{
	out.clear();
	for (size_t i = 0; i < in.size();)
	{
		const unsigned char c = (unsigned char)in[i];
		std::uint32_t cp = 0;
		size_t n = 0;
		if (c < 0x80)
		{
			cp = c;
			n = 1;
		}
		else if ((c & 0xE0) == 0xC0)
		{
			cp = c & 0x1F;
			n = 2;
		}
		else if ((c & 0xF0) == 0xE0)
		{
			cp = c & 0x0F;
			n = 3;
		}
		else if ((c & 0xF8) == 0xF0)
		{
			cp = c & 0x07;
			n = 4;
		}
		else
		{
			return false;
		}
		if (i + n > in.size())
		{
			return false;
		}
		for (size_t k = 1; k < n; ++k)
		{
			const unsigned char cc = (unsigned char)in[i + k];
			if ((cc & 0xC0) != 0x80)
			{
				return false;
			}
			cp = (cp << 6) | (cc & 0x3F);
		}
		static const std::uint32_t minimum[5] = { 0, 0, 0x80, 0x800, 0x10000 };
		if (cp < minimum[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			return false; // overlong, beyond Unicode, or a surrogate
		}
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			out.push_back((char16_t)(0xD800 + (cp >> 10)));
			out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
		}
		else
		{
			out.push_back((char16_t)cp);
		}
		i += n;
	}
	return true;
}

bool parse(const std::string &json, const ProfileIdentity &ownProfile, const Limits &limits, LobbyStart &out, std::string *error, Kind kindWanted)
{
	JsonValue root;
	std::string why;
	if (!JsonValue::parse(json, root, &why))
	{
		return fail(error, "not JSON: " + why);
	}
	if (!root.isObject())
	{
		return fail(error, "not a JSON object");
	}
	long long v = 0;
	std::string sub;
	if (!intField(root, "v", 1, 1, v, &sub))
	{
		return fail(error, "version: this client reads START version 1");
	}
	std::string kind, gameHex, relay, ticket, profile;
	const bool replay = kindWanted == Kind::ReplayStart;
	const bool spectate = kindWanted == Kind::Spectate; // lane ONLINE-3
	const char *wantKind = replay ? "openbfme-replay-start" : spectate ? "openbfme-spectate" : "openbfme-start";
	if (!stringField(root, "kind", kind, &sub) || kind != wantKind)
	{
		return fail(error, std::string("\"kind\" must be \"") + wantKind + "\"");
	}
	if (replay && (root.get("ticket") || root.get("slot")))
	{
		return fail(error, "a replay START carries no ticket and no slot"); // ONLINE-2 r2: the published record is never a credential
	}
	if (spectate && (root.get("slot") || root.get("rejoin")))
	{
		return fail(error, "a spectator START names no slot and no rejoin"); // lane ONLINE-3: a spectator is no player
	}
	if (!stringField(root, "game", gameHex, &sub) || (!replay && (!stringField(root, "relay", relay, &sub) || !stringField(root, "ticket", ticket, &sub)))
		|| !stringField(root, "profile", profile, &sub))
	{
		return fail(error, sub);
	}
	LobbyStart s;
	s.online = true;
	if (gameHex.size() != 16 || gameHex.find_first_not_of("0123456789abcdef") != std::string::npos)
	{
		return fail(error, "\"game\" must be 16 lower-case hex digits");
	}
	s.onlineGameId = std::stoull(gameHex, nullptr, 16);
	if (!replay && (!NetAddress::parse(relay, s.relay) || s.relay.ip == 0 || s.relay.port == 0))
	{
		return fail(error, "\"relay\" must be a.b.c.d:port, got " + relay);
	}
	if (profile != ownProfile.digest)
	{
		// lane JOINVER-1: said on screen (a watch or a rejoin, AptOnlineShell startFailed): in plain words, without the profile ids
		return fail(error, "this game runs a different OpenBFME version or different game files than yours: both need the same version "
			"(update through the launcher)");
	}
	s.profile = ownProfile;
	RelayProtocol::Ticket fields;
	if (!replay && (!RelayProtocol::base64UrlDecode(ticket, s.ticket)
		|| !RelayProtocol::readTicketFields(s.ticket.data(), s.ticket.size(), fields, &sub)))
	{
		return fail(error, "\"ticket\": invalid base64url or ticket fields: " + sub);
	}
	if (!replay && fields.spectator != spectate)
	{
		return fail(error, spectate ? "\"ticket\" must be a spectator ticket (version 3 or 6)" : "\"ticket\" must be a player ticket (version 1, 2, 4 or 5)");
	}
	// lane AUTH-1 / ANTICHEAT-1 (ONLINE16-FIX, one scheme): the START's "session_key", 16 bytes of base64url (RelayAuth::sessionKey): the relay
	// recomputes it from the ticket inside the HELLO; an old lobby's START carries none and the game stays
	// unauthenticated (never a silent weak key). A replay START never carries one
	if (!replay)
	{
		std::string text;
		if (root.get("session_key") && (!stringField(root, "session_key", text, &sub) || !RelayProtocol::base64UrlDecode(text, s.macKey)
			|| s.macKey.size() != RelayAuth::kSessionKeyBytes))
		{
			return fail(error, "\"session_key\" must be " + std::to_string(RelayAuth::kSessionKeyBytes) + " bytes of base64url");
		}
	}
	// lane ONLINE-3: a rejoin START carries "rejoin": the generation its version 2 ticket names (both or neither)
	if (!replay && !spectate)
	{
		const int ticketGeneration = fields.generation;
		long long rejoin = 0;
		if (root.get("rejoin") && !intField(root, "rejoin", 1, 0xFFFF, rejoin, &sub))
		{
			return fail(error, sub);
		}
		if (rejoin != ticketGeneration)
		{
			return fail(error, "\"rejoin\" (" + std::to_string(rejoin) + ") must name the ticket's generation (" + std::to_string(ticketGeneration) + ")");
		}
		s.rejoinGeneration = ticketGeneration;
	}
	else if (replay && root.get("rejoin"))
	{
		return fail(error, "a replay START carries no \"rejoin\"");
	}
	long long slot = 0, runAhead = 0, crc = 0, spectator = 0, delay = 0;
	if (spectate)
	{
		// lane ONLINE-3: the spectator's index and delay, as its ticket names them
		if (!intField(root, "spectator", 0, RelayProtocol::kMaxSpectators - 1, spectator, &sub) || !intField(root, "delay", 0, 600, delay, &sub))
		{
			return fail(error, sub);
		}
		if (fields.slot != (std::uint8_t)spectator || fields.delaySeconds != delay)
		{
			return fail(error, "the spectator ticket's index or delay is not the START's");
		}
		s.spectatorIndex = (int)spectator;
		s.spectatorDelay = (int)delay;
	}
	if ((!replay && !spectate && !intField(root, "slot", 0, MAX_SLOTS - 1, slot, &sub)) || !intField(root, "runAhead", 1, 25, runAhead, &sub)
		|| !intField(root, "crcInterval", 0, 1000, crc, &sub))
	{
		return fail(error, sub);
	}
	s.localSlot = spectate ? -1 : (int)slot; // lane ONLINE-3: a spectator has no slot (the observer's ReplayObserver side is its local player)
	s.runAhead = (int)runAhead;
	s.crcInterval = (int)crc;
	if (!replay && !spectate && fields.slot != (std::uint8_t)slot)
	{
		return fail(error, "the ticket is for slot " + std::to_string(fields.slot) + ", the START names slot " + std::to_string(slot));
	}
	const JsonValue *setup = root.get("setup");
	if (!setup || !setup->isObject())
	{
		return fail(error, "\"setup\" must be an object");
	}
	SkirmishGameInfo &g = s.game.game;
	long long capacity = LEGACY_MAX_SLOTS;
	if (setup->get("mapStartCount") && !intField(*setup, "mapStartCount", LEGACY_MAX_SLOTS, MAX_SLOTS, capacity, &sub))
	{
		return fail(error, sub);
	}
	g.mapStartCount = (int)capacity;
	g.simulationProfile = ownProfile.simulationProfile(); // lane UBERJOIN-1: the game runs the profile its identity names (as the LAN start: LANLobby.cpp), never the default
	s.game.mode = NewGameMode::Skirmish;
	long long seed = 0, cash = 0, sw = 0, lh = 0;
	sub = "\"map\" is empty";
	if (!stringField(*setup, "map", g.mapName, &sub) || g.mapName.empty() || !intField(*setup, "seed", 0, 0xFFFFFFFFll, seed, &sub)
		|| !intField(*setup, "startingCash", -1, 1000000000, cash, &sub) || !intField(*setup, "superweaponRestriction", 0, 1, sw, &sub)
		|| !intField(*setup, "legendaryHeroes", LegendaryHeroes::kOff, 4, lh, &sub))
	{
		return fail(error, sub);
	}
	g.seed = (std::uint32_t)seed;
	g.startingCash = (int)cash;
	g.superweaponRestriction = (int)sw;
	g.legendaryHeroes = (int)lh;
	if (!LegendaryHeroes::valid(g.legendaryHeroes))
	{
		return fail(error, "\"legendaryHeroes\" " + std::to_string(lh) + " is not a known factor");
	}
	const JsonValue *rules = setup->get("rules");
	if (!rules)
	{
		return fail(error, "\"rules\" must be null or an array of " + std::to_string(g.rules.size()) + " integers");
	}
	if (rules->type != JsonValue::NUL)
	{
		if (!rules->isArray() || rules->array.size() != g.rules.size())
		{
			return fail(error, "\"rules\" must be null or an array of " + std::to_string(g.rules.size()) + " integers");
		}
		for (size_t i = 0; i < g.rules.size(); ++i)
		{
			const JsonValue &r = rules->array[i];
			if (!r.isNumber() || std::floor(r.number) != r.number || r.number < -2147483648.0 || r.number > 2147483647.0)
			{
				return fail(error, "rule " + std::to_string(i) + " is not an integer");
			}
			g.rules[i] = (std::int32_t)r.number;
		}
	}
	const JsonValue *slots = setup->get("slots");
	if (!slots || !slots->isArray() || slots->array.empty() || slots->array.size() > (size_t)g.serializedSlotCount()
		|| (g.mapStartCount > LEGACY_MAX_SLOTS && slots->array.size() != (size_t)MAX_SLOTS))
	{
		return fail(error, "\"slots\" must be 1 .. 8 legacy slots, or 16 slots with an explicit mapStartCount of 9 .. 16");
	}
	static const struct
	{
		const char *name;
		SlotState state;
	} kStates[] = { { "open", SLOT_OPEN }, { "closed", SLOT_CLOSED }, { "player", SLOT_PLAYER }, { "easy", SLOT_EASY_AI }, { "medium", SLOT_MED_AI },
		{ "hard", SLOT_HARD_AI }, { "brutal", SLOT_BRUTAL_AI } };
	for (size_t i = 0; i < slots->array.size(); ++i)
	{
		const JsonValue &js = slots->array[i];
		const std::string at = "slot " + std::to_string(i) + ": ";
		if (!js.isObject())
		{
			return fail(error, at + "not an object");
		}
		SkirmishGameSlot &gs = g.slots[i];
		std::string state, name;
		if (!stringField(js, "state", state, &sub))
		{
			return fail(error, at + sub);
		}
		bool known = false;
		for (const auto &k : kStates)
		{
			if (state == k.name)
			{
				gs.state = k.state;
				known = true;
			}
		}
		if (!known)
		{
			return fail(error, at + "unknown state \"" + state + "\"");
		}
		if (i >= (size_t)g.mapStartCount && gs.state != SLOT_CLOSED)
		{
			return fail(error, at + "exceeds the map's start positions");
		}
		if (!gs.isOccupied())
		{
			continue;
		}
		long long faction = 0, color = 0, startPos = 0, team = 0;
		if (!stringField(js, "name", name, &sub) || !intField(js, "faction", -1, limits.playerTemplateCount - 1, faction, &sub)
			|| !intField(js, "color", -1, limits.colorCount - 1, color, &sub) || !intField(js, "startPos", -1, g.mapStartCount - 1, startPos, &sub)
			|| !intField(js, "team", -1, g.serializedSlotCount() - 1, team, &sub))
		{
			return fail(error, at + sub);
		}
		if (name.empty() || name.size() > 64 || !utf8ToUtf16(name, gs.name))
		{
			return fail(error, at + "the name must be 1 .. 64 bytes of UTF-8");
		}
		gs.playerTemplate = (int)faction;
		gs.color = (int)color;
		gs.startPos = (int)startPos;
		gs.teamNumber = (int)team;
		gs.accepted = true;
		// lane ONLINEMENU-1: the slot's handicap (GameSlot + 0x20; lobby.mjs signAndStart). Absent: a START or replay record written before the lobby
		// carried one, whose games were all played at 0; present, it must be one of the Handicap combo's values (RW 0x840B9D) or the START is refused
		gs.handicap = 0;
		if (js.get("handicap"))
		{
			long long handicap = 0;
			sub.clear();
			if (!intField(js, "handicap", -95, 0, handicap, &sub) || !SkirmishGameSlot::handicapValid((int)handicap))
			{
				return fail(error, at + (sub.empty() ? std::string("\"handicap\" must be 0 or -5 .. -95 in steps of 5") : sub));
			}
			gs.handicap = (int)handicap;
		}
		if (const JsonValue *cah = js.get("cah"))
		{
			std::vector<std::uint8_t> bytes;
			if (!cah->isString() || !RelayProtocol::base64Decode(cah->string, bytes) || !gs.setCreateAHeroBytes(bytes, &sub))
			{
				return fail(error, at + "\"cah\": " + (sub.empty() ? std::string("not base64") : sub));
			}
		}
		if (gs.isHuman())
		{
			s.networkSlot[i] = true;
		}
	}
	g.clearCreateAHeroesIfNotAllowed();
	// lane CHEAT-1: the optional "cheatMode" (lobby.mjs signAndStart: the host's slot when the host turned Cheat Mode on; absent: off). The seat must be a human
	// slot of the game: anything else is a refused START, never a game without the seat (the replay's record reads it again: NetPacket::readGameInfo)
	g.cheatMode = -1;
	if (const JsonValue *cheat = setup->get("cheatMode"); cheat && cheat->type != JsonValue::NUL)
	{
		long long seat = -1;
		if (!intField(*setup, "cheatMode", 0, MAX_SLOTS - 1, seat, &sub) || !g.slots[seat].isHuman())
		{
			return fail(error, "\"cheatMode\" must name a human player's slot");
		}
		g.cheatMode = (int)seat;
	}
	// lane PLATFORMICON-1: the optional top-level "platforms" (lobby.mjs signAndStart: one entry per slot, null or windows / linux / macos). Display data for
	// the running game's player list: only a human slot's entry counts, and anything unusable (not an array, a wrong length, another value) is no icon,
	// never a refusal of the START
	if (const JsonValue *platforms = root.get("platforms"); platforms && platforms->isArray() && platforms->array.size() == (size_t)g.serializedSlotCount())
	{
		for (int i = 0; i < g.serializedSlotCount(); ++i)
		{
			PlatformKind kind = PlatformKind::None;
			const JsonValue &p = platforms->array[(size_t)i];
			if (g.slots[i].isHuman() && p.isString() && platformFromName(p.string, kind))
			{
				g.slots[i].platform = kind;
			}
		}
	}
	// lane CHAT-1: the optional top-level "chatMuted" (lobby.mjs signAndStart: true for a player slot whose account the moderators muted): enforcement
	// of the lobby's mute in the game's chat (Network::setChatLobbyMuted). A human slot's true counts; anything else mutes nobody (an older lobby)
	if (const JsonValue *muted = root.get("chatMuted"); muted && muted->isArray())
	{
		for (int i = 0; i < MAX_SLOTS && (size_t)i < muted->array.size(); ++i)
		{
			const JsonValue &m = muted->array[(size_t)i];
			if (g.slots[i].isHuman() && m.type == JsonValue::BOOL && m.boolean)
			{
				s.chatMutedMask |= 1u << i;
			}
		}
	}
	if (replay)
	{
		// the log converter's start: the first human slot records (GameNetwork/RelayLog.h)
		s.localSlot = -1;
		for (int i = 0; i < MAX_SLOTS && s.localSlot < 0; ++i)
		{
			s.localSlot = s.networkSlot[(size_t)i] ? i : -1;
		}
		if (s.localSlot < 0)
		{
			return fail(error, "a replay START with no human slot");
		}
		out = std::move(s);
		return true;
	}
	if (!spectate && !s.networkSlot[(size_t)s.localSlot])
	{
		return fail(error, "this client's slot " + std::to_string(s.localSlot) + " is not a human player's");
	}
	std::uint16_t mask = 0;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		mask = (std::uint16_t)(mask | (s.networkSlot[(size_t)i] ? 1u << i : 0u));
	}
	if (fields.slotMask != mask)
	{
		return fail(error, "the ticket's slot mask does not match the START's human slots");
	}
	out = std::move(s);
	return true;
}
} // namespace OnlineStart
