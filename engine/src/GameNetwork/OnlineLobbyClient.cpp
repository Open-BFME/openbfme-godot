// OpenBFME. GPL-3.0.
// See GameNetwork/OnlineLobbyClient.h (lane ONLINE-4).

#include "GameNetwork/OnlineLobbyClient.h"

#include "Common/MiniJson.h"
#include "Common/Sha256.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Transport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
std::u16string u16(const std::string &utf8)
{
	std::u16string out;
	if (!OnlineStart::utf8ToUtf16(utf8, out))
	{
		out.clear();
		for (char c : utf8)
		{
			out.push_back((unsigned char)c < 0x80 ? (char16_t)c : u'?');
		}
	}
	return out;
}

bool intOf(const JsonValue *v, long long lo, long long hi, long long &out)
{
	if (!v || !v->isNumber() || std::floor(v->number) != v->number || v->number < (double)lo || v->number > (double)hi)
	{
		return false;
	}
	out = (long long)v->number;
	return true;
}

std::string strOf(const JsonValue *v)
{
	return v && v->isString() ? v->string : std::string();
}

// Sol r1: a string field within its bound (absent or not a string: the field's own check says so)
bool fits(const JsonValue *v, std::size_t maxBytes)
{
	return !v || !v->isString() || v->string.size() <= maxBytes;
}

bool isBase64UrlChar(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// lane ONLINEHUB-1: lobby.mjs ROOM_CODE
bool validRoomCode(const std::string &code)
{
	if (code.size() < 6 || code.size() > 32)
	{
		return false;
	}
	for (char c : code)
	{
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
		{
			return false;
		}
	}
	return true;
}

bool isHexId(const std::string &id)
{
	if (id.size() != 16)
	{
		return false;
	}
	for (char c : id)
	{
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return false;
		}
	}
	return true;
}

std::string socialPercentEncode(const std::string &value)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : value)
	{
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
		{
			out.push_back((char)c);
		}
		else
		{
			out.push_back('%');
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 15]);
		}
	}
	return out;
}

// MiniJson writes nothing: the START is written back as JSON (integers stay integers; the lobby sends no fractions)
std::string toJson(const JsonValue &v)
{
	switch (v.type)
	{
	case JsonValue::NUL: return "null";
	case JsonValue::BOOL: return v.boolean ? "true" : "false";
	case JsonValue::NUMBER:
	{
		char buf[40];
		if (std::floor(v.number) == v.number && std::fabs(v.number) < 9007199254740992.0)
		{
			std::snprintf(buf, sizeof(buf), "%lld", (long long)v.number);
		}
		else
		{
			std::snprintf(buf, sizeof(buf), "%.17g", v.number);
		}
		return buf;
	}
	case JsonValue::STRING: return OnlineLobbyClient::jsonQuote(v.string);
	case JsonValue::ARRAY:
	{
		std::string out = "[";
		for (size_t i = 0; i < v.array.size(); ++i)
		{
			out += (i ? "," : "") + toJson(v.array[i]);
		}
		return out + "]";
	}
	case JsonValue::OBJECT:
	{
		std::string out = "{";
		bool first = true;
		for (const auto &kv : v.object)
		{
			out += (first ? "" : ",") + OnlineLobbyClient::jsonQuote(kv.first) + ":" + toJson(kv.second);
			first = false;
		}
		return out + "}";
	}
	}
	return "null";
}

const struct
{
	const char *name;
	SlotState state;
} kSlotStates[] = { { "open", SLOT_OPEN }, { "closed", SLOT_CLOSED }, { "player", SLOT_PLAYER }, { "easy", SLOT_EASY_AI }, { "medium", SLOT_MED_AI },
	{ "hard", SLOT_HARD_AI }, { "brutal", SLOT_BRUTAL_AI } };
} // namespace

OnlineLobbyClient::OnlineLobbyClient(const Options &options) : m_options(options)
{
}

std::string OnlineLobbyClient::redact(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	size_t i = 0;
	while (i < text.size())
	{
		if (!isBase64UrlChar(text[i]) && text[i] != '.')
		{
			out.push_back(text[i++]);
			continue;
		}
		// a run of base64url characters and dots: redacted whole when one of its dot-separated parts has 40 or more characters
		size_t end = i, part = 0, longest = 0;
		while (end < text.size() && (isBase64UrlChar(text[end]) || text[end] == '.'))
		{
			part = text[end] == '.' ? 0 : part + 1;
			longest = std::max(longest, part);
			++end;
		}
		size_t last = end; // a sentence's full stop after a token is not part of it
		while (last > i && text[last - 1] == '.')
		{
			--last;
		}
		out += longest >= 40 ? std::string("<redacted>") + text.substr(last, end - last) : text.substr(i, end - i);
		i = end;
	}
	return out;
}

std::string OnlineLobbyClient::utf16ToUtf8(const std::u16string &in)
{
	std::string out;
	for (size_t i = 0; i < in.size(); ++i)
	{
		std::uint32_t c = in[i];
		if (c >= 0xD800 && c <= 0xDBFF && i + 1 < in.size() && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF)
		{
			c = 0x10000 + ((c - 0xD800) << 10) + (in[i + 1] - 0xDC00);
			++i;
		}
		else if (c >= 0xD800 && c <= 0xDFFF)
		{
			c = 0xFFFD; // a lone surrogate
		}
		if (c < 0x80)
		{
			out.push_back((char)c);
		}
		else if (c < 0x800)
		{
			out.push_back((char)(0xC0 | (c >> 6)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
		else if (c < 0x10000)
		{
			out.push_back((char)(0xE0 | (c >> 12)));
			out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
		else
		{
			out.push_back((char)(0xF0 | (c >> 18)));
			out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
			out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
	}
	return out;
}

std::string OnlineLobbyClient::jsonQuote(const std::string &utf8)
{
	std::string out = "\"";
	for (unsigned char c : utf8)
	{
		if (c == '"' || c == '\\')
		{
			out.push_back('\\');
			out.push_back((char)c);
		}
		else if (c < 0x20)
		{
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\u%04x", c);
			out += buf;
		}
		else
		{
			out.push_back((char)c);
		}
	}
	out.push_back('"');
	return out;
}

const char *OnlineLobbyClient::slotStateName(SlotState s)
{
	for (const auto &k : kSlotStates)
	{
		if (k.state == s)
		{
			return k.name;
		}
	}
	return "closed";
}

void OnlineLobbyClient::queue(const std::string &kind, const std::string &method, const std::string &path, const std::string &body)
{
	Pending p;
	p.kind = kind;
	p.request.id = m_nextId++;
	p.request.method = method;
	p.request.path = path;
	p.request.body = body;
	m_queue.push_back(p);
}

void OnlineLobbyClient::refuse(const std::string &request, const std::string &code, const std::string &message)
{
	if (request == "leave" && !m_pendingSocialAcceptInvite.empty())
	{
		m_pendingSocialAcceptInvite.clear();
		socialRefuse("accept_invite", code);
	}
	Event e{ Event::Refused };
	e.request = request;
	e.code = code;
	// Sol r1: a refusal is shown and noted: a credential the lobby (or anything between) echoed in it is masked, this client's own token whatever its shape
	std::string safe = redact(message);
	if (m_token.size() >= 8)
	{
		for (size_t at = safe.find(m_token); at != std::string::npos; at = safe.find(m_token, at))
		{
			safe.replace(at, m_token.size(), "<redacted>");
		}
	}
	e.text = u16(safe);
	m_events.push_back(e);
}

void OnlineLobbyClient::signInGuest(const std::u16string &name)
{
	std::string body = "{\"name\":" + jsonQuote(utf16ToUtf8(name));
	if (m_options.installId.empty() && !m_options.deviceHash.empty())
	{
		refuse("signin", "bad_identity", "a device hash needs an install id");
		return;
	}
	if (!m_options.installId.empty())
	{
		const auto hex = [](const std::string &s, size_t n) {
			return s.size() == n && s.find_first_not_of("0123456789abcdef") == std::string::npos;
		};
		if (!hex(m_options.installId, 32) || (!m_options.deviceHash.empty() && !hex(m_options.deviceHash, 64)) ||
			m_options.clientPlatform.empty() || m_options.clientPlatform.size() > 32 ||
			m_options.clientVersion.empty() || m_options.clientVersion.size() > 64)
		{
			refuse("signin", "bad_identity", "the install identity or client version is invalid");
			return;
		}
		body += ",\"install\":" + jsonQuote(m_options.installId);
		if (!m_options.deviceHash.empty()) body += ",\"device\":" + jsonQuote(m_options.deviceHash);
		body += ",\"client\":{\"platform\":" + jsonQuote(m_options.clientPlatform) + ",\"version\":" + jsonQuote(m_options.clientVersion) + "}";
	}
	resetSocialSession();
	m_token.clear();
	m_accountName.clear();
	// ONLINEMENU-1 (BATCH-P3): the lobby's features first (the queue is serial: they are known when the sign-in's answer starts the social
	// session); a lobby that names no "social" (the deployed lobby before the social routes) gets no social request at all
	m_lobbyFeatures.clear();
	if (m_options.enableSocial)
	{
		queue("features", "GET", "/v1/health", "");
	}
	queue("signin", "POST", "/v1/auth/guest", body + "}");
}

void OnlineLobbyClient::refreshList(bool running)
{
	queue("list", "GET", running ? "/v1/games?running=1" : "/v1/games", "");
}

// a session token's payload (lobby.mjs makeToken: base64url(JSON).base64url(HMAC)); false when it is not one
static bool sessionPayload(const std::string &token, JsonValue &payload)
{
	const size_t dot = token.find('.');
	if (dot == std::string::npos || dot == 0)
	{
		return false;
	}
	// base64url without padding
	std::string bytes;
	unsigned acc = 0;
	int bits = 0;
	for (size_t i = 0; i < dot; ++i)
	{
		const char c = token[i];
		int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '-' ? 62 : c == '_' ? 63 : -1;
		if (v < 0)
		{
			return false;
		}
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			bytes.push_back((char)((acc >> bits) & 0xFF));
		}
	}
	std::string why;
	if (!JsonValue::parse(bytes, payload, &why) || !payload.isObject())
	{
		return false;
	}
	return true;
}

std::u16string OnlineLobbyClient::sessionName(const std::string &token)
{
	JsonValue payload;
	return sessionPayload(token, payload) ? u16(strOf(payload.get("name"))) : std::u16string();
}

bool OnlineLobbyClient::sessionMatchesInstall(const std::string &token, const std::string &installId)
{
	JsonValue payload;
	if (installId.empty() || !sessionPayload(token, payload))
	{
		return false;
	}
	static const char kTagKey[] = "openbfme-install-tag-v1";
	const auto mac = HmacSha256(reinterpret_cast<const std::uint8_t *>(kTagKey), sizeof(kTagKey) - 1,
		reinterpret_cast<const std::uint8_t *>(installId.data()), installId.size());
	const std::string want = HexBytes(mac.data(), 16);
	const std::string have = strOf(payload.get("ih"));
	return have.size() == want.size() && ConstantTimeEqual(reinterpret_cast<const std::uint8_t *>(have.data()),
		reinterpret_cast<const std::uint8_t *>(want.data()), want.size());
}

void OnlineLobbyClient::useSession(const std::string &token)
{
	resetSocialSession();
	m_token = token;
	m_accountName = sessionName(token);
	// ONLINEMENU-1 (BATCH-P3): a kept session starts the social session too (below, once /v1/mine accepts it): the features first, as for a
	// guest sign-in
	m_lobbyFeatures.clear();
	if (m_options.enableSocial)
	{
		queue("features", "GET", "/v1/health", "");
	}
	queue("session", "GET", "/v1/mine", "");
}

void OnlineLobbyClient::fetchMine()
{
	queue("mine", "GET", "/v1/mine", "");
}

void OnlineLobbyClient::rejoinGame(const std::string &id)
{
	if (!isHexId(id))
	{
		refuse("rejoin", "bad_field", "a game id is 16 hex digits");
		return;
	}
	queue("rejoin", "POST", "/v1/games/" + id + "/rejoin", "");
}

void OnlineLobbyClient::spectateGame(const std::string &id)
{
	if (!isHexId(id))
	{
		refuse("spectate", "bad_field", "a game id is 16 hex digits");
		return;
	}
	queue("spectate", "POST", "/v1/games/" + id + "/spectate", "");
}

void OnlineLobbyClient::fetchMotd()
{
	queue("motd", "GET", "/v1/motd", "");
}

void OnlineLobbyClient::fetchRelays()
{
	m_relaysAsked = true;
	queue("health", "GET", "/v1/health", "");
}

namespace
{
std::string slotChoices(int faction, int color, int startPos, int team, int handicap = OnlineLobbyClient::kKeep)
{
	std::string out;
	auto add = [&](const char *key, int v) {
		if (v != OnlineLobbyClient::kKeep)
		{
			out += std::string(",\"") + key + "\":" + std::to_string(v);
		}
	};
	add("faction", faction);
	add("color", color);
	add("startPos", startPos);
	add("team", team);
	add("handicap", handicap); // lane ONLINEMENU-1: lobby.mjs checkSlotChoice (0, -5 .. -95)
	return out;
}

std::string setupFields(const SkirmishGameInfo &s)
{
	std::string out = "\"map\":" + OnlineLobbyClient::jsonQuote(s.mapName) + ",\"startingCash\":" + std::to_string(s.startingCash) +
		",\"superweaponRestriction\":" + std::to_string(s.superweaponRestriction) + ",\"legendaryHeroes\":" + std::to_string(s.legendaryHeroes) + ",\"rules\":";
	if (MpGameRules::isNone(s.rules))
	{
		out += "null";
	}
	else
	{
		out += "[";
		for (size_t i = 0; i < s.rules.size(); ++i)
		{
			out += (i ? "," : "") + std::to_string(s.rules[i]);
		}
		out += "]";
	}
	return out;
}
} // namespace

void OnlineLobbyClient::createGame(const std::u16string &name, const std::string &password, const SkirmishGameInfo &setup, int hostSlot, const std::string &roomCode)
{
	if (!roomCode.empty() && (!validRoomCode(roomCode) || !password.empty()))
	{
		refuse("create", "bad_field", "a room code is 6 .. 32 characters of A-Z a-z 0-9 _ - and has no password beside it");
		return;
	}
	// lobby.mjs create: the slots up to the last one that is not closed; the host's "player", the other human places "open"
	int last = MAX_SLOTS;
	while (last > 2 && setup.slots[last - 1].state == SLOT_CLOSED)
	{
		--last;
	}
	std::string slots;
	for (int i = 0; i < last; ++i)
	{
		const SkirmishGameSlot &s = setup.slots[i];
		const SlotState state = i == hostSlot ? SLOT_PLAYER : (s.state == SLOT_PLAYER ? SLOT_OPEN : s.state);
		slots += std::string(i ? "," : "") + "{\"state\":\"" + slotStateName(state) + "\"";
		if (state != SLOT_OPEN && state != SLOT_CLOSED)
		{
			if (state != SLOT_PLAYER)
			{
				slots += ",\"name\":" + jsonQuote(utf16ToUtf8(s.name.empty() ? u"Computer" : s.name));
			}
			slots += slotChoices(s.playerTemplate, s.color, s.startPos, s.teamNumber, s.handicap);
		}
		slots += "}";
	}
	std::string body = "{\"name\":" + jsonQuote(utf16ToUtf8(name)) + ",\"profile\":" + jsonQuote(m_options.profileDigest) + ",\"mode\":\"" +
		modeName(m_options.profile) + "\"" + platformField() + ",\"hostSlot\":" +
		std::to_string(hostSlot) + ",\"setup\":{" + setupFields(setup) + ",\"slots\":[" + slots + "]}";
	if (!password.empty())
	{
		body += ",\"password\":" + jsonQuote(password);
	}
	if (!roomCode.empty())
	{
		body += ",\"room_code\":" + jsonQuote(roomCode);
	}
	body += "}";
	m_gamePasswordProtected = !password.empty();
	queue("create", "POST", "/v1/games", body);
}

void OnlineLobbyClient::joinByCode(const std::string &roomCode)
{
	if (!validRoomCode(roomCode))
	{
		refuse("join", "bad_field", "a room code is 6 .. 32 characters of A-Z a-z 0-9 _ -");
		return;
	}
	queue("join:code", "POST", "/v1/rooms/" + roomCode + "/join", "{\"profile\":" + jsonQuote(m_options.profileDigest) + ",\"mode\":\"" + modeName(m_options.profile) + "\"" + platformField() + "}");
}

bool OnlineLobbyClient::parseMode(const std::string &name, SimulationProfile *out)
{
	if (name == "normal")
	{
		*out = SimulationProfile::Faithful;
		return true;
	}
	if (name == "uber")
	{
		*out = SimulationProfile::Uber;
		return true;
	}
	return false;
}

bool OnlineLobbyClient::setProfile(SimulationProfile profile, const std::string &digest, std::string *why)
{
	if (inGame())
	{
		*why = "the performance mode cannot change inside a room (leave it first)";
		return false;
	}
	if (!simulationProfileValid((std::int32_t)profile) || digest.size() != 32)
	{
		*why = "the performance mode needs a known profile and its identity's digest";
		return false;
	}
	m_options.profile = profile;
	m_options.profileDigest = digest;
	return true;
}

std::string OnlineLobbyClient::platformField() const
{
	// lane PLATFORMICON-1: lobby.mjs checkPlatform: windows / linux / macos or absent
	return m_options.platform == PlatformKind::None ? std::string() : std::string(",\"platform\":\"") + platformName(m_options.platform) + "\"";
}

// SOCIAL-1: the identity fields of a join body (an invite's accept joins with them too): the same as createGame / joinGame send
std::string OnlineLobbyClient::joinIdentityFields() const
{
	return "\"profile\":" + jsonQuote(m_options.profileDigest) + ",\"mode\":\"" + modeName(m_options.profile) + "\"" + platformField();
}

void OnlineLobbyClient::joinGame(const std::string &id, const std::string &password)
{
	if (!isHexId(id))
	{
		refuse("join", "bad_field", "a game id is 16 hex digits");
		return;
	}
	std::string body = "{\"profile\":" + jsonQuote(m_options.profileDigest) + ",\"mode\":\"" + modeName(m_options.profile) + "\"" + platformField();
	if (!password.empty())
	{
		body += ",\"password\":" + jsonQuote(password);
	}
	body += "}";
	m_gamePasswordProtected = !password.empty();
	queue("join:" + id, "POST", "/v1/games/" + id + "/join", body);
}

void OnlineLobbyClient::leaveGame()
{
	if (!inGame())
	{
		return;
	}
	queue("leave", "POST", "/v1/games/" + m_gameId + "/leave", "");
	leftGame("left");
}

void OnlineLobbyClient::setOwnSlot(int faction, int color, int startPos, int team, int handicap)
{
	if (!inGame())
	{
		return;
	}
	const std::string fields = slotChoices(faction, color, startPos, team, handicap);
	queue("slot", "POST", "/v1/games/" + m_gameId + "/slot", "{" + (fields.empty() ? std::string() : fields.substr(1)) + "}");
}

bool OnlineLobbyClient::setOwnCreateAHero(const CreateAHeroHero *hero, std::string *error)
{
	if (!inGame())
	{
		if (error)
		{
			*error = "not in an online game";
		}
		return false;
	}
	std::string value = "null";
	if (hero)
	{
		const std::vector<std::uint8_t> bytes = hero->save();
		if (bytes.empty() || bytes.size() > kMaxCreateAHeroBytes)
		{
			if (error)
			{
				*error = "a Create-a-Hero record of " + std::to_string(bytes.size()) + " bytes (1 .. " + std::to_string(kMaxCreateAHeroBytes) + ")";
			}
			return false;
		}
		value = "\"" + RelayProtocol::base64Encode(bytes.data(), bytes.size()) + "\"";
	}
	queue("slot", "POST", "/v1/games/" + m_gameId + "/slot", "{\"cah\":" + value + "}");
	return true;
}

void OnlineLobbyClient::reportLink(int rttMs, int lossPct)
{
	if (!inGame() || lossPct < 0 || lossPct > 100 || (rttMs != -1 && (rttMs < 1 || rttMs > 10000)) || (rttMs == -1 && lossPct != 100))
	{
		return;
	}
	queue("link", "POST", "/v1/games/" + m_gameId + "/link",
		rttMs == -1 ? std::string("{\"rtt\":null}") : "{\"rtt\":" + std::to_string(rttMs) + ",\"loss\":" + std::to_string(lossPct) + "}");
}

void OnlineLobbyClient::setOtherSlot(int slot, SlotState state, int faction, int color, int startPos, int team, int handicap)
{
	if (!inGame() || slot < 0 || slot >= MAX_SLOTS)
	{
		return;
	}
	std::string body = "{\"slot\":" + std::to_string(slot) + ",\"state\":\"" + slotStateName(state) + "\"";
	if (state != SLOT_OPEN && state != SLOT_CLOSED)
	{
		const std::u16string &name = m_info.slots[slot].name;
		body += ",\"name\":" + jsonQuote(utf16ToUtf8(name.empty() ? u"Computer" : name)) + slotChoices(faction, color, startPos, team, handicap);
	}
	body += "}";
	queue("slot", "POST", "/v1/games/" + m_gameId + "/slot", body);
}

void OnlineLobbyClient::setSetup(const SkirmishGameInfo &setup)
{
	if (!inGame())
	{
		return;
	}
	queue("setup", "POST", "/v1/games/" + m_gameId + "/setup", "{" + setupFields(setup) + "}");
}

void OnlineLobbyClient::setReady(bool ready)
{
	if (!inGame())
	{
		return;
	}
	queue("ready", "POST", "/v1/games/" + m_gameId + "/ready", std::string("{\"ready\":") + (ready ? "true" : "false") + "}");
}

void OnlineLobbyClient::chat(const std::u16string &text)
{
	if (!inGame())
	{
		return;
	}
	queue("chat", "POST", "/v1/games/" + m_gameId + "/chat", "{\"text\":" + jsonQuote(utf16ToUtf8(text)) + "}");
	if (m_holdCap > 0)
	{
		// lane ONLINE-5: the sender reads its own line at once (it used to wait for the next poll: up to a second and a round trip). A lobby that
		// announces "wait" marks the sender's lines in its polls ("you"), so the copy that comes back is not shown again; an older lobby marks
		// nothing and the line appears with the poll, as before
		Event e{ Event::Chat };
		e.from = m_accountName;
		e.text = text;
		m_events.push_back(e);
		++m_ownEcho;
	}
}

void OnlineLobbyClient::kick(int slot)
{
	if (!inGame())
	{
		return;
	}
	queue("kick", "POST", "/v1/games/" + m_gameId + "/kick", "{\"slot\":" + std::to_string(slot) + "}");
}

void OnlineLobbyClient::start()
{
	if (!inGame())
	{
		return;
	}
	queue("start", "POST", "/v1/games/" + m_gameId + "/start", "");
}

std::string OnlineLobbyClient::finishGame()
{
	const std::string id = m_playedGame;
	m_playedGame.clear();
	if (id.empty())
	{
		return id;
	}
	queue("leave", "POST", "/v1/games/" + id + "/leave", "");
	m_mine.erase(std::remove_if(m_mine.begin(), m_mine.end(), [&](const Listing &l) { return l.id == id; }), m_mine.end());
	if (m_gameId == id)
	{
		leftGame("finished");
	}
	return id;
}

void OnlineLobbyClient::forgetPlayedGame()
{
	if (inGame())
	{
		leftGame("game_failed"); // the started game's room: no leave (no "Removed" box: the shell shows the reason it was given)
	}
	m_playedGame.clear();
}

void OnlineLobbyClient::dropStart()
{
	if (inGame())
	{
		leftGame("start_dropped"); // also forgets the START
	}
	m_startJson.clear();
	m_playedGame.clear(); // lane ONLINEUX-1 r2 (F2): an unplayed START is no played game (no leave is sent for it)
}

void OnlineLobbyClient::update(std::uint64_t nowMs)
{
	m_now = nowMs;
	if (m_socialStarted && !m_socialStopped && m_token.size() && m_socialHeldInFlight == 0 &&
		nowMs >= m_socialNextPoll && nowMs >= m_socialBackoffUntil)
	{
		m_socialDue = true;
	}
	if (m_options.longPollS > 0 && m_holdCap > 0)
	{
		// lane ONLINE-5: the lobby holds polls: one held poll at a time, on its own connection, the next as soon as the last was answered
		if (inGame() && m_startJson.empty() && m_heldInFlight == 0 && !m_pollQueued && nowMs >= m_nextPoll)
		{
			m_heldDue = true;
		}
		return;
	}
	if (inGame() && m_startJson.empty() && !m_pollQueued && m_heldInFlight == 0 && nowMs >= m_nextPoll)
	{
		queue("poll", "GET", "/v1/games/" + m_gameId + "?since=" + std::to_string(m_chatSince), "");
		m_pollQueued = true;
		m_nextPoll = nowMs + m_options.pollMs;
	}
}

std::vector<OnlineLobbyClient::HttpRequest> OnlineLobbyClient::takeRequests()
{
	std::vector<HttpRequest> out;
	if (m_inFlight == 0 && !m_queue.empty())
	{
		auto selected = m_queue.begin();
		if (selected->kind.rfind("social_", 0) == 0 && m_now < m_socialBackoffUntil)
		{
			selected = std::find_if(m_queue.begin(), m_queue.end(), [](const Pending &pending) {
				return pending.kind.rfind("social_", 0) != 0;
			});
		}
		if (selected != m_queue.end())
		{
			m_current = *selected;
			m_queue.erase(selected);
			m_current.request.token = m_token;
			m_inFlight = m_current.request.id;
			m_inFlightSentAt = m_now;
			++m_sent[m_current.kind.substr(0, m_current.kind.find(':'))];
			out.push_back(m_current.request);
		}
	}
	if (m_heldDue && m_heldInFlight == 0 && inGame() && m_startJson.empty())
	{
		// lane ONLINE-5: the held poll (lobby.mjs: answered at once unless the game's seq is still `seq`, else at its next change or after `wait`)
		m_heldDue = false;
		HttpRequest r;
		r.id = m_nextId++;
		r.method = "GET";
		r.held = true;
		r.channel = HttpRequest::Room;
		r.holdSeconds = std::min<int>((int)m_options.longPollS, m_holdCap);
		r.path = "/v1/games/" + m_gameId + "?since=" + std::to_string(m_chatSince) + "&seq=" + std::to_string(m_seq) + "&wait=" + std::to_string(r.holdSeconds);
		r.token = m_token;
		m_heldInFlight = r.id;
		m_heldSentAt = m_now;
		m_heldSeq = m_seq;
		m_heldSince = m_chatSince;
		++m_sent["poll"];
		++m_sent["held"];
		out.push_back(r);
	}
	m_heldDue = false;
	if (m_socialDue && m_socialHeldInFlight == 0 && m_socialStarted && !m_socialStopped &&
		m_now >= m_socialBackoffUntil && !m_token.empty())
	{
		m_socialDue = false;
		HttpRequest r;
		r.id = m_nextId++;
		r.method = "GET";
		r.held = true;
		r.channel = HttpRequest::Social;
		r.holdSeconds = 25;
		r.path = "/v1/inbox";
		if (!m_social.seq.empty())
		{
			r.path += "?since=" + socialPercentEncode(m_social.seq) + "&wait=25";
		}
		else
		{
			r.path += "?wait=25";
		}
		r.token = m_token;
		m_socialHeld = Pending{ "social_inbox", r };
		m_socialHeldInFlight = r.id;
		m_socialSentAt = m_now;
		m_socialAskedSeq = m_social.seq;
		++m_sent["social_inbox"];
		++m_sent["held_social"];
		out.push_back(r);
	}
	return out;
}

int OnlineLobbyClient::lobbyRttMs() const
{
	return m_lobbyRtts.empty() ? -1 : *std::min_element(m_lobbyRtts.begin(), m_lobbyRtts.end());
}

std::vector<OnlineLobbyClient::Event> OnlineLobbyClient::takeEvents()
{
	std::vector<Event> out;
	out.swap(m_events);
	return out;
}

void OnlineLobbyClient::leftGame(const std::string &why)
{
	m_gameId.clear();
	m_gameName.clear();
	m_localSlot = m_hostSlot = -1;
	m_info = SkirmishGameInfo();
	m_slots = {};
	m_seq = 0;
	m_chatSince = 0;
	m_startJson.clear();
	m_pollQueued = false;
	m_nextPoll = 0;
	m_heldInFlight = 0; // lane ONLINE-5: the held poll's answer, when it comes, is an answer to nothing we asked
	m_heldDue = false;
	m_holdCap = 0;      // the next game's first poll says again whether its lobby holds polls
	m_ownEcho = 0;
	m_gamePasswordProtected = false;
	// a queued poll / change of the game left behind is dropped (its answer would say "not in game")
	for (auto it = m_queue.begin(); it != m_queue.end();)
	{
		const bool gameRequest = it->kind == "poll" || it->kind == "slot" || it->kind == "setup" || it->kind == "ready" || it->kind == "chat" ||
			it->kind == "kick" || it->kind == "start" || it->kind == "link";
		it = gameRequest ? m_queue.erase(it) : it + 1;
	}
	Event e{ Event::Removed };
	e.code = why;
	m_events.push_back(e);
}

bool OnlineLobbyClient::readGameView(const JsonValue &g, std::string *error, bool fresh)
{
	// lobby.mjs gameView: { id, name, state, hostSlot, profile, setup: { map, startingCash, superweaponRestriction, legendaryHeroes, rules }, slots: [8 x
	// { slot, state, name, faction, color, startPos, team, ready, you }], seq }
	auto fail = [&](const std::string &why) {
		*error = "the lobby's game view: " + why;
		return false;
	};
	if (!g.isObject())
	{
		return fail("not an object");
	}
	const std::string id = strOf(g.get("id"));
	if (!isHexId(id))
	{
		return fail("\"id\" is not 16 hex digits");
	}
	long long hostSlot = 0, seq = 0;
	if (!intOf(g.get("hostSlot"), 0, MAX_SLOTS - 1, hostSlot) || !intOf(g.get("seq"), 0, 0xFFFFFFFFll, seq))
	{
		return fail("\"hostSlot\" / \"seq\" out of range");
	}
	const JsonValue *setup = g.get("setup");
	const JsonValue *slots = g.get("slots");
	if (!setup || !setup->isObject() || !slots || !slots->isArray() || slots->array.size() != (size_t)MAX_SLOTS)
	{
		return fail("\"setup\" must be an object and \"slots\" 8 slots");
	}
	if (!fits(g.get("name"), kMaxGameNameBytes) || !fits(setup->get("map"), kMaxMapBytes))
	{
		return fail("the game's name or map is beyond the lobby's limits");
	}
	SkirmishGameInfo info = m_info;
	info.mapName = strOf(setup->get("map"));
	long long cash = 0, sw = 0, lh = 0;
	if (info.mapName.empty() || !intOf(setup->get("startingCash"), -1, 1000000000, cash) || !intOf(setup->get("superweaponRestriction"), 0, 1, sw) ||
		!intOf(setup->get("legendaryHeroes"), 1, 4, lh))
	{
		return fail("the setup's map / startingCash / superweaponRestriction / legendaryHeroes");
	}
	info.startingCash = (int)cash;
	// lane UBERJOIN-1: the room's mode (absent: "normal"); a room's mode is the host's, so it's the game's simulation profile in the staging room's model
	info.simulationProfile = SimulationProfile::Faithful;
	if (const JsonValue *mode = g.get("mode"))
	{
		if (!mode->isString() || !parseMode(mode->string, &info.simulationProfile))
		{
			return fail("\"mode\" is neither \"normal\" nor \"uber\"");
		}
	}
	info.superweaponRestriction = (int)sw;
	info.legendaryHeroes = (int)lh;
	const JsonValue *rules = setup->get("rules");
	info.rules = MpGameRules::none();
	if (rules && rules->type != JsonValue::NUL)
	{
		if (!rules->isArray() || rules->array.size() != info.rules.size())
		{
			return fail("\"rules\" must be null or 10 integers");
		}
		for (size_t i = 0; i < info.rules.size(); ++i)
		{
			long long r = 0;
			if (!intOf(&rules->array[i], -2147483648ll, 2147483647ll, r))
			{
				return fail("rule " + std::to_string(i) + " is not an integer");
			}
			info.rules[i] = (std::int32_t)r;
		}
	}
	std::array<SlotView, MAX_SLOTS> views{};
	int local = -1;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const JsonValue &js = slots->array[(size_t)i];
		const std::string at = "slot " + std::to_string(i) + ": ";
		if (!js.isObject())
		{
			return fail(at + "not an object");
		}
		const std::string state = strOf(js.get("state"));
		bool known = false;
		SkirmishGameSlot gs = info.slots[i];
		for (const auto &k : kSlotStates)
		{
			if (state == k.name)
			{
				gs.state = k.state;
				known = true;
			}
		}
		if (!known)
		{
			return fail(at + "unknown state \"" + state + "\"");
		}
		long long faction = 0, color = 0, startPos = 0, team = 0;
		if (!intOf(js.get("faction"), -1, m_options.playerTemplateCount - 1, faction) || !intOf(js.get("color"), -1, m_options.colorCount - 1, color) ||
			!intOf(js.get("startPos"), -1, MAX_SLOTS - 1, startPos) || !intOf(js.get("team"), -1, MAX_SLOTS - 1, team))
		{
			return fail(at + "faction / color / startPos / team out of range (" + std::to_string(m_options.playerTemplateCount) + " factions, " +
				std::to_string(m_options.colorCount) + " colours)");
		}
		const JsonValue *name = js.get("name");
		if (!fits(name, kMaxPlayerNameBytes))
		{
			return fail(at + "the name is longer than " + std::to_string(kMaxPlayerNameBytes) + " bytes");
		}
		gs.name = name && name->isString() ? u16(name->string) : std::u16string();
		gs.playerTemplate = (int)faction;
		gs.color = (int)color;
		gs.startPos = (int)startPos;
		gs.teamNumber = (int)team;
		// lane ONLINEMENU-1: lobby.mjs gameView `handicap` (0, -5 .. -95). Absent: a lobby from before it carried one, which keeps every slot at 0 (its
		// STARTs carry none either); present, it must be one of the Handicap combo's values (RW 0x840B9D) or the view is refused
		gs.handicap = 0;
		if (const JsonValue *h = js.get("handicap"))
		{
			long long handicap = 0;
			if (!intOf(h, -95, 0, handicap) || !SkirmishGameSlot::handicapValid((int)handicap))
			{
				return fail(at + "\"handicap\" must be 0 or -5 .. -95 in steps of 5");
			}
			gs.handicap = (int)handicap;
		}
		const JsonValue *ready = js.get("ready");
		const JsonValue *you = js.get("you");
		if (!ready || ready->type != JsonValue::BOOL || !you || you->type != JsonValue::BOOL)
		{
			return fail(at + "\"ready\" / \"you\" must be true or false");
		}
		gs.accepted = ready->boolean;
		// lane PLATFORMICON-1: lobby.mjs gameView `platform`: null / absent (an older lobby, an AI, an open slot) or windows / linux / macos. Display data:
		// anything else is no icon, named in platformNotes (a view is never refused for it)
		gs.platform = PlatformKind::None;
		if (const JsonValue *plat = js.get("platform"); plat && plat->type != JsonValue::NUL)
		{
			PlatformKind kind = PlatformKind::None;
			if (plat->isString() && state == "player" && platformFromName(plat->string, kind))
			{
				gs.platform = kind;
			}
			else
			{
				const std::string note = at + "platform " + (plat->isString() ? "\"" + plat->string.substr(0, 24) + "\"" : std::string("(not a string)")) +
					(state == "player" ? " is not windows, linux or macos" : " on a slot that is not a player's");
				if (m_platformNotes.size() < kMaxPlatformNotes && (m_platformNotes.empty() || m_platformNotes.back() != note))
				{
					m_platformNotes.push_back(note);
				}
			}
		}
		views[(size_t)i] = SlotView{ state, ready->boolean, you->boolean };
		// lane ONLINEMENU-1: lobby.mjs gameView `link`: null / absent (not reported, an older lobby) or { rtt: 1 .. 10000 | null, loss: 0 .. 100 }; a
		// player's slot only. Anything else refuses the view (a protocol error, as every other field)
		if (const JsonValue *link = js.get("link"); link && link->type != JsonValue::NUL)
		{
			long long loss = 0, rtt = 0;
			const JsonValue *r = link->isObject() ? link->get("rtt") : nullptr;
			if (state != "player" || !link->isObject() || !r || !intOf(link->get("loss"), 0, 100, loss) ||
				(r->type != JsonValue::NUL && !intOf(r, 1, 10000, rtt)) || (r->type == JsonValue::NUL && loss != 100))
			{
				return fail(at + "\"link\" must be null or {rtt: 1 .. 10000 | null, loss: 0 .. 100} on a player's slot");
			}
			views[(size_t)i].linkKnown = true;
			views[(size_t)i].rttMs = r->type == JsonValue::NUL ? -1 : (int)rtt;
			views[(size_t)i].lossPct = (int)loss;
		}
		// lane ONLINEMENU-1: lobby.mjs gameView `cah`: null / absent (no hero, an older lobby) or a player's record (base64 of its wire form). A record that
		// does not read (SkirmishGameSlot::setCreateAHeroBytes: the stream, the checksum, the wire form) is no hero plus SlotView::heroProblem: the view
		// itself stays (one player's bad record must not stop everyone's room)
		gs.clearCreateAHero();
		if (const JsonValue *cah = js.get("cah"); cah && cah->type != JsonValue::NUL)
		{
			std::vector<std::uint8_t> bytes;
			std::string why;
			if (state != "player")
			{
				why = "a Create-a-Hero on a slot that is not a player's";
			}
			else if (!cah->isString() || cah->string.size() > 4 * ((kMaxCreateAHeroBytes + 2) / 3) || !RelayProtocol::base64Decode(cah->string, bytes))
			{
				why = "a Create-a-Hero record that is not base64 of 1 .. " + std::to_string(kMaxCreateAHeroBytes) + " bytes";
			}
			else if (!gs.setCreateAHeroBytes(bytes, &why))
			{
				gs.clearCreateAHero();
			}
			views[(size_t)i].heroProblem = why;
		}
		const JsonValue *playerId = js.get("player_id");
		if (playerId && playerId->type != JsonValue::NUL)
		{
			if (!playerId->isString() || playerId->string.empty() || playerId->string.size() > 128)
			{
				return fail(at + "invalid reporting player id");
			}
			views[(size_t)i].playerId = playerId->string;
		}
		const JsonValue *pid = js.get("pid");
		const JsonValue *blocked = js.get("blocked");
		if (pid && pid->type != JsonValue::NUL)
		{
			if (!pid->isString() || !isHexId(pid->string))
			{
				return fail(at + "invalid social pid");
			}
			views[(size_t)i].pid = pid->string;
		}
		if (blocked)
		{
			if (blocked->type != JsonValue::BOOL)
			{
				return fail(at + "invalid blocked flag");
			}
			views[(size_t)i].blocked = blocked->boolean;
		}
		if (you->boolean)
		{
			local = i;
		}
		info.slots[i] = gs;
	}
	if (local < 0)
	{
		return fail("no slot is ours");
	}
	if (!fresh && id == m_gameId && (std::uint32_t)seq < m_seq)
	{
		// lane ONLINE-5: two connections (the held poll and the ordinary requests): an answer made before the one already read is not a step back.
		// (lobby.mjs: a game's seq only grows.) A view of another game than the one we are in is no answer to us at all
		return true;
	}
	if (!fresh && !m_gameId.empty() && id != m_gameId)
	{
		return fail("it is game " + id + "'s, not ours");
	}
	m_gameId = id;
	m_gameName = u16(strOf(g.get("name")));
	m_hostSlot = (int)hostSlot;
	m_localSlot = local;
	m_seq = (std::uint32_t)seq;
	m_info = info;
	m_slots = views;
	// lane ONLINEMENU-1: the room's relay (lobby.mjs gameView `relay`: "a.b.c.d:port" or null; absent from an older lobby); a malformed one is none
	m_roomRelay.clear();
	if (const JsonValue *relay = g.get("relay"); relay && relay->isString() && relay->string.size() <= 32)
	{
		NetAddress address;
		if (NetAddress::parse(relay->string, address) && address.port != 0)
		{
			m_roomRelay = relay->string;
		}
	}
	return true;
}

void OnlineLobbyClient::deliver(int id, int status, const std::string &body, const std::string &transportError)
{
	const bool socialHeld = id != 0 && id == m_socialHeldInFlight;
	if (id == 0 || (id != m_inFlight && id != m_heldInFlight && !socialHeld))
	{
		return; // an answer to nothing we asked
	}
	Pending p;
	const bool held = id == m_heldInFlight;
	if (socialHeld)
	{
		p = m_socialHeld;
		m_socialHeldInFlight = 0;
		m_socialNextPoll = m_now + m_options.pollMs;
	}
	else if (held)
	{
		// lane ONLINE-5: the held poll's answer. The next one goes out at once when this one was really held or brought something; a lobby (or
		// something between) that answers a held poll at once with nothing new is polled at the plain interval, never in a tight loop; so is a
		// lobby that failed
		p.kind = "poll";
		p.request.id = id;
		m_heldInFlight = 0;
		m_nextPoll = m_now + m_options.pollMs;
	}
	else
	{
		p = m_current;
		m_inFlight = 0;
		if (transportError.empty() && status > 0 && m_now >= m_inFlightSentAt)
		{
			m_lobbyRtts.push_back((int)std::min<std::uint64_t>(m_now - m_inFlightSentAt, 60000));
			if (m_lobbyRtts.size() > kLobbyRttSamples)
			{
				m_lobbyRtts.pop_front();
			}
		}
	}
	const std::string kind = p.kind.substr(0, p.kind.find(':'));
	if (kind == "features")
	{
		// ONLINEMENU-1: data for the social session; any failure leaves the features empty (no social), nothing refused, no box
		m_lobbyFeatures.clear();
		JsonValue health;
		std::string why;
		if (transportError.empty() && status == 200 && body.size() <= kMaxBodyBytes && JsonValue::parse(body, health, &why) && health.isObject())
		{
			if (const JsonValue *features = health.get("features"); features && features->isArray() && features->array.size() <= 32)
			{
				for (const JsonValue &f : features->array)
				{
					if (f.isString() && f.string.size() <= 32) m_lobbyFeatures.insert(f.string);
				}
			}
		}
		return;
	}
	if (kind == "health")
	{
		// lane ONLINEMENU-1 (fetchRelays): display data; whatever goes wrong is kept for the screens' note, nothing refused, no box
		m_lobbyRelay.clear();
		m_relayLookupError.clear();
		JsonValue health;
		std::string why;
		if (!transportError.empty())
		{
			m_relayLookupError = "transport: " + redact(transportError);
		}
		else if (status != 200 || body.size() > kMaxBodyBytes || !JsonValue::parse(body, health, &why) || !health.isObject())
		{
			m_relayLookupError = "the lobby's /v1/health answered HTTP " + std::to_string(status) + " with something unreadable";
		}
		else if (const JsonValue *relays = health.get("relays"); !relays || !relays->isArray())
		{
			m_relayLookupError = "the lobby's /v1/health has no \"relays\" list";
		}
		else if (relays->array.empty())
		{
			m_relayLookupError = "the lobby names no relay";
		}
		else
		{
			const JsonValue *address = relays->array[0].isObject() ? relays->array[0].get("address") : nullptr;
			if (!fits(address, 21) || strOf(address).empty())
			{
				m_relayLookupError = "the lobby's first relay has no address";
			}
			else
			{
				m_lobbyRelay = strOf(address); // RelayProbe::start checks its form
			}
		}
		return;
	}
	const bool socialRequest = kind.rfind("social_", 0) == 0;
	if (kind == "poll" && !held)
	{
		m_pollQueued = false;
	}
	if (kind == "chat" && (!transportError.empty() || status != 200) && m_ownEcho > 0)
	{
		--m_ownEcho; // the line shown at its sending never comes back from the lobby (the refusal below says why)
	}
	if (!transportError.empty())
	{
		if (socialRequest)
		{
			socialRefuse(kind, "transport");
			return;
		}
		if (kind == "session")
		{
			m_token.clear();
			m_accountName.clear();
		}
		refuse(kind, "transport", transportError);
		return;
	}
	if (body.size() > kMaxBodyBytes)
	{
		// Sol r1: a hostile or broken lobby's answer is not read into memory beyond the bound (the transport stops at it too)
		if (kind == "session")
		{
			m_token.clear();
			m_accountName.clear();
		}
		if (socialRequest)
		{
			socialRefuse(kind, "protocol");
		}
		else
		{
			refuse(kind, "protocol", "the lobby's answer is larger than " + std::to_string(kMaxBodyBytes) + " bytes");
		}
		return;
	}
	JsonValue root;
	std::string why;
	if (!JsonValue::parse(body, root, &why) || !root.isObject() || !root.get("v") || !root.get("v")->isNumber() || root.get("v")->number != 1)
	{
		if (socialRequest)
		{
			socialRefuse(kind, "protocol");
		}
		else
		{
			refuse(kind, "protocol", "the lobby answered HTTP " + std::to_string(status) + " with something that is not lobby version 1");
		}
		return;
	}
	if (status != 200)
	{
		const std::string code = fits(root.get("error"), 64) ? strOf(root.get("error")) : std::string("protocol");
		if (socialRequest)
		{
			long long retry = 0;
			if (status == 429 && intOf(root.get("retry_after"), 1, 86400, retry))
			{
				m_socialBackoffUntil = std::max(m_socialBackoffUntil, m_now + (std::uint64_t)retry * 1000u);
				m_socialNextPoll = std::max(m_socialNextPoll, m_socialBackoffUntil);
			}
			socialRefuse(kind, code.empty() ? "http_" + std::to_string(status) : code);
			return;
		}
		stopSocial(code, kind);
		const std::size_t limit = code == "banned" || code == "suspended" || code == "muted" ? kMaxStatusBytes : kMaxRefusalBytes;
		const std::string message = fits(root.get("message"), limit) ? strOf(root.get("message"))
			: "the lobby's refusal is longer than " + std::to_string(limit) + " bytes";
		if (kind == "poll" && inGame() && (code == "not_in_game" || code == "no_game"))
		{
			// lobby.mjs poll: a kicked player gets 403 not_in_game ("kicked or left"), a game the host closed is gone (404 no_game)
			leftGame(code == "no_game" ? "closed" : "kicked");
			return;
		}
		if (kind == "session")
		{
			m_token.clear(); // the kept session is no longer the lobby's (expired, another lobby): the caller signs in afresh
			m_accountName.clear();
		}
		refuse(kind, code.empty() ? "http_" + std::to_string(status) : code, message.empty() ? "HTTP " + std::to_string(status) : message);
		return;
	}
	if (socialRequest)
	{
		handleSocial(p, root);
		if (socialHeld && !m_socialStopped)
		{
			const std::uint64_t elapsed = m_now >= m_socialSentAt ? m_now - m_socialSentAt : 0;
			if (m_social.seq == m_socialAskedSeq && elapsed < 500)
			{
				m_socialNextPoll = std::max(m_socialNextPoll, m_now + m_options.pollMs);
			}
			else
			{
				m_socialNextPoll = std::max(m_socialNextPoll, m_socialBackoffUntil);
			}
		}
		return;
	}
	const std::uint32_t seqBefore = m_seq, sinceBefore = m_chatSince;
	const std::size_t eventsBefore = m_events.size();
	handle(p, status, root);
	if (!held && kind == "poll" && inGame() && m_holdCap > 0 && m_options.longPollS > 0)
	{
		m_nextPoll = 0; // the plain poll that told us the lobby holds polls: the first held one follows at once
	}
	if (kind == "poll" && inGame() && m_backoffS > 0)
	{
		// Sol r1: the lobby did not hold the poll (a bound was reached) and asked for a pause: kept whatever else the answer says
		m_nextPoll = std::max<std::uint64_t>(m_nextPoll, m_now + (std::uint64_t)m_backoffS * 1000u);
		m_backoffS = 0;
		return;
	}
	if (held && inGame())
	{
		const bool refused = std::any_of(m_events.begin() + (std::ptrdiff_t)std::min(eventsBefore, m_events.size()), m_events.end(),
			[](const Event &e) { return e.kind == Event::Refused; });
		// "something new" is measured against what this poll asked from (m_heldSeq): our own change's answer may have been read first, and the
		// lobby still answered this poll for a reason
		long long answered = 0;
		const JsonValue *game = root.get("game");
		const bool moved = game && game->isObject() && intOf(game->get("seq"), 0, 0xFFFFFFFFll, answered) && (std::uint32_t)answered != m_heldSeq;
		const bool news = moved || m_seq != seqBefore || m_chatSince != sinceBefore || !m_startJson.empty();
		const bool waited = m_now >= m_heldSentAt + 500;
		if (!refused && (news || waited))
		{
			m_nextPoll = 0;
		}
	}
}

void OnlineLobbyClient::handle(const Pending &p, int, const JsonValue &root)
{
	const std::string kind = p.kind.substr(0, p.kind.find(':'));
	std::string error;
	if (kind == "signin")
	{
		const JsonValue *account = root.get("account");
		const std::string token = strOf(root.get("token"));
		if (token.empty() || !account || !account->isObject() || strOf(account->get("name")).empty())
		{
			refuse(kind, "protocol", "the sign-in answer has no token or account name");
			return;
		}
		if (token.size() > kMaxTokenBytes || !fits(account->get("name"), kMaxPlayerNameBytes))
		{
			refuse(kind, "protocol", "the sign-in answer's token or account name is too long");
			return;
		}
		m_token = token;
		m_accountName = u16(strOf(account->get("name")));
		const JsonValue *notice = root.get("notice");
		if (notice && notice->type != JsonValue::NUL && (!notice->isObject() ||
			!fits(notice->get("id"), 128) || strOf(notice->get("id")).empty() ||
			!fits(notice->get("text"), kMaxNoticeBytes) || strOf(notice->get("text")).empty()))
		{
			m_token.clear();
			m_accountName.clear();
			refuse(kind, "protocol", "the sign-in notice is malformed");
			return;
		}
		Event e{ Event::SignedIn };
		e.text = m_accountName;
		m_events.push_back(e);
		if (notice && notice->isObject() && strOf(notice->get("id")) != m_lastNotice)
		{
			m_lastNotice = strOf(notice->get("id"));
			refuse("notice", "notice", strOf(notice->get("text")));
			m_events.back().kind = Event::Notice;
		}
		startSocial();
		return;
	}
	if (kind == "motd")
	{
		const JsonValue *motd = root.get("motd");
		if (!motd || (motd->type != JsonValue::NUL && !motd->isString()))
		{
			refuse(kind, "protocol", "\"motd\" must be a string or null");
			return;
		}
		if (!fits(motd, kMaxMotdBytes))
		{
			refuse(kind, "protocol", "the message of the day is longer than " + std::to_string(kMaxMotdBytes) + " bytes");
			return;
		}
		m_motdKnown = true;
		m_hasMotd = motd->isString() && !motd->string.empty();
		m_motd = m_hasMotd ? u16(motd->string) : std::u16string();
		m_events.push_back(Event{ Event::Motd });
		return;
	}
	if (kind == "list" || kind == "mine" || kind == "session")
	{
		const JsonValue *games = root.get("games");
		if (!games || !games->isArray())
		{
			if (kind == "session")
			{
				m_token.clear();
			}
			refuse(kind, "protocol", "\"games\" is not a list");
			return;
		}
		const std::size_t most = kind == "list" ? kMaxGames : kMaxMine;
		if (games->array.size() > most)
		{
			if (kind == "session")
			{
				m_token.clear();
			}
			refuse(kind, "protocol", "the lobby listed more than " + std::to_string(most) + " games");
			return;
		}
		std::vector<Listing> list;
		for (const JsonValue &g : games->array)
		{
			Listing l;
			long long players = 0, open = 0, slot = -1;
			l.id = strOf(g.get("id"));
			const JsonValue *pw = g.get("password");
			if (!g.isObject() || !isHexId(l.id) || !intOf(g.get("players"), 0, MAX_SLOTS, players) || !intOf(g.get("openSlots"), 0, MAX_SLOTS, open) || !pw ||
				pw->type != JsonValue::BOOL || (kind != "list" && !intOf(g.get("slot"), 0, MAX_SLOTS - 1, slot)) || !fits(g.get("name"), kMaxGameNameBytes) ||
				!fits(g.get("host"), kMaxPlayerNameBytes) || !fits(g.get("map"), kMaxMapBytes) || !fits(g.get("state"), 16))
			{
				if (kind == "session")
				{
					m_token.clear();
				}
				refuse(kind, "protocol", "a listed game lacks its id, players, openSlots, password flag or (a running game of the player) its slot, or has "
					"a name, host or map beyond the lobby's limits");
				return;
			}
			l.name = u16(strOf(g.get("name")));
			l.host = u16(strOf(g.get("host")));
			if (const JsonValue *hp = g.get("hostPlatform"); hp && hp->isString())
			{
				platformFromName(hp->string, l.hostPlatform); // display data: another value is no icon
			}
			l.map = strOf(g.get("map"));
			l.players = (int)players;
			l.openSlots = (int)open;
			l.password = pw->boolean;
			// lane UBERJOIN-1: the game's "mode" is optional (absent: "normal", a room of an old client); a value outside the two names is a protocol refusal
			if (const JsonValue *mode = g.get("mode"))
			{
				if (!mode->isString() || !parseMode(mode->string, &l.mode))
				{
					if (kind == "session")
					{
						m_token.clear();
					}
					refuse(kind, "protocol", "a listed game's \"mode\" is neither \"normal\" nor \"uber\"");
					return;
				}
			}
			l.state = strOf(g.get("state"));
			l.slot = (int)slot;
			list.push_back(l);
		}
		if (kind == "list")
		{
			m_games = list;
			m_events.push_back(Event{ Event::GameList });
			return;
		}
		// lane ONLINEUX-1 r2 (Sol 4): the games the account left (optional: an older lobby has no "left"); a list beyond kMaxMine or a non-id entry is
		// refused by name, as a bad "games"
		std::vector<std::string> left;
		if (const JsonValue *lv = root.get("left"))
		{
			bool good = lv->isArray() && lv->array.size() <= kMaxMine;
			for (size_t i = 0; good && i < lv->array.size(); ++i)
			{
				good = lv->array[i].isString() && isHexId(lv->array[i].string);
				if (good)
				{
					left.push_back(lv->array[i].string);
				}
			}
			if (!good)
			{
				if (kind == "session")
				{
					m_token.clear();
				}
				refuse(kind, "protocol", "\"left\" is not a list of at most " + std::to_string(kMaxMine) + " game ids");
				return;
			}
		}
		m_mine = list;
		m_mineLeft = std::move(left);
		if (kind == "session")
		{
			// the kept session is valid: signed in as its account (lane ONLINE-4: a restarted game is the same player, so it can rejoin)
			Event e{ Event::SignedIn };
			e.text = m_accountName;
			e.flag = true;
			m_events.push_back(e);
			startSocial();
		}
		m_events.push_back(Event{ Event::Mine });
		return;
	}
	if (kind == "rejoin" || kind == "spectate")
	{
		const JsonValue *game = root.get("game");
		if (kind == "rejoin" && game && !readGameView(*game, &error, true))
		{
			refuse(kind, "protocol", error);
			return;
		}
		const JsonValue *start = root.get("start");
		if (!start || !start->isObject())
		{
			refuse(kind, "protocol", "no START in the answer");
			return;
		}
		const std::string json = toJson(*start);
		if (json.size() > kMaxStartBytes)
		{
			refuse(kind, "protocol", "the START is larger than " + std::to_string(kMaxStartBytes) + " bytes");
			return;
		}
		m_startJson = json;
		Event e{ Event::GameStart };
		e.code = kind;
		m_events.push_back(e);
		return;
	}
	if (kind == "create" || kind == "join")
	{
		const JsonValue *game = root.get("game");
		if (!game || !readGameView(*game, &error, true))
		{
			refuse(kind, "protocol", error.empty() ? "no game in the answer" : error);
			return;
		}
		m_chatSince = 0;
		m_startJson.clear();
		m_nextPoll = 0; // the first poll at once (it brings the chat so far)
		Event e{ kind == "create" ? Event::GameCreated : Event::GameJoined };
		e.slot = m_localSlot;
		m_events.push_back(e);
		return;
	}
	if (kind == "leave")
	{
		const JsonValue *left = root.get("left");
		if (!left || left->type != JsonValue::BOOL)
		{
			refuse("leave", "protocol", "the lobby did not confirm leaving the room");
			return;
		}
		if (!left->boolean)
		{
			refuse("leave", "not_in_game", "the lobby did not confirm leaving the room");
			return;
		}
		if (m_pendingSocialAcceptInvite.empty()) return;
		const std::string inviteId = std::move(m_pendingSocialAcceptInvite);
		m_pendingSocialAcceptInvite.clear();
		if (!acceptInvite(inviteId))
		{
			const std::string code = m_lastSocialError.empty() ? "social_unavailable" : m_lastSocialError;
			socialRefuse("accept_invite", code);
		}
		return;
	}
	if (kind == "slot" || kind == "setup" || kind == "ready" || kind == "kick" || kind == "link")
	{
		if (!inGame())
		{
			return;
		}
		const JsonValue *game = root.get("game");
		if (!game || !readGameView(*game, &error))
		{
			refuse(kind, "protocol", error.empty() ? "no game in the answer" : error);
			return;
		}
		m_events.push_back(Event{ Event::GameUpdated });
		return;
	}
	if (kind == "chat")
	{
		m_nextPoll = 0; // the line comes back with the next poll
		return;
	}
	if (kind == "start" || kind == "poll")
	{
		if (!inGame())
		{
			return;
		}
		// Sol r1 (ONLINE-5): a poll answer is checked whole before anything of it is kept: a refused answer changes nothing (the view, the ready flags,
		// the chat read so far, the hold). The game view is the last check (readGameView keeps it only when all of it is good)
		const JsonValue *game = kind == "poll" ? root.get("game") : nullptr;
		if (kind == "poll" && (!game || !game->isObject()))
		{
			refuse(kind, "protocol", "no game in the answer");
			return;
		}
		// lane ONLINE-5: "wait": the lobby holds polls for up to that many seconds (lobby.mjs MAX_WAIT_S) and marks the reader's own chat lines
		// ("you"). No such field: an older lobby, polled plainly. Sol r1: "backoff": the lobby did not hold this poll (a bound was reached) and
		// asks for that many seconds before the next one
		long long cap = 0, backoff = 0;
		const JsonValue *wait = kind == "poll" ? root.get("wait") : nullptr;
		if (wait && !intOf(wait, 1, kMaxHoldSeconds, cap))
		{
			refuse(kind, "protocol", "the lobby's \"wait\" is not a number of seconds from 1 to " + std::to_string(kMaxHoldSeconds));
			return;
		}
		const JsonValue *back = kind == "poll" ? root.get("backoff") : nullptr;
		if (back && !intOf(back, 1, kMaxHoldSeconds, backoff))
		{
			refuse(kind, "protocol", "the lobby's \"backoff\" is not a number of seconds from 1 to " + std::to_string(kMaxHoldSeconds));
			return;
		}
		const JsonValue *chat = kind == "poll" ? root.get("chat") : nullptr;
		if (chat && chat->type != JsonValue::NUL && !chat->isArray())
		{
			refuse(kind, "protocol", "\"chat\" is not a list");
			return;
		}
		if (chat && chat->array.size() > kMaxChatLines)
		{
			refuse(kind, "protocol", "the lobby sent more than " + std::to_string(kMaxChatLines) + " chat lines");
			return;
		}
		if (chat)
		{
			for (const JsonValue &c : chat->array)
			{
				long long seq = 0;
				const JsonValue *mine = c.get("you");
				if (!c.isObject() || !intOf(c.get("seq"), 0, 0xFFFFFFFFll, seq))
				{
					refuse(kind, "protocol", "a chat line without its seq");
					return;
				}
				if (!fits(c.get("from"), kMaxPlayerNameBytes) || !fits(c.get("text"), kMaxChatBytes))
				{
					refuse(kind, "protocol", "a chat line beyond the lobby's limits");
					return;
				}
				if (mine && mine->type != JsonValue::BOOL)
				{
					refuse(kind, "protocol", "a chat line's \"you\" must be true or false");
					return;
				}
			}
		}
		const JsonValue *start = root.get("start");
		std::string startJson;
		if (start && start->isObject())
		{
			// the START is handed on whole: OnlineStart::parse checks every field (the screens before the host program's net_online)
			startJson = toJson(*start);
			if (startJson.size() > kMaxStartBytes)
			{
				refuse(kind, "protocol", "the START is larger than " + std::to_string(kMaxStartBytes) + " bytes");
				return;
			}
		}
		if (game && !readGameView(*game, &error))
		{
			refuse(kind, "protocol", error);
			return;
		}
		// ---- everything checked: kept ----
		if (kind == "poll")
		{
			m_holdCap = (int)cap;
			m_backoffS = (int)backoff;
			m_events.push_back(Event{ Event::GameUpdated });
		}
		if (chat)
		{
			for (const JsonValue &c : chat->array)
			{
				long long seq = 0;
				intOf(c.get("seq"), 0, 0xFFFFFFFFll, seq);
				const JsonValue *mine = c.get("you");
				if ((std::uint32_t)seq <= m_chatSince)
				{
					continue; // lane ONLINE-5: a line an earlier answer brought already (two connections: answers may cross)
				}
				m_chatSince = std::max<std::uint32_t>(m_chatSince, (std::uint32_t)seq);
				if (mine && mine->boolean && m_ownEcho > 0)
				{
					--m_ownEcho; // the sender's own line: shown when it was sent (chat())
					continue;
				}
				const JsonValue *from = c.get("from");
				Event e{ Event::Chat };
				e.flag = !from || from->type == JsonValue::NUL;
				e.from = e.flag ? std::u16string() : u16(strOf(from));
				e.text = u16(strOf(c.get("text")));
				m_events.push_back(e);
			}
		}
		if (!startJson.empty())
		{
			m_startJson = startJson;
			m_events.push_back(Event{ Event::GameStart });
		}
		return;
	}
}
