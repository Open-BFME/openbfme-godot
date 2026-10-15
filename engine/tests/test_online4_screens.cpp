// OpenBFME unit tests. GPL-3.0.
// Lane ONLINE-4 (docs/ONLINE.md section 5, phase O2): RotWK's online screens (OnlineShell.apt with OnlineLogin / OnlineHome / OnlineOpenPlay and the
// MpGameSetup staging room, GameClient/GUI/AptScreens/AptOnlineShell.h) on the OpenBFME lobby service, through the lobby client
// (GameNetwork/OnlineLobbyClient.h), against a real local lobby: services/online/lobby/src/server.mjs run by Node.js (and, for the START, a real
// openbfme_relay and two openbfme_peer processes). The tests SKIP LOUDLY where Node.js, the retail installs or POSIX processes are missing.

#include "doctest.h"

#include "GameClient/AptView3D.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/Transport.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include "StartShellFx.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#define ONLINE4_POSIX 1
#endif

namespace
{
OnlineLobbyClient::Options clientOptions(int factions = 16, int colors = 16)
{
	OnlineLobbyClient::Options o;
	o.profileDigest = "0123456789abcdef0123456789abcdef";
	o.playerTemplateCount = factions;
	o.colorCount = colors;
	o.pollMs = 100;
	return o;
}

#ifdef ONLINE4_POSIX
// ---- a plain HTTP/1.1 client for the local lobby (the game's own transport is Godot's HTTPRequest: scripts/online_http.gd) ----
struct HttpAnswer
{
	int status = 0;
	std::string body;
	std::string error;
};

HttpAnswer httpRequest(int port, const OnlineLobbyClient::HttpRequest &r)
{
	HttpAnswer a;
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		a.error = "socket";
		return a;
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((std::uint16_t)port);
	addr.sin_addr.s_addr = htonl(0x7F000001u);
	if (connect(fd, (sockaddr *)&addr, sizeof(addr)) != 0)
	{
		close(fd);
		a.error = "connection refused";
		return a;
	}
	std::string req = r.method + " " + r.path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Type: application/json\r\n";
	if (!r.token.empty())
	{
		req += "Authorization: Bearer " + r.token + "\r\n";
	}
	req += "Content-Length: " + std::to_string(r.body.size()) + "\r\n\r\n" + r.body;
	size_t sent = 0;
	while (sent < req.size())
	{
		const ssize_t n = send(fd, req.data() + sent, req.size() - sent, 0);
		if (n <= 0)
		{
			break;
		}
		sent += (size_t)n;
	}
	std::string all;
	char buf[4096];
	for (;;)
	{
		const ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n <= 0)
		{
			break;
		}
		all.append(buf, (size_t)n);
	}
	close(fd);
	const size_t sp = all.find(' ');
	const size_t hdrEnd = all.find("\r\n\r\n");
	if (sp == std::string::npos || hdrEnd == std::string::npos)
	{
		a.error = "no HTTP answer";
		return a;
	}
	a.status = std::atoi(all.c_str() + sp + 1);
	const std::string headers = all.substr(0, hdrEnd);
	std::string body = all.substr(hdrEnd + 4);
	std::string lower = headers;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	if (lower.find("transfer-encoding: chunked") != std::string::npos)
	{
		std::string out;
		size_t at = 0;
		for (;;)
		{
			const size_t eol = body.find("\r\n", at);
			if (eol == std::string::npos)
			{
				break;
			}
			const size_t len = std::strtoul(body.substr(at, eol - at).c_str(), nullptr, 16);
			if (len == 0)
			{
				break;
			}
			out += body.substr(eol + 2, len);
			at = eol + 2 + len + 2;
		}
		body = out;
	}
	a.body = body;
	return a;
}

// a test's change to a lobby answer before the client sees it (Sol r1: hostile replies): the request, and the answer to rewrite
using Tamper = std::function<void(const OnlineLobbyClient::HttpRequest &, HttpAnswer &)>;

// one round of the client's requests (at most one in flight: sent and answered at once)
void pumpHttp(OnlineLobbyClient &c, int port, const Tamper &tamper = nullptr)
{
	for (int i = 0; i < 16; ++i)
	{
		const std::vector<OnlineLobbyClient::HttpRequest> rs = c.takeRequests();
		if (rs.empty())
		{
			return;
		}
		HttpAnswer a = httpRequest(port, rs[0]);
		if (tamper)
		{
			tamper(rs[0], a);
		}
		c.deliver(rs[0].id, a.status, a.body, a.error);
	}
}

int freeTcpPort()
{
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(0x7F000001u);
	bind(fd, (sockaddr *)&addr, sizeof(addr));
	socklen_t len = sizeof(addr);
	getsockname(fd, (sockaddr *)&addr, &len);
	const int port = ntohs(addr.sin_port);
	close(fd);
	return port;
}

std::string lobbyServerPath()
{
	if (const char *e = std::getenv("OPENBFME_LOBBY_SERVER"))
	{
		return e;
	}
#ifdef OPENBFME_LOBBY_SERVER
	return OPENBFME_LOBBY_SERVER;
#else
	return "";
#endif
}

bool haveNode()
{
	return std::system("node --version > /dev/null 2>&1") == 0 && std::filesystem::exists(lobbyServerPath());
}

const char *kTicketKeyHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

// a local lobby: node server.mjs on a free port, with or without a relay
struct NodeLobby
{
	pid_t pid = -1;
	int port = 0;
	std::filesystem::path dir;
	NodeLobby(const std::string &relays, const std::string &motd)
	{
		dir = std::filesystem::temp_directory_path() / ("openbfme-online4-" + std::to_string(getpid()) + "-" + std::to_string(NetMilliseconds()));
		std::filesystem::create_directories(dir / "data");
		port = freeTcpPort();
		const std::string bind = "127.0.0.1:" + std::to_string(port);
		const std::string data = (dir / "data").string();
		const std::string keys = std::string("1:") + kTicketKeyHex + "\n";
		const std::string server = lobbyServerPath();
		std::fflush(nullptr); // the child must not write the parent's buffered output again
		pid = fork();
		if (pid == 0)
		{
			setenv("LOBBY_BIND", bind.c_str(), 1);
			setenv("LOBBY_DATA", data.c_str(), 1);
			setenv("TICKET_KEYS", keys.c_str(), 1);
			setenv("SESSION_KEY", "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf", 1);
			setenv("RELAYS", relays.c_str(), 1);
			if (!motd.empty())
			{
				setenv("MOTD", motd.c_str(), 1);
			}
			const std::string log = (dir / "lobby.log").string();
			freopen(log.c_str(), "w", stdout);
			freopen(log.c_str(), "a", stderr);
			execlp("node", "node", server.c_str(), (char *)nullptr);
			_exit(127);
		}
		// up when /v1/health answers
		for (int i = 0; i < 100; ++i)
		{
			OnlineLobbyClient::HttpRequest h;
			h.method = "GET";
			h.path = "/v1/health";
			if (httpRequest(port, h).status == 200)
			{
				return;
			}
			NetSleepMilliseconds(50);
		}
	}
	~NodeLobby()
	{
		if (pid > 0)
		{
			kill(pid, SIGTERM);
			int st = 0;
			waitpid(pid, &st, 0);
		}
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
	std::string log() const
	{
		std::ifstream f(dir / "lobby.log");
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}
};

std::vector<OnlineLobbyClient::Event> drive(OnlineLobbyClient &c, int port, int rounds = 3)
{
	std::vector<OnlineLobbyClient::Event> all;
	for (int i = 0; i < rounds; ++i)
	{
		c.update(NetMilliseconds());
		pumpHttp(c, port);
		for (const auto &e : c.takeEvents())
		{
			all.push_back(e);
		}
	}
	return all;
}

bool hasEvent(const std::vector<OnlineLobbyClient::Event> &es, OnlineLobbyClient::Event::Kind k, const std::string &code = std::string())
{
	return std::any_of(es.begin(), es.end(), [&](const OnlineLobbyClient::Event &e) { return e.kind == k && (code.empty() || e.code == code); });
}

const OnlineLobbyClient::Event *findEvent(const std::vector<OnlineLobbyClient::Event> &es, OnlineLobbyClient::Event::Kind k, const std::string &request)
{
	for (const auto &e : es)
	{
		if (e.kind == k && e.request == request)
		{
			return &e;
		}
	}
	return nullptr;
}
#endif

#define ONLINE4_REQUIRE_NODE()                                                                                                                      \
	if (!haveNode())                                                                                                                                \
	{                                                                                                                                               \
		MESSAGE("SKIP LOUDLY: Node.js or services/online/lobby/src/server.mjs is missing here (set OPENBFME_LOBBY_SERVER), so the online lobby " \
				"tests did not run");                                                                                                               \
		return;                                                                                                                                     \
	}
} // namespace

TEST_CASE("online4: the lobby client's requests and its checks of the answers (no lobby needed)")
{
	OnlineLobbyClient c(clientOptions(4, 4));
	c.signInGuest(u"Güst \"1\"");
	c.refreshList();
	std::vector<OnlineLobbyClient::HttpRequest> rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].method == "POST");
	CHECK(rs[0].path == "/v1/auth/guest");
	CHECK(rs[0].body == "{\"name\":\"G\xc3\xbcst \\\"1\\\"\"}"); // UTF-8, quotes escaped
	CHECK(c.takeRequests().empty());                              // one request in flight
	// a transport error reaches an event, never a silent default
	c.deliver(rs[0].id, 0, "", "connection refused");
	std::vector<OnlineLobbyClient::Event> es = c.takeEvents();
	REQUIRE(es.size() == 1);
	CHECK(es[0].kind == OnlineLobbyClient::Event::Refused);
	CHECK(es[0].request == "signin");
	CHECK(es[0].code == "transport");
	CHECK_FALSE(c.signedIn());
	// the list: an answer that is not lobby version 1 is refused
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].path == "/v1/games");
	c.deliver(rs[0].id, 200, "{\"games\":[]}", "");
	es = c.takeEvents();
	REQUIRE(es.size() == 1);
	CHECK(es[0].code == "protocol");
	// a refused request carries the lobby's code and message
	c.signInGuest(u"x");
	rs = c.takeRequests();
	c.deliver(rs[0].id, 429, "{\"v\":1,\"error\":\"rate_limited\",\"message\":\"too many requests: wait a moment\"}", "");
	es = c.takeEvents();
	REQUIRE(es.size() == 1);
	CHECK(es[0].code == "rate_limited");
	CHECK(es[0].text == u"too many requests: wait a moment");
	// a game view with a faction beyond the PlayerTemplate store is refused (the client was given 4 factions)
	c.signInGuest(u"x");
	rs = c.takeRequests();
	c.deliver(rs[0].id, 200, "{\"v\":1,\"token\":\"t.t\",\"account\":{\"id\":\"guest:1\",\"name\":\"x\",\"kind\":\"guest\"}}", "");
	es = c.takeEvents();
	REQUIRE(es.size() == 1);
	CHECK(es[0].kind == OnlineLobbyClient::Event::SignedIn);
	c.joinGame("0123456789abcdef", "");
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].token == "t.t");
	CHECK(rs[0].body == "{\"profile\":\"0123456789abcdef0123456789abcdef\"}");
	std::string slots;
	for (int i = 0; i < 8; ++i)
	{
		slots += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i) + ",\"state\":\"" + (i < 2 ? "player" : "closed") +
			"\",\"name\":\"p\",\"faction\":" + (i == 1 ? "9" : "-1") + ",\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":false,\"you\":" +
			(i == 1 ? "true" : "false") + "}";
	}
	const std::string view = "{\"id\":\"0123456789abcdef\",\"name\":\"g\",\"state\":\"open\",\"hostSlot\":0,\"profile\":\"x\",\"setup\":{\"map\":\"m\","
							 "\"startingCash\":1500,\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null},\"slots\":[" +
		slots + "],\"seq\":3}";
	c.deliver(rs[0].id, 200, "{\"v\":1,\"slot\":1,\"game\":" + view + "}", "");
	es = c.takeEvents();
	REQUIRE(es.size() == 1);
	CHECK(es[0].kind == OnlineLobbyClient::Event::Refused);
	CHECK(es[0].code == "protocol");
	CHECK(OnlineLobbyClient::utf16ToUtf8(es[0].text).find("faction / color / startPos / team out of range (4 factions") != std::string::npos);
	CHECK_FALSE(c.inGame());
}

TEST_CASE("online4: hostile lobby answers are bounded and refused, and credentials are redacted (Sol r1; no lobby needed)")
{
	using C = OnlineLobbyClient;
	const std::string ticket = "AQEjRWeJq83vASNFZ4mrze8AAQIDBAUGBwgJCgsMDQ4";                         // a relay ticket: 32 bytes of base64url
	const std::string rejoinTicket = "AgEjRWeJq83vASNFZ4mrze8AAQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA"; // a rejoin ticket (version 2)
	const std::string session = "eyJpZCI6Imd1ZXN0OjEyMzQ1Njc4IiwibmFtZSI6IlJvaGFuIiwia2luZCI6Imd1ZXN0In0.qL5x0v9a4R2mTzQe7cJb1nWs8YkHd3FgUo6PiXaZtVw";
	// ---- redact: tickets and session tokens are masked; the lobby's ids, the profile digest, map keys and words stay ----
	const std::string start = "{\"v\":1,\"kind\":\"openbfme-start\",\"game\":\"0123456789abcdef\",\"slot\":1,\"relay\":\"127.0.0.1:7350\",\"ticket\":\"" + ticket +
		"\",\"profile\":\"0123456789abcdef0123456789abcdef\"}";
	CHECK(C::redact(start) == "{\"v\":1,\"kind\":\"openbfme-start\",\"game\":\"0123456789abcdef\",\"slot\":1,\"relay\":\"127.0.0.1:7350\",\"ticket\":\"<redacted>\","
							  "\"profile\":\"0123456789abcdef0123456789abcdef\"}");
	CHECK(C::redact("rejoin " + rejoinTicket + " with Bearer " + session + ".") == "rejoin <redacted> with Bearer <redacted>.");
	CHECK(C::redact("maps/map mp fall back 4p/map mp fall back 4p.map: Rohan's game") == "maps/map mp fall back 4p/map mp fall back 4p.map: Rohan's game");

	// ---- a refusal that echoes credentials: shown and noted without them (also this client's own token, whatever its shape) ----
	C c(clientOptions(4, 4));
	auto only = [&](C::Event::Kind kind) {
		std::vector<C::Event> es = c.takeEvents();
		REQUIRE(es.size() == 1);
		CHECK(es[0].kind == kind);
		return es[0];
	};
	auto refusalText = [&] {
		const C::Event e = only(C::Event::Refused);
		return C::utf16ToUtf8(e.text);
	};
	auto answer = [&](int status, const std::string &body) {
		std::vector<C::HttpRequest> rs = c.takeRequests();
		REQUIRE(rs.size() == 1);
		c.deliver(rs[0].id, status, body, "");
	};
	c.signInGuest(u"Rohan");
	answer(200, "{\"v\":1,\"token\":\"" + session + "\",\"account\":{\"id\":\"guest:1\",\"name\":\"Rohan\",\"kind\":\"guest\"}}");
	only(C::Event::SignedIn);
	c.refreshList();
	answer(400, "{\"v\":1,\"error\":\"bad_field\",\"message\":\"your token " + session + " and ticket " + ticket + " were refused\"}");
	CHECK(refusalText() == "your token <redacted> and ticket <redacted> were refused");
	{
		C shortToken(clientOptions(4, 4));
		shortToken.signInGuest(u"x");
		std::vector<C::HttpRequest> rs = shortToken.takeRequests();
		shortToken.deliver(rs[0].id, 200, "{\"v\":1,\"token\":\"tok-123456\",\"account\":{\"id\":\"guest:2\",\"name\":\"x\",\"kind\":\"guest\"}}", "");
		shortToken.takeEvents();
		shortToken.fetchMine();
		rs = shortToken.takeRequests();
		shortToken.deliver(rs[0].id, 401, "{\"v\":1,\"error\":\"auth\",\"message\":\"bad token tok-123456\"}", "");
		std::vector<C::Event> es = shortToken.takeEvents();
		REQUIRE(es.size() == 1);
		CHECK(es[0].text == u"bad token <redacted>");
	}
	// a refusal's message beyond its bound is not shown
	c.refreshList();
	answer(400, "{\"v\":1,\"error\":\"bad_field\",\"message\":\"" + std::string(C::kMaxRefusalBytes + 1, 'm') + "\"}");
	CHECK(refusalText() == "the lobby's refusal is longer than 512 bytes");

	// ---- the body: an answer beyond kMaxBodyBytes is refused before it is read (Sol's case: a 1 MiB message of the day) ----
	c.fetchMotd();
	answer(200, "{\"v\":1,\"motd\":\"" + std::string(1024 * 1024, 'x') + "\"}");
	CHECK(refusalText() == "the lobby's answer is larger than 262144 bytes");
	CHECK_FALSE(c.motdKnown());
	c.refreshList(); // a valid list padded beyond the bound: refused for its size alone
	answer(200, "{\"v\":1,\"games\":[]}" + std::string(C::kMaxBodyBytes, ' '));
	CHECK(refusalText() == "the lobby's answer is larger than 262144 bytes");
	// ---- the message of the day: the lobby's 2000 bytes are shown, more are refused ----
	c.fetchMotd();
	answer(200, "{\"v\":1,\"motd\":\"" + std::string(C::kMaxMotdBytes + 1, 'x') + "\"}");
	CHECK(refusalText() == "the message of the day is longer than 2000 bytes");
	CHECK_FALSE(c.motdKnown());
	c.fetchMotd();
	answer(200, "{\"v\":1,\"motd\":\"" + std::string(C::kMaxMotdBytes, 'x') + "\"}");
	only(C::Event::Motd);
	CHECK(c.motd().size() == C::kMaxMotdBytes);
	// ---- the game list: 200 games (the lobby's most), not 201; a name, host or map beyond the lobby's limits ----
	auto listing = [](int i, const std::string &name, const std::string &host, const std::string &map) {
		char id[17];
		std::snprintf(id, sizeof(id), "%016x", i);
		return "{\"id\":\"" + std::string(id) + "\",\"name\":\"" + name + "\",\"host\":\"" + host + "\",\"map\":\"" + map +
			"\",\"players\":1,\"openSlots\":1,\"password\":false,\"state\":\"open\"}";
	};
	auto list = [&](size_t n) {
		std::string games;
		for (size_t i = 0; i < n; ++i)
		{
			games += (i ? "," : "") + listing((int)i, "g", "h", "maps/m/m.map");
		}
		return "{\"v\":1,\"games\":[" + games + "]}";
	};
	c.refreshList();
	answer(200, list(C::kMaxGames + 1));
	CHECK(refusalText() == "the lobby listed more than 200 games");
	CHECK(c.games().empty());
	c.refreshList();
	answer(200, list(C::kMaxGames));
	only(C::Event::GameList);
	CHECK(c.games().size() == C::kMaxGames);
	for (const std::string &bad : { listing(1, std::string(C::kMaxGameNameBytes + 1, 'n'), "h", "m"), listing(1, "g", std::string(C::kMaxPlayerNameBytes + 1, 'h'), "m"),
			 listing(1, "g", "h", std::string(C::kMaxMapBytes + 1, 'm')) })
	{
		c.refreshList();
		answer(200, "{\"v\":1,\"games\":[" + bad + "]}");
		CHECK(only(C::Event::Refused).code == "protocol");
		CHECK(c.games().size() == C::kMaxGames); // the last good list stays
	}
	// ---- the sign-in: a token or name beyond its bound ----
	{
		C d(clientOptions(4, 4));
		d.signInGuest(u"x");
		std::vector<C::HttpRequest> rs = d.takeRequests();
		d.deliver(rs[0].id, 200, "{\"v\":1,\"token\":\"" + std::string(C::kMaxTokenBytes + 1, 't') + "\",\"account\":{\"id\":\"guest:1\",\"name\":\"x\",\"kind\":\"guest\"}}", "");
		std::vector<C::Event> es = d.takeEvents();
		REQUIRE(es.size() == 1);
		CHECK(es[0].kind == C::Event::Refused);
		CHECK_FALSE(d.signedIn());
	}
	// ---- the staging room: a game view, then polls with too many or too long chat lines, an oversized START, a START dropped ----
	auto view = [](const std::string &name, const std::string &slotName) {
		std::string slots;
		for (int i = 0; i < 8; ++i)
		{
			slots += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i) + ",\"state\":\"" + (i < 2 ? "player" : "closed") + "\",\"name\":\"" +
				(i == 1 ? slotName : std::string("Host")) + "\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":false,\"you\":" +
				(i == 1 ? "true" : "false") + "}";
		}
		return "{\"id\":\"0123456789abcdef\",\"name\":\"" + name + "\",\"state\":\"open\",\"hostSlot\":0,\"profile\":\"x\",\"setup\":{\"map\":\"m\","
			   "\"startingCash\":1500,\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null},\"slots\":[" + slots + "],\"seq\":3}";
	};
	c.joinGame("0123456789abcdef", "");
	answer(200, "{\"v\":1,\"slot\":1,\"game\":" + view("g", std::string(C::kMaxPlayerNameBytes + 1, 'p')) + "}");
	CHECK(only(C::Event::Refused).code == "protocol");
	CHECK_FALSE(c.inGame());
	c.joinGame("0123456789abcdef", "");
	answer(200, "{\"v\":1,\"slot\":1,\"game\":" + view(std::string(C::kMaxGameNameBytes + 1, 'g'), "Rohan") + "}");
	CHECK(only(C::Event::Refused).code == "protocol");
	c.joinGame("0123456789abcdef", "");
	answer(200, "{\"v\":1,\"slot\":1,\"game\":" + view("g", "Rohan") + "}");
	only(C::Event::GameJoined);
	REQUIRE(c.inGame());
	std::uint64_t now = 1000;
	auto poll = [&](const std::string &chat, const std::string &startJson) {
		now += 1000;
		c.update(now);
		answer(200, "{\"v\":1,\"game\":" + view("g", "Rohan") + ",\"chat\":[" + chat + "],\"start\":" + startJson + "}");
		return c.takeEvents();
	};
	auto refusedIn = [](const std::vector<C::Event> &es) {
		return std::any_of(es.begin(), es.end(), [](const C::Event &e) { return e.kind == C::Event::Refused && e.code == "protocol"; });
	};
	std::string lines;
	for (size_t i = 0; i <= C::kMaxChatLines; ++i)
	{
		lines += (i ? "," : "") + std::string("{\"seq\":") + std::to_string(i + 1) + ",\"from\":\"Host\",\"text\":\"hi\"}";
	}
	CHECK(refusedIn(poll(lines, "null")));
	CHECK(refusedIn(poll("{\"seq\":1,\"from\":\"Host\",\"text\":\"" + std::string(C::kMaxChatBytes + 1, 'c') + "\"}", "null")));
	CHECK(refusedIn(poll("{\"seq\":1,\"from\":\"" + std::string(C::kMaxPlayerNameBytes + 1, 'f') + "\",\"text\":\"hi\"}", "null")));
	CHECK(refusedIn(poll("", "{\"v\":1,\"pad\":\"" + std::string(C::kMaxStartBytes, 's') + "\"}")));
	CHECK(c.startJson().empty());
	CHECK(c.inGame());
	// a START within its bound is handed on; dropStart forgets it and the game (no leave is sent: the slot stays the player's, for a rejoin)
	std::vector<C::Event> es = poll("", start);
	CHECK(std::any_of(es.begin(), es.end(), [](const C::Event &e) { return e.kind == C::Event::GameStart; }));
	CHECK_FALSE(c.startJson().empty());
	c.dropStart();
	CHECK(c.startJson().empty());
	CHECK_FALSE(c.inGame());
	CHECK(c.takeRequests().empty());
	CHECK(c.sentCount("leave") == 0);
}

TEST_CASE("online5: the lobby client's held polls (long polling) and the sender's own chat line: asked only of a lobby that announces \"wait\", on a "
		  "connection of their own, never in a tight loop, and hostile answers refused (no lobby needed)")
{
	using C = OnlineLobbyClient;
	OnlineLobbyClient::Options o = clientOptions(4, 4);
	o.pollMs = 1000;
	o.longPollS = 20;
	C c(o);
	std::uint64_t now = 5000;
	auto view = [](int seq, bool ready = false) {
		std::string slots;
		for (int i = 0; i < 8; ++i)
		{
			slots += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i) + ",\"state\":\"" + (i < 2 ? "player" : "open") + "\",\"name\":" +
				(i == 0 ? "\"Host\"" : i == 1 ? "\"Me\"" : "null") + ",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":" +
				(i == 1 && ready ? "true" : "false") + ",\"you\":" + (i == 1 ? "true" : "false") + "}";
		}
		return "{\"id\":\"0123456789abcdef\",\"name\":\"g\",\"state\":\"open\",\"hostSlot\":0,\"profile\":\"x\",\"setup\":{\"map\":\"m\",\"startingCash\":1500,"
			   "\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null},\"slots\":[" + slots + "],\"seq\":" + std::to_string(seq) + "}";
	};
	auto pollBody = [&](int seq, const std::string &chat, const std::string &wait, bool ready = false) {
		return "{\"v\":1,\"game\":" + view(seq, ready) + ",\"chat\":[" + chat + "],\"start\":null" + (wait.empty() ? "" : ",\"wait\":" + wait) + "}";
	};
	auto chatEvents = [](const std::vector<C::Event> &es) {
		std::string out;
		for (const C::Event &e : es)
		{
			if (e.kind == C::Event::Chat)
			{
				out += OnlineLobbyClient::utf16ToUtf8(e.from) + ":" + OnlineLobbyClient::utf16ToUtf8(e.text) + ";";
			}
		}
		return out;
	};
	auto refusals = [](const std::vector<C::Event> &es) {
		std::string out;
		for (const C::Event &e : es)
		{
			if (e.kind == C::Event::Refused)
			{
				out += e.request + "/" + e.code + ";";
			}
		}
		return out;
	};
	c.signInGuest(u"Me");
	std::vector<C::HttpRequest> rs = c.takeRequests();
	c.deliver(rs[0].id, 200, "{\"v\":1,\"token\":\"t.t\",\"account\":{\"id\":\"guest:1\",\"name\":\"Me\",\"kind\":\"guest\"}}", "");
	c.joinGame("0123456789abcdef", "");
	rs = c.takeRequests();
	c.deliver(rs[0].id, 200, "{\"v\":1,\"slot\":1,\"game\":" + view(3) + "}", "");
	c.takeEvents();
	REQUIRE(c.inGame());
	// ---- the first poll is a plain one (the client does not know the lobby yet) ----
	c.update(now);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].path == "/v1/games/0123456789abcdef?since=0");
	CHECK_FALSE(rs[0].held);
	CHECK(c.holdSeconds() == 0);
	// before the lobby said it marks own lines, a chat line is not shown at its sending (an older lobby: it comes with the poll, as before)
	c.chat(u"early");
	CHECK(chatEvents(c.takeEvents()).empty());
	c.deliver(rs[0].id, 200, pollBody(3, "{\"seq\":2,\"from\":null,\"text\":\"Me joined\"}", "25"), "");
	CHECK(chatEvents(c.takeEvents()) == ":Me joined;");
	CHECK(c.holdSeconds() == 25);
	rs = c.takeRequests(); // the chat POST
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].path == "/v1/games/0123456789abcdef/chat");
	const int chatId = rs[0].id;
	// ---- now the polls are held: one, beside the ordinary request that is still out, with the view's seq and min(20, 25) seconds ----
	c.update(now += 10);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].held);
	CHECK(rs[0].holdSeconds == 20);
	CHECK(rs[0].method == "GET");
	CHECK(rs[0].path == "/v1/games/0123456789abcdef?since=2&seq=3&wait=20");
	CHECK(rs[0].token == "t.t");
	CHECK(c.pollHeld());
	const int held1 = rs[0].id;
	c.update(now += 10);
	CHECK(c.takeRequests().empty()); // one held poll and one ordinary request are out: nothing more
	c.deliver(chatId, 200, "{\"v\":1,\"seq\":4}", "");
	c.update(now += 10);
	CHECK(c.takeRequests().empty()); // the held poll is still out: no second poll of any kind
	// "early" was sent before the lobby's marks were known: its copy is shown once, from the poll, marked or not
	c.deliver(held1, 200, pollBody(4, "{\"seq\":4,\"from\":\"Me\",\"text\":\"early\",\"you\":true}", "25"), "");
	CHECK(chatEvents(c.takeEvents()) == "Me:early;");
	CHECK_FALSE(c.pollHeld());
	// ---- the answer brought something: the next held poll goes out at once ----
	c.update(now += 1);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].path == "/v1/games/0123456789abcdef?since=4&seq=4&wait=20");
	const int held2 = rs[0].id;
	// ---- the sender's own line: shown at once, sent, and its marked copy is not shown again; another player's same text is ----
	c.chat(u"hello");
	CHECK(chatEvents(c.takeEvents()) == "Me:hello;");
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].body == "{\"text\":\"hello\"}");
	c.deliver(held2, 200, pollBody(6, "{\"seq\":5,\"from\":\"Me\",\"text\":\"hello\"},{\"seq\":6,\"from\":\"Me\",\"text\":\"hello\",\"you\":true}", "25"), "");
	CHECK(chatEvents(c.takeEvents()) == "Me:hello;"); // seq 5: another player named Me; seq 6: ours, dropped
	c.deliver(rs[0].id, 200, "{\"v\":1,\"seq\":6}", "");
	// a marked line we did not just send (the room's history after a restart) is shown
	c.update(now += 1);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 200, pollBody(7, "{\"seq\":7,\"from\":\"Me\",\"text\":\"old\",\"you\":true}", "25"), "");
	CHECK(chatEvents(c.takeEvents()) == "Me:old;");
	// a line an earlier answer brought already is not shown twice (answers of the two connections may cross)
	c.update(now += 1);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 200, pollBody(8, "{\"seq\":7,\"from\":\"Me\",\"text\":\"old\",\"you\":true},{\"seq\":8,\"from\":\"Host\",\"text\":\"hi\"}", "25"), "");
	CHECK(chatEvents(c.takeEvents()) == "Host:hi;");
	// ---- a refused chat line: the refusal is reported, and the next marked line is not swallowed in its place ----
	c.update(now += 1);
	std::vector<C::HttpRequest> heldReq = c.takeRequests();
	REQUIRE(heldReq.size() == 1);
	c.chat(u"spam");
	CHECK(chatEvents(c.takeEvents()) == "Me:spam;");
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 429, "{\"v\":1,\"error\":\"rate_limited\",\"message\":\"too many requests: wait a moment\"}", "");
	CHECK(refusals(c.takeEvents()) == "chat/rate_limited;");
	c.deliver(heldReq[0].id, 200, pollBody(9, "{\"seq\":9,\"from\":\"Me\",\"text\":\"from my other window\",\"you\":true}", "25"), "");
	CHECK(chatEvents(c.takeEvents()) == "Me:from my other window;");
	// ---- an older view never replaces a newer one: a ready answer (seq 11) is read before the held poll's (made at seq 10) ----
	c.update(now += 1);
	heldReq = c.takeRequests();
	REQUIRE(heldReq.size() == 1);
	c.setReady(true);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 200, "{\"v\":1,\"game\":" + view(11, true) + "}", "");
	CHECK(c.slotViews()[1].ready);
	CHECK(c.viewSeq() == 11);
	c.deliver(heldReq[0].id, 200, pollBody(10, "", "25", false), "");
	CHECK(c.slotViews()[1].ready);
	CHECK(c.viewSeq() == 11);
	CHECK(refusals(c.takeEvents()).empty());
	// ---- a lobby that answers a held poll at once with nothing new is not hammered: the next poll waits the plain interval ----
	c.update(now += 1);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.update(now += 5);
	c.deliver(rs[0].id, 200, pollBody(11, "", "25", true), "");
	c.takeEvents();
	c.update(now += 100);
	CHECK(c.takeRequests().empty());
	c.update(now += 898);
	CHECK(c.takeRequests().empty());
	c.update(now += 2);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	// a poll that was really held (20 s passed) and brought nothing: the next at once
	c.update(now += 20000);
	c.deliver(rs[0].id, 200, pollBody(11, "", "25", true), "");
	c.takeEvents();
	c.update(now += 1);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	// ---- a held poll that failed: reported, and the next one after the plain interval ----
	c.update(now += 3000);
	c.deliver(rs[0].id, 0, "", "the lobby did not answer");
	CHECK(refusals(c.takeEvents()) == "poll/transport;");
	c.update(now += 999);
	CHECK(c.takeRequests().empty());
	c.update(now += 1);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].held);
	// ---- hostile answers to a held poll: a "wait" or "backoff" outside 1 .. 60, a "you" that is not a boolean, a chat line without its seq, another
	// game's view. Sol r1: each answer carries a NEWER view (seq 12, slot 1 not ready) and a new chat line beside the bad field: a refused answer keeps
	// none of it (the view stays seq 11 with slot 1 ready, the chat read so far stays, the hold stays) ----
	REQUIRE(c.viewSeq() == 11);
	REQUIRE(c.slotViews()[1].ready);
	const int sentBefore = c.sentCount("held");
	auto withBackoff = [](std::string body, const std::string &value) { return body.insert(body.size() - 1, ",\"backoff\":" + value); };
	const std::string goodLine = "{\"seq\":12,\"from\":\"Host\",\"text\":\"new\"}";
	const std::vector<std::string> bads = { pollBody(12, goodLine, "100000", false), pollBody(12, goodLine, "0", false), pollBody(12, goodLine, "\"25\"", false),
		pollBody(12, goodLine, "2.5", false), pollBody(12, goodLine + ",{\"seq\":13,\"from\":\"Host\",\"text\":\"x\",\"you\":\"yes\"}", "25", false),
		pollBody(12, goodLine + ",{\"from\":\"Host\",\"text\":\"no seq\"}", "25", false), withBackoff(pollBody(12, goodLine, "25", false), "0"),
		withBackoff(pollBody(12, goodLine, "25", false), "61"), withBackoff(pollBody(12, goodLine, "25", false), "\"10\"") };
	c.update(now += 3000);
	for (size_t i = 0; i < bads.size(); ++i)
	{
		if (i > 0)
		{
			c.update(now += 1000);
			rs = c.takeRequests();
			REQUIRE(rs.size() == 1);
			c.update(now += 3000);
		}
		c.deliver(rs[0].id, 200, bads[i], "");
		const std::vector<C::Event> es = c.takeEvents();
		CHECK_MESSAGE(refusals(es) == "poll/protocol;", bads[i]);
		CHECK(chatEvents(es).empty());
		CHECK_FALSE(std::any_of(es.begin(), es.end(), [](const C::Event &e) { return e.kind == C::Event::GameUpdated; }));
		CHECK_MESSAGE(c.viewSeq() == 11, bads[i]);
		CHECK_MESSAGE(c.slotViews()[1].ready, bads[i]);
		CHECK(c.holdSeconds() == 25);
	}
	CHECK(c.sentCount("held") == sentBefore + (int)bads.size() - 1); // the first bad answer's poll was out before the count
	c.update(now += 1000);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].path == "/v1/games/0123456789abcdef?since=9&seq=11&wait=20"); // nothing of the refused answers was kept
	std::string other = pollBody(13, "", "25", true);
	other.replace(other.find("0123456789abcdef"), 16, "fedcba9876543210");
	c.update(now += 3000);
	c.deliver(rs[0].id, 200, other, "");
	CHECK(refusals(c.takeEvents()) == "poll/protocol;");
	CHECK(c.gameId() == "0123456789abcdef");
	CHECK(c.viewSeq() == 11);
	// ---- Sol r1: a poll the lobby did not hold for a bound carries "backoff": the next poll waits that long, whatever else the answer brought ----
	c.update(now += 1000);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.update(now += 5);
	c.deliver(rs[0].id, 200, withBackoff(pollBody(12, "", "25", true), "10"), "");
	CHECK(refusals(c.takeEvents()).empty());
	CHECK(c.viewSeq() == 12); // a good answer is kept
	c.update(now += 9990);
	CHECK(c.takeRequests().empty());
	c.update(now += 10);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].held);
	c.update(now += 3000);
	c.deliver(rs[0].id, 200, pollBody(12, "", "25", true), "");
	c.takeEvents();
	// ---- a lobby without "wait" in its answer any more (an older lobby took over): plain polls again, through the ordinary queue ----
	c.update(now += 1000);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.update(now += 3000);
	c.deliver(rs[0].id, 200, pollBody(13, "", "", true), "");
	c.takeEvents();
	CHECK(c.holdSeconds() == 0);
	c.update(now += 1);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK_FALSE(rs[0].held);
	CHECK(rs[0].path == "/v1/games/0123456789abcdef?since=9"); // the last chat line read (the refused answers moved nothing)
	c.chat(u"plain");
	CHECK(chatEvents(c.takeEvents()).empty()); // no marks: no line at the sending
	// ---- the game is left with a held poll out: its late answer is nobody's ----
	c.deliver(rs[0].id, 200, pollBody(13, "", "25", true), "");
	rs = c.takeRequests(); // the chat POST
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 200, "{\"v\":1,\"seq\":14}", "");
	c.update(now += 1);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	REQUIRE(rs[0].held);
	c.leaveGame();
	c.takeEvents();
	c.deliver(rs[0].id, 200, pollBody(14, "{\"seq\":14,\"from\":\"Host\",\"text\":\"late\"}", "25", true), "");
	CHECK(c.takeEvents().empty());
	CHECK_FALSE(c.inGame());
	CHECK_FALSE(c.pollHeld());
	// longPollS 0 (the tests' blocking transport, or the option turned off): never a held poll, whatever the lobby announces
	C plain(clientOptions(4, 4));
	plain.signInGuest(u"Me");
	rs = plain.takeRequests();
	plain.deliver(rs[0].id, 200, "{\"v\":1,\"token\":\"t.t\",\"account\":{\"id\":\"guest:1\",\"name\":\"Me\",\"kind\":\"guest\"}}", "");
	plain.joinGame("0123456789abcdef", "");
	rs = plain.takeRequests();
	plain.deliver(rs[0].id, 200, "{\"v\":1,\"slot\":1,\"game\":" + view(3) + "}", "");
	for (int i = 0; i < 3; ++i)
	{
		plain.update(now += 1000);
		rs = plain.takeRequests();
		REQUIRE(rs.size() == 1);
		CHECK_FALSE(rs[0].held);
		plain.deliver(rs[0].id, 200, pollBody(3, "", "25"), "");
	}
	CHECK(plain.sentCount("held") == 0);
	CHECK(plain.holdSeconds() == 25);
	// ... but the sender's own line is shown at once there too (the lobby marks it)
	plain.takeEvents();
	plain.chat(u"hi");
	CHECK(chatEvents(plain.takeEvents()) == "Me:hi;");
}

#ifdef ONLINE4_POSIX
TEST_CASE("online4: the lobby client against a local lobby (server.mjs): guest sign-in, the list with a locked game, a wrong and a right password, a "
		  "slot change, chat, ready, START refused without a relay (no_relay), a kick, the host leaving")
{
	ONLINE4_REQUIRE_NODE();
	NodeLobby lobby("", "");
	OnlineLobbyClient host(clientOptions()), join(clientOptions());
	host.signInGuest(u"Hostname");
	join.signInGuest(u"Joiner");
	std::vector<OnlineLobbyClient::Event> he = drive(host, lobby.port), je = drive(join, lobby.port);
	REQUIRE_MESSAGE(host.signedIn(), lobby.log());
	REQUIRE(join.signedIn());
	CHECK(host.accountName() == u"Hostname");
	// the message of the day: this lobby has none (null), said so
	host.fetchMotd();
	he = drive(host, lobby.port);
	CHECK(hasEvent(he, OnlineLobbyClient::Event::Motd));
	CHECK(host.motdKnown());
	CHECK_FALSE(host.hasMotd());
	SkirmishGameInfo setup;
	setup.mapName = "maps/map mp fords of isen ii/map mp fords of isen ii.map";
	setup.startingCash = 2000;
	setup.slots[0].state = SLOT_PLAYER;
	setup.slots[0].playerTemplate = 3;
	setup.slots[1].state = SLOT_OPEN;
	setup.slots[2].state = SLOT_HARD_AI;
	setup.slots[2].playerTemplate = 5;
	host.createGame(u"Host's game", "pw", setup, 0);
	he = drive(host, lobby.port);
	REQUIRE(hasEvent(he, OnlineLobbyClient::Event::GameCreated));
	CHECK(host.amIHost());
	CHECK(host.gameInfo().slots[2].state == SLOT_HARD_AI);
	CHECK(host.gameInfo().slots[2].playerTemplate == 5);
	CHECK(host.gameInfo().startingCash == 2000);
	join.refreshList();
	je = drive(join, lobby.port);
	REQUIRE(join.games().size() == 1);
	CHECK(join.games()[0].name == u"Host's game");
	CHECK(join.games()[0].host == u"Hostname");
	CHECK(join.games()[0].password);
	CHECK(join.games()[0].players == 1);
	CHECK(join.games()[0].openSlots == 1);
	const std::string id = join.games()[0].id;
	join.joinGame(id, "wrong");
	je = drive(join, lobby.port);
	const OnlineLobbyClient::Event *refused = findEvent(je, OnlineLobbyClient::Event::Refused, "join");
	REQUIRE(refused);
	CHECK(refused->code == "password");
	CHECK_FALSE(join.inGame());
	join.joinGame(id, "pw");
	je = drive(join, lobby.port);
	REQUIRE(hasEvent(je, OnlineLobbyClient::Event::GameJoined));
	CHECK(join.localSlot() == 1);
	CHECK_FALSE(join.amIHost());
	// the joiner's own slot, through the lobby, reaches the host's view
	join.setOwnSlot(7, 2, OnlineLobbyClient::kKeep, 1);
	join.chat(u"hello host");
	join.setReady(true);
	je = drive(join, lobby.port);
	for (int i = 0; i < 5; ++i)
	{
		NetSleepMilliseconds(120);
		he = drive(host, lobby.port);
		if (host.slotViews()[1].ready)
		{
			break;
		}
	}
	CHECK(host.gameInfo().slots[1].state == SLOT_PLAYER);
	CHECK(host.gameInfo().slots[1].name == u"Joiner");
	CHECK(host.gameInfo().slots[1].playerTemplate == 7);
	CHECK(host.gameInfo().slots[1].color == 2);
	CHECK(host.gameInfo().slots[1].teamNumber == 1);
	CHECK(host.slotViews()[1].ready);
	// the host's chat saw the joiner's line (all the poll's lines since the start: the lobby's "joined" line included)
	std::vector<std::u16string> chat;
	for (const auto &e : he)
	{
		if (e.kind == OnlineLobbyClient::Event::Chat)
		{
			chat.push_back(e.flag ? u"*" + e.text : e.from + u": " + e.text);
		}
	}
	CHECK(std::find(chat.begin(), chat.end(), u"Joiner: hello host") != chat.end());
	CHECK(std::find(chat.begin(), chat.end(), u"*Joiner joined") != chat.end());
	// START without a relay: the lobby's no_relay
	host.start();
	he = drive(host, lobby.port);
	const OnlineLobbyClient::Event *noRelay = findEvent(he, OnlineLobbyClient::Event::Refused, "start");
	REQUIRE(noRelay);
	CHECK(noRelay->code == "no_relay");
	CHECK(host.startJson().empty());
	// the kick: the joiner's next poll says it is out
	host.kick(1);
	he = drive(host, lobby.port);
	CHECK(host.gameInfo().slots[1].state == SLOT_OPEN);
	NetSleepMilliseconds(120);
	je = drive(join, lobby.port);
	CHECK(hasEvent(je, OnlineLobbyClient::Event::Removed, "kicked"));
	CHECK_FALSE(join.inGame());
	// it joins again; the host leaves: the joiner's poll says the game closed
	join.joinGame(id, "pw");
	je = drive(join, lobby.port);
	REQUIRE(join.inGame());
	host.leaveGame();
	he = drive(host, lobby.port);
	CHECK(hasEvent(he, OnlineLobbyClient::Event::Removed, "left"));
	NetSleepMilliseconds(120);
	je = drive(join, lobby.port);
	CHECK(hasEvent(je, OnlineLobbyClient::Event::Removed, "closed"));
	join.refreshList();
	je = drive(join, lobby.port);
	CHECK(join.games().empty());
}
#endif

#ifdef ONLINE4_POSIX
namespace
{
std::string dumpNotes(WindowManager &wm, size_t from)
{
	std::string out;
	for (size_t i = from; i < wm.notes().size(); ++i)
	{
		out += "  note " + wm.notes()[i].kind + ": " + wm.notes()[i].detail + "\n";
	}
	return out;
}
} // namespace

namespace
{
// one player of the screen tests: a real shell over the retail archives with OnlineShell.apt on its own lobby client
struct OnlinePlayer
{
	OnlineLobbyClient client;
	OnlineShellContext ctx;
	starttest::ShellFx fx;
	AptOnlineShell *sh = nullptr;
	int port = 0;
	std::vector<std::u16string> remembered;
	Tamper tamper; ///< Sol r1: rewrites the lobby's answers (the hostile-reply test)
	OnlinePlayer(starttest::Shared &s, int lobbyPort, std::uint32_t seed, const std::u16string &lastName)
		: client(clientOptions(s.world->playerTemplates().getPlayerTemplateCount(), (int)s.settings.multiplayerColors.size())), fx(s, seed), port(lobbyPort)
	{
		ctx.client = &client;
		ctx.rememberedName = lastName;
		ctx.rememberName = [this](const std::u16string &n) { remembered.push_back(n); };
		fx.environment.online = &ctx;
		fx.environment.services = &fx.services;
		fx.shell->push("OnlineShell.apt");
		REQUIRE(fx.shell->errors().empty());
		sh = dynamic_cast<AptOnlineShell *>(fx.shell->top());
		REQUIRE(sh);
	}
	void tick(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			fx.tick(1);
			pumpHttp(client, port, tamper);
		}
	}
	bool hasNote(const std::string &kind, const std::string &part = std::string()) const
	{
		return std::any_of(fx.wm->notes().begin(), fx.wm->notes().end(),
			[&](const WindowManagerNote &n) { return n.kind == kind && (part.empty() || n.detail.find(part) != std::string::npos); });
	}
	std::string notes() const
	{
		std::string out = dumpNotes(*fx.wm, 0);
		for (const std::string &t : fx.wm->traces())
		{
			out += "  trace " + t + "\n";
		}
		return out;
	}
	AptCharacterInst *at(const std::string &path) { return fx.at(sh->level(), path); }
	// every visible button of the shell's level (its target path)
	void buttons(AptCharacterInst *c, std::vector<AptButtonInst *> &out)
	{
		if (!c)
		{
			return;
		}
		if (AptButtonInst *b = c->asButton())
		{
			if (b->globallyVisible())
			{
				out.push_back(b);
			}
			return;
		}
		if (AptSpriteInst *sp = c->asSprite())
		{
			for (AptCharacterInst *k : sp->children())
			{
				buttons(k, out);
			}
		}
	}
	// the visible button whose path contains `part` when the pointer at its centre reaches it (null: none, or another clip covers it)
	AptButtonInst *reachable(const std::string &part)
	{
		std::vector<AptButtonInst *> all;
		buttons(fx.wm->apt().level(sh->level()), all);
		for (AptButtonInst *b : all)
		{
			float x0, y0, x1, y1, x, y;
			if (b->targetPath().find(part) == std::string::npos || !b->contentBounds(x0, y0, x1, y1))
			{
				continue;
			}
			b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
			AptButtonInst *hit = fx.wm->apt().input().hitTestButtons(x, y);
			const std::string target = b->targetPath();
			const std::string clip = target.substr(0, target.rfind('.'));
			if (hit && (hit == b || hit->targetPath().compare(0, clip.size(), clip) == 0))
			{
				return b;
			}
		}
		return nullptr;
	}
	// waits (ticking) until the button whose path contains `part` is shown and nothing covers it (a pop-up's own reveal and close play first)
	bool waitShown(const std::string &part, int limit = 300)
	{
		for (int i = 0; i < limit; ++i)
		{
			if (reachable(part))
			{
				tick(5); // the clip's reveal
				return reachable(part) != nullptr;
			}
			tick(1);
		}
		return false;
	}
	// waits until the button is shown and reachable, then clicks it; false (and the list) after `limit` ticks
	bool clickWhenShown(const std::string &part, int limit = 300)
	{
		waitShown(part, limit);
		return click(part);
	}
	// clicks the visible button whose path contains `part` (the movie's own button, as a player's click); false (and the list) when there is none
	bool click(const std::string &part)
	{
		std::vector<AptButtonInst *> all;
		buttons(fx.wm->apt().level(sh->level()), all);
		for (AptButtonInst *b : all)
		{
			if (b->targetPath().find(part) != std::string::npos)
			{
				starttest::clickButton(fx, *b);
				return true;
			}
		}
		std::string list;
		for (AptButtonInst *b : all)
		{
			list += "  " + b->targetPath() + "\n";
		}
		MESSAGE("no visible button '" << part << "'; the visible buttons:\n" << list << "notes:\n" << notes());
		return false;
	}
	// the player's Ok on the message box (the box clip's own command, then its hide as its Ok button plays it)
	void dismissBox(const char *button = "Ok")
	{
		if (AptMessageBox *b = sh->messageBox())
		{
			b->hide(true);
			fx.wm->fscommand("_level" + std::to_string(b->level()) + ".MessageBox_OnButton" + button, "");
			tick(10);
		}
	}
};

// both shells tick until `done` (each tick pumps its lobby client)
bool runUntil(std::vector<OnlinePlayer *> ps, const std::function<bool()> &done, int limit = 600)
{
	for (int i = 0; i < limit; ++i)
	{
		for (OnlinePlayer *p : ps)
		{
			p->tick(1);
		}
		if (done())
		{
			return true;
		}
		NetSleepMilliseconds(2);
	}
	return false;
}

std::string listText(GameWindow *list, int row, int col)
{
	return OnlineLobbyClient::utf16ToUtf8(GadgetListBoxGetText(list, row, col));
}

// signs in through OnlineLogin (the nickname combo, the Login button's command) and opens Open Play through the shell's nav button
void signIn(OnlinePlayer &p, const std::u16string &name, bool motdExpected = true)
{
	// the page places its fields twice (OnlineLogin's instance1, then Login.componentGroup, the one it keeps): the name goes into the kept one
	REQUIRE_MESSAGE(runUntil({ &p }, [&] {
		GameWindow *nick = p.sh->nicknameWindow();
		for (const auto &c : p.fx.wm->components())
		{
			if (c.instance && c.window.get() == nick && c.instancePath.find("componentGroup.OnlineLogin::Nickname") != std::string::npos)
			{
				return !nick->winIsHidden();
			}
		}
		return false;
	}, 300), p.notes());
	// the name in the Nickname combo the page shows (its edit text; typing into the gadget is the gadget layer's, tested there)
	GadgetComboBoxSetText(p.sh->nicknameWindow(), name);
	REQUIRE(!p.sh->nicknameWindow()->winIsHidden());
	REQUIRE(p.clickWhenShown("Login.LogIn")); // the page's Log In (AptOnline::Login::Login)
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.client.signedIn() && p.sh->currentPage() == "OnlineHome"; }, 300), p.notes());
	if (motdExpected)
	{
		REQUIRE(runUntil({ &p }, [&] { return p.client.motdKnown(); }, 300));
	}
}

// the nav's Open Play button, clicked: the server browser's list
void openPlay(OnlinePlayer &p)
{
	p.tick(30);
	AptButtonInst *openPlay = starttest::firstButtonIn(p.at("Toggles.ButtonNav.OpenPlay"));
	REQUIRE(openPlay);
	starttest::clickButton(p.fx, *openPlay);
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.sh->currentPage() == "OnlineOpenPlay" && p.sh->openPlayState() == 2 && p.sh->gameList(); }, 600),
		p.notes() << "\n" << p.fx.wm->apt().dumpTree(p.fx.wm->apt().level(p.sh->level()), 3));
}

void signInAndOpenPlay(OnlinePlayer &p, const std::u16string &name)
{
	signIn(p, name);
	openPlay(p);
}
} // namespace

TEST_CASE("online4 retail: OnlineShell.apt on a local lobby: guest sign-in, the home page's message of the day, the server browser (list, sort, password), "
		  "Create Game, a second player joins from the list, its faction reaches the host, chat, ready, Play Game refused without a relay (no_relay "
		  "shown plainly), Cancel back to the list")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "Welcome to the test lobby\\nSecond line");
	OnlinePlayer host(*s, lobby.port, 11, u"LastTime");
	OnlinePlayer join(*s, lobby.port, 12, u"");
	// the lane's stops are reported when the screen is made
	for (const std::string &line : AptOnlineShell::stopLines())
	{
		CHECK(host.hasNote("online-stops", line));
	}
	CHECK(AptOnlineShell::stopLines().size() == 13); // lane ONLINE-5: S-4550 .. S-4554 added
	// ---- OnlineLogin: the guest name of the last sign-in is offered; the account features are disabled ----
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.sh->nicknameWindow() != nullptr; }, 300), host.notes());
	CHECK(host.sh->currentPage() == "OnlineLogin");
	CHECK(GadgetComboBoxGetText(host.sh->nicknameWindow()) == u"LastTime");
	CHECK_FALSE(host.hasNote("movie-function-missing"));
	// an empty name is the retail box GUI:GSErrorTitle / GUI:GSNoLoginInfoNickname, and nothing is sent
	GadgetComboBoxSetText(host.sh->nicknameWindow(), u"  ");
	host.fx.wm->invokeCallback("AptOnline::Login::Login", "");
	host.tick(5);
	REQUIRE(host.sh->boxesShown().size() == 1);
	CHECK(host.sh->boxesShown()[0] == "GUI:GSErrorTitle|GUI:GSNoLoginInfoNickname");
	CHECK(host.client.sentCount("signin") == 0);
	host.dismissBox();
	// the account buttons show the movie's own disabled state (each GenericButton clip's "_disabled" segment of its timeline); the official site
	// stays enabled (it says "not available" when pressed)
	auto disabledShown = [&](const char *clip) {
		AptCharacterInst *c = host.at(clip);
		AptSpriteInst *sp = c ? c->asSprite() : nullptr;
		if (!sp || !sp->frames)
		{
			return false;
		}
		const std::map<std::string, int> &labels = host.fx.wm->apt().labelsOf(sp->frames);
		auto it = labels.find("_disabled");
		if (it == labels.end() || sp->frame < it->second)
		{
			return false;
		}
		for (const auto &kv : labels)
		{
			if (kv.second > it->second && sp->frame >= kv.second)
			{
				return false; // past the segment
			}
		}
		return true;
	};
	for (const char *clip : { "OnlineLogin.Login.Register", "OnlineLogin.Login.DeleteNickname", "OnlineLogin.Login.ServiceTerms" })
	{
		CHECK_MESSAGE(runUntil({ &host }, [&] { return disabledShown(clip); }, 120), clip);
	}
	CHECK_FALSE(disabledShown("OnlineLogin.Login.OfficialSite"));
	CHECK(host.hasNote("online-login", "the account buttons are disabled (S-4071)"));
	// Register is not available: the box says so (S-4071)
	host.fx.wm->invokeCallback("AptOnline::Login::Register", "");
	CHECK(host.hasNote("unported", "Register is not available in OpenBFME's online play [S-4071]"));
	host.dismissBox();
	signInAndOpenPlay(host, u"Hostname");
	CHECK(host.client.accountName() == u"Hostname");
	REQUIRE(host.remembered.size() == 1);
	CHECK(host.remembered[0] == u"Hostname");
	CHECK(host.ctx.rememberedName == u"Hostname");
	// ---- OnlineHome showed the lobby's message of the day (two lines) ----
	REQUIRE(host.sh->motdLines().size() == 2);
	CHECK(host.sh->motdLines()[0] == "Welcome to the test lobby");
	CHECK(host.sh->motdLines()[1] == "Second line");
	// ---- Open Play: an empty list; Create Game (name, password) ----
	CHECK(host.sh->gameRows().empty());
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	const bool popupGadgets = runUntil({ &host }, [&] { return host.sh->openPlayState() == 3 && host.sh->gameNameWindow() && host.sh->gamePasswordWindow(); }, 300);
	REQUIRE_MESSAGE(popupGadgets, host.notes());
	CHECK(host.sh->openPlayState() == 3);

	CHECK(GadgetTextEntryGetText(host.sh->gameNameWindow()) == u"Hostname"); // the player's name (BFME2 0x5A0DC2)
	GadgetTextEntrySetText(host.sh->gameNameWindow(), u"Test game");
	GadgetTextEntrySetText(host.sh->gamePasswordWindow(), u"pw");
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept")); // the pop-up's Accept (AptOnline::CustomMatch::CreateGame)
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	CHECK(host.client.amIHost());

	CHECK(host.client.gameName() == u"Test game");
	CHECK(host.sh->setup()->info().slots[0].name == u"Hostname");
	CHECK(host.sh->setup()->info().slots[1].state == SLOT_OPEN);
	CHECK_FALSE(host.hasNote("movie-function-missing"));
	// ---- the joiner: the list, Refresh, the sorter, the password prompt, the join: every step through the movie's own buttons (Sol r1) ----
	// the map column shows the map's display name, as retail's list (BFME2 0x59F496: bfme_getBaseDisplayName of the map cache's entry), not its file
	// name. The fixture's game text has no map labels: the two maps' display name labels are given here (the game reads the map.str / game text)
	REQUIRE(join.sh->setup());
	const SkirmishGameSetup &model = *join.sh->setup();
	auto labelOf = [](const MapCacheEntry &m) {
		const std::string l = OnlineLobbyClient::utf16ToUtf8(m.displayName);
		return !l.empty() && l[0] == '$' ? l.substr(1) : l;
	};
	const std::string testMap = host.client.gameInfo().mapName;
	REQUIRE(model.findMap(testMap));
	const std::string testTitle = "Test map title";
	join.fx.text.set(labelOf(*model.findMap(testMap)), testTitle);
	std::string otherMap;
	for (int i : model.listedMaps())
	{
		const MapCacheEntry &m = model.source().maps()[(size_t)i];
		if (labelOf(m) != labelOf(*model.findMap(testMap)))
		{
			otherMap = m.name;
			join.fx.text.set(labelOf(m), "Another map title");
			break;
		}
	}
	const std::string otherTitle = "Another map title";
	signInAndOpenPlay(join, u"Joiner");
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return join.sh->gameRows().size() == 1; }, 300), join.notes());
	{
		int open = 0;
		for (const SkirmishGameSlot &sl : host.client.gameInfo().slots)
		{
			open += sl.state == SLOT_OPEN ? 1 : 0;
		}
		CHECK(join.sh->gameRows()[0] == "Test game|" + testTitle + "|1/" + std::to_string(1 + open) + "|lock");
		CHECK(listText(join.sh->gameList(), 0, 4) == testTitle);
	}
	CHECK_FALSE(join.hasNote("online-map-unknown"));
	// a second game on another map (a third player, through the bare client) appears after the joiner's Refresh
	REQUIRE(!otherMap.empty());
	OnlineLobbyClient other(clientOptions(s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size()));
	other.signInGuest(u"Other");
	SkirmishGameInfo otherSetup = host.client.gameInfo();
	otherSetup.mapName = otherMap;
	other.createGame(u"Another game", "", otherSetup, 0);
	for (int i = 0; i < 4 && !other.inGame(); ++i)
	{
		pumpHttp(other, lobby.port);
	}
	REQUIRE(other.inGame());
	join.tick(30);
	CHECK(join.sh->gameRows().size() == 1); // the list is the one the page asked for
	REQUIRE(join.click("Main.Refresh.buttonRefresh"));
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return join.sh->gameRows().size() == 2; }, 300), join.notes());
	auto names = [&] {
		std::string out;
		for (const std::string &r : join.sh->gameRows())
		{
			out += r.substr(0, r.find('|')) + ";";
		}
		return out;
	};
	// the sorter's column headers: Name, a second press reverses it; Map (by the display names). Each press plays the header clip's _down, whose last
	// frame calls the strip's DefaultButtonCallBack (it traces "DefaultButtonCallBack: <column>") and GameSorter::<column>
	auto sortPress = [&](const std::string &column) {
		auto pressed = [&] {
			return std::count_if(join.fx.wm->traces().begin(), join.fx.wm->traces().end(),
				[&](const std::string &t) { return t == "DefaultButtonCallBack: " + column; });
		};
		const auto before = pressed();
		REQUIRE(join.clickWhenShown("Main.instance13.instance8." + column + "."));
		REQUIRE_MESSAGE(runUntil({ &join }, [&] { return pressed() == before + 1; }, 120), join.notes());
	};
	sortPress("OnSortName");
	REQUIRE_MESSAGE(runUntil({ &join }, [&] { return names() == "Another game;Test game;"; }, 60), names());
	CHECK(listText(join.sh->gameList(), 0, 3) == "Another game");
	sortPress("OnSortName");
	REQUIRE_MESSAGE(runUntil({ &join }, [&] { return names() == "Test game;Another game;"; }, 60), names());
	CHECK(listText(join.sh->gameList(), 0, 3) == "Test game");
	sortPress("OnSortMap");
	const std::string byMap = otherTitle < testTitle ? "Another game;Test game;" : "Test game;Another game;";
	REQUIRE_MESSAGE(runUntil({ &join }, [&] { return names() == byMap; }, 60), names() << " " << otherTitle << " / " << testTitle);
	auto testRow = [&] {
		const auto &ids = join.sh->gameIds();
		return (int)(std::find(ids.begin(), ids.end(), host.client.gameId()) - ids.begin());
	};
	// nothing selected: Join Game says GUI:Error / GUI:NoGameSelected
	GadgetListBoxSetSelected(join.sh->gameList(), -1);
	REQUIRE(join.clickWhenShown("Main.Controls.JoinGame"));
	REQUIRE(runUntil({ &join }, [&] { return !join.sh->boxesShown().empty(); }, 60));
	CHECK(join.sh->boxesShown().back() == "GUI:Error|GUI:NoGameSelected");
	CHECK(join.client.sentCount("join") == 0);
	join.dismissBox();
	REQUIRE(runUntil({ &join }, [&] { return join.sh->openPlayState() == 2; }, 60));
	// the locked game: Join Game opens the password prompt; a wrong password is the lobby's refusal in the box, back to the list
	GadgetListBoxSetSelected(join.sh->gameList(), testRow());
	REQUIRE(join.clickWhenShown("Main.Controls.JoinGame"));
	REQUIRE_MESSAGE(runUntil({ &join }, [&] { return join.sh->openPlayState() == 10; }, 60), join.notes());
	REQUIRE(join.waitShown("AcceptJoin")); // the pop-up places its own password entry as it opens
	GadgetTextEntrySetText(join.sh->gamePasswordWindow(), u"nope");
	REQUIRE(join.clickWhenShown("AcceptJoin"));
	REQUIRE(runUntil({ &join }, [&] { return join.sh->openPlayState() == 2; }, 300));
	CHECK(join.sh->boxesShown().back() == "GUI:JoinFailedDefault|wrong password");
	CHECK_FALSE(join.client.inGame());
	join.dismissBox();
	GadgetListBoxSetSelected(join.sh->gameList(), testRow());
	REQUIRE(join.clickWhenShown("Main.Controls.JoinGame"));
	REQUIRE(runUntil({ &join }, [&] { return join.sh->openPlayState() == 10; }, 60));
	REQUIRE(join.waitShown("AcceptJoin"));
	GadgetTextEntrySetText(join.sh->gamePasswordWindow(), u"pw");
	REQUIRE(join.clickWhenShown("AcceptJoin"));
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return join.client.inGame() && join.sh->openPlayState() == 12; }, 300), join.notes());
	CHECK(join.client.localSlot() == 1);
	// the host's staging room shows the joiner in slot 1
	REQUIRE(runUntil({ &host, &join }, [&] { return host.sh->setup()->info().slots[1].state == SLOT_PLAYER; }, 300));
	CHECK(host.sh->setup()->info().slots[1].name == u"Joiner");
	// ---- the joiner's faction through its own combo reaches the host through the lobby (as the LAN lobby's test) ----
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] {
		GameWindow *f = join.sh->slotGadget(1, "PlayerTemplate");
		return f && GadgetComboBoxGetLength(f) > 2 && host.sh->slotGadget(1, "PlayerTemplate");
	}, 300), join.notes());
	GadgetComboBoxSetSelectedPos(join.sh->slotGadget(1, "PlayerTemplate"), 2, false);
	const int faction = join.sh->setup()->info().slots[1].playerTemplate;
	REQUIRE(faction >= 0); // tolerance: a valid player template index (-1 = random is not what was selected)
	REQUIRE(runUntil({ &host, &join }, [&] { return host.client.gameInfo().slots[1].playerTemplate == faction; }, 300));
	REQUIRE(runUntil({ &host, &join }, [&] { return host.sh->setup()->info().slots[1].playerTemplate == faction; }, 300));
	CHECK(join.client.sentCount("slot") == 1); // one change of the combo, one request to the lobby
	// the host's slot is not the joiner's to change; the joiner's is not the host's
	auto enabled = [](GameWindow *w) { return w && (w->winGetStatus() & WIN_STATUS_ENABLED) != 0; };
	CHECK(!enabled(join.sh->slotGadget(0, "PlayerTemplate")));
	CHECK(enabled(join.sh->slotGadget(1, "PlayerTemplate")));
	CHECK(!enabled(host.sh->slotGadget(1, "PlayerTemplate")));
	// ---- chat through the chat tab's entry (AptMpChat::Send): the line reaches the host's chat list through the lobby ----
	// the joiner's setup page shows its chat at once (Join.instance1.TabChat); the host's has a CHAT tab
	REQUIRE(host.clickWhenShown("TabButtons.ChatTab"));
	REQUIRE(runUntil({ &host, &join }, [&] { return join.sh->chatEntryWindow() != nullptr && host.sh->chatListWindow() != nullptr; }, 300));
	GadgetTextEntrySetText(join.sh->chatEntryWindow(), u"  hello host ");
	CHECK(join.fx.wm->invokeCallback("AptMpChat::Send", ""));
	CHECK(GadgetTextEntryGetText(join.sh->chatEntryWindow()).empty());
	REQUIRE(runUntil({ &host, &join }, [&] {
		return std::find(host.sh->chatLines().begin(), host.sh->chatLines().end(), "[Joiner] hello host") != host.sh->chatLines().end();
	}, 300));
	REQUIRE(host.sh->chatListWindow());
	CHECK(GadgetListBoxGetText(host.sh->chatListWindow(), GadgetListBoxGetNumEntries(host.sh->chatListWindow()) - 1, 0) == u"[Joiner] hello host");
	// ---- ready: the joiner's press of its own box in the Ready column (lane ONLINE-5: RW 0x84211D gives the boxes their states), seen by the host ----
	REQUIRE(runUntil({ &host, &join }, [&] { return join.sh->readyIcon(1) == "_enabledUnchecked"; }, 300));
	REQUIRE(join.click("~1.Ready"));
	REQUIRE(runUntil({ &host, &join }, [&] { return host.client.slotViews()[1].ready && host.sh->setup()->info().slots[1].accepted; }, 300));
	// ---- Play Game without a relay: the lobby refuses (no_relay) and the player is told plainly ----
	const size_t hostBoxes = host.sh->boxesShown().size();
	REQUIRE(host.clickWhenShown("LowerButtons_Host.PlayGame"));
	REQUIRE(runUntil({ &host, &join }, [&] { return host.sh->boxesShown().size() > hostBoxes; }, 300)); // tolerance: waits for the one refusal box; the next CHECK asserts its exact text
	CHECK(host.sh->boxesShown().back() == "no_relay|Online games can't start yet: no relay server is available.");
	CHECK(host.hasNote("online-refused", "start: no_relay:"));
	CHECK_FALSE(host.sh->connectingShown());
	CHECK(host.fx.services.countRequests(ShellAction::OnlineGameStart) == 0);
	CHECK(join.fx.services.countRequests(ShellAction::OnlineGameStart) == 0);
	CHECK(host.client.inGame()); // still in the staging room
	// ---- the joiner's Cancel: out of the game, back to the list; the host's room shows the slot open again ----
	REQUIRE(join.clickWhenShown("LowerButtons_Join.CancelJoin"));
	REQUIRE(runUntil({ &host, &join }, [&] { return !join.client.inGame() && join.sh->openPlayState() == 2; }, 300));
	REQUIRE(runUntil({ &host, &join }, [&] { return host.sh->setup()->info().slots[1].state == SLOT_OPEN; }, 300));
	CHECK_FALSE(host.hasNote("movie-function-missing"));
	CHECK_FALSE(join.hasNote("movie-function-missing"));
}

TEST_CASE("online4 retail hostile: the online screens on bounded, recoverable lobby errors (Sol r1): a 1 MiB message of the day, lost polls, a "
		  "malformed START (a lobby error, back to the list: the game goes on), refusals that echo the session token and a ticket (redacted)")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer host(*s, lobby.port, 13, u"");
	const std::string ticket = "AQEjRWeJq83vASNFZ4mrze8AAQIDBAUGBwgJCgsMDQ4"; // a relay ticket's shape: 32 bytes of base64url
	bool pollsLost = false;
	std::string startToSend, refusalToSend;
	host.tamper = [&](const OnlineLobbyClient::HttpRequest &r, HttpAnswer &a) {
		if (r.path == "/v1/motd")
		{
			a.body = "{\"v\":1,\"motd\":\"" + std::string(1024 * 1024, 'x') + "\"}"; // Sol's case
		}
		const bool poll = r.method == "GET" && r.path.rfind("/v1/games/", 0) == 0 && r.path.find("?since=") != std::string::npos;
		if (poll && pollsLost)
		{
			a = HttpAnswer();
			a.error = "connection reset by peer";
		}
		const size_t at = a.body.find("\"start\":null");
		if (poll && !startToSend.empty() && at != std::string::npos)
		{
			a.body.replace(at, 12, "\"start\":" + startToSend);
			startToSend.clear();
		}
		if (r.path == "/v1/games?running=1" && !refusalToSend.empty())
		{
			a.status = 400;
			a.body = refusalToSend;
			refusalToSend.clear();
		}
	};
	signIn(host, u"Hostname", false);
	// ---- the home page: the message of the day was refused for its size, and the player is told (the client kept none of it) ----
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return !host.sh->motdLines().empty() && host.sh->motdLines()[0] != "..."; }, 300), host.notes());
	CHECK(host.sh->motdLines()[0] == "The lobby's message of the day could not be read: the lobby's answer is larger than 262144 bytes");
	CHECK_FALSE(host.client.motdKnown());
	CHECK(host.client.motd().empty());
	openPlay(host);
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->openPlayState() == 3 && host.sh->gameNameWindow(); }, 300));
	GadgetTextEntrySetText(host.sh->gameNameWindow(), u"Hostile");
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	// ---- lost polls: said once in the room's chat (it keeps asking), and again when the lobby answers ----
	auto chatCount = [&](const std::string &line) { return std::count(host.sh->chatLines().begin(), host.sh->chatLines().end(), line); };
	const std::string lost = "The lobby does not answer: connection reset by peer (still trying)";
	pollsLost = true;
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return chatCount(lost) == 1; }, 600), host.notes());
	const int pollsBefore = host.client.sentCount("poll");
	REQUIRE(runUntil({ &host }, [&] { return host.client.sentCount("poll") == pollsBefore + 2; }, 600)); // two more lost polls
	CHECK(chatCount(lost) == 1);
	CHECK(host.client.inGame());
	pollsLost = false;
	REQUIRE(runUntil({ &host }, [&] { return chatCount("The lobby answers again.") == 1; }, 600));
	// ---- a malformed START (another profile; it carries a ticket): a lobby error in the box, back to the list; nothing reaches the game ----
	startToSend = "{\"v\":1,\"kind\":\"openbfme-start\",\"game\":\"" + host.client.gameId() + "\",\"slot\":0,\"relay\":\"127.0.0.1:1\",\"ticket\":\"" + ticket +
		"\",\"runAhead\":3,\"crcInterval\":10,\"profile\":\"ffffffffffffffffffffffffffffffff\",\"setup\":{}}";
	const size_t boxes = host.sh->boxesShown().size();
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.sh->boxesShown().size() == boxes + 1; }, 600), host.notes());
	CHECK(host.sh->boxesShown().back().rfind("GUI:GSErrorTitle|The online game could not start: online START: the game's profile identity ffff", 0) == 0);
	CHECK(host.fx.services.countRequests(ShellAction::OnlineGameStart) == 0);
	CHECK(host.hasNote("online-start-failed", "profile identity"));
	CHECK_FALSE(host.client.inGame());
	CHECK(host.client.startJson().empty());
	CHECK(host.client.sentCount("leave") == 0); // the slot stays the player's (a running game would be offered for a rejoin)
	REQUIRE(runUntil({ &host }, [&] { return host.sh->openPlayState() == 2; }, 300));
	host.dismissBox();
	// the host program's own refusal (GameWorld.net_online, through AptMenuPlayer.online_start_failed) takes the same way, its text redacted
	host.sh->startFailed("the relay socket would not open: ticket " + ticket);
	CHECK(host.sh->boxesShown().back() == "GUI:GSErrorTitle|The online game could not start: the relay socket would not open: ticket <redacted>");
	host.dismissBox();
	// ---- a refusal that echoes the session token and a ticket: shown and noted without them ----
	const std::string token = host.client.sessionToken();
	REQUIRE(OnlineLobbyClient::redact(token) == "<redacted>"); // the lobby's session token has a credential's shape
	refusalToSend = "{\"v\":1,\"error\":\"bad_field\",\"message\":\"token " + token + " ticket " + ticket + "\"}";
	REQUIRE(runUntil({ &host }, [&] { return host.sh->openPlayState() == 2; }, 300));
	REQUIRE(host.click("Main.Refresh.buttonRefresh"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->boxesShown().back().rfind("GUI:GSErrorTitle|token ", 0) == 0; }, 300));
	CHECK(host.sh->boxesShown().back() == "GUI:GSErrorTitle|token <redacted> ticket <redacted>");
	// no note, box or chat line carries a credential
	for (const WindowManagerNote &n : host.fx.wm->notes())
	{
		CHECK_MESSAGE(n.detail.find(token) == std::string::npos, n.kind);
		CHECK_MESSAGE(n.detail.find(ticket) == std::string::npos, n.kind);
	}
	for (const std::vector<std::string> *texts : { &host.sh->boxesShown(), &host.sh->chatLines() })
	{
		for (const std::string &t : *texts)
		{
			CHECK(t.find(token) == std::string::npos);
			CHECK(t.find(ticket) == std::string::npos);
		}
	}
}

namespace
{
std::string envOr(const char *name, const char *compiled)
{
	const char *e = std::getenv(name);
	return e && *e ? std::string(e) : std::string(compiled ? compiled : "");
}
std::string readText(const std::string &path)
{
	std::ifstream f(path);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}
std::string reportField(const std::string &report, const std::string &key)
{
	std::istringstream in(report);
	std::string line;
	while (std::getline(in, line))
	{
		if (line.rfind(key + " ", 0) == 0)
		{
			return line.substr(key.size() + 1);
		}
	}
	return "";
}
#ifdef OPENBFME_PEER_EXE
const char *kPeerExe = OPENBFME_PEER_EXE;
#else
const char *kPeerExe = nullptr;
#endif
#ifdef OPENBFME_RELAY_EXE
const char *kRelayExe = OPENBFME_RELAY_EXE;
#else
const char *kRelayExe = nullptr;
#endif
} // namespace

TEST_CASE("online4 retail e2e: the START from the online screens' staging room through a local relay: two openbfme_peer processes play it in lockstep")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	const std::string peer = envOr("OPENBFME_PEER", kPeerExe), relayExe = envOr("OPENBFME_RELAY", kRelayExe);
	if (peer.empty() || relayExe.empty() || !std::filesystem::exists(peer) || !std::filesystem::exists(relayExe))
	{
		MESSAGE("SKIP LOUDLY: no openbfme_peer / openbfme_relay here (OPENBFME_PEER / OPENBFME_RELAY), so the online START was not played");
		return;
	}
	const std::string base = "\"" + peer + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL") + "\"";
	// the peers' profile identity: the lobby compares it, the START carries it, the peers check it
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme-online4-e2e-" + std::to_string(getpid()));
	std::filesystem::create_directories(dir);
	const std::string profileFile = (dir / "profile.txt").string();
	REQUIRE(std::system((base + " --print-profile > \"" + profileFile + "\"").c_str()) == 0);
	std::string profile = readText(profileFile);
	profile = profile.substr(0, profile.find('\n'));
	REQUIRE(profile.size() == 32);
	// the relay with the lobby's ticket key
	{
		std::ofstream k(dir / "keys.txt");
		k << "1:" << kTicketKeyHex << "\n";
	}
	int relayPort = 0;
	{
		UDP tmp;
		std::string error;
		REQUIRE(tmp.bind(0x7F000001u, 0, &error));
		relayPort = tmp.localAddress().port;
	}
	const std::string relayBind = "127.0.0.1:" + std::to_string(relayPort), keys = (dir / "keys.txt").string(), logs = (dir / "relaylog").string(),
					  status = (dir / "relay-status.json").string();
	std::filesystem::create_directories(logs);
	std::fflush(nullptr);
	const pid_t relay = fork();
	REQUIRE(relay >= 0); // tolerance: fork() returns the child's pid (>0) or 0 in the child; -1 is the failure
	if (relay == 0)
	{
		execl(relayExe.c_str(), relayExe.c_str(), "--bind", relayBind.c_str(), "--keys", keys.c_str(), "--log-dir", logs.c_str(), "--status", status.c_str(),
			"--stats-s", "0", "--idle-s", "60", (char *)nullptr);
		_exit(127);
	}
	struct Stop
	{
		pid_t pid;
		~Stop()
		{
			kill(pid, SIGTERM);
			int st = 0;
			waitpid(pid, &st, 0);
		}
	} stopRelay{ relay };
	NodeLobby lobby("local=" + relayBind, "");
	OnlinePlayer host(*s, lobby.port, 21, u"");
	OnlinePlayer join(*s, lobby.port, 22, u"");
	// the clients compare the peers' identity
	OnlineLobbyClient::Options o = clientOptions(s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	o.profileDigest = profile;
	host.client = OnlineLobbyClient(o);
	join.client = OnlineLobbyClient(o);
	signInAndOpenPlay(host, u"Host");
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	signInAndOpenPlay(join, u"Joiner");
	REQUIRE(runUntil({ &host, &join }, [&] { return join.sh->gameRows().size() == 1; }, 300));
	GadgetListBoxSetSelected(join.sh->gameList(), 0);
	REQUIRE(join.fx.wm->invokeCallback("AptOnline::CustomMatch::JoinGame", ""));
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return join.client.inGame() && join.sh->openPlayState() == 12; }, 300), join.notes());
	REQUIRE(join.fx.wm->invokeCallback("MpGameSetup::OnReadyPress", "1"));
	REQUIRE(runUntil({ &host, &join }, [&] { return host.client.slotViews()[1].ready; }, 300));
	REQUIRE(host.fx.wm->invokeCallback("AptOnline::CustomMatch::PlayGame", ""));
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] {
		return host.fx.services.countRequests(ShellAction::OnlineGameStart) == 1 && join.fx.services.countRequests(ShellAction::OnlineGameStart) == 1;
	}, 600), host.notes() << "\n" << join.notes());
	CHECK(host.sh->connectingShown()); // the connecting pop-up is up while the game loads
	std::vector<std::string> starts;
	for (OnlinePlayer *p : { &host, &join })
	{
		for (const ShellRequest &r : p->fx.services.requests)
		{
			if (r.action == ShellAction::OnlineGameStart)
			{
				starts.push_back(r.argument);
			}
		}
	}
	REQUIRE(starts.size() == 2);
	CHECK(starts[0].find("\"kind\":\"openbfme-start\"") != std::string::npos);
	CHECK(starts[0].find("\"slot\":0") != std::string::npos);
	CHECK(starts[1].find("\"slot\":1") != std::string::npos);
	CHECK(starts[0].find("\"relay\":\"" + relayBind + "\"") != std::string::npos);
	// both STARTs are what OnlineStart reads (the game's net_online and openbfme_peer --online-start)
	const int kFrames = 300;
	std::vector<std::string> files, reports;
	for (size_t i = 0; i < starts.size(); ++i)
	{
		files.push_back((dir / ("start-" + std::to_string(i) + ".json")).string());
		reports.push_back((dir / ("report-" + std::to_string(i) + ".txt")).string());
		std::ofstream(files[i]) << starts[i];
	}
	std::vector<std::thread> peers;
	std::vector<int> rcs(starts.size(), -1);
	for (size_t i = 0; i < starts.size(); ++i)
	{
		const std::string cmd = base + " --online-start \"" + files[i] + "\" --script --frames " + std::to_string(kFrames) + " --report \"" + reports[i] +
			"\" > \"" + (dir / ("peer-" + std::to_string(i) + ".log")).string() + "\" 2>&1";
		peers.emplace_back([&rcs, i, cmd] {
			const int rc = std::system(cmd.c_str());
			rcs[i] = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
		});
	}
	for (std::thread &t : peers)
	{
		t.join();
	}
	const std::string r0 = readText(reports[0]), r1 = readText(reports[1]);
	MESSAGE("host peer:\n" << r0 << "\njoiner peer:\n" << r1);
	CHECK(rcs[0] == 0);
	CHECK(rcs[1] == 0);
	CHECK(reportField(r0, "frames") == std::to_string(kFrames));
	CHECK(reportField(r1, "frames") == std::to_string(kFrames));
	CHECK(reportField(r0, "desyncs") == "0");
	CHECK(reportField(r1, "desyncs") == "0");
	CHECK(!reportField(r0, "final_hash").empty());
	CHECK(reportField(r0, "final_hash") == reportField(r1, "final_hash"));
	CHECK(reportField(r0, "relay").find(" bound 1 ") != std::string::npos);
	CHECK(reportField(r1, "relay").find(" bound 1 ") != std::string::npos);
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}
#endif

#ifdef ONLINE4_POSIX
namespace
{
// a shell command as its own process (exec: the pid is the command's), so a test can kill it
pid_t spawn(const std::string &cmd)
{
	std::fflush(nullptr);
	const pid_t pid = fork();
	if (pid == 0)
	{
		execl("/bin/sh", "sh", "-c", ("exec " + cmd).c_str(), (char *)nullptr);
		_exit(127);
	}
	return pid;
}
int waitExit(pid_t pid)
{
	int st = 0;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
std::string startOf(const RecordingShellServices &s)
{
	for (const ShellRequest &r : s.requests)
	{
		if (r.action == ShellAction::OnlineGameStart)
		{
			return r.argument;
		}
	}
	return "";
}
} // namespace

TEST_CASE("online4 retail rejoin: a player killed mid-game restarts, signs in with its kept session, is offered its running game (Yes / No box, the "
		  "REJOIN row of Open Play), and Join Game on the row gives the rejoin START: the restarted peer catches up through the relay and plays on in sync")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	const std::string peer = envOr("OPENBFME_PEER", kPeerExe), relayExe = envOr("OPENBFME_RELAY", kRelayExe);
	if (peer.empty() || relayExe.empty() || !std::filesystem::exists(peer) || !std::filesystem::exists(relayExe))
	{
		MESSAGE("SKIP LOUDLY: no openbfme_peer / openbfme_relay here (OPENBFME_PEER / OPENBFME_RELAY), so the rejoin was not played");
		return;
	}
	const std::string base = "\"" + peer + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL") + "\"";
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme-online4-rejoin-" + std::to_string(getpid()));
	std::filesystem::create_directories(dir / "relaylog");
	const std::string profileFile = (dir / "profile.txt").string();
	REQUIRE(std::system((base + " --print-profile > \"" + profileFile + "\"").c_str()) == 0);
	std::string profile = readText(profileFile);
	profile = profile.substr(0, profile.find('\n'));
	REQUIRE(profile.size() == 32);
	{
		std::ofstream k(dir / "keys.txt");
		k << "1:" << kTicketKeyHex << "\n";
	}
	int relayPort = 0;
	{
		UDP tmp;
		std::string error;
		REQUIRE(tmp.bind(0x7F000001u, 0, &error));
		relayPort = tmp.localAddress().port;
	}
	const std::string relayBind = "127.0.0.1:" + std::to_string(relayPort);
	const pid_t relay = spawn("\"" + relayExe + "\" --bind " + relayBind + " --keys \"" + (dir / "keys.txt").string() + "\" --log-dir \"" +
		(dir / "relaylog").string() + "\" --status \"" + (dir / "relay-status.json").string() + "\" --stats-s 0 --idle-s 60 > \"" + (dir / "relay.txt").string() + "\" 2>&1");
	struct Stop
	{
		pid_t pid;
		~Stop()
		{
			kill(pid, SIGTERM);
			int st = 0;
			waitpid(pid, &st, 0);
		}
	} stopRelay{ relay };
	NodeLobby lobby("local=" + relayBind, "");
	OnlineLobbyClient::Options o = clientOptions(s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	o.profileDigest = profile;
	// ---- the game: a host and a joiner from the screens, played by two peer processes ----
	std::string joinerSession;
	std::string starts[2];
	{
		OnlinePlayer host(*s, lobby.port, 31, u"");
		OnlinePlayer join(*s, lobby.port, 32, u"");
		host.client = OnlineLobbyClient(o);
		join.client = OnlineLobbyClient(o);
		signInAndOpenPlay(host, u"Host");
		REQUIRE(host.clickWhenShown("Controls.CreateGame"));
		REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
		REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
		REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
		signInAndOpenPlay(join, u"Joiner");
		CHECK(join.ctx.sessionToken == join.client.sessionToken()); // the session the host program keeps for a restart
		joinerSession = join.ctx.sessionToken;
		REQUIRE(runUntil({ &host, &join }, [&] { return join.sh->gameRows().size() == 1; }, 300));
		GadgetListBoxSetSelected(join.sh->gameList(), 0);
		REQUIRE(join.fx.wm->invokeCallback("AptOnline::CustomMatch::JoinGame", ""));
		REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return join.client.inGame() && join.sh->openPlayState() == 12; }, 300), join.notes());
		REQUIRE(join.fx.wm->invokeCallback("MpGameSetup::OnReadyPress", "1"));
		REQUIRE(runUntil({ &host, &join }, [&] { return host.client.slotViews()[1].ready; }, 300));
		REQUIRE(host.fx.wm->invokeCallback("AptOnline::CustomMatch::PlayGame", ""));
		REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return !startOf(host.fx.services).empty() && !startOf(join.fx.services).empty(); }, 600),
			host.notes() << "\n" << join.notes());
		starts[0] = startOf(host.fx.services);
		starts[1] = startOf(join.fx.services);
	} // both screens are gone (the players are in the game)
	const int kFrames = 400; // 80 s paced
	std::string files[3], reports[3];
	for (int i = 0; i < 3; ++i)
	{
		files[i] = (dir / ("start-" + std::to_string(i) + ".json")).string();
		reports[i] = (dir / ("report-" + std::to_string(i) + ".txt")).string();
	}
	std::ofstream(files[0]) << starts[0];
	std::ofstream(files[1]) << starts[1];
	auto runPeer = [&](int i, const std::string &start) {
		return spawn(base + " --online-start \"" + start + "\" --script --paced --frames " + std::to_string(kFrames) + " --logic-threads 2 --report \"" + reports[i] +
			"\" > \"" + (dir / ("peer-" + std::to_string(i) + ".log")).string() + "\" 2>&1");
	};
	const pid_t p0 = runPeer(0, files[0]);
	const pid_t p1 = runPeer(1, files[1]);
	// ---- the joiner's game dies 20 s in (SIGKILL: a crash, no goodbye) ----
	NetSleepMilliseconds(20000);
	kill(p1, SIGKILL);
	waitExit(p1);
	// ---- the game restarts: a new shell with the session the host program kept; Online, the same name ----
	OnlinePlayer again(*s, lobby.port, 33, u"Joiner");
	again.client = OnlineLobbyClient(o);
	again.ctx.sessionToken = joinerSession;
	signIn(again, u"Joiner");
	CHECK(again.hasNote("online-session", "the kept lobby session of Joiner is used"));
	CHECK(again.client.sessionToken() == joinerSession); // the same player, no new guest
	CHECK(again.client.sentCount("signin") == 0);
	// the running game is offered at once (GuiFX's Yes / No box)
	REQUIRE_MESSAGE(runUntil({ &again }, [&] { return !again.sh->boxesShown().empty(); }, 300), again.notes());
	CHECK(again.sh->boxesShown().back() == "rejoin-offer|Your game \"Host\" is still running. Rejoin it now?");
	again.dismissBox("No"); // the player looks at the server browser first
	CHECK(startOf(again.fx.services).empty());
	openPlay(again);
	REQUIRE_MESSAGE(runUntil({ &again }, [&] {
		const auto &rows = again.sh->gameRows();
		return std::any_of(rows.begin(), rows.end(), [](const std::string &r) { return r.size() > 7 && r.compare(r.size() - 7, 7, "|rejoin") == 0; });
	}, 300), again.notes());
	const auto &rows = again.sh->gameRows();
	const int row = (int)(std::find_if(rows.begin(), rows.end(), [](const std::string &r) { return r.size() > 7 && r.compare(r.size() - 7, 7, "|rejoin") == 0; }) - rows.begin());
	CHECK(listText(again.sh->gameList(), row, 3) == "Host  - REJOIN");
	GadgetListBoxSetSelected(again.sh->gameList(), row);
	REQUIRE(again.clickWhenShown("Controls.JoinGame")); // the page's own Join Game
	REQUIRE_MESSAGE(runUntil({ &again }, [&] { return !startOf(again.fx.services).empty(); }, 300), again.notes());
	CHECK(again.hasNote("online-rejoin", "rejoin game"));
	const std::string rejoinStart = startOf(again.fx.services);
	CHECK(rejoinStart.find("\"rejoin\":1") != std::string::npos);
	CHECK(rejoinStart.find("\"slot\":1") != std::string::npos);
	std::ofstream(files[2]) << rejoinStart;
	// ---- the restarted peer plays the rejoin START: catch-up from the relay's log, then the lockstep ----
	const pid_t p2 = runPeer(2, files[2]);
	const int rc0 = waitExit(p0), rc2 = waitExit(p2);
	const std::string r0 = readText(reports[0]), r2 = readText(reports[2]);
	MESSAGE("host peer:\n" << r0 << "\nrejoined peer:\n" << r2);
	CHECK(rc0 == 0);
	CHECK(rc2 == 0);
	CHECK(reportField(r2, "rejoin").rfind("generation 1 live_frame ", 0) == 0);
	CHECK(reportField(r0, "frames") == std::to_string(kFrames));
	CHECK(reportField(r2, "frames") == std::to_string(kFrames));
	CHECK(reportField(r0, "desyncs") == "0");
	CHECK(reportField(r2, "desyncs") == "0");
	CHECK(!reportField(r0, "final_hash").empty());
	CHECK(reportField(r0, "final_hash") == reportField(r2, "final_hash"));
	// the claim: the rejoined state was checked against the host's (ONLINE-3's online3_e2e.sh check)
	// tolerance: how many CRC exchanges follow the rejoin depends on the frame the packet router names in real time
	CHECK(std::atoi(reportField(r2, "crc_checks_passed").c_str()) >= 1);
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}
#endif

#ifdef ONLINE4_POSIX
// ---- lane ONLINE-5: the staging room as three players met it (the owner's game of 2026-10-13) ---------------------------------------------------------
namespace
{
// the clip of the level whose target path ends in `suffix` (null: none)
AptCharacterInst *clipEnding(AptCharacterInst *c, const std::string &suffix)
{
	if (!c)
	{
		return nullptr;
	}
	const std::string path = c->targetPath();
	if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
	{
		return c;
	}
	if (AptSpriteInst *sp = c->asSprite())
	{
		for (AptCharacterInst *k : sp->children())
		{
			if (AptCharacterInst *hit = clipEnding(k, suffix))
			{
				return hit;
			}
		}
	}
	return nullptr;
}

// the label of the timeline segment the slot's Ready box shows in the movie ("" when the box does not exist): what the player sees in the Ready column
std::string readyBoxShown(OnlinePlayer &p, int slot)
{
	AptCharacterInst *c = clipEnding(p.fx.wm->apt().level(p.sh->level()), "~" + std::to_string(slot) + ".Ready");
	AptSpriteInst *sp = c ? c->asSprite() : nullptr;
	if (!sp || !sp->frames || !sp->globallyVisible())
	{
		return "";
	}
	std::string best;
	int bestFrame = -1;
	for (const auto &kv : p.fx.wm->apt().labelsOf(sp->frames))
	{
		if (kv.second <= sp->frame && kv.second > bestFrame)
		{
			bestFrame = kv.second;
			best = kv.first;
		}
	}
	return best;
}

bool gadgetEnabled(GameWindow *w)
{
	return w && (w->winGetStatus() & WIN_STATUS_ENABLED) != 0;
}

// a signed-in player joins the one listed game through the page's own Join Game button
void joinListed(OnlinePlayer &p, std::vector<OnlinePlayer *> all, const std::u16string &name)
{
	signInAndOpenPlay(p, name);
	REQUIRE_MESSAGE(runUntil(all, [&] { return p.sh->gameRows().size() == 1; }, 300), p.notes());
	GadgetListBoxSetSelected(p.sh->gameList(), 0);
	REQUIRE(p.clickWhenShown("Main.Controls.JoinGame"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return p.client.inGame() && p.sh->openPlayState() == 12; }, 300), p.notes());
}
} // namespace

TEST_CASE("online5 retail e2e: three players in the staging room: the Ready column's boxes (RW 0x84211D), the two joiners ready through their own box, the "
		  "host's Play Game refused by name while one is not ready, a change unreadies, a press for another player's slot is refused; then the START "
		  "through a local relay and three openbfme_peer processes in lockstep")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	const std::string peer = envOr("OPENBFME_PEER", kPeerExe), relayExe = envOr("OPENBFME_RELAY", kRelayExe);
	if (peer.empty() || relayExe.empty() || !std::filesystem::exists(peer) || !std::filesystem::exists(relayExe))
	{
		MESSAGE("SKIP LOUDLY: no openbfme_peer / openbfme_relay here (OPENBFME_PEER / OPENBFME_RELAY), so the three-player online game was not played");
		return;
	}
	const std::string base = "\"" + peer + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL") + "\"";
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme-online5-e2e-" + std::to_string(getpid()));
	std::filesystem::create_directories(dir / "relaylog");
	const std::string profileFile = (dir / "profile.txt").string();
	REQUIRE(std::system((base + " --print-profile > \"" + profileFile + "\"").c_str()) == 0);
	std::string profile = readText(profileFile);
	profile = profile.substr(0, profile.find('\n'));
	REQUIRE(profile.size() == 32);
	{
		std::ofstream k(dir / "keys.txt");
		k << "1:" << kTicketKeyHex << "\n";
	}
	int relayPort = 0;
	{
		UDP tmp;
		std::string error;
		REQUIRE(tmp.bind(0x7F000001u, 0, &error));
		relayPort = tmp.localAddress().port;
	}
	const std::string relayBind = "127.0.0.1:" + std::to_string(relayPort);
	const pid_t relay = spawn("\"" + relayExe + "\" --bind " + relayBind + " --keys \"" + (dir / "keys.txt").string() + "\" --log-dir \"" + (dir / "relaylog").string() +
							  "\" --status \"" + (dir / "relay-status.json").string() + "\" --stats-s 0 --idle-s 60 > \"" + (dir / "relay.log").string() + "\" 2>&1");
	REQUIRE(relay > 0); // tolerance: fork() returns the child's pid (>0); -1 is the failure
	struct Stop
	{
		pid_t pid;
		~Stop()
		{
			kill(pid, SIGTERM);
			int st = 0;
			waitpid(pid, &st, 0);
		}
	} stopRelay{ relay };
	NodeLobby lobby("local=" + relayBind, "");
	OnlinePlayer host(*s, lobby.port, 51, u""), a(*s, lobby.port, 52, u""), b(*s, lobby.port, 53, u"");
	OnlineLobbyClient::Options o = clientOptions(s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	o.profileDigest = profile;
	host.client = OnlineLobbyClient(o);
	a.client = OnlineLobbyClient(o);
	b.client = OnlineLobbyClient(o);
	const std::vector<OnlinePlayer *> all{ &host, &a, &b };
	host.fx.text.set("GUI:NotifiedStartIntent", "The host wants to start the game.");
	signInAndOpenPlay(host, u"Host");
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	joinListed(a, all, u"JoinerA");
	joinListed(b, all, u"JoinerB");
	REQUIRE(a.client.localSlot() == 1);
	REQUIRE(b.client.localSlot() == 2);
	REQUIRE(runUntil(all, [&] { return host.sh->setup()->info().slots[2].state == SLOT_PLAYER && a.sh->setup()->info().slots[2].state == SLOT_PLAYER; }, 300));
	// ---- the Ready column: what the engine told the movie (RW 0x84211D) and what each player's movie shows ----
	auto icons = [](OnlinePlayer &p) {
		std::string out;
		for (int i = 0; i < 4; ++i)
		{
			out += p.sh->readyIcon(i) + (i < 3 ? " " : "");
		}
		return out;
	};
	REQUIRE_MESSAGE(runUntil(all, [&] { return readyBoxShown(a, 1) == "_enabledUnchecked" && readyBoxShown(b, 2) == "_enabledUnchecked" && readyBoxShown(host, 2) == "_disabledUnchecked"; }, 300),
		icons(host) << " | " << icons(a) << " | " << icons(b) << "\n" << a.notes() << "\n" << a.fx.wm->apt().dumpTree(a.fx.wm->apt().level(a.sh->level()), 9));
	CHECK(icons(host) == "_disabledChecked _disabledUnchecked _disabledUnchecked _none");
	CHECK(icons(a) == "_disabledChecked _enabledUnchecked _disabledUnchecked _none");
	CHECK(icons(b) == "_disabledChecked _disabledUnchecked _enabledUnchecked _none");
	CHECK(readyBoxShown(host, 0) == "_disabledChecked");
	CHECK(readyBoxShown(host, 1) == "_disabledUnchecked");
	CHECK(readyBoxShown(a, 0) == "_disabledChecked");
	CHECK(readyBoxShown(a, 2) == "_disabledUnchecked");
	CHECK_FALSE(host.hasNote("movie-function-missing"));
	// ---- the host's Play Game while nobody is ready: refused by name (RW 0x8431B8: GUI:NotifiedStartIntent), every player reads why, no START asked ----
	REQUIRE(host.clickWhenShown("LowerButtons_Host.PlayGame"));
	REQUIRE(runUntil(all, [&] { return !host.sh->startRefusals().empty(); }, 300));
	CHECK(host.sh->startRefusals().back() == "GUI:NotifiedStartIntent");
	const std::string notice = "[Host] The host wants to start the game.";
	REQUIRE(runUntil(all, [&] {
		return std::count(a.sh->chatLines().begin(), a.sh->chatLines().end(), notice) == 1 && std::count(b.sh->chatLines().begin(), b.sh->chatLines().end(), notice) == 1;
	}, 300));
	CHECK(host.client.sentCount("start") == 0);
	CHECK(host.fx.services.countRequests(ShellAction::OnlineGameStart) == 0);
	// ---- joiner A readies through its own box (the movie's button in the Ready column) ----
	REQUIRE_MESSAGE(a.click("~1.Ready"), a.fx.wm->apt().dumpTree(a.fx.wm->apt().level(a.sh->level()), 9));
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.client.slotViews()[1].ready && readyBoxShown(host, 1) == "_disabledChecked" && readyBoxShown(a, 1) == "_enabledChecked" &&
											 readyBoxShown(b, 1) == "_disabledChecked"; }, 300), icons(host) << " | " << icons(a) << " | " << icons(b) << "\n" << a.notes());
	CHECK(a.client.sentCount("ready") == 1);
	CHECK_FALSE(b.client.slotViews()[2].ready);
	// a player who is ready cannot change its row until it unreadies (RW 0x843F6E: every gadget off)
	REQUIRE(a.sh->slotGadget(1, "PlayerTemplate"));
	CHECK_FALSE(gadgetEnabled(a.sh->slotGadget(1, "PlayerTemplate")));
	CHECK_FALSE(gadgetEnabled(a.sh->slotGadget(1, "Color")));
	CHECK(gadgetEnabled(b.sh->slotGadget(2, "PlayerTemplate")));
	// ---- one of two ready: Play Game is still refused by name ----
	REQUIRE(host.clickWhenShown("LowerButtons_Host.PlayGame"));
	REQUIRE(runUntil(all, [&] { return host.sh->startRefusals().size() == 2; }, 300));
	CHECK(host.sh->startRefusals().back() == "GUI:NotifiedStartIntent");
	CHECK(host.client.sentCount("start") == 0);
	// ---- a press that names another player's slot is refused by name and nothing is sent: the room is unchanged ----
	// (the second notice has reached everyone first: it moves the room's seq)
	REQUIRE(runUntil(all, [&] { return std::count(a.sh->chatLines().begin(), a.sh->chatLines().end(), notice) == 2 && std::count(b.sh->chatLines().begin(), b.sh->chatLines().end(), notice) == 2 &&
								   std::count(host.sh->chatLines().begin(), host.sh->chatLines().end(), notice) == 2; }, 300));
	const std::uint32_t seqBefore = host.client.viewSeq();
	REQUIRE(a.fx.wm->invokeCallback("MpGameSetup::OnReadyPress", "2"));
	REQUIRE(b.fx.wm->invokeCallback("MpGameSetup::OnReadyPress", "1"));
	REQUIRE(host.fx.wm->invokeCallback("MpGameSetup::OnReadyPress", "1"));
	REQUIRE(a.fx.wm->invokeCallback("MpGameSetup::OnReadyPress", "9"));
	for (OnlinePlayer *p : all)
	{
		p->tick(40);
	}
	CHECK(a.hasNote("online-ready-refused", "MpGameSetup::OnReadyPress(2)"));
	CHECK(a.hasNote("online-ready-refused", "MpGameSetup::OnReadyPress(9): not a player slot"));
	CHECK(b.hasNote("online-ready-refused", "MpGameSetup::OnReadyPress(1)"));
	CHECK(host.hasNote("online-ready-refused", "MpGameSetup::OnReadyPress(1)"));
	CHECK(a.client.sentCount("ready") == 1);
	CHECK(b.client.sentCount("ready") == 0);
	CHECK(host.client.sentCount("ready") == 0);
	CHECK(host.client.viewSeq() == seqBefore);
	CHECK(host.client.slotViews()[1].ready);
	CHECK_FALSE(host.client.slotViews()[2].ready);
	// ---- the host changes the game (its own army): everyone is unready again, and A's row is A's to change again ----
	REQUIRE(host.sh->slotGadget(0, "PlayerTemplate"));
	GadgetComboBoxSetSelectedPos(host.sh->slotGadget(0, "PlayerTemplate"), 2, false);
	REQUIRE_MESSAGE(runUntil(all, [&] { return !a.client.slotViews()[1].ready && readyBoxShown(a, 1) == "_enabledUnchecked" && readyBoxShown(host, 1) == "_disabledUnchecked"; }, 300),
		icons(host) << " | " << icons(a));
	CHECK(gadgetEnabled(a.sh->slotGadget(1, "PlayerTemplate")));
	// ---- both joiners ready through their boxes; the host starts ----
	REQUIRE(a.click("~1.Ready"));
	REQUIRE(b.click("~2.Ready"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.client.slotViews()[1].ready && host.client.slotViews()[2].ready && readyBoxShown(host, 1) == "_disabledChecked" &&
											 readyBoxShown(host, 2) == "_disabledChecked"; }, 300), icons(host) << "\n" << b.notes());
	REQUIRE(host.clickWhenShown("LowerButtons_Host.PlayGame"));
	REQUIRE_MESSAGE(runUntil(all, [&] {
		return host.fx.services.countRequests(ShellAction::OnlineGameStart) == 1 && a.fx.services.countRequests(ShellAction::OnlineGameStart) == 1 &&
			b.fx.services.countRequests(ShellAction::OnlineGameStart) == 1;
	}, 900), host.notes() << "\n" << a.notes());
	CHECK(host.sh->startRefusals().size() == 2);
	// ---- the three STARTs play in lockstep through the relay ----
	const int kFrames = 300;
	std::vector<std::string> reports;
	std::vector<pid_t> peers;
	int n = 0;
	for (OnlinePlayer *p : all)
	{
		const std::string start = startOf(p->fx.services);
		REQUIRE(start.find("\"slot\":" + std::to_string(n)) != std::string::npos);
		const std::string file = (dir / ("start-" + std::to_string(n) + ".json")).string();
		std::ofstream(file) << start;
		reports.push_back((dir / ("report-" + std::to_string(n) + ".txt")).string());
		peers.push_back(spawn(base + " --online-start \"" + file + "\" --script --frames " + std::to_string(kFrames) + " --report \"" + reports.back() + "\" > \"" +
							  (dir / ("peer-" + std::to_string(n) + ".log")).string() + "\" 2>&1"));
		++n;
	}
	std::vector<std::string> texts;
	for (size_t i = 0; i < peers.size(); ++i)
	{
		CHECK(waitExit(peers[i]) == 0);
		texts.push_back(readText(reports[i]));
		MESSAGE("peer " << i << ":\n" << texts.back());
		CHECK(reportField(texts.back(), "frames") == std::to_string(kFrames));
		CHECK(reportField(texts.back(), "desyncs") == "0");
		CHECK(reportField(texts.back(), "relay").find(" bound 1 ") != std::string::npos);
	}
	REQUIRE(texts.size() == 3);
	CHECK(!reportField(texts[0], "final_hash").empty());
	CHECK(reportField(texts[0], "final_hash") == reportField(texts[1], "final_hash"));
	CHECK(reportField(texts[0], "final_hash") == reportField(texts[2], "final_hash"));
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}
#endif

#ifdef ONLINE4_POSIX
TEST_CASE("online5 retail: start positions in the online staging room (RW 0x976F59: the MAP tab's spots while no game runs): a joiner takes a spot on the "
		  "map, every player's map shows whose it is, a taken spot stays its holder's (on the screen and against a raw claim), the host places its AI, and "
		  "the START carries the positions")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("local=127.0.0.1:9", ""); // the START is built without a word to the relay
	OnlinePlayer host(*s, lobby.port, 61, u""), a(*s, lobby.port, 62, u""), b(*s, lobby.port, 63, u"");
	const std::vector<OnlinePlayer *> all{ &host, &a, &b };
	signInAndOpenPlay(host, u"Host");
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	joinListed(a, all, u"JoinerA");
	joinListed(b, all, u"JoinerB");
	const MapCacheEntry *map = host.sh->setup()->findMap(host.client.gameInfo().mapName);
	REQUIRE(map);
	REQUIRE(map->isMultiplayer);
	const int spotsOfMap = (int)map->numPlayers;
	REQUIRE(spotsOfMap >= 3); // tolerance: the test needs three spots; the exact count is the map's and is asserted spot by spot below
	// ---- the MAP tab: every player's map window shows the map's start spots, and only those ----
	auto openMap = [&](OnlinePlayer &p) {
		p.tick(20);
		if (p.reachable("TabButtons.MapTab")) // the tab that is up has no button (the host's room opens on its map)
		{
			REQUIRE_MESSAGE(p.click("TabButtons.MapTab"), p.notes());
		}
		REQUIRE_MESSAGE(runUntil(all, [&] { return p.sh->currentMapWindow() && p.sh->currentMapSpots().size() == 8 && !p.sh->currentMapSpots()[0]->winIsHidden(); }, 300),
			p.notes());
	};
	for (OnlinePlayer *p : all)
	{
		openMap(*p);
		for (int i = 0; i < 8; ++i)
		{
			const bool beyondTheMap = i >= spotsOfMap;
			CHECK_MESSAGE(p->sh->currentMapSpots()[(size_t)i]->winIsHidden() == beyondTheMap, "spot " << i);
		}
	}
	auto spotText = [](OnlinePlayer &p, int spot) { return OnlineLobbyClient::utf16ToUtf8(p.sh->currentMapSpots()[(size_t)spot]->winGetText()); };
	auto clickSpot = [&](OnlinePlayer &p, int spot) {
		starttest::clickWindowCentre(p.fx, p.sh->currentMapSpots()[(size_t)spot]);
		for (OnlinePlayer *q : all)
		{
			q->tick(30);
		}
	};
	auto positions = [](OnlinePlayer &p) {
		std::string out;
		for (int i = 0; i < 3; ++i)
		{
			out += std::to_string(p.client.gameInfo().slots[i].startPos) + (i < 2 ? " " : "");
		}
		return out;
	};
	const std::string n1 = "1", n2 = "2", n3 = "3";
	for (OnlinePlayer *p : all)
	{
		p->fx.text.set("NUMBER:1", n1);
		p->fx.text.set("NUMBER:2", n2);
		p->fx.text.set("NUMBER:3", n3);
	}
	// ---- joiner A (slot 1) clicks spot 2: it is A's in the lobby and on every player's map ----
	clickSpot(a, 1);
	REQUIRE_MESSAGE(runUntil(all, [&] { return positions(host) == "-1 1 -1" && positions(b) == "-1 1 -1" && positions(a) == "-1 1 -1"; }, 300),
		positions(host) << " | " << positions(a) << " | " << positions(b) << "\n" << a.notes());
	CHECK(a.client.sentCount("slot") == 1);
	for (OnlinePlayer *p : all)
	{
		REQUIRE(runUntil(all, [&] { return spotText(*p, 1) == n2; }, 300));
		CHECK(spotText(*p, 0).empty());
		CHECK(spotText(*p, 2).empty());
	}
	// ---- joiner B clicks A's spot: nothing is asked of the lobby and nothing changes; the host cannot move a player either ----
	clickSpot(b, 1);
	clickSpot(host, 1);
	CHECK(b.client.sentCount("slot") == 0);
	CHECK(host.client.sentCount("slot") == 0);
	CHECK(positions(host) == "-1 1 -1");
	CHECK(spotText(b, 1) == n2);
	// a raw claim of A's spot by B (a client that skips the screen's own rule): the lobby refuses it by name, B reads why, the room is unchanged
	const std::uint32_t seqBefore = host.client.viewSeq();
	b.client.setOwnSlot(OnlineLobbyClient::kKeep, OnlineLobbyClient::kKeep, 1, OnlineLobbyClient::kKeep);
	REQUIRE(runUntil(all, [&] { return b.hasNote("online-refused", "slot: spot_taken: start position 2 is JoinerA's"); }, 300));
	CHECK(std::count(b.sh->chatLines().begin(), b.sh->chatLines().end(), "start position 2 is JoinerA's") == 1);
	for (OnlinePlayer *p : all)
	{
		p->tick(30);
	}
	CHECK(positions(host) == "-1 1 -1");
	CHECK(host.client.viewSeq() == seqBefore);
	// ---- B takes spot 3, the host spot 1; each map shows the three numbers ----
	clickSpot(b, 2);
	clickSpot(host, 0);
	REQUIRE_MESSAGE(runUntil(all, [&] { return positions(host) == "0 1 2" && positions(a) == "0 1 2" && positions(b) == "0 1 2"; }, 300),
		positions(host) << " | " << positions(a) << " | " << positions(b));
	for (OnlinePlayer *p : all)
	{
		REQUIRE(runUntil(all, [&] { return spotText(*p, 0) == n1 && spotText(*p, 1) == n2 && spotText(*p, 2) == n3; }, 300));
	}
	// ---- A's own spot again: a joiner has no other slot to pass it to (RW 0x975C3A), so it is free again: back to a random start ----
	clickSpot(a, 1);
	REQUIRE_MESSAGE(runUntil(all, [&] { return positions(host) == "0 -1 2"; }, 300), positions(host) << "\n" << a.notes());
	REQUIRE(runUntil(all, [&] { return spotText(host, 1).empty(); }, 300));
	clickSpot(a, 1);
	REQUIRE(runUntil(all, [&] { return positions(host) == "0 1 2"; }, 300));
	// ---- both ready, the host starts: every START carries the three positions as OnlineStart reads them ----
	REQUIRE(a.clickWhenShown("TabButtons.ChatTab"));
	REQUIRE(b.clickWhenShown("TabButtons.ChatTab"));
	REQUIRE(runUntil(all, [&] { return a.sh->readyIcon(1) == "_enabledUnchecked" && b.sh->readyIcon(2) == "_enabledUnchecked"; }, 300));
	REQUIRE(a.click("~1.Ready"));
	REQUIRE(b.click("~2.Ready"));
	REQUIRE(runUntil(all, [&] { return host.client.slotViews()[1].ready && host.client.slotViews()[2].ready; }, 300));
	REQUIRE(host.clickWhenShown("LowerButtons_Host.PlayGame"));
	REQUIRE_MESSAGE(runUntil(all, [&] {
		return host.fx.services.countRequests(ShellAction::OnlineGameStart) == 1 && a.fx.services.countRequests(ShellAction::OnlineGameStart) == 1 &&
			b.fx.services.countRequests(ShellAction::OnlineGameStart) == 1;
	}, 900), host.notes());
	for (OnlinePlayer *p : all)
	{
		const std::string start = startOf(p->fx.services);
		OnlineStart::Limits limits;
		limits.playerTemplateCount = s->world->playerTemplates().getPlayerTemplateCount();
		limits.colorCount = (int)s->settings.multiplayerColors.size();
		LobbyStart parsed;
		std::string error;
		ProfileIdentity identity;
		identity.digest = p->client.options().profileDigest;
		REQUIRE_MESSAGE(OnlineStart::parse(start, identity, limits, parsed, &error), error);
		CHECK(parsed.game.game.slots[0].startPos == 0);
		CHECK(parsed.game.game.slots[1].startPos == 1);
		CHECK(parsed.game.game.slots[2].startPos == 2);
	}
}
#endif

#ifdef ONLINE4_POSIX
TEST_CASE("online5 retail: the staging room's rows and buttons: the chat's Players list names the room, a row's drop-downs work for its owner and are "
		  "locked for the others, Hero and Handicap are locked by name, and the Messenger and Connections buttons say what they are (nothing silent)")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer host(*s, lobby.port, 71, u""), a(*s, lobby.port, 72, u"");
	const std::vector<OnlinePlayer *> all{ &host, &a };
	for (const char *stop : { "[S-4550]", "[S-4551]", "[S-4552]", "[S-4553]", "[S-4554]" })
	{
		CHECK_MESSAGE(host.hasNote("online-stops", stop), stop);
	}
	signInAndOpenPlay(host, u"Host");
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	joinListed(a, all, u"JoinerA");
	// ---- the chat tab's Players list: the room's players, on both screens; it follows a leave ----
	REQUIRE(host.clickWhenShown("TabButtons.ChatTab"));
	auto players = [](OnlinePlayer &p) {
		std::string out;
		GameWindow *list = p.sh->chatPlayersWindow();
		for (int i = 0; list && i < GadgetListBoxGetNumEntries(list); ++i)
		{
			out += OnlineLobbyClient::utf16ToUtf8(GadgetListBoxGetText(list, i, 0)) + ";";
		}
		return list ? out : std::string("(no list)");
	};
	REQUIRE_MESSAGE(runUntil(all, [&] { return players(host) == "Host;JoinerA;" && players(a) == "Host;JoinerA;"; }, 300), players(host) << " | " << players(a));
	// ---- the joiner goes to the MAP tab and back to CHAT: the lists are made again while the tab plays in, and show from their first row (the
	// whole chat history and both players), not scrolled to the last one ----
	REQUIRE(runUntil(all, [&] { return a.sh->chatLines().size() >= 2; }, 300)); // tolerance: the lobby's own lines so far ("created", "joined")
	REQUIRE(a.clickWhenShown("TabButtons.MapTab"));
	a.tick(30);
	REQUIRE(a.clickWhenShown("TabButtons.ChatTab"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return players(a) == "Host;JoinerA;" && a.sh->chatListWindow() &&
		GadgetListBoxGetNumEntries(a.sh->chatListWindow()) == (int)a.sh->chatLines().size(); }, 300), players(a));
	a.tick(60); // the tab's clip has played in
	REQUIRE(a.sh->chatPlayersWindow());
	CHECK(GadgetListBoxGetTopVisibleEntry(a.sh->chatPlayersWindow()) == 0);
	CHECK(GadgetListBoxGetTopVisibleEntry(a.sh->chatListWindow()) == 0);
	CHECK(GadgetListBoxGetText(a.sh->chatListWindow(), 0, 0) == u"Host created the game");
	// ---- the rows: the owner's drop-downs are enabled, everyone else's are locked; Hero and Handicap are locked for everyone ----
	REQUIRE(runUntil(all, [&] { return host.sh->slotGadget(1, "Team") && a.sh->slotGadget(1, "Team") && a.sh->slotGadget(0, "Handicap"); }, 300));
	for (const char *leaf : { "PlayerTemplate", "Team", "Color" })
	{
		CHECK_MESSAGE(gadgetEnabled(host.sh->slotGadget(0, leaf)), leaf);  // the host's own row
		CHECK_MESSAGE(!gadgetEnabled(host.sh->slotGadget(1, leaf)), leaf); // a player's row is not the host's
		CHECK_MESSAGE(gadgetEnabled(host.sh->slotGadget(2, leaf)), leaf);  // an open slot is the host's to fill (an AI's choices)
		CHECK_MESSAGE(gadgetEnabled(a.sh->slotGadget(1, leaf)), leaf);     // the joiner's own row
		CHECK_MESSAGE(!gadgetEnabled(a.sh->slotGadget(0, leaf)), leaf);
		CHECK_MESSAGE(!gadgetEnabled(a.sh->slotGadget(2, leaf)), leaf);
	}
	for (int slot = 0; slot < 3; ++slot)
	{
		for (OnlinePlayer *p : all)
		{
			REQUIRE(p->sh->slotGadget(slot, "Hero"));
			REQUIRE(p->sh->slotGadget(slot, "Handicap"));
			CHECK_MESSAGE(!gadgetEnabled(p->sh->slotGadget(slot, "Hero")), "slot " << slot);     // S-4076 / S-4552
			CHECK_MESSAGE(!gadgetEnabled(p->sh->slotGadget(slot, "Handicap")), "slot " << slot); // S-178 / S-4552
			CHECK(GadgetComboBoxGetLength(p->sh->slotGadget(slot, "Handicap")) == 0);
		}
	}
	// the joiner's team and colour through its own combos reach the host's screen (a row's drop-downs work for its owner)
	GadgetComboBoxSetSelectedPos(a.sh->slotGadget(1, "Team"), 2, false);
	const int team = a.sh->setup()->info().slots[1].teamNumber;
	REQUIRE(team == 1); // the combo's rows: "-" (no team), then the teams from 1: row 2 is team index 1
	REQUIRE(runUntil(all, [&] { return host.sh->setup()->info().slots[1].teamNumber == team; }, 300));
	// ---- the RULES tab: the online panel's flag 0x40 shows the Clan Game rule (RW 0x441ECE's twin: hidden only without it, as in LAN and skirmish) ----
	REQUIRE(host.clickWhenShown("TabButtons.RulesTab"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.sh->rulesPopulated() && host.sh->ruleWindow(RULE_CLAN_GAME) != nullptr; }, 300), host.notes());
	CHECK_FALSE(host.sh->ruleWindow(RULE_CLAN_GAME)->winIsHidden());
	CHECK(gadgetEnabled(host.sh->ruleWindow(RULE_CLAN_GAME)));
	REQUIRE(host.clickWhenShown("TabButtons.ChatTab"));
	// ---- the Messenger button: its command says the Messenger is not available (it used to reach no handler) ----
	const size_t boxes = host.sh->boxesShown().size();
	REQUIRE(host.fx.wm->invokeCallback("AptMessenger::OnMessengerBttn", ""));
	host.tick(5);
	REQUIRE(host.sh->boxesShown().size() == boxes + 1);
	CHECK(host.sh->boxesShown().back() == "GUI:GSErrorTitle|not available: the Messenger");
	CHECK(host.hasNote("unported", "the Messenger is not available in OpenBFME's online play [S-4075]"));
	host.dismissBox();
	// ---- the Connections button (the movie's own button): refused by name, and the staging room comes back whole ----
	REQUIRE(host.clickWhenShown("LowerButtons_Host.Connections"));
	REQUIRE(runUntil(all, [&] { return host.sh->boxesShown().size() == boxes + 2; }, 300));
	CHECK(host.sh->boxesShown().back() ==
		  "GUI:GSErrorTitle|not available: the Connections screen (online games go through the relay server, players do not connect to each other)");
	CHECK(host.hasNote("unported", "[S-4550]"));
	host.dismissBox();
	REQUIRE_MESSAGE(host.waitShown("LowerButtons_Host.PlayGame", 600), host.notes());
	REQUIRE(host.waitShown("LowerButtons_Host.Connections", 600));
	CHECK(host.client.inGame());
	CHECK(host.sh->openPlayState() == 6);
	CHECK_FALSE(host.hasNote("movie-function-missing"));
	REQUIRE(runUntil(all, [&] { return host.sh->slotGadget(1, "Team") != nullptr; }, 300));
	CHECK(host.sh->setup()->info().slots[1].teamNumber == team);
	// ---- the joiner leaves through its Cancel: the host's Players list is the host alone ----
	REQUIRE(a.clickWhenShown("LowerButtons_Join.CancelJoin"));
	REQUIRE(runUntil(all, [&] { return !a.client.inGame(); }, 300));
	REQUIRE(host.clickWhenShown("TabButtons.ChatTab"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return players(host) == "Host;"; }, 300), players(host));
}
#endif

#ifdef ONLINE4_POSIX
TEST_CASE("online5 retail e2e rejoin: three players through a local relay; one is killed mid-game, comes back with a rejoin START and catches up; all "
		  "three end on one hash")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	const std::string peer = envOr("OPENBFME_PEER", kPeerExe), relayExe = envOr("OPENBFME_RELAY", kRelayExe);
	if (peer.empty() || relayExe.empty() || !std::filesystem::exists(peer) || !std::filesystem::exists(relayExe))
	{
		MESSAGE("SKIP LOUDLY: no openbfme_peer / openbfme_relay here (OPENBFME_PEER / OPENBFME_RELAY), so the three-player rejoin was not played");
		return;
	}
	const std::string base = "\"" + peer + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL") + "\"";
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme-online5-rejoin-" + std::to_string(getpid()));
	std::filesystem::create_directories(dir / "relaylog");
	REQUIRE(std::system((base + " --print-profile > \"" + (dir / "profile.txt").string() + "\"").c_str()) == 0);
	std::string profile = readText((dir / "profile.txt").string());
	profile = profile.substr(0, profile.find('\n'));
	{
		std::ofstream k(dir / "keys.txt");
		k << "1:" << kTicketKeyHex << "\n";
	}
	int relayPort = 0;
	{
		UDP tmp;
		std::string error;
		REQUIRE(tmp.bind(0x7F000001u, 0, &error));
		relayPort = tmp.localAddress().port;
	}
	const std::string relayBind = "127.0.0.1:" + std::to_string(relayPort);
	const pid_t relay = spawn("\"" + relayExe + "\" --bind " + relayBind + " --keys \"" + (dir / "keys.txt").string() + "\" --log-dir \"" + (dir / "relaylog").string() +
							  "\" --status \"" + (dir / "relay-status.json").string() + "\" --stats-s 0 --idle-s 120 > \"" + (dir / "relay.log").string() + "\" 2>&1");
	struct Stop
	{
		pid_t pid;
		~Stop()
		{
			kill(pid, SIGTERM);
			int st = 0;
			waitpid(pid, &st, 0);
		}
	} stopRelay{ relay };
	NodeLobby lobby("local=" + relayBind, "");
	OnlineLobbyClient::Options o = clientOptions(s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	o.profileDigest = profile;
	OnlineLobbyClient host(o), a(o), b(o);
	auto pump = [&](OnlineLobbyClient &c) {
		for (int i = 0; i < 4; ++i)
		{
			c.update(NetMilliseconds());
			pumpHttp(c, lobby.port);
		}
		c.takeEvents();
	};
	host.signInGuest(u"Host");
	a.signInGuest(u"JoinerA");
	b.signInGuest(u"JoinerB");
	for (OnlineLobbyClient *c : { &host, &a, &b })
	{
		pump(*c);
	}
	SkirmishGameInfo info;
	// the game: the setup of a three-player staging room (the first multiplayer map with three spots)
	OnlinePlayer probe(*s, lobby.port, 81, u"");
	REQUIRE(probe.sh->setup());
	info = probe.sh->setup()->info();
	for (int i = 1; i < MAX_SLOTS; ++i) // as the screen's Create Game: the host in slot 0, every other slot open
	{
		info.slots[i] = SkirmishGameSlot();
		info.slots[i].state = SLOT_OPEN;
	}
	host.createGame(u"Three", "", info, 0);
	pump(host);
	REQUIRE(host.inGame());
	for (OnlineLobbyClient *c : { &a, &b })
	{
		c->joinGame(host.gameId(), "");
		pump(*c);
		REQUIRE(c->inGame());
	}
	a.setReady(true);
	b.setReady(true);
	pump(a);
	pump(b);
	pump(host);
	host.start();
	pump(host);
	NetSleepMilliseconds(150);
	pump(a);
	pump(b);
	REQUIRE(!host.startJson().empty());
	REQUIRE(!a.startJson().empty());
	REQUIRE(!b.startJson().empty());
	const std::string bSession = b.sessionToken();
	const int kFrames = 600; // 120 s paced
	std::string files[4], reports[4];
	const std::string starts[3] = { host.startJson(), a.startJson(), b.startJson() };
	for (int i = 0; i < 4; ++i)
	{
		files[i] = (dir / ("start-" + std::to_string(i) + ".json")).string();
		reports[i] = (dir / ("report-" + std::to_string(i) + ".txt")).string();
		if (i < 3)
		{
			std::ofstream(files[i]) << starts[i];
		}
	}
	auto runPeer = [&](int i) {
		return spawn(base + " --online-start \"" + files[i] + "\" --script --paced --frames " + std::to_string(kFrames) + " --logic-threads 2 --report \"" + reports[i] +
			"\" > \"" + (dir / ("peer-" + std::to_string(i) + ".log")).string() + "\" 2>&1");
	};
	const pid_t p0 = runPeer(0), p1 = runPeer(1), p2 = runPeer(2);
	NetSleepMilliseconds(50000); // three peers load in parallel first (about 30 s on a loaded 6-core machine): the kill comes mid-game
	kill(p2, SIGKILL);
	waitExit(p2);
	// joiner B's game restarts with its kept session and asks for its running game
	OnlineLobbyClient again(o);
	again.useSession(bSession);
	pump(again);
	REQUIRE(again.signedIn());
	REQUIRE(again.mine().size() == 1);
	again.rejoinGame(again.mine()[0].id);
	pump(again);
	REQUIRE(!again.startJson().empty());
	std::ofstream(files[3]) << again.startJson();
	const pid_t p3 = runPeer(3);
	const int rc0 = waitExit(p0), rc1 = waitExit(p1), rc3 = waitExit(p3);
	std::string r[4];
	for (int i : { 0, 1, 3 })
	{
		r[i] = readText(reports[i]);
		MESSAGE("peer " << i << " (rc " << (i == 0 ? rc0 : i == 1 ? rc1 : rc3) << "):\n" << r[i] << "\nlog tail:\n" << readText((dir / ("peer-" + std::to_string(i) + ".log")).string()).substr(0, 4000));
	}
	CHECK(rc0 == 0);
	CHECK(rc1 == 0);
	CHECK(rc3 == 0);
	CHECK(reportField(r[3], "rejoin").rfind("generation 1 live_frame ", 0) == 0);
	for (int i : { 0, 1, 3 })
	{
		CHECK(reportField(r[i], "frames") == std::to_string(kFrames));
		CHECK(reportField(r[i], "desyncs") == "0");
	}
	CHECK(!reportField(r[0], "final_hash").empty());
	CHECK(reportField(r[0], "final_hash") == reportField(r[1], "final_hash"));
	CHECK(reportField(r[0], "final_hash") == reportField(r[3], "final_hash"));
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}
#endif

#ifndef ONLINE4_POSIX
TEST_CASE("online4: the lobby client and the online screens on a local lobby (Node.js, POSIX processes)")
{
	MESSAGE("SKIP LOUDLY: the local lobby (server.mjs), the relay and the peers are started with fork / exec here: the online screen tests run on Linux");
}
#endif


#ifdef ONLINE4_POSIX
TEST_CASE("online5 retail: S-4555 is the menu frame's alone: the online login's 3D frame is a shell screen's View3D (stretched with the stage), the end "
		  "screen's victory and defeat rings are GuiFX's (retail's aspect)")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer p(*s, lobby.port, 91, u"");
	p.tick(120);
	auto view3d = [&]() {
		std::vector<std::pair<std::string, std::string>> out; // render object, path
		AptRenderList rl;
		p.fx.wm->apt().buildRenderList(rl);
		for (const AptRenderCommand &c : rl.commands)
		{
			if (c.kind == AptRenderCommand::Kind::Placeholder && c.symbolName == "View3D")
			{
				out.emplace_back(c.renderObject, c.path);
			}
		}
		return out;
	};
	auto menuLevels = [&]() {
		std::vector<int> levels;
		for (int i = 0; i < p.fx.shell->screenCount(); ++i)
		{
			levels.push_back(p.fx.shell->screenAt(i)->level());
		}
		return levels;
	};
	auto stretched = [&](const std::string &object) {
		for (const auto &v : view3d())
		{
			if (v.first == object)
			{
				return AptView3DOnMenuLevel(v.second, menuLevels()) ? 1 : 0;
			}
		}
		return -1; // not drawn
	};
	CHECK(stretched("SFE_MenuLogin") == 1);
	AptMessageBox box(*p.fx.wm, *p.fx.shell);
	for (int i = 0; i < 30 && !p.fx.wm->isAptWindowLoaded(box.level()); ++i)
	{
		p.tick(1);
	}
	std::string error;
	REQUIRE_MESSAGE(p.fx.wm->invokeAS(box.level(), "ShowEndGame", { "1", "", "" }, nullptr, &error), error); // RW 0x808E5B: "1" for a good side
	p.tick(15);
	CHECK(stretched("SFE_GoodVW") == 0);
	CHECK(stretched("SFE_MenuLogin") == 1);
	REQUIRE(p.fx.wm->invokeAS(box.level(), "ShowEndGame", { "0", "", "" }, nullptr, &error));
	p.tick(15);
	CHECK(stretched("SFE_EvilVW") == 0);
}
#endif
