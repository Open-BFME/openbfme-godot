// OpenBFME. GPL-3.0.
// Social client routes from docs/SOCIAL-API.md; transport and lobby scheduling stay in OnlineLobbyClient.
#include "GameNetwork/OnlineLobbyClient.h"

#include "Common/MiniJson.h"
#include "GameNetwork/OnlineStart.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
constexpr std::uint64_t kSafeInteger = 9007199254740991ull;
constexpr std::size_t kSocialNickBytes = 24;

std::string stringOf(const JsonValue *v)
{
	return v && v->isString() ? v->string : std::string();
}

bool numberOf(const JsonValue *v, std::uint64_t max, std::uint64_t &out)
{
	if (!v || !v->isNumber() || !std::isfinite(v->number) || std::floor(v->number) != v->number || v->number < 0 ||
		v->number > (double)std::min(max, kSafeInteger))
	{
		return false;
	}
	out = (std::uint64_t)v->number;
	return true;
}

bool queryNumber(const std::string &path, const std::string &key, std::uint64_t max, std::uint64_t &out)
{
	const std::string marker = "?" + key + "=";
	const std::size_t found = path.find(marker);
	if (found == std::string::npos || path.find(marker, found + marker.size()) != std::string::npos)
	{
		return false;
	}
	const std::size_t begin = found + marker.size();
	const std::size_t end = path.find('&', begin);
	const std::string digits = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
	if (digits.empty())
	{
		return false;
	}
	std::uint64_t value = 0;
	for (char c : digits)
	{
		if (c < '0' || c > '9')
		{
			return false;
		}
		const unsigned digit = (unsigned)(c - '0');
		if (value > (max - digit) / 10)
		{
			return false;
		}
		value = value * 10 + digit;
	}
	out = value;
	return true;
}

bool lowerHex(const std::string &s, std::size_t n)
{
	return s.size() == n && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

bool unicodeWhitespace(std::uint32_t cp)
{
	return (cp >= 0x0009 && cp <= 0x000D) || cp == 0x0020 || cp == 0x0085 || cp == 0x00A0 ||
		cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 ||
		cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

bool playerCode(const std::string &s)
{
	std::string raw = s;
	if (s.size() == 12 && s[4] == '-' && s[9] == '-')
	{
		raw = s.substr(0, 4) + s.substr(5, 4) + s.substr(10, 2);
	}
	if (raw.size() != 10)
	{
		return false;
	}
	for (char c : raw)
	{
		if (std::string("0123456789ABCDEFGHJKMNPQRSTVWXYZ").find(c) == std::string::npos)
		{
			return false;
		}
	}
	return true;
}

bool unicodeText(const std::string &s, std::size_t maxScalars, bool allowEmpty, bool trimRequired)
{
	if (s.empty())
	{
		return allowEmpty;
	}
	std::size_t count = 0;
	std::uint32_t first = 0, last = 0;
	bool hasNonWhitespace = false;
	for (std::size_t i = 0; i < s.size();)
	{
		const unsigned char c = (unsigned char)s[i];
		std::uint32_t cp = 0;
		std::size_t n = 0;
		if (c < 0x80)
		{
			cp = c;
			n = 1;
		}
		else if (c >= 0xC2 && c <= 0xDF)
		{
			cp = c & 0x1F;
			n = 2;
		}
		else if (c >= 0xE0 && c <= 0xEF)
		{
			cp = c & 0x0F;
			n = 3;
		}
		else if (c >= 0xF0 && c <= 0xF4)
		{
			cp = c & 0x07;
			n = 4;
		}
		else
		{
			return false;
		}
		if (i + n > s.size())
		{
			return false;
		}
		for (std::size_t j = 1; j < n; ++j)
		{
			const unsigned char tail = (unsigned char)s[i + j];
			if ((tail & 0xC0) != 0x80)
			{
				return false;
			}
			cp = (cp << 6) | (tail & 0x3F);
		}
		if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
			cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20 || cp == 0x7F)
		{
			return false;
		}
		if (count == 0) first = cp;
		last = cp;
		hasNonWhitespace = hasNonWhitespace || !unicodeWhitespace(cp);
		++count;
		i += n;
	}
	if (count > maxScalars)
	{
		return false;
	}
	if (trimRequired && (!hasNonWhitespace || unicodeWhitespace(first) || unicodeWhitespace(last)))
	{
		return false;
	}
	return true;
}

bool parsePlayer(const JsonValue &v, OnlineLobbyClient::Player &out)
{
	if (!v.isObject())
	{
		return false;
	}
	const std::string pid = stringOf(v.get("pid"));
	const std::string code = stringOf(v.get("code"));
	const std::string nick = stringOf(v.get("nick"));
	if (!lowerHex(pid, 16) || !playerCode(code) || nick.empty() || nick.size() > kSocialNickBytes ||
		!unicodeText(nick, 24, false, false) || !OnlineStart::utf8ToUtf16(nick, out.nick))
	{
		return false;
	}
	out.pid = pid;
	out.code = code;
	return true;
}

bool parsePresence(const JsonValue &v, OnlineLobbyClient::Presence &out)
{
	if (!v.isObject())
	{
		return false;
	}
	out.pid = stringOf(v.get("pid"));
	out.state = stringOf(v.get("state"));
	const std::set<std::string> states{ "offline", "online", "in_room", "matching", "loading", "playing", "away" };
	if (!lowerHex(out.pid, 16) || !states.count(out.state))
	{
		return false;
	}
	const JsonValue *game = v.get("game"), *map = v.get("map"), *mode = v.get("mode"), *joinable = v.get("joinable");
	if (!game || !map || !mode || !joinable || joinable->type != JsonValue::BOOL ||
		(game->type != JsonValue::NUL && (!game->isString() || !lowerHex(game->string, 16))) ||
		(map->type != JsonValue::NUL && (!map->isString() || map->string.size() > 200)) ||
		(mode->type != JsonValue::NUL && (!mode->isString() || (mode->string != "normal" && mode->string != "uber"))))
	{
		return false;
	}
	const JsonValue *since = v.get("since");
	if (since && since->type == JsonValue::NUL && out.state == "offline")
	{
		out.since = 0;
	}
	else if (!numberOf(since, kSafeInteger, out.since))
	{
		return false;
	}
	out.joinable = joinable->boolean;
	out.hasGame = game->isString();
	out.hasMap = map->isString();
	out.hasMode = mode->isString();
	if (out.hasGame) out.game = game->string;
	if (out.hasMap) out.map = map->string;
	if (out.hasMode) out.mode = mode->string;
	return true;
}

bool parseRequest(const JsonValue &v, OnlineLobbyClient::Request &out)
{
	const JsonValue *from = v.get("from"), *to = v.get("to");
	if (!v.isObject() || !from || !to || !parsePlayer(*from, out.from) || !parsePlayer(*to, out.to))
	{
		return false;
	}
	out.id = stringOf(v.get("id"));
	return lowerHex(out.id, 16) && numberOf(v.get("at"), kSafeInteger, out.at);
}

bool parseInvite(const JsonValue &v, OnlineLobbyClient::Invite &out)
{
	const JsonValue *from = v.get("from"), *to = v.get("to"), *mode = v.get("mode");
	if (!v.isObject() || !from || !to || !mode || !parsePlayer(*from, out.from) || !parsePlayer(*to, out.to))
	{
		return false;
	}
	out.id = stringOf(v.get("id"));
	out.game = stringOf(v.get("game"));
	out.kind = stringOf(v.get("kind"));
	const JsonValue *note = v.get("note");
	if (!lowerHex(out.id, 16) || !lowerHex(out.game, 16) ||
		(out.kind != "invite" && out.kind != "challenge") ||
		(mode->type != JsonValue::NUL && (!mode->isString() || (mode->string != "normal" && mode->string != "uber"))) ||
		!note || !note->isString() || !unicodeText(note->string, 200, true, true) ||
		!numberOf(v.get("expires"), kSafeInteger, out.expires))
	{
		return false;
	}
	out.hasMode = mode->isString();
	if (out.hasMode) out.mode = mode->string;
	out.note = note->string;
	return true;
}

bool validPrivacy(const OnlineLobbyClient::Privacy &p)
{
	return (p.invites == "friends_or_room" || p.invites == "friends") &&
		(p.challenges == "friends_or_room" || p.challenges == "friends") &&
		(p.requests == "anyone" || p.requests == "nobody") &&
		(p.presence == "friends" || p.presence == "nobody");
}

bool parsePrivacy(const JsonValue *v, OnlineLobbyClient::Privacy &out)
{
	if (!v || !v->isObject())
	{
		return false;
	}
	out.invites = stringOf(v->get("invites"));
	out.challenges = stringOf(v->get("challenges"));
	out.requests = stringOf(v->get("requests"));
	out.presence = stringOf(v->get("presence"));
	return validPrivacy(out);
}

bool parseMe(const JsonValue &root, OnlineLobbyClient::Me &out)
{
	const std::string nick = stringOf(root.get("nick"));
	const JsonValue *hidden = root.get("hidden");
	out.pid = stringOf(root.get("pid"));
	out.code = stringOf(root.get("code"));
	if (!root.isObject() || !lowerHex(out.pid, 16) || !playerCode(out.code) || nick.empty() ||
		nick.size() > kSocialNickBytes || !unicodeText(nick, 24, false, false) ||
		!OnlineStart::utf8ToUtf16(nick, out.nick) || !parsePrivacy(root.get("privacy"), out.privacy) ||
		!hidden || hidden->type != JsonValue::BOOL)
	{
		return false;
	}
	out.hidden = hidden->boolean;
	return true;
}

bool parseStats(const JsonValue *v, OnlineLobbyClient::SocialStats &out)
{
	return v && v->isObject() && numberOf(v->get("players"), 1000000, out.players) &&
		numberOf(v->get("games_open"), 1000000, out.gamesOpen) &&
		numberOf(v->get("games_running"), 1000000, out.gamesRunning) &&
		numberOf(v->get("as_of"), kSafeInteger, out.asOf);
}

std::string urlEncode(const std::string &s)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s)
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

bool validNote(const std::string &note)
{
	return note.empty() || unicodeText(note, 200, true, true);
}

bool validId(const std::string &id)
{
	return lowerHex(id, 16);
}

std::string safeSocialCode(const std::string &code)
{
	static const std::set<std::string> codes{
		"already_friends", "already_reported", "already_requested", "bad_field", "bad_json", "bad_query", "busy",
		"bad_reason", "bad_recovery", "bad_version", "banned", "block_limit", "chat_muted", "cooldown",
		"friend_limit", "full", "idempotency_conflict", "in_game", "install_in_use", "install_required",
		"invite_limit", "muted", "no_game", "not_found", "not_host", "not_in_game", "profile",
		"profile_mismatch", "protocol", "rate_limited", "report_expired", "request_limit", "room_required",
		"self_target", "session_revoked", "shadowed", "signed_out", "social_store_full", "social_unavailable",
		"suspended", "started", "too_big", "transport", "unavailable"
	};
	return codes.count(code) ? code : "protocol";
}

bool terminalSocialCode(const std::string &code)
{
	return code == "signed_out" || code == "session_revoked" || code == "install_required" ||
		code == "banned" || code == "suspended";
}

std::string socialRefusalText(const std::string &code)
{
	return "Social request refused: " + code + ".";
}

} // namespace

void OnlineLobbyClient::socialQueue(const std::string &kind, const std::string &method, const std::string &path, const std::string &body)
{
	queue("social_" + kind, method, path, body);
	m_lastSocialError.clear();
}

void OnlineLobbyClient::resetSocialSession()
{
	m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [](const Pending &pending) {
		return pending.kind.rfind("social_", 0) == 0;
	}), m_queue.end());
	if (m_current.kind.rfind("social_", 0) == 0)
	{
		if (m_inFlight == m_current.request.id) m_inFlight = 0;
		m_current = Pending{};
	}
	m_social = SocialState{};
	m_socialEvents.clear();
	m_lastSocialError.clear();
	m_recoveryCode.clear();
	m_pendingSocialAcceptInvite.clear();
	m_seenSocialNotices.clear();
	m_socialStarted = false;
	m_socialStopped = false;
	m_socialDue = false;
	m_socialFirstInbox = true;
	m_socialHeldInFlight = 0;
	m_socialHeld = Pending{};
	m_socialNextPoll = 0;
	m_socialBackoffUntil = 0;
	m_socialSentAt = 0;
	m_socialAskedSeq.clear();
}

bool OnlineLobbyClient::socialReady(const std::string &request)
{
	if (m_socialStarted && !m_socialStopped && m_social.available && signedIn())
	{
		m_lastSocialError.clear();
		return true;
	}
	m_lastSocialError = m_options.installId.empty() ? "install_required" : "social_unavailable";
	return false;
}

void OnlineLobbyClient::socialRefuse(const std::string &request, const std::string &code)
{
	const std::string safeCode = safeSocialCode(code);
	if (terminalSocialCode(safeCode))
	{
		stopSocial(safeCode, request);
		return;
	}
	if (safeCode == "not_found" && (request == "social_me" || request == "social_inbox"))
	{
		markSocialUnsupported(); // a lobby without the social routes that still listed the feature: stop asking, say "not available"
		return;
	}
	SocialEvent event;
	event.kind = SocialEvent::Error;
	event.request = request;
	event.code = safeCode;
	event.text = socialRefusalText(event.code);
	m_socialEvents.push_back(event);
	m_lastSocialError = event.code;
	if (!m_socialStopped)
	{
		m_socialNextPoll = std::max(m_socialNextPoll, m_now + m_options.pollMs);
	}
}

void OnlineLobbyClient::stopSocial(const std::string &code, const std::string &request)
{
	const std::string safeCode = safeSocialCode(code);
	if (!terminalSocialCode(safeCode) || m_socialStopped)
	{
		return;
	}
	resetSocialSession();
	m_socialStopped = true;
	m_socialStarted = true;
	SocialEvent event;
	event.kind = SocialEvent::Error;
	event.request = request;
	event.code = safeCode;
	event.text = socialRefusalText(event.code);
	m_socialEvents.push_back(event);
	m_lastSocialError = event.code;
}

void OnlineLobbyClient::markSocialUnsupported()
{
	resetSocialSession();
	m_socialStarted = true;
	m_socialStopped = true;
	m_social.unsupported = true;
}

void OnlineLobbyClient::startSocial()
{
	if (!m_options.enableSocial || m_socialStarted || !signedIn())
	{
		return;
	}
	m_socialStarted = true;
	if (m_options.installId.empty())
	{
		socialRefuse("social", "install_required");
		return;
	}
	if (!lobbyHasFeature("social"))
	{
		markSocialUnsupported();
		return;
	}
	m_socialStopped = false;
	m_social.available = true;
	m_socialDue = true;
	m_socialNextPoll = m_now;
	socialQueue("me", "GET", "/v1/me");
	socialQueue("friends", "GET", "/v1/friends");
	socialQueue("recent", "GET", "/v1/recent");
}

std::vector<OnlineLobbyClient::SocialEvent> OnlineLobbyClient::takeSocialEvents()
{
	std::vector<SocialEvent> out;
	out.swap(m_socialEvents);
	for (SocialEvent &event : out)
	{
		if (event.kind == SocialEvent::Recovery)
		{
			event.text.swap(m_recoveryCode);
			m_recoveryCode.clear();
		}
	}
	return out;
}

std::string OnlineLobbyClient::takeRecoveryCode()
{
	std::string code;
	code.swap(m_recoveryCode);
	return code;
}

void OnlineLobbyClient::refreshSocial()
{
	if (!socialReady("refresh_social")) return;
	socialQueue("me", "GET", "/v1/me");
	socialQueue("friends", "GET", "/v1/friends");
	socialQueue("recent", "GET", "/v1/recent");
}

bool OnlineLobbyClient::setPrivacy(const Privacy &privacy)
{
	if (!socialReady("set_privacy")) return false;
	if (!validPrivacy(privacy)) { m_lastSocialError = "bad_field"; return false; }
	const std::string body = "{\"v\":1,\"privacy\":{\"invites\":" + jsonQuote(privacy.invites) +
		",\"challenges\":" + jsonQuote(privacy.challenges) + ",\"requests\":" + jsonQuote(privacy.requests) +
		",\"presence\":" + jsonQuote(privacy.presence) + "}}";
	socialQueue("privacy", "PUT", "/v1/me/privacy", body);
	return true;
}

bool OnlineLobbyClient::setPresence(const std::string &state, bool hasHidden, bool hidden)
{
	if (!socialReady("set_presence")) return false;
	if (state != "online" && state != "away" && state != "matching") { m_lastSocialError = "bad_field"; return false; }
	std::string body = "{\"v\":1,\"state\":" + jsonQuote(state);
	if (hasHidden) body += std::string(",\"hidden\":") + (hidden ? "true" : "false");
	socialQueue("presence", "POST", "/v1/me/status", body + "}");
	return true;
}

bool OnlineLobbyClient::requestFriendCode(const std::string &code)
{
	if (!socialReady("request_friend_code")) return false;
	if (!playerCode(code)) { m_lastSocialError = "bad_field"; return false; }
	socialQueue("friend_request_code", "POST", "/v1/friends/requests", "{\"v\":1,\"code\":" + jsonQuote(code) + "}");
	return true;
}

bool OnlineLobbyClient::requestFriend(const std::string &pid)
{
	if (!socialReady("request_friend")) return false;
	if (!validId(pid)) { m_lastSocialError = "bad_field"; return false; }
	socialQueue("friend_request_pid", "POST", "/v1/friends/requests", "{\"v\":1,\"pid\":" + jsonQuote(pid) + "}");
	return true;
}

bool OnlineLobbyClient::acceptFriend(const std::string &requestId)
{
	if (!socialReady("accept_friend")) return false;
	if (!validId(requestId)) { m_lastSocialError = "bad_field"; return false; }
	socialQueue("accept_friend", "POST", "/v1/friends/requests/" + requestId + "/accept", "{\"v\":1}");
	return true;
}

bool OnlineLobbyClient::declineFriend(const std::string &requestId)
{
	if (!socialReady("decline_friend")) return false;
	if (!validId(requestId)) { m_lastSocialError = "bad_field"; return false; }
	socialQueue("decline_friend", "POST", "/v1/friends/requests/" + requestId + "/decline", "{\"v\":1}");
	return true;
}

bool OnlineLobbyClient::removeFriend(const std::string &pid)
{
	if (!socialReady("remove_friend")) return false;
	if (!validId(pid)) { m_lastSocialError = "bad_field"; return false; }
	socialQueue("remove_friend", "DELETE", "/v1/friends/" + pid, "");
	return true;
}

bool OnlineLobbyClient::blockPlayer(const std::string &pid, const std::string &note)
{
	if (!socialReady("block_player")) return false;
	if (!validId(pid) || !validNote(note)) { m_lastSocialError = "bad_field"; return false; }
	std::string body = "{\"v\":1,\"pid\":" + jsonQuote(pid);
	if (!note.empty()) body += ",\"note\":" + jsonQuote(note);
	socialQueue("block_player", "POST", "/v1/blocks", body + "}");
	return true;
}

bool OnlineLobbyClient::unblockPlayer(const std::string &pid)
{
	if (!socialReady("unblock_player")) return false;
	if (!validId(pid)) { m_lastSocialError = "bad_field"; return false; }
	socialQueue("unblock_player", "DELETE", "/v1/blocks/" + pid, "");
	return true;
}

bool OnlineLobbyClient::fetchBlocks(const std::string &after)
{
	if (!socialReady("fetch_blocks")) return false;
	if (!after.empty() && !validId(after)) { m_lastSocialError = "bad_field"; return false; }
	std::string path = "/v1/blocks?limit=50";
	if (!after.empty()) path += "&after=" + urlEncode(after);
	socialQueue("blocks", "GET", path);
	return true;
}

bool OnlineLobbyClient::sendInvite(const std::string &to, const std::string &kind, const std::string &mode, const std::string &note)
{
	if (!socialReady("send_invite")) return false;
	if (!validId(to) || !validNote(note) || (kind != "invite" && kind != "challenge") ||
		(kind == "invite" && !mode.empty()) ||
		(kind == "challenge" && (mode != "normal" && mode != "uber")))
	{
		m_lastSocialError = "bad_field";
		return false;
	}
	if (!inGame() || m_gameId.empty() || (kind == "challenge" && (!amIHost() || !m_gamePasswordProtected)))
	{
		m_lastSocialError = "room_required";
		return false;
	}
	std::string body = "{\"v\":1,\"to\":" + jsonQuote(to) + ",\"game\":" + jsonQuote(m_gameId) + ",\"kind\":" + jsonQuote(kind);
	if (!mode.empty()) body += ",\"mode\":" + jsonQuote(mode);
	if (!note.empty()) body += ",\"note\":" + jsonQuote(note);
	socialQueue("send_invite", "POST", "/v1/invites", body + "}");
	return true;
}

bool OnlineLobbyClient::acceptInvite(const std::string &id)
{
	if (!socialReady("accept_invite")) return false;
	if (!validId(id) || !lowerHex(m_options.profileDigest, 32)) { m_lastSocialError = "bad_field"; return false; }
	if (inGame()) { m_lastSocialError = "in_game"; return false; }
	socialQueue("accept_invite", "POST", "/v1/invites/" + id + "/accept",
		"{\"v\":1," + joinIdentityFields() + "}");
	return true;
}

bool OnlineLobbyClient::acceptInviteAfterLeave(const std::string &id)
{
	if (!socialReady("accept_invite")) return false;
	if (!validId(id) || !lowerHex(m_options.profileDigest, 32)) { m_lastSocialError = "bad_field"; return false; }
	if (!m_pendingSocialAcceptInvite.empty()) { m_lastSocialError = "busy"; return false; }
	if (!inGame()) return acceptInvite(id);
	m_pendingSocialAcceptInvite = id;
	leaveGame();
	return true;
}

bool OnlineLobbyClient::declineInvite(const std::string &id, bool busy)
{
	if (!socialReady("decline_invite")) return false;
	if (!validId(id)) { m_lastSocialError = "bad_field"; return false; }
	std::string body = "{\"v\":1";
	if (busy) body += ",\"busy\":true";
	socialQueue("decline_invite", "POST", "/v1/invites/" + id + "/decline", body + "}");
	return true;
}

bool OnlineLobbyClient::fetchRecent()
{
	if (!socialReady("fetch_recent")) return false;
	socialQueue("recent", "GET", "/v1/recent");
	return true;
}

bool OnlineLobbyClient::fetchThreads(const std::string &after)
{
	if (!socialReady("fetch_threads")) return false;
	if (!after.empty() && !validId(after)) { m_lastSocialError = "bad_field"; return false; }
	std::string path = "/v1/chat?limit=50";
	if (!after.empty()) path += "&after=" + urlEncode(after);
	socialQueue("threads", "GET", path);
	return true;
}

bool OnlineLobbyClient::fetchMessages(const std::string &pid, std::uint64_t after)
{
	if (!socialReady("fetch_messages")) return false;
	if (!validId(pid) || after > kSafeInteger) { m_lastSocialError = "bad_field"; return false; }
	socialQueue("messages", "GET", "/v1/chat/" + pid + "/messages?after=" + std::to_string(after) + "&limit=50");
	return true;
}

bool OnlineLobbyClient::sendMessage(const std::string &pid, const std::string &clientId, const std::string &text)
{
	if (!socialReady("send_message")) return false;
	if (!validId(pid) || !lowerHex(clientId, 32) || text.size() > 4000 || !unicodeText(text, 1000, false, true))
	{
		m_lastSocialError = "bad_field";
		return false;
	}
	socialQueue("send_message", "POST", "/v1/chat/" + pid + "/messages",
		"{\"v\":1,\"client_id\":" + jsonQuote(clientId) + ",\"text\":" + jsonQuote(text) + "}");
	return true;
}

bool OnlineLobbyClient::markRead(const std::string &pid, std::uint64_t through)
{
	if (!socialReady("mark_read")) return false;
	if (!validId(pid) || through > kSafeInteger) { m_lastSocialError = "bad_field"; return false; }
	socialQueue("mark_read", "POST", "/v1/chat/" + pid + "/read",
		"{\"v\":1,\"through\":" + std::to_string(through) + "}");
	return true;
}

bool OnlineLobbyClient::generateRecovery()
{
	if (!socialReady("generate_recovery")) return false;
	// ONLINEMENU-1 (PR #8 review finding 2): the lobby makes a recovery code only for a request that names the install the session was issued to
	if (!lowerHex(m_options.installId, 32)) { m_lastSocialError = "install_required"; return false; }
	socialQueue("recovery", "POST", "/v1/me/recovery", "{\"v\":1,\"install\":\"" + m_options.installId + "\"}");
	return true;
}

void OnlineLobbyClient::handleSocial(const Pending &p, const JsonValue &root)
{
	const std::string kind = p.kind.substr(std::string("social_").size());
	auto protocol = [&]() { socialRefuse(p.kind, "protocol"); };
	auto changed = [&]() {
		SocialEvent event;
		event.kind = SocialEvent::Changed;
		m_socialEvents.push_back(event);
	};
	if (kind == "me" || kind == "privacy")
	{
		Me me;
		if (!parseMe(root, me)) { protocol(); return; }
		m_social.me = std::move(me);
		changed();
		return;
	}
	if (kind == "friends")
	{
		const JsonValue *array = root.get("friends");
		if (!array || !array->isArray() || array->array.size() > 200) { protocol(); return; }
		std::vector<Player> friends;
		std::vector<Presence> presence;
		std::set<std::string> ids;
		for (const JsonValue &entry : array->array)
		{
			Player player;
			Presence pstate;
			if (!parsePlayer(entry, player) || !numberOf(entry.get("since"), kSafeInteger, pstate.since) ||
				!entry.get("presence") || !parsePresence(*entry.get("presence"), pstate) ||
				pstate.pid != player.pid || !ids.insert(player.pid).second)
			{
				protocol();
				return;
			}
			friends.push_back(player);
			presence.push_back(pstate);
		}
		m_social.friends = std::move(friends);
		m_social.presence = std::move(presence);
		changed();
		return;
	}
	if (kind == "recent")
	{
		const JsonValue *games = root.get("games");
		if (!games || !games->isArray() || games->array.size() > 20) { protocol(); return; }
		std::vector<RecentGame> recent;
		for (const JsonValue &entry : games->array)
		{
			RecentGame game;
			const JsonValue *players = entry.get("players");
			game.game = stringOf(entry.get("game"));
			if (!entry.isObject() || !lowerHex(game.game, 16) || !numberOf(entry.get("at"), kSafeInteger, game.at) ||
				!players || !players->isArray() || players->array.size() > 7)
			{
				protocol();
				return;
			}
			std::set<std::string> ids;
			for (const JsonValue &item : players->array)
			{
				RecentPlayer player;
				const JsonValue *target = item.get("target");
				if (!parsePlayer(item, player.player) || !target || !target->isString() || target->string.empty() ||
					target->string.size() > 128 || !ids.insert(player.player.pid).second)
				{
					protocol();
					return;
				}
				player.target = target->string;
				game.players.push_back(std::move(player));
			}
			recent.push_back(std::move(game));
		}
		m_social.recent = std::move(recent);
		changed();
		return;
	}
	if (kind == "inbox")
	{
		const std::string seq = stringOf(root.get("seq"));
		const JsonValue *reset = root.get("reset"), *presence = root.get("presence"), *requests = root.get("requests"),
			*invites = root.get("invites"), *notices = root.get("notices"), *heads = root.get("chat"), *backoff = root.get("backoff");
		const std::size_t colon = seq.find(':');
		if (!root.isObject() || seq.size() > 80 || colon != 16 || !lowerHex(seq.substr(0, colon), 16) ||
			seq.find(':', colon + 1) != std::string::npos || seq.size() <= colon + 1 ||
			seq.substr(colon + 1).find_first_not_of("0123456789") != std::string::npos ||
			!reset || reset->type != JsonValue::BOOL ||
			!presence || !presence->isArray() || presence->array.size() > 200 ||
			!requests || !requests->isArray() || requests->array.size() > 250 ||
			!invites || !invites->isArray() || invites->array.size() > 200 ||
			!notices || !notices->isArray() || notices->array.size() > 50 ||
			!heads || !heads->isArray() || heads->array.size() > 200)
		{
			protocol();
			return;
		}
		std::uint64_t revision = 0;
		try
		{
			const std::string digits = seq.substr(colon + 1);
			if (digits.size() > 16) { protocol(); return; }
			revision = std::stoull(digits);
			if (revision > kSafeInteger) { protocol(); return; }
		}
		catch (...) { protocol(); return; }
		(void)revision;
		std::vector<Presence> parsedPresence;
		std::vector<Request> parsedRequests;
		std::vector<Invite> parsedInvites;
		std::vector<Notice> parsedNotices;
		std::vector<ChatHead> parsedHeads;
		std::set<std::string> ids;
		for (const JsonValue &v : presence->array)
		{
			Presence item;
			if (!parsePresence(v, item) || !ids.insert(item.pid).second) { protocol(); return; }
			parsedPresence.push_back(std::move(item));
		}
		ids.clear();
		for (const JsonValue &v : requests->array)
		{
			Request item;
			if (!parseRequest(v, item) || !ids.insert(item.id).second) { protocol(); return; }
			parsedRequests.push_back(std::move(item));
		}
		ids.clear();
		for (const JsonValue &v : invites->array)
		{
			Invite item;
			if (!parseInvite(v, item) || !ids.insert(item.id).second) { protocol(); return; }
			parsedInvites.push_back(std::move(item));
		}
		ids.clear();
		for (const JsonValue &v : notices->array)
		{
			Notice item;
			const JsonValue *invite = v.get("invite");
			item.id = stringOf(v.get("id"));
			item.kind = stringOf(v.get("kind"));
			if (!v.isObject() || !validId(item.id) ||
				(item.kind != "declined" && item.kind != "busy" && item.kind != "expired" && item.kind != "recovered") ||
				!invite || (invite->type != JsonValue::NUL && (!invite->isString() || !validId(invite->string))) ||
				!numberOf(v.get("at"), kSafeInteger, item.at) || !ids.insert(item.id).second)
			{
				protocol();
				return;
			}
			item.hasInvite = invite->isString();
			if (item.hasInvite) item.invite = invite->string;
			parsedNotices.push_back(std::move(item));
		}
		ids.clear();
		for (const JsonValue &v : heads->array)
		{
			ChatHead item;
			item.pid = stringOf(v.get("pid"));
			if (!v.isObject() || !lowerHex(item.pid, 16) ||
				!numberOf(v.get("last_id"), kSafeInteger, item.lastId) ||
				!numberOf(v.get("read_id"), kSafeInteger, item.readId) ||
				item.readId > item.lastId || !ids.insert(item.pid).second)
			{
				protocol();
				return;
			}
			parsedHeads.push_back(std::move(item));
		}
		SocialStats stats;
		if (!parseStats(root.get("stats"), stats)) { protocol(); return; }
		std::uint64_t backoffS = 0;
		if (backoff && !numberOf(backoff, 60, backoffS)) { protocol(); return; }
		const std::vector<std::string> oldMembers = [&]() {
			std::vector<std::string> v;
			for (const Presence &pstate : m_social.presence) v.push_back(pstate.pid);
			std::sort(v.begin(), v.end());
			return v;
		}();
		std::vector<std::string> newMembers;
		for (const Presence &pstate : parsedPresence) newMembers.push_back(pstate.pid);
		std::sort(newMembers.begin(), newMembers.end());
		const bool membershipChanged = oldMembers != newMembers;
		m_social.presence = std::move(parsedPresence);
		m_social.requests = std::move(parsedRequests);
		m_social.invites = std::move(parsedInvites);
		m_social.notices = std::move(parsedNotices);
		m_social.chatHeads = std::move(parsedHeads);
		m_social.stats = stats;
		m_social.seq = seq;
		for (const Notice &notice : m_social.notices)
		{
			if (m_seenSocialNotices.insert(notice.id).second)
			{
				SocialEvent event;
				event.kind = SocialEvent::NoticeEvent;
				event.id = notice.id;
				event.code = notice.kind;
				m_socialEvents.push_back(event);
				if (m_seenSocialNotices.size() > 1000) m_seenSocialNotices.erase(m_seenSocialNotices.begin());
			}
		}
		if (backoffS)
		{
			m_socialBackoffUntil = std::max(m_socialBackoffUntil, m_now + backoffS * 1000u);
			m_socialNextPoll = std::max(m_socialNextPoll, m_socialBackoffUntil);
		}
		if (m_socialFirstInbox || membershipChanged)
		{
			socialQueue("friends", "GET", "/v1/friends");
		}
		m_socialFirstInbox = false;
		changed();
		return;
	}
	if (kind == "blocks")
	{
		const JsonValue *array = root.get("blocks"), *next = root.get("next");
		if (!array || !array->isArray() || array->array.size() > 50 || !next ||
			(next->type != JsonValue::NUL && (!next->isString() || !validId(next->string))))
		{
			protocol();
			return;
		}
		std::vector<Block> page;
		for (const JsonValue &v : array->array)
		{
			Block item;
			const JsonValue *note = v.get("note");
			if (!parsePlayer(v, item.player) || !note || !note->isString() || !validNote(note->string) ||
				!numberOf(v.get("at"), kSafeInteger, item.at))
			{
				protocol();
				return;
			}
			item.note = note->string;
			page.push_back(std::move(item));
		}
		const bool append = p.request.path.find("?limit=50&after=") != std::string::npos;
		if (!append) m_social.blocks.clear();
		for (Block &item : page)
		{
			auto it = std::find_if(m_social.blocks.begin(), m_social.blocks.end(), [&](const Block &old) { return old.player.pid == item.player.pid; });
			if (it == m_social.blocks.end()) m_social.blocks.push_back(std::move(item)); else *it = std::move(item);
		}
		m_social.hasBlocksNext = next->isString();
		m_social.blocksNext = m_social.hasBlocksNext ? next->string : "";
		changed();
		return;
	}
	if (kind == "threads")
	{
		const JsonValue *array = root.get("threads"), *next = root.get("next");
		if (!array || !array->isArray() || array->array.size() > 50 || !next ||
			(next->type != JsonValue::NUL && (!next->isString() || !validId(next->string))))
		{
			protocol();
			return;
		}
		std::vector<Thread> page;
		std::set<std::string> ids;
		for (const JsonValue &v : array->array)
		{
			Thread item;
			Player player;
			if (!parsePlayer(v, player) || !numberOf(v.get("last_id"), kSafeInteger, item.lastId) ||
				!numberOf(v.get("read_id"), kSafeInteger, item.readId) || item.readId > item.lastId ||
				!ids.insert(player.pid).second)
			{
				protocol();
				return;
			}
			item.pid = player.pid;
			item.code = player.code;
			item.nick = player.nick;
			page.push_back(std::move(item));
		}
		const bool append = p.request.path.find("?limit=50&after=") != std::string::npos;
		if (!append) m_social.threads.clear();
		for (Thread &item : page)
		{
			auto it = std::find_if(m_social.threads.begin(), m_social.threads.end(), [&](const Thread &old) { return old.pid == item.pid; });
			if (it == m_social.threads.end()) m_social.threads.push_back(std::move(item)); else *it = std::move(item);
		}
		m_social.hasThreadsNext = next->isString();
		m_social.threadsNext = m_social.hasThreadsNext ? next->string : "";
		changed();
		return;
	}
	if (kind == "messages")
	{
		const JsonValue *array = root.get("messages"), *next = root.get("next");
		const std::string peer = p.request.path.substr(std::string("/v1/chat/").size(), 16);
		std::uint64_t after = 0, last = 0, read = 0, nextId = 0;
		if (!validId(peer) || !array || !array->isArray() || array->array.size() > 50 || !next ||
			!queryNumber(p.request.path, "after", kSafeInteger, after) ||
			(next->type != JsonValue::NUL && !numberOf(next, kSafeInteger, nextId)) ||
			!numberOf(root.get("last_id"), kSafeInteger, last) || !numberOf(root.get("read_id"), kSafeInteger, read) || read > last)
		{
			protocol();
			return;
		}
		std::vector<Message> page;
		std::uint64_t previous = after;
		for (const JsonValue &v : array->array)
		{
			Message msg;
			const JsonValue *text = v.get("text");
			msg.from = stringOf(v.get("from"));
			msg.to = stringOf(v.get("to"));
			msg.clientId = stringOf(v.get("client_id"));
			if (text && text->isString()) msg.text = text->string;
			if (!v.isObject() || !numberOf(v.get("id"), kSafeInteger, msg.id) || msg.id == 0 ||
				!numberOf(v.get("at"), kSafeInteger, msg.at) || !validId(msg.from) || !validId(msg.to) ||
				!text || !text->isString() || !lowerHex(msg.clientId, 32) || msg.text.size() > 4000 ||
				!unicodeText(msg.text, 1000, false, true) ||
				!((msg.from == m_social.me.pid && msg.to == peer) || (msg.from == peer && msg.to == m_social.me.pid)) ||
				msg.id <= previous || msg.id > last)
			{
				protocol();
				return;
			}
			previous = msg.id;
			page.push_back(std::move(msg));
		}
		if (next->isNumber() &&
			(page.empty() || nextId <= after || nextId != page.back().id || nextId > last))
		{
			protocol();
			return;
		}
		Conversation conv = m_social.conversations[peer];
		for (const Message &msg : page)
		{
			if (std::none_of(conv.messages.begin(), conv.messages.end(), [&](const Message &old) { return old.id == msg.id; }))
			{
				conv.messages.push_back(msg);
			}
		}
		std::sort(conv.messages.begin(), conv.messages.end(), [](const Message &a, const Message &b) { return a.id < b.id; });
		conv.lastId = std::max(conv.lastId, last);
		conv.readId = std::max(conv.readId, read);
		// A tail refresh must not discard the cursor for an unfetched history gap.
		if (after <= conv.historyThrough && !page.empty())
		{
			conv.historyThrough = std::max(conv.historyThrough, page.back().id);
		}
		conv.hasNext = conv.historyThrough < conv.lastId;
		conv.next = conv.hasNext ? conv.historyThrough : 0;
		m_social.conversations[peer] = std::move(conv);
		SocialEvent event;
		event.kind = SocialEvent::Chat;
		event.pid = peer;
		m_socialEvents.push_back(event);
		changed();
		return;
	}
	if (kind == "accept_invite")
	{
		std::string gameError;
		const std::string id = stringOf(root.get("id"));
		std::uint64_t slot = 0;
		const JsonValue *game = root.get("game");
		OnlineLobbyClient candidate = *this;
		if (!validId(id) || !game || !numberOf(root.get("slot"), 7, slot) ||
			!candidate.readGameView(*game, &gameError, true) || candidate.m_gameId != id || candidate.m_localSlot != (int)slot)
		{
			protocol();
			return;
		}
		m_gameId = candidate.m_gameId;
		m_gameName = candidate.m_gameName;
		m_localSlot = candidate.m_localSlot;
		m_hostSlot = candidate.m_hostSlot;
		m_info = candidate.m_info;
		m_slots = candidate.m_slots;
		m_seq = candidate.m_seq;
		m_chatSince = 0;
		m_startJson.clear();
		m_gamePasswordProtected = false;
		m_nextPoll = 0;
		m_pollQueued = false;
		Event joined{ Event::GameJoined };
		joined.slot = m_localSlot;
		m_events.push_back(joined);
		changed();
		return;
	}
	if (kind == "send_message")
	{
		const JsonValue *message = root.get("message");
		if (!message || !message->isObject()) { protocol(); return; }
		JsonValue submitted;
		std::string error;
		if (!JsonValue::parse(p.request.body, submitted, &error) || !submitted.isObject())
		{
			protocol();
			return;
		}
		const std::string peer = p.request.path.substr(std::string("/v1/chat/").size(), 16);
		const JsonValue *submittedClient = submitted.get("client_id");
		const JsonValue *submittedText = submitted.get("text");
		std::uint64_t version = 0;
		if (!validId(peer) || !numberOf(submitted.get("v"), 1, version) || version != 1 ||
			!submittedClient || !submittedClient->isString() || !lowerHex(submittedClient->string, 32) ||
			!submittedText || !submittedText->isString() || !unicodeText(submittedText->string, 1000, false, true))
		{
			protocol();
			return;
		}
		Message msg;
		const JsonValue *text = message->get("text");
		msg.from = stringOf(message->get("from"));
		msg.to = stringOf(message->get("to"));
		msg.clientId = stringOf(message->get("client_id"));
		if (text && text->isString()) msg.text = text->string;
		if (!numberOf(message->get("id"), kSafeInteger, msg.id) || msg.id == 0 ||
			!numberOf(message->get("at"), kSafeInteger, msg.at) || !validId(msg.from) || !validId(msg.to) ||
			!text || !text->isString() || !lowerHex(msg.clientId, 32) || msg.text.size() > 4000 ||
			!unicodeText(msg.text, 1000, false, true) || msg.from != m_social.me.pid || msg.to != peer ||
			msg.clientId != submittedClient->string || msg.text != submittedText->string)
		{
			protocol();
			return;
		}
		Conversation &conv = m_social.conversations[peer];
		if (std::none_of(conv.messages.begin(), conv.messages.end(), [&](const Message &old) { return old.clientId == msg.clientId; }))
		{
			conv.messages.push_back(msg);
			std::sort(conv.messages.begin(), conv.messages.end(), [](const Message &a, const Message &b) { return a.id < b.id; });
		}
		conv.lastId = std::max(conv.lastId, msg.id);
		SocialEvent event;
		event.kind = SocialEvent::Chat;
		event.pid = peer;
		event.id = msg.clientId;
		event.text = msg.text;
		m_socialEvents.push_back(event);
		changed();
		return;
	}
	if (kind == "recovery")
	{
		const std::string code = stringOf(root.get("code"));
		if (code.size() != 16 || code.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZ") != std::string::npos)
		{
			protocol();
			return;
		}
		m_recoveryCode = code;
		SocialEvent event;
		event.kind = SocialEvent::Recovery;
		m_socialEvents.push_back(event);
		changed();
		return;
	}
	if (kind == "mark_read")
	{
		const std::string peer = p.request.path.substr(std::string("/v1/chat/").size(), 16);
		JsonValue submitted;
		std::string error;
		std::uint64_t through = 0, read = 0;
		if (!validId(peer) || !JsonValue::parse(p.request.body, submitted, &error) || !submitted.isObject() ||
			!numberOf(submitted.get("through"), kSafeInteger, through) ||
			!numberOf(root.get("read_id"), kSafeInteger, read) || read < through)
		{
			protocol();
			return;
		}
		Conversation &conv = m_social.conversations[peer];
		conv.readId = std::max(conv.readId, read);
		conv.lastId = std::max(conv.lastId, read);
		conv.hasNext = conv.historyThrough < conv.lastId;
		conv.next = conv.hasNext ? conv.historyThrough : 0;
		changed();
		return;
	}
	if (kind == "send_invite" || kind == "friend_request_code" || kind == "friend_request_pid")
	{
		const char *field = kind == "send_invite" ? "invite" : "request";
		const JsonValue *value = root.get(field);
		if (!value || !value->isObject()) { protocol(); return; }
		if (kind == "send_invite")
		{
			Invite item;
			if (!parseInvite(*value, item)) { protocol(); return; }
		}
		else
		{
			Request item;
			if (!parseRequest(*value, item)) { protocol(); return; }
		}
	}
	else if (kind == "privacy")
	{
		Me me;
		if (!parseMe(root, me)) { protocol(); return; }
		m_social.me = std::move(me);
	}
	else if (kind == "presence")
	{
		const JsonValue *ok = root.get("ok");
		if (!ok || ok->type != JsonValue::BOOL || !ok->boolean) { protocol(); return; }
		JsonValue submitted;
		std::string error;
		if (!JsonValue::parse(p.request.body, submitted, &error) || !submitted.isObject())
		{
			protocol();
			return;
		}
		const JsonValue *hidden = submitted.get("hidden");
		if (hidden)
		{
			if (hidden->type != JsonValue::BOOL)
			{
				protocol();
				return;
			}
			m_social.me.hidden = hidden->boolean;
		}
	}
	else
	{
		const JsonValue *ok = root.get("ok");
		if (!ok || ok->type != JsonValue::BOOL || !ok->boolean) { protocol(); return; }
	}
	if (kind == "accept_friend" || kind == "remove_friend")
	{
		socialQueue("friends", "GET", "/v1/friends");
	}
	if (kind == "block_player" || kind == "unblock_player")
	{
		fetchBlocks();
	}
	if (kind != "messages" && kind != "threads" && kind != "blocks" && kind != "recent" && kind != "me" && kind != "friends")
	{
		m_socialDue = true;
		m_socialNextPoll = m_now;
	}
	changed();
}
