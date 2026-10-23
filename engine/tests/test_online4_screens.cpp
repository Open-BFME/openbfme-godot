// OpenBFME unit tests. GPL-3.0.
// Lane ONLINE-4 (docs/ONLINE.md section 5, phase O2): RotWK's online screens (OnlineShell.apt with OnlineLogin / OnlineHome / OnlineOpenPlay and the
// MpGameSetup staging room, GameClient/GUI/AptScreens/AptOnlineShell.h) on the OpenBFME lobby service, through the lobby client
// (GameNetwork/OnlineLobbyClient.h), against a real local lobby: services/online/lobby/src/server.mjs run by Node.js (and, for the START, a real
// openbfme_relay and two openbfme_peer processes). The tests SKIP LOUDLY where Node.js, the retail installs or POSIX processes are missing.

#include "doctest.h"
#include "TestChild.h"
#include "ProcessTestUtil.h"

#include "GameClient/AptView3D.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/CreateAHeroHeroList.h"
#include "GameClient/GUI/AptScreens/AptCreateAHero.h"
#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/MenuNavigator.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GUI/ProfileSwitch.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/RelayProtocol.h"
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
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <atomic>
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
OnlineLobbyClient::Options clientOptions(int factions = 16, int colors = 16, PlatformKind platform = PlatformKind::None)
{
	OnlineLobbyClient::Options o;
	o.platform = platform; // lane PLATFORMICON-1
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

TEST_CASE("online4: install identity and pending notices (no lobby needed)")
{
	auto options = clientOptions(4, 4);
	options.installId = std::string(32, 'a');
	options.deviceHash = std::string(64, 'b');
	options.clientPlatform = "linux";
	options.clientVersion = "test-build";
	OnlineLobbyClient client(options);
	client.signInGuest(u"Guest");
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].body == "{\"name\":\"Guest\",\"install\":\"" + options.installId + "\",\"device\":\"" + options.deviceHash +
		"\",\"client\":{\"platform\":\"linux\",\"version\":\"test-build\"}}");
	const std::string answer = R"({"v":1,"token":"test-session-token","account":{"name":"Guest"},"notice":{"id":"N-1","text":"Please read this warning. test-session-token"}})";
	client.deliver(requests[0].id, 200, answer, "");
	auto events = client.takeEvents();
	REQUIRE(events.size() == 2);
	CHECK(events[0].kind == OnlineLobbyClient::Event::SignedIn);
	CHECK(events[1].kind == OnlineLobbyClient::Event::Notice);
	CHECK(events[1].text == u"Please read this warning. <redacted>");
	client.signInGuest(u"Guest");
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200, answer, "");
	CHECK(client.takeEvents().size() == 1);
	for (const std::string code : {"banned", "suspended", "muted"})
	{
		client.refreshList();
		requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		client.deliver(requests[0].id, 403, "{\"v\":1,\"error\":\"" + code + "\",\"message\":\"Reason\"}", "");
		events = client.takeEvents();
		REQUIRE(events.size() == 1);
		CHECK(events[0].kind == OnlineLobbyClient::Event::Refused);
		CHECK(events[0].code == code);
		CHECK(events[0].text == u"Reason");
	}
	options.installId = "invalid";
	OnlineLobbyClient invalid(options);
	invalid.signInGuest(u"Guest");
	CHECK(invalid.takeRequests().empty());
	events = invalid.takeEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].code == "bad_identity");
	options.installId = std::string(32, 'a');
	options.clientVersion = std::string(65, 'v');
	OnlineLobbyClient invalidVersion(options);
	invalidVersion.signInGuest(u"Guest");
	CHECK(invalidVersion.takeRequests().empty());
	events = invalidVersion.takeEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].code == "bad_identity");
	client.signInGuest(u"Guest");
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200, R"({"v":1,"token":"token","account":{"name":"Guest"},"notice":{"id":"N-2","text":5}})", "");
	CHECK_FALSE(client.signedIn());
	events = client.takeEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].code == "protocol");
}

// DEVINPR-1 (review of WP-C): AptOnlineShell keeps a session only when it was issued to this install; a session from before the client sent an
// install, or another install's, signs in again so the lobby's enforcement and reports see the install. ONLINEMENU-1 (PR #8 review finding 2): the
// token carries the install sealed ("ix") and a public tag of it ("ih", lobby.mjs installTag), never the install itself; a token that shows its
// install is an older lobby's (refused there) and is not kept
TEST_CASE("devinpr1: a session token names the install it was issued to (no lobby needed)")
{
	const std::string install(32, 'a');
	// {"id":"guest:0123456789abcdef","name":"Guest","kind":"guest","ix":"AA..A","ih":"63339ffe1fed3ce23c7546e36dde1ad3","iat":1,"exp":1}: the tag of
	// "aa..a" (HMAC-SHA256 keyed "openbfme-install-tag-v1", its first 16 bytes; lobby.mjs installTag gives the same)
	const std::string tagged = "eyJpZCI6Imd1ZXN0OjAxMjM0NTY3ODlhYmNkZWYiLCJuYW1lIjoiR3Vlc3QiLCJraW5kIjoiZ3Vlc3QiLCJpeCI6IkFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBIiwiaWgiOiI2MzMzOWZmZTFmZWQzY2UyM2M3NTQ2ZTM2ZGRlMWFkMyIsImlhdCI6MSwiZXhwIjoxfQ.c2ln";
	// the same with another install's tag
	const std::string otherTag = "eyJpZCI6Imd1ZXN0OjAxMjM0NTY3ODlhYmNkZWYiLCJuYW1lIjoiR3Vlc3QiLCJraW5kIjoiZ3Vlc3QiLCJpeCI6IkFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBIiwiaWgiOiIwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMCIsImlhdCI6MSwiZXhwIjoxfQ.c2ln";
	// {"id":..,"name":"Guest","kind":"guest","install":"aa..a","exp":1}: a token from before the seal
	const std::string clear = "eyJpZCI6Imd1ZXN0OjAxMjM0NTY3ODlhYmNkZWYiLCJuYW1lIjoiR3Vlc3QiLCJraW5kIjoiZ3Vlc3QiLCJpbnN0YWxsIjoiYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWEiLCJleHAiOjF9.c2ln";
	const std::string without = "eyJpZCI6Imd1ZXN0OjAxMjM0NTY3ODlhYmNkZWYiLCJuYW1lIjoiR3Vlc3QiLCJraW5kIjoiZ3Vlc3QiLCJleHAiOjF9.c2ln"; // no install at all
	CHECK(OnlineLobbyClient::sessionName(tagged) == u"Guest");
	CHECK(OnlineLobbyClient::sessionMatchesInstall(tagged, install));
	CHECK_FALSE(OnlineLobbyClient::sessionMatchesInstall(tagged, std::string(32, 'b')));
	CHECK_FALSE(OnlineLobbyClient::sessionMatchesInstall(tagged, ""));
	CHECK_FALSE(OnlineLobbyClient::sessionMatchesInstall(otherTag, install));
	CHECK_FALSE(OnlineLobbyClient::sessionMatchesInstall(clear, install));
	CHECK(OnlineLobbyClient::sessionName(without) == u"Guest");
	CHECK_FALSE(OnlineLobbyClient::sessionMatchesInstall(without, install));
	CHECK_FALSE(OnlineLobbyClient::sessionMatchesInstall("not a token", install));
	CHECK_FALSE(OnlineLobbyClient::sessionMatchesInstall("", install));
}

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
	CHECK(rs[0].body == "{\"profile\":\"0123456789abcdef0123456789abcdef\",\"mode\":\"normal\"}"); // lane UBERJOIN-1: the mode rides along
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
	std::string reportView = view;
	const std::string badFaction = "\"faction\":9";
	reportView.replace(reportView.find(badFaction), badFaction.size(), "\"faction\":-1");
	reportView.insert(reportView.find("\"you\":"), "\"player_id\":\"guest:2222222222222222\",");
	c.joinGame("0123456789abcdef", "");
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 200, "{\"v\":1,\"slot\":1,\"game\":" + reportView + "}", "");
	CHECK(c.inGame());
	CHECK(c.slotViews()[0].playerId == "guest:2222222222222222");
	CHECK(c.slotViews()[1].playerId.empty());
}

// lane UBERJOIN-1 (S-4860): the lobby's "mode" of a room: listed, read from the view, sent at create / join, closed set, refused inside a room
TEST_CASE("uberjoin1: the lobby client reads and sends a room's performance mode (no lobby needed)")
{
	auto signedIn = [](OnlineLobbyClient &c) {
		c.signInGuest(u"x");
		auto rs = c.takeRequests();
		REQUIRE(rs.size() == 1);
		c.deliver(rs[0].id, 200, "{\"v\":1,\"token\":\"t.t\",\"account\":{\"id\":\"guest:1\",\"name\":\"x\",\"kind\":\"guest\"}}", "");
		c.takeEvents();
	};
	auto listOf = [](const std::string &modeField) {
		return "{\"v\":1,\"games\":[{\"id\":\"0123456789abcdef\",\"name\":\"g\",\"host\":\"h\",\"map\":\"m\",\"players\":1,\"openSlots\":1,\"state\":\"open\","
			   "\"password\":false" + modeField + "}]}";
	};
	auto refresh = [&](OnlineLobbyClient &c, const std::string &body) {
		c.refreshList();
		auto rs = c.takeRequests();
		REQUIRE(rs.size() == 1);
		c.deliver(rs[0].id, 200, body, "");
		return c.takeEvents();
	};
	OnlineLobbyClient c(clientOptions(4, 4));
	signedIn(c);
	// absent = normal (a room of an old client), "uber" and "normal" read, anything else a protocol refusal (the list stays as it was)
	auto es = refresh(c, listOf(""));
	REQUIRE(es.size() == 1);
	REQUIRE(c.games().size() == 1);
	CHECK(c.games()[0].mode == SimulationProfile::Faithful);
	es = refresh(c, listOf(",\"mode\":\"uber\""));
	REQUIRE(c.games().size() == 1);
	CHECK(c.games()[0].mode == SimulationProfile::Uber);
	es = refresh(c, listOf(",\"mode\":\"normal\""));
	CHECK(c.games()[0].mode == SimulationProfile::Faithful);
	es = refresh(c, listOf(",\"mode\":\"uber\""));
	for (const char *bad : { ",\"mode\":\"Uber\"", ",\"mode\":\"faithful\"", ",\"mode\":\"\"", ",\"mode\":1", ",\"mode\":null", ",\"mode\":[\"uber\"]" })
	{
		es = refresh(c, listOf(bad));
		REQUIRE_MESSAGE(es.size() == 1, bad);
		CHECK_MESSAGE(es[0].kind == OnlineLobbyClient::Event::Refused, bad);
		CHECK_MESSAGE(es[0].code == "protocol", bad);
		CHECK_MESSAGE(OnlineLobbyClient::utf16ToUtf8(es[0].text).find("\"mode\"") != std::string::npos, bad);
		REQUIRE(c.games().size() == 1);
		CHECK_MESSAGE(c.games()[0].mode == SimulationProfile::Uber, bad << ": the list is unchanged"); // a refused answer changes nothing
	}
	CHECK(performanceModeSwitchPrompt(SimulationProfile::Faithful) ==
		u"This game uses Normal performance mode. Switch to Normal mode and join? (Your setting changes back after the game.)");
	// the profile switch: its mode and digest together; a bad digest is refused by name
	std::string why;
	const std::string uberDigest = "fedcba9876543210fedcba9876543210";
	CHECK_FALSE(c.setProfile(SimulationProfile::Uber, "short", &why));
	CHECK(why.find("digest") != std::string::npos);
	CHECK(c.profile() == SimulationProfile::Faithful);
	REQUIRE(c.setProfile(SimulationProfile::Uber, uberDigest, &why));
	CHECK(c.profile() == SimulationProfile::Uber);
	c.joinGame("0123456789abcdef", "");
	auto rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].body == "{\"profile\":\"" + uberDigest + "\",\"mode\":\"uber\"}");
	// the join's answer: a view of an Uber room is read into the game's simulation profile
	std::string slots;
	for (int i = 0; i < 8; ++i)
	{
		slots += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i) + ",\"state\":\"" + (i < 2 ? "player" : "closed") +
			"\",\"name\":\"p\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":false,\"you\":" + (i == 1 ? "true" : "false") + "}";
	}
	auto view = [&](const std::string &modeField) {
		return "{\"id\":\"0123456789abcdef\",\"name\":\"g\",\"state\":\"open\",\"hostSlot\":0,\"profile\":\"x\"" + modeField +
			   ",\"setup\":{\"map\":\"m\",\"startingCash\":1500,\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null},\"slots\":[" + slots +
			   "],\"seq\":3}";
	};
	c.deliver(rs[0].id, 200, "{\"v\":1,\"slot\":1,\"game\":" + view(",\"mode\":\"sideways\"") + "}", "");
	es = c.takeEvents();
	REQUIRE(es.size() == 1);
	CHECK(es[0].code == "protocol");
	CHECK_FALSE(c.inGame());
	c.joinGame("0123456789abcdef", "");
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 200, "{\"v\":1,\"slot\":1,\"game\":" + view(",\"mode\":\"uber\"") + "}", "");
	c.takeEvents();
	REQUIRE(c.inGame());
	CHECK(c.gameMode() == SimulationProfile::Uber);
	CHECK(c.gameInfo().simulationProfile == SimulationProfile::Uber);
	// nobody changes mode inside a room
	why.clear();
	CHECK_FALSE(c.setProfile(SimulationProfile::Faithful, "0123456789abcdef0123456789abcdef", &why));
	CHECK(why.find("inside a room") != std::string::npos);
	CHECK(c.profile() == SimulationProfile::Uber);
	CHECK(c.options().profileDigest == uberDigest);
	// the create body carries the mode too
	OnlineLobbyClient h(clientOptions(4, 4));
	signedIn(h);
	SkirmishGameInfo info;
	info.mapName = "m";
	info.slots[0].state = SLOT_PLAYER;
	h.createGame(u"room", "", info, 0);
	rs = h.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].body.find(",\"mode\":\"normal\",\"hostSlot\":0") != std::string::npos);
	// a view with no "mode" is a Normal room
	OnlineLobbyClient o(clientOptions(4, 4));
	signedIn(o);
	o.joinGame("0123456789abcdef", "");
	rs = o.takeRequests();
	o.deliver(rs[0].id, 200, "{\"v\":1,\"slot\":1,\"game\":" + view("") + "}", "");
	o.takeEvents();
	REQUIRE(o.inGame());
	CHECK(o.gameMode() == SimulationProfile::Faithful);
}

TEST_CASE("onlinemenu1: the lobby client sends and reads a slot's handicap and Create-a-Hero record; a bad handicap refuses the view, a bad record "
		  "is only that slot's problem (no lobby needed)")
{
	using C = OnlineLobbyClient;
	// a record built here (no retail bytes), in its wire form as a .cah load gives it
	CreateAHeroHero hero;
	hero.name = u"Menuhero";
	hero.classIndex = 3;
	hero.subClassIndex = 1;
	hero.primaryColor = 0xFF102030u;
	hero.powers[0] = { "Command_TestPowerA", 0, 1 };
	hero.bling = { { "CreateAHero_Weapon", 1 } };
	hero.uniqueID = "ONLINEMENU1HERO";
	CreateAHeroHero loaded;
	std::string error;
	REQUIRE_MESSAGE(loaded.load(hero.save(), &error), error);
	const std::vector<std::uint8_t> wire = loaded.save();
	const std::string b64 = RelayProtocol::base64Encode(wire.data(), wire.size());
	C c(clientOptions(4, 4));
	auto answer = [&](const std::string &body) {
		std::vector<C::HttpRequest> rs = c.takeRequests();
		REQUIRE(rs.size() == 1);
		c.deliver(rs[0].id, 200, body, "");
		return c.takeEvents();
	};
	c.signInGuest(u"x");
	answer("{\"v\":1,\"token\":\"t.t\",\"account\":{\"id\":\"guest:1\",\"name\":\"x\",\"kind\":\"guest\"}}");
	auto view = [](const std::string &slot1Extra, const std::string &slot2Extra = "") {
		std::string slots;
		for (int i = 0; i < 8; ++i)
		{
			slots += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i) + ",\"state\":\"" + (i < 2 ? "player" : "closed") + "\",\"name\":\"" +
				(i == 1 ? "Rohan" : "Host") + "\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":false,\"you\":" + (i == 1 ? "true" : "false") +
				(i == 1 ? slot1Extra : i == 2 ? slot2Extra : std::string()) + "}";
		}
		return "{\"v\":1,\"slot\":1,\"game\":{\"id\":\"0123456789abcdef\",\"name\":\"g\",\"state\":\"open\",\"hostSlot\":0,\"profile\":\"x\",\"setup\":{\"map\":\"m\","
			   "\"startingCash\":1500,\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null},\"slots\":[" + slots + "],\"seq\":3}}";
	};
	// not in a game: nothing is sent
	CHECK_FALSE(c.setOwnCreateAHero(&loaded, &error));
	CHECK(c.takeRequests().empty());
	// ---- reading: a handicap and a record ----
	c.joinGame("0123456789abcdef", "");
	answer(view(",\"handicap\":-15,\"cah\":\"" + b64 + "\""));
	REQUIRE(c.inGame());
	CHECK(c.gameInfo().slots[1].handicap == -15);
	REQUIRE(c.gameInfo().slots[1].hasCreateAHero);
	CHECK(c.gameInfo().slots[1].createAHero.save() == wire);
	CHECK(c.slotViews()[1].heroProblem.empty());
	CHECK(c.gameInfo().slots[0].handicap == 0); // absent: 0
	CHECK_FALSE(c.gameInfo().slots[0].hasCreateAHero);
	// ---- sending: the handicap with the slot's choices, the record as base64 of its wire form, none as null ----
	c.setOwnSlot(C::kKeep, C::kKeep, C::kKeep, C::kKeep, -20);
	std::vector<C::HttpRequest> rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].path == "/v1/games/0123456789abcdef/slot");
	CHECK(rs[0].body == "{\"handicap\":-20}");
	c.deliver(rs[0].id, 200, view(",\"handicap\":-20"), "");
	c.takeEvents();
	REQUIRE(c.setOwnCreateAHero(&loaded, &error));
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].body == "{\"cah\":\"" + b64 + "\"}");
	c.deliver(rs[0].id, 200, view(",\"handicap\":-20"), "");
	c.takeEvents();
	REQUIRE(c.setOwnCreateAHero(nullptr, &error));
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].body == "{\"cah\":null}");
	c.deliver(rs[0].id, 200, view(",\"handicap\":-20"), "");
	c.takeEvents();
	// ---- a handicap outside RotWK's set refuses the whole view (the last good one stays) ----
	for (const std::string &bad : { ",\"handicap\":-7", ",\"handicap\":5", ",\"handicap\":-100", ",\"handicap\":\"-10\"", ",\"handicap\":null" })
	{
		c.setReady(true);
		std::vector<C::Event> es = answer(view(bad));
		CHECK_MESSAGE(std::any_of(es.begin(), es.end(), [](const C::Event &e) { return e.kind == C::Event::Refused && e.code == "protocol"; }), bad);
		CHECK(c.gameInfo().slots[1].handicap == -20);
	}
	// ---- a record that does not read is that slot's problem only: the view stays, the slot shows no hero ----
	const std::string broken = b64.substr(0, b64.size() - 4) + "AAAA";
	for (const std::string &bad : { std::string("!!!!"), broken, std::string(5468, 'A'), std::string("42") })
	{
		c.setReady(true);
		const std::string field = bad == "42" ? ",\"cah\":42" : ",\"cah\":\"" + bad + "\"";
		std::vector<C::Event> es = answer(view(",\"handicap\":-10" + field));
		CHECK_FALSE(std::any_of(es.begin(), es.end(), [](const C::Event &e) { return e.kind == C::Event::Refused; }));
		CHECK(c.gameInfo().slots[1].handicap == -10);
		CHECK_FALSE(c.gameInfo().slots[1].hasCreateAHero);
		CHECK_MESSAGE(!c.slotViews()[1].heroProblem.empty(), bad.substr(0, 16));
	}
	// a record on a slot that is not a player's
	c.setReady(true);
	answer(view("", ",\"cah\":\"" + b64 + "\""));
	CHECK_FALSE(c.gameInfo().slots[2].hasCreateAHero);
	CHECK(c.slotViews()[2].heroProblem == "a Create-a-Hero on a slot that is not a player's");
	// and the problem goes with the next good view
	c.setReady(true);
	answer(view(",\"cah\":\"" + b64 + "\""));
	CHECK(c.slotViews()[1].heroProblem.empty());
	CHECK(c.slotViews()[2].heroProblem.empty());
	CHECK(c.gameInfo().slots[1].hasCreateAHero);
}

TEST_CASE("onlinemenu1: the lobby client reads the room's relay and each player's link, refuses a malformed link, and reports its own (no lobby needed)")
{
	using C = OnlineLobbyClient;
	C c(clientOptions(4, 4));
	auto answer = [&](const std::string &body) {
		std::vector<C::HttpRequest> rs = c.takeRequests();
		REQUIRE(rs.size() == 1);
		c.deliver(rs[0].id, 200, body, "");
		return c.takeEvents();
	};
	c.signInGuest(u"x");
	answer("{\"v\":1,\"token\":\"t.t\",\"account\":{\"id\":\"guest:1\",\"name\":\"x\",\"kind\":\"guest\"}}");
	auto view = [](const std::string &relay, const std::string &link0, const std::string &link2 = "") {
		std::string slots;
		for (int i = 0; i < 8; ++i)
		{
			slots += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i) + ",\"state\":\"" + (i < 2 ? "player" : (i == 2 ? "hard" : "closed")) +
				"\",\"name\":\"" + (i == 1 ? "Rohan" : "Host") + "\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":false,\"you\":" +
				(i == 1 ? "true" : "false") + (i == 0 ? link0 : i == 2 ? link2 : std::string()) + "}";
		}
		return "{\"v\":1,\"slot\":1,\"game\":{\"id\":\"0123456789abcdef\",\"name\":\"g\",\"state\":\"open\",\"hostSlot\":0,\"profile\":\"x\"" + relay +
			",\"setup\":{\"map\":\"m\",\"startingCash\":1500,\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null},\"slots\":[" + slots + "],\"seq\":3}}";
	};
	// not in a game: nothing is sent
	c.reportLink(40, 0);
	CHECK(c.takeRequests().empty());
	c.joinGame("0123456789abcdef", "");
	answer(view(",\"relay\":\"192.0.2.10:7350\"", ",\"link\":{\"rtt\":42,\"loss\":20}"));
	REQUIRE(c.inGame());
	CHECK(c.roomRelay() == "192.0.2.10:7350");
	CHECK(c.slotViews()[0].linkKnown);
	CHECK(c.slotViews()[0].rttMs == 42);
	CHECK(c.slotViews()[0].lossPct == 20);
	CHECK_FALSE(c.slotViews()[1].linkKnown);
	// a failed link; a relay that is no address is none; an older lobby's view (neither field) is "not known"
	c.setReady(true);
	answer(view(",\"relay\":\"relay.example:7350\"", ",\"link\":{\"rtt\":null,\"loss\":100}"));
	CHECK(c.slotViews()[0].rttMs == -1);
	CHECK(c.slotViews()[0].lossPct == 100);
	CHECK(c.roomRelay().empty());
	c.setReady(true);
	answer(view("", ""));
	CHECK_FALSE(c.slotViews()[0].linkKnown);
	CHECK(c.roomRelay().empty());
	// a malformed link refuses the whole view (the last good one stays)
	c.setReady(true);
	answer(view(",\"relay\":\"192.0.2.10:7350\"", ",\"link\":{\"rtt\":42,\"loss\":20}"));
	for (const std::string &bad : { std::string(",\"link\":{\"rtt\":0,\"loss\":0}"), std::string(",\"link\":{\"rtt\":10001,\"loss\":0}"),
			 std::string(",\"link\":{\"rtt\":5,\"loss\":101}"), std::string(",\"link\":{\"rtt\":null,\"loss\":3}"), std::string(",\"link\":{\"rtt\":5}"),
			 std::string(",\"link\":7"), std::string(",\"link\":{\"rtt\":\"5\",\"loss\":0}") })
	{
		c.setReady(true);
		std::vector<C::Event> es = answer(view(",\"relay\":\"192.0.2.10:7350\"", bad));
		CHECK_MESSAGE(std::any_of(es.begin(), es.end(), [](const C::Event &e) { return e.kind == C::Event::Refused && e.code == "protocol"; }), bad);
		CHECK(c.slotViews()[0].rttMs == 42);
	}
	// a link on a slot that is not a player's (an AI)
	c.setReady(true);
	std::vector<C::Event> es = answer(view(",\"relay\":\"192.0.2.10:7350\"", "", ",\"link\":{\"rtt\":5,\"loss\":0}"));
	CHECK(std::any_of(es.begin(), es.end(), [](const C::Event &e) { return e.kind == C::Event::Refused && e.code == "protocol"; }));
	// reporting: the lobby's body; out of range values send nothing
	c.reportLink(40, 0);
	std::vector<C::HttpRequest> rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].path == "/v1/games/0123456789abcdef/link");
	CHECK(rs[0].body == "{\"rtt\":40,\"loss\":0}");
	c.deliver(rs[0].id, 200, view(",\"relay\":\"192.0.2.10:7350\"", ",\"link\":{\"rtt\":42,\"loss\":20}"), "");
	c.takeEvents();
	c.reportLink(-1, 100);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].body == "{\"rtt\":null}");
	c.deliver(rs[0].id, 200, view(",\"relay\":\"192.0.2.10:7350\"", ""), "");
	c.takeEvents();
	for (const auto &bad : { std::pair<int, int>{ 0, 0 }, { 10001, 0 }, { 40, -1 }, { 40, 101 }, { -1, 50 } })
	{
		c.reportLink(bad.first, bad.second);
		CHECK(c.takeRequests().empty());
	}
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
	OnlinePlayer(starttest::Shared &s, int lobbyPort, std::uint32_t seed, const std::u16string &lastName, PlatformKind platform = PlatformKind::None)
		: client(clientOptions(s.world->playerTemplates().getPlayerTemplateCount(), (int)s.settings.multiplayerColors.size(), platform)), fx(s, seed), port(lobbyPort)
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
	// lane ONLINEMENU-1: a press where the pointer really reaches the button (any point of a 9 x 9 grid over its bounds; the centre of a ring-shaped
	// button can be empty): false, with every point's hit named, when none does
	bool pressReachable(const std::string &part, std::string *why)
	{
		std::vector<AptButtonInst *> all;
		buttons(fx.wm->apt().level(sh->level()), all);
		for (AptButtonInst *b : all)
		{
			if (b->targetPath().find(part) == std::string::npos)
			{
				continue;
			}
			float x0, y0, x1, y1;
			if (!b->contentBounds(x0, y0, x1, y1))
			{
				continue;
			}
			std::string hits;
			for (int i = 1; i < 10; ++i)
			{
				for (int j = 1; j < 10; ++j)
				{
					float x, y;
					b->globalMatrix().apply(x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * j / 10, x, y);
					AptButtonInst *hit = fx.wm->apt().input().hitTestButtons(x, y);
					if (hit == b)
					{
						starttest::clickAt(fx, x, y);
						return true;
					}
					hits += hit ? hit->targetPath() + ";" : std::string("-;");
				}
			}
			if (why)
			{
				*why = b->targetPath() + ": " + hits;
			}
			return false;
		}
		if (why)
		{
			*why = "no visible button " + part;
		}
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
	// lane ONLINEUX-1: typed as a player types it: the page's Login button follows the nickname (RW 0x97A668: enabled only with a printable
	// character, told by the combo's text change), so a name put in without typing leaves Login as the empty field left it
	GadgetComboBoxSetText(p.sh->nicknameWindow(), u"");
	starttest::typeText(p.fx, GadgetComboBoxGetEditBox(p.sh->nicknameWindow()), name);
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
	CHECK(AptOnlineShell::stopLines().size() == 23); // lane ONLINE-5: S-4550 .. S-4554 added; lane ONLINEHUB-1: S-4870, 4871, 4873 .. 4875, 4877; ONLINEUX-1: S-5140 .. S-5143; ONLINEMENU-1: S-4076 resolved
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
	// retains its timeline state but is hidden by the nickname-only page; OpenBFME's seal stays visible and clickable
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
	REQUIRE(host.clickWhenShown("OnlineLogin.GameSpy.bttn"));
	REQUIRE_FALSE(host.fx.services.requests.empty());
	CHECK(host.fx.services.requests.back().action == ShellAction::OpenProjectWebsite);
	CHECK(host.fx.services.requests.back().argument.empty());
	CHECK_FALSE(host.hasNote("unported", "GameSpy"));
	CHECK(host.hasNote("online-login", "the account buttons are disabled (S-4071)"));
	// lane MENUWORK-1 (S-4750): the page shows the nickname only. Every account part of the movie is hidden (its clip's _visible: a hidden button takes
	// no press), the e-mail / password / remember-me gadgets are hidden, the nickname combo is shown, and the e-mail row's label says what the
	// nickname is for; the notes name the stop and no part is missing
	CHECK(host.hasNote("online-login", "[S-4750] the login page shows the nickname only"));
	auto partAt = [&](const std::string &part) -> AptCharacterInst * {
		// "<path>#<depth>": the clip's child at that depth (WindowManager::setAptVisible)
		const size_t hash = part.find('#');
		AptCharacterInst *c = host.at(part.substr(0, hash));
		if (!c || hash == std::string::npos)
		{
			return c;
		}
		AptSpriteInst *sprite = c->asSprite();
		for (AptCharacterInst *child : sprite ? sprite->children() : std::vector<AptCharacterInst *>())
		{
			if (child->depth() == std::atoi(part.c_str() + hash + 1))
			{
				return child;
			}
		}
		return nullptr;
	};
	for (const std::string &part : AptOnlineShell::loginAccountParts())
	{
		AptCharacterInst *c = partAt(part);
		REQUIRE_MESSAGE(c, part);
		CHECK_MESSAGE(!c->visible, part);
	}
	// the password row's frame is the nickname row's now (the e-mail / nickname frames are one hidden shape)
	AptCharacterInst *frame = partAt("OnlineLogin.Login.textAndBorders.InputFields#3");
	REQUIRE(frame);
	CHECK(frame->visible);
	AptValue y, xscale;
	REQUIRE(frame->getMember("_y", y));
	REQUIRE(frame->getMember("_xscale", xscale));
	CHECK(y.toNumber() == doctest::Approx(AptOnlineShell::kNicknameFrameY)); // tolerance: the member is kept as the matrix's float
	CHECK(xscale.toNumber() == doctest::Approx(AptOnlineShell::kNicknameFrameXScale)); // tolerance: the scale is read back from the matrix
	CHECK_FALSE_MESSAGE(host.hasNote("online-login", "was not hidden"), host.notes());
	CHECK_FALSE(host.hasNote("online-login", "has no place"));
	CHECK_FALSE(host.sh->nicknameWindow()->winIsHidden());
	auto *nicknameLine = dynamic_cast<AptTextInst *>(host.at("OnlineLogin.Login.textAndBorders.InputLabels.AccountName"));
	REQUIRE(nicknameLine);
	CHECK(nicknameLine->visible);
	CHECK(nicknameLine->text == AptOnlineShell::kNicknameLine);
	auto *nicknameLabel = host.at("OnlineLogin.Login.textAndBorders.InputLabels.NickName");
	REQUIRE(nicknameLabel);
	CHECK(nicknameLabel->visible);
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
	host.tick(60);
	MenuNavigator controller(*host.fx.wm);
	controller.setMenu(host.sh->level(), host.sh);
	REQUIRE(controller.supportsControllerSections());
	controller.setControllerSections(true);
	for (int section : { 2, 0, 1 })
	{
		controller.switchControllerSection(1);
		INFO("controller section ", section);
		CHECK(controller.controllerSection() == section);
		REQUIRE_FALSE(controller.targets().empty());
		bool representative = false;
		for (const auto &target : controller.targets())
			representative |= target.id.find(section == 2 ? ".MapList" : section == 0 ? ".PlayGame." : ".PlayerList.") != std::string::npos;
		CHECK(representative);
	}
	// RB cycles Map -> Rules -> Chat -> Map; LB wraps back through Chat.
	const char *activeTabs[] = { "RulesTab", "ChatTab", "MapTab", "ChatTab", "RulesTab", "MapTab" };
	int tabStep = 0;
	for (MenuNavKey key : { MenuNavKey::PageNext, MenuNavKey::PageNext, MenuNavKey::PageNext,
		MenuNavKey::PagePrevious, MenuNavKey::PagePrevious, MenuNavKey::PagePrevious })
	{
		REQUIRE(controller.key(key));
		host.tick(60);
		CHECK(controller.controllerSection() == 2);
		for (const char *name : { "MapTab", "RulesTab", "ChatTab" })
		{
			bool available = false;
			for (const auto &target : controller.targets()) available |= target.id.find(std::string(".TabButtons.") + name + ".") != std::string::npos;
			CHECK(available == (std::string(name) != activeTabs[tabStep]));
		}
		++tabStep;
		if (std::string(activeTabs[tabStep - 1]) == "ChatTab")
		{
			REQUIRE(host.sh->chatEntryWindow());
			bool chatInSection = false;
			for (const auto &target : controller.targets()) chatInSection |= target.window == host.sh->chatEntryWindow();
			CHECK(chatInSection);
		}
	}
	controller.setControllerSections(false);


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
	// lane MENUWORK-1: an online page OpenBFME does not have (War of the Ring): a box in the player's words, then the page
	// the player came from again
	CHECK(join.sh->currentPage() == "OnlineOpenPlay");
	for (const char *page : { "OnlineStrategic" })
	{
		REQUIRE(join.fx.wm->invokeCallback("AptOnline::ShellLoadScreen", page));
		REQUIRE(join.sh->messageBox());
		CHECK(join.sh->messageBox()->text() == AptOnlineShell::unavailablePageText(page));
		CHECK(join.sh->boxesShown().back() == std::string("GUI:GSErrorTitle|not available: ") + page);
		REQUIRE(runUntil({ &host, &join }, [&] { return join.sh->currentPage() == "OnlineOpenPlay"; }, 120));
		join.dismissBox();
	}
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
	auto chatCount = [&](const std::string &line) {
		// lane ONLINEUX-1: the lost-lobby line is in the player's words (it was "The lobby does not answer: connection reset by peer (still trying)"):
		// counted by its start
		return std::count_if(host.sh->chatLines().begin(), host.sh->chatLines().end(), [&](const std::string &l) { return l.rfind(line, 0) == 0; });
	};
	const std::string lost = "The online lobby does not answer (still trying): Can't reach the OpenBFME online lobby";
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
	// lane DEVPORT-7: TestChild.h stops the relay with this scope, kills it with the test and keeps it off the test's output
	TestChild relay(relayExe, { "--bind", relayBind, "--keys", keys, "--log-dir", logs, "--status", status, "--stats-s", "0", "--idle-s", "60" },
		(dir / "relay.log").string());
	REQUIRE(relay.started());
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
		peers.emplace_back([&rcs, i, cmd] { rcs[i] = processtest::runCommand(cmd); });
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
// lane ONLINEMENU-1: waitExit with a cap: -1 and *late when the process is still running after `capMs` (it is left running)
int waitExitFor(pid_t pid, std::uint64_t capMs, bool *late)
{
	const std::uint64_t until = NetMilliseconds() + capMs;
	int st = 0;
	while (waitpid(pid, &st, WNOHANG) == 0)
	{
		if (NetMilliseconds() >= until)
		{
			*late = true;
			return -1;
		}
		NetSleepMilliseconds(100);
	}
	*late = false;
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

TEST_CASE("online4 retail rejoin: a player that crashes mid-game restarts, signs in with its kept session, is offered its running game (Yes / No box, the "
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
	// PR #8: a kept session is used only by the install it was made for (OnlineLobbyClient::sessionMatchesInstall): the joiner is one install
	// before and after its restart
	OnlineLobbyClient::Options joinerOptions = o;
	joinerOptions.installId = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	joinerOptions.clientPlatform = "linux"; // an install's sign-in names its client (signInGuest)
	joinerOptions.clientVersion = "test";
	std::string joinerSession;
	std::string starts[2];
	{
		OnlinePlayer host(*s, lobby.port, 31, u"");
		OnlinePlayer join(*s, lobby.port, 32, u"");
		host.client = OnlineLobbyClient(o);
		join.client = OnlineLobbyClient(joinerOptions);
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
	const int kCrashFrame = 100;
	std::string files[3], reports[3];
	for (int i = 0; i < 3; ++i)
	{
		files[i] = (dir / ("start-" + std::to_string(i) + ".json")).string();
		reports[i] = (dir / ("report-" + std::to_string(i) + ".txt")).string();
	}
	std::ofstream(files[0]) << starts[0];
	std::ofstream(files[1]) << starts[1];
	auto runPeer = [&](int i, const std::string &start) {
		const std::string crash = i == 1 ? " --vanish-at " + std::to_string(kCrashFrame) : "";
		return spawn(base + crash + " --online-start \"" + start + "\" --script --paced --frames " + std::to_string(kFrames) + " --logic-threads 2 --report \"" + reports[i] +
			"\" > \"" + (dir / ("peer-" + std::to_string(i) + ".log")).string() + "\" 2>&1");
	};
	const pid_t p0 = runPeer(0, files[0]);
	const pid_t p1 = runPeer(1, files[1]);
	// Crash without goodbye after 100 played frames, not 20 s of loading plus play.
	REQUIRE(waitExit(p1) == 0);
	REQUIRE(reportField(readText(reports[1]), "vanished_at") == std::to_string(kCrashFrame));
	// ---- the game restarts: a new shell with the session the host program kept; Online, the same name ----
	OnlinePlayer again(*s, lobby.port, 33, u"Joiner");
	again.client = OnlineLobbyClient(joinerOptions);
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
	// ---- the rows: the owner's drop-downs are enabled, everyone else's are locked (lane ONLINEMENU-1: Hero and Handicap too, now that the lobby carries
	// them: the handicap test below drives them) ----
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
			// lane ONLINEMENU-1: a row's Handicap works for its owner (the host for an open slot's AI) and is locked for the others
			const bool owner = (p == &host && slot != 1) || (p == &a && slot == 1);
			CHECK_MESSAGE(gadgetEnabled(p->sh->slotGadget(slot, "Handicap")) == owner, "slot " << slot);
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
	// ---- the frame's MESSENGER button (SOCIAL-1, landed by ONLINEMENU-1): it opens RotWK's Messenger over the room (no "not available" box any more);
	// a second press closes it ----
	const size_t boxes = host.sh->boxesShown().size();
	std::string why;
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.pressReachable("MessengerButton", &why); }, 300), why);
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.sh->messenger() != nullptr; }, 300), host.notes());
	CHECK(host.sh->boxesShown().size() == boxes);
	CHECK_FALSE(host.hasNote("unported", "the Messenger is not available"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.pressReachable("MessengerButton", &why); }, 300), why);
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.sh->messenger() == nullptr; }, 300), host.notes());
	// ---- the Connections button (the movie's own button): lane ONLINEMENU-1: the screen opens (no box); this lobby has no relay, so every cell
	// between the two players is failed; the screen's Done brings the staging room back whole ----
	REQUIRE(host.clickWhenShown("LowerButtons_Host.Connections"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.sh->connectionsOpen() && host.sh->connectionCell(0, 1) == "_failed"; }, 300), host.notes());
	CHECK(host.sh->boxesShown().size() == boxes); // no box
	REQUIRE(host.sh->networkPageOpen()); // lane ONLINEMENU-1: the page in words over the grid
	{
		bool said = false;
		for (const OpenBFMEPageRow &r : host.sh->networkPage()->spec().rows)
		{
			said = said || (r.key == "relay" && r.valueText == u"none: this lobby cannot start games");
		}
		CHECK(said);
	}
	CHECK_FALSE(host.hasNote("command-unhandled", "Connection::RedrawGrid"));
	CHECK_FALSE(host.fx.wm->registerCommand("Connection::RedrawGrid", [](const std::string &) {})); // already registered: a second registration is refused
	REQUIRE(host.clickWhenShown("Done"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return !host.sh->connectionsOpen(); }, 600), host.notes());
	CHECK_FALSE(host.sh->networkPageOpen()); // the screen's DONE closes the page with it
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
TEST_CASE("platformicon1 retail: the online staging room shows each human's system left of its name from the lobby's slots, and nothing for the computer, an open slot or "
		  "a client that told nothing")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer host(*s, lobby.port, 81, u"", PlatformKind::Linux), a(*s, lobby.port, 82, u"", PlatformKind::Windows), b(*s, lobby.port, 83, u"");
	const std::vector<OnlinePlayer *> all{ &host, &a, &b };
	signInAndOpenPlay(host, u"Host");
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	// the server browser: the host's system is an icon before the game's name (the list a player sees before it joins)
	signInAndOpenPlay(a, u"JoinerA");
	REQUIRE_MESSAGE(runUntil(all, [&] { return a.sh->gameRows().size() == 1; }, 300), a.notes());
	CHECK(GadgetListBoxGetCellBadge(a.sh->gameList(), 0, 3) == "OpenBFMEPlatformLinux");
	GadgetListBoxSetSelected(a.sh->gameList(), 0);
	REQUIRE(a.clickWhenShown("Main.Controls.JoinGame"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return a.client.inGame() && a.sh->openPlayState() == 12; }, 300), a.notes());
	joinListed(b, all, u"OldB"); // a client of an older build: it told nothing
	auto badge = [](OnlinePlayer &p, int slot) -> std::string {
		GameWindow *w = p.sh->slotGadget(slot, "Player");
		return w ? w->winBadgeImage() : std::string("(no window)");
	};
	REQUIRE_MESSAGE(runUntil(all, [&] {
		for (OnlinePlayer *p : all)
		{
			if (badge(*p, 0) != "OpenBFMEPlatformLinux" || badge(*p, 1) != "OpenBFMEPlatformWindows" || !p->client.gameInfo().slots[2].isHuman())
			{
				return false;
			}
		}
		return true;
	}, 600), badge(host, 0) << " " << badge(host, 1) << " " << badge(a, 0) << " " << badge(b, 1));
	for (OnlinePlayer *p : all)
	{
		REQUIRE(p->client.inGame());
		CHECK(p->client.gameInfo().slots[2].isHuman());
		CHECK(badge(*p, 2).empty());                        // the third player told nothing
		for (int open = 3; open < 6; ++open)
		{
			CHECK_MESSAGE(badge(*p, open).empty(), open);     // open slots
		}
		CHECK(p->client.platformNotes().empty());
		CHECK(p->hasNote("platform-icon", "[S-4850]"));
		CHECK(p->sh->slotGadget(0, "Player")->winBadgeHelp() == "Playing on Linux");
	}
	// the chat tab's Players list: each human's system before its name (the old client's row has none)
	REQUIRE(host.clickWhenShown("TabButtons.ChatTab"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.sh->chatPlayersWindow() && GadgetListBoxGetNumEntries(host.sh->chatPlayersWindow()) == 3; }, 300), host.notes());
	host.tick(60); // the lists are made again once the tab's clip has played in
	GameWindow *chatPlayers = host.sh->chatPlayersWindow();
	REQUIRE(chatPlayers);
	REQUIRE(GadgetListBoxGetNumEntries(chatPlayers) == 3);
	CHECK(GadgetListBoxGetText(chatPlayers, 0, 0) == u"Host");
	CHECK(GadgetListBoxGetCellBadge(chatPlayers, 0, 0) == "OpenBFMEPlatformLinux");
	CHECK(GadgetListBoxGetCellBadge(chatPlayers, 1, 0) == "OpenBFMEPlatformWindows");
	CHECK(GadgetListBoxGetCellBadge(chatPlayers, 2, 0).empty());
	// the lobby's own view of the slots
	REQUIRE(host.sh->setup());
	CHECK(host.client.gameInfo().slots[0].platform == PlatformKind::Linux);
	CHECK(a.client.gameInfo().slots[1].platform == PlatformKind::Windows);
	CHECK(b.client.gameInfo().slots[2].platform == PlatformKind::None);
	// the joiner leaves: its slot shows nothing on the others' screens
	REQUIRE(a.clickWhenShown("LowerButtons_Join.CancelJoin"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return !a.client.inGame() && badge(host, 1).empty() && badge(b, 1).empty(); }, 600), badge(host, 1));
	CHECK(badge(host, 0) == "OpenBFMEPlatformLinux");
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
	// lane ONLINEMENU-1: B crashes mid-game by itself: it vanishes (no leave, nothing more sent, as a kill) once it has played kVanishFrame
	// frames, so it has reached the relay and is in the game whatever the machine's load. A fixed 50 s before a SIGKILL ended, at a load of
	// 30-50 on 8 cores, before B had connected ("cannot rejoin: the original player never connected to the relay")
	const int kVanishFrame = 100;
	auto runPeer = [&](int i) {
		const std::string vanish = i == 2 ? " --vanish-at " + std::to_string(kVanishFrame) : "";
		return spawn(base + vanish + " --online-start \"" + files[i] + "\" --script --paced --frames " + std::to_string(kFrames) + " --logic-threads 2 --report \"" +
			reports[i] + "\" > \"" + (dir / ("peer-" + std::to_string(i) + ".log")).string() + "\" 2>&1");
	};
	const pid_t p0 = runPeer(0), p1 = runPeer(1), p2 = runPeer(2);
	bool late = false;
	const int rc2 = waitExitFor(p2, 600000, &late); // generous: three peers load in parallel (about 30 s on a loaded 6-core machine), then 20 s of play
	if (late)
	{
		for (pid_t p : { p0, p1, p2 })
		{
			kill(p, SIGKILL);
			waitExit(p);
		}
	}
	REQUIRE_MESSAGE(!late, "B did not reach frame " << kVanishFrame << " in 10 minutes:\n" << readText((dir / "peer-2.log").string()).substr(0, 4000));
	CHECK(rc2 == 0);
	REQUIRE(reportField(readText(reports[2]), "vanished_at") == std::to_string(kVanishFrame));
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
namespace
{
// the host program's side of the mode switch (GodotGameWorld's switch_profile_for_game / restore_simulation_profile), kept here as plain state
struct FakeModeWorld
{
	SimulationProfile own = SimulationProfile::Faithful, current = SimulationProfile::Faithful;
	int requests = 0, restores = 0;
	std::string refuse; ///< non-empty: request() refuses with this reason
	ProfileSwitch ps;
	static std::string digest(SimulationProfile p) { return p == SimulationProfile::Uber ? "fedcba9876543210fedcba9876543210" : "0123456789abcdef0123456789abcdef"; }
	explicit FakeModeWorld(SimulationProfile o) : own(o), current(o)
	{
		ps.current = [this]() { return current; };
		ps.digestOf = [](SimulationProfile p) { return digest(p); };
		ps.request = [this](SimulationProfile p, std::string *why) {
			++requests;
			if (!refuse.empty())
			{
				*why = refuse;
				return false;
			}
			current = p;
			return true;
		};
		ps.restore = [this]() {
			++restores;
			current = own;
		};
	}
};
} // namespace

// lane UBERJOIN-1 (S-4860): the online screens: a game's mode in the list, the Yes / No box on a join in the other mode, Yes switches and joins,
// No stays in the list, the room shows its mode, leaving puts the player's own mode back
TEST_CASE("uberjoin1 retail: the server browser tags an Uber game; Join from Normal asks Yes / No: No stays, Yes switches and joins, leaving restores")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer host(*s, lobby.port, 31, u"");
	OnlinePlayer join(*s, lobby.port, 32, u"");
	FakeModeWorld hostWorld(SimulationProfile::Uber), joinWorld(SimulationProfile::Faithful);
	host.fx.environment.profileSwitch = &hostWorld.ps;
	join.fx.environment.profileSwitch = &joinWorld.ps;
	std::string why;
	REQUIRE(host.client.setProfile(SimulationProfile::Uber, FakeModeWorld::digest(SimulationProfile::Uber), &why));
	signInAndOpenPlay(host, u"Host");
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	CHECK(host.client.gameMode() == SimulationProfile::Uber);
	CHECK(std::find(host.sh->chatLines().begin(), host.sh->chatLines().end(), "This room uses Uber performance mode.") != host.sh->chatLines().end());
	signInAndOpenPlay(join, u"Joiner");
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return join.sh->gameRows().size() == 1; }, 300), join.notes());
	const std::string uberRow = join.sh->gameRows()[0];
	CHECK(uberRow.rfind("|uber") == uberRow.size() - 5);
	CHECK(listText(join.sh->gameList(), 0, 3).find("  - UBER") != std::string::npos);
	const std::string prompt = "mode-switch|This game uses Uber performance mode. Switch to Uber mode and join? (Your setting changes back after the game.)";
	// Join -> the box; nothing is sent and nothing switched until the player answers
	GadgetListBoxSetSelected(join.sh->gameList(), 0);
	REQUIRE(join.clickWhenShown("Main.Controls.JoinGame"));
	join.tick(10);
	REQUIRE(join.sh->boxesShown().size() == 1);
	CHECK(join.sh->boxesShown()[0] == prompt);
	CHECK(joinWorld.requests == 0);
	CHECK(join.client.profile() == SimulationProfile::Faithful);
	CHECK_FALSE(join.client.inGame());
	// No: stays in the list, unswitched, no error box
	join.dismissBox("No");
	join.tick(20);
	CHECK(join.sh->boxesShown().size() == 1);
	CHECK(join.sh->openPlayState() == 2);
	CHECK(joinWorld.requests == 0);
	CHECK(joinWorld.current == SimulationProfile::Faithful);
	CHECK(join.client.profile() == SimulationProfile::Faithful);
	CHECK_FALSE(join.client.inGame());
	// Yes: the world is asked, the client follows, the join goes out with the Uber digest and mode
	GadgetListBoxSetSelected(join.sh->gameList(), 0);
	REQUIRE(join.clickWhenShown("Main.Controls.JoinGame"));
	join.tick(10);
	REQUIRE(join.sh->boxesShown().size() == 2);
	CHECK(join.sh->boxesShown()[1] == prompt);
	join.dismissBox("Yes");
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return join.client.inGame() && join.sh->openPlayState() == 12; }, 300), join.notes());
	CHECK(joinWorld.requests == 1);
	CHECK(joinWorld.current == SimulationProfile::Uber);
	CHECK(joinWorld.own == SimulationProfile::Faithful); // the player's own setting is never written
	CHECK(join.client.profile() == SimulationProfile::Uber);
	CHECK(join.client.gameMode() == SimulationProfile::Uber);
	CHECK(join.sh->boxesShown().size() == 2); // no error box
	CHECK(std::find(join.sh->chatLines().begin(), join.sh->chatLines().end(), "This room uses Uber performance mode.") != join.sh->chatLines().end());
	// leaving the room: the player's own mode again, in the world and in the client
	const int restoresBefore = joinWorld.restores;
	REQUIRE(join.fx.wm->invokeCallback("AptOnline::CustomMatch::Cancel", ""));
	join.tick(20);
	CHECK(joinWorld.restores == restoresBefore + 1);
	CHECK(joinWorld.current == SimulationProfile::Faithful);
	CHECK(join.client.profile() == SimulationProfile::Faithful);
	CHECK(join.client.options().profileDigest == FakeModeWorld::digest(SimulationProfile::Faithful));
	// a world that refuses the switch: a named box, still in the list, never a silent refusal
	joinWorld.refuse = "the simulation profile cannot change inside a LAN game (leave it first)";
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return join.sh->gameRows().size() == 1 && join.sh->openPlayState() == 2; }, 300), join.notes());
	GadgetListBoxSetSelected(join.sh->gameList(), 0);
	REQUIRE(join.clickWhenShown("Main.Controls.JoinGame"));
	join.tick(10);
	join.dismissBox("Yes");
	join.tick(20);
	REQUIRE(join.sh->boxesShown().size() == 4);
	CHECK(join.sh->boxesShown()[3] == "mode-switch-failed|The performance mode could not be switched: " + joinWorld.refuse);
	CHECK_FALSE(join.client.inGame());
	CHECK(join.client.profile() == SimulationProfile::Faithful);
	// a refused rejoin / watch after a switch (the game is not running: the lobby refuses the spectator START): the setting is back, the refusal is told
	joinWorld.refuse.clear();
	const std::string gameId = join.sh->gameIds()[0];
	const int restoresBeforeSpectate = joinWorld.restores;
	join.sh->startRunning(gameId, false);
	join.tick(10);
	REQUIRE(join.sh->boxesShown().size() == 5);
	CHECK(join.sh->boxesShown()[4] == prompt);
	join.dismissBox("Yes");
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return join.sh->boxesShown().size() == 6; }, 300), join.notes());
	CHECK(join.sh->boxesShown()[5].rfind("rejoin-refused|", 0) == 0);
	CHECK(joinWorld.requests == 3); // the first Yes, the refused switch, this one
	CHECK(joinWorld.restores == restoresBeforeSpectate + 1);
	CHECK(joinWorld.current == SimulationProfile::Faithful);
	CHECK(join.client.profile() == SimulationProfile::Faithful);
	CHECK(join.client.options().profileDigest == FakeModeWorld::digest(SimulationProfile::Faithful));
}

#endif

#ifdef ONLINE4_POSIX
// ---- lane ONLINEUX-1: the online menus as the owner's real-use audit met them (docs/ONLINE.md "The online menus in real use") ---------------------------
namespace
{
// the movie's own disabled look of a GenericButton clip: its timeline is in the "_disabled" segment (as the account buttons' check above)
bool movieDisabled(OnlinePlayer &p, const char *clip)
{
	AptCharacterInst *c = p.at(clip);
	AptSpriteInst *sp = c ? c->asSprite() : nullptr;
	if (!sp || !sp->frames)
	{
		return false;
	}
	const std::map<std::string, int> &labels = p.fx.wm->apt().labelsOf(sp->frames);
	const auto it = labels.find("_disabled");
	if (it == labels.end() || sp->frame < it->second)
	{
		return false;
	}
	for (const auto &kv : labels)
	{
		if (kv.second > it->second && sp->frame >= kv.second)
		{
			return false;
		}
	}
	return true;
}

// the frame label the shell's ButtonNav clip is in (the last label at or before its frame): the highlighted button's page
std::string navLabel(OnlinePlayer &p)
{
	AptCharacterInst *c = p.at("Toggles.ButtonNav");
	AptSpriteInst *sp = c ? c->asSprite() : nullptr;
	if (!sp || !sp->frames)
	{
		return "(no ButtonNav)";
	}
	std::string best;
	int bestFrame = -1;
	for (const auto &kv : p.fx.wm->apt().labelsOf(sp->frames))
	{
		if (kv.second <= sp->frame && kv.second > bestFrame)
		{
			best = kv.first;
			bestFrame = kv.second;
		}
	}
	return best;
}

void waitNickname(OnlinePlayer &p)
{
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
}

// every line of a box's text fits the box (OnlineShell's wrap: a long line was squeezed into an unreadable one)
bool linesFit(const std::u16string &text, size_t width)
{
	size_t line = 0;
	for (char16_t c : text)
	{
		line = c == u'\n' ? 0 : line + 1;
		if (line > width + 1)
		{
			return false;
		}
	}
	return true;
}

// the game played and over: game.gd empties the shell for the game and the score screen's Continue pushes the shell's start screens again (the lobby
// client and the screens' context stay with the host program)
void backAfterGame(OnlinePlayer &p)
{
	p.fx.shell->clear();
	p.tick(5);
	p.fx.shell->push("OnlineShell.apt");
	REQUIRE(p.fx.shell->errors().empty());
	p.sh = dynamic_cast<AptOnlineShell *>(p.fx.shell->top());
	REQUIRE(p.sh);
}
} // namespace

TEST_CASE("onlineux1 retail: the login page's Login button follows the nickname (RW 0x97A668): disabled while the name is empty, enabled by typing one; "
		  "a lobby that does not answer is said in the player's words and Login works again")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer p(*s, lobby.port, 81, u"");
	waitNickname(p);
	// the page's fill: no name, Login disabled (the movie's own look); before ONLINEUX-1 it was enabled, a press with no name disabled it (the movie
	// does on its press) and nothing enabled it again: the audit's dead end
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return movieDisabled(p, "OnlineLogin.Login.LogIn"); }, 300), p.notes());
	CHECK(p.sh->loginButtonState() == 0);
	// a typed name enables it, and the press signs in
	starttest::typeText(p.fx, GadgetComboBoxGetEditBox(p.sh->nicknameWindow()), u"Ann");
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return !movieDisabled(p, "OnlineLogin.Login.LogIn"); }, 120), p.notes());
	CHECK(p.sh->loginButtonState() == 1);
	// a press with a blank name (the movie disables its button on the press): the retail box, and Login follows the name again
	GadgetComboBoxSetText(p.sh->nicknameWindow(), u"  ");
	p.sh->nicknameEdited();
	REQUIRE(runUntil({ &p }, [&] { return movieDisabled(p, "OnlineLogin.Login.LogIn"); }, 120));
	p.fx.wm->invokeCallback("AptOnline::Login::Login", "");
	p.tick(5);
	REQUIRE(p.sh->boxesShown().size() == 1);
	CHECK(p.sh->boxesShown()[0] == "GUI:GSErrorTitle|GUI:GSNoLoginInfoNickname");
	CHECK(p.client.sentCount("signin") == 0);
	p.dismissBox();
	GadgetComboBoxSetText(p.sh->nicknameWindow(), u"");
	starttest::typeText(p.fx, GadgetComboBoxGetEditBox(p.sh->nicknameWindow()), u"Bo");
	REQUIRE(runUntil({ &p }, [&] { return !movieDisabled(p, "OnlineLogin.Login.LogIn"); }, 120));
	REQUIRE(p.clickWhenShown("Login.LogIn"));
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.client.signedIn() && p.sh->currentPage() == "OnlineHome"; }, 300), p.notes());
	CHECK(p.client.accountName() == u"Bo");

	// a lobby that does not answer (nothing listens on the port): the player reads what happened and what to do, not "did not answer (2)"
	OnlinePlayer q(*s, freeTcpPort(), 82, u"");
	q.ctx.lobbyUrl = "https://play.example.test";
	waitNickname(q);
	starttest::typeText(q.fx, GadgetComboBoxGetEditBox(q.sh->nicknameWindow()), u"Cy");
	REQUIRE(runUntil({ &q }, [&] { return !movieDisabled(q, "OnlineLogin.Login.LogIn"); }, 120));
	REQUIRE(q.clickWhenShown("Login.LogIn"));
	REQUIRE_MESSAGE(runUntil({ &q }, [&] { return q.sh->messageBox() && q.sh->messageBox()->type() != AptMessageBox::TYPE_NONE; }, 300), q.notes());
	const std::u16string said = q.sh->messageBox()->text();
	CHECK_MESSAGE(said.find(u"Can't reach the OpenBFME online lobby (play.example.test)") != std::u16string::npos, OnlineLobbyClient::utf16ToUtf8(said));
	CHECK(said.find(u"internet connection") != std::u16string::npos);
	CHECK(said.find(u"did not answer") == std::u16string::npos);
	CHECK(linesFit(said, 58));
	CHECK(q.sh->messageBox()->title() == u"Online");
	q.dismissBox();
	// Login is usable again (the sign-in can be tried once the lobby is back)
	REQUIRE(runUntil({ &q }, [&] { return !movieDisabled(q, "OnlineLogin.Login.LogIn"); }, 120));
	CHECK(q.sh->loginButtonState() == 1);
	CHECK_FALSE(q.client.signedIn());
}

TEST_CASE("onlineux1 r2 retail: without a mouse, Tab to the login's nickname takes the typed name (its combo box's edit box gets the focus) and LOG IN "
		  "follows it, as with a click (the OPTS-6 keyboard walk of the login page)")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer p(*s, lobby.port, 83, u"");
	waitNickname(p);
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return movieDisabled(p, "OnlineLogin.Login.LogIn"); }, 300), p.notes());
	MenuNavigator nav(*p.fx.wm);
	nav.setMenu(p.sh->level(), p.sh);
	for (int i = 0; i < 12 && nav.focusId().find("OnlineLogin::Nickname") == std::string::npos; ++i)
	{
		REQUIRE(nav.key(MenuNavKey::Next));
	}
	REQUIRE_MESSAGE(nav.focusId().find("OnlineLogin::Nickname") != std::string::npos, nav.focusId());
	CHECK(p.fx.wm->gadgetLayer()->gadgets().winGetFocus() == GadgetComboBoxGetEditBox(p.sh->nicknameWindow()));
	for (char16_t c : std::u16string(u"Kim"))
	{
		p.fx.wm->postTextInput(c); // to the window with the keyboard focus (no click)
		p.tick(1);
	}
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return !movieDisabled(p, "OnlineLogin.Login.LogIn"); }, 120), p.notes());
	CHECK(GadgetComboBoxGetText(p.sh->nicknameWindow()) == u"Kim");
}

TEST_CASE("onlineux1 retail: Options over the online screens gives the button bar back (nav, Log Out, Options: OnlineShell.apt's ShowButtonBar); a page "
		  "OpenBFME does not have puts the nav's highlight back on the page the player is on")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer p(*s, lobby.port, 83, u"");
	signIn(p, u"Dee");
	REQUIRE(p.waitShown("ButtonNav.OpenPlay"));
	REQUIRE(p.waitShown("Exit.Button_Shell"));
	// OPTIONS: the movie hides its button bar (HideButtonBar("Options")) and asks for the Options screen
	REQUIRE(p.clickWhenShown("Options.Button_Shell"));
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.fx.shell->top() && p.fx.shell->top()->filename() == "Options.apt"; }, 300), p.notes());
	p.tick(30);
	// the Options screen's own Cancel, as the player's click
	AptScreen *options = p.fx.shell->top();
	std::vector<AptButtonInst *> buttons;
	p.buttons(p.fx.wm->apt().level(options->level()), buttons);
	AptButtonInst *cancel = nullptr;
	for (AptButtonInst *b : buttons)
	{
		cancel = !cancel && b->targetPath().find("Cancel") != std::string::npos ? b : cancel;
	}
	REQUIRE_MESSAGE(cancel, "no Cancel button on the Options screen");
	starttest::clickButton(p.fx, *cancel);
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.fx.shell->top() == p.sh; }, 300), p.notes());
	// the bar is back: the nav, Log Out and Options take clicks again (before ONLINEUX-1 they stayed hidden: no way out but quitting the game)
	CHECK_MESSAGE(p.waitShown("ButtonNav.OpenPlay", 300), p.notes());
	CHECK(p.waitShown("Exit.Button_Shell", 60));
	CHECK(p.waitShown("Options.Button_Shell", 60));
	CHECK(p.hasNote("online-button-bar"));
	openPlay(p);
	CHECK(navLabel(p) == "_OpenPlay");
	// WAR OF THE RING: not available (its box); the page stays Open Play and so does the nav's highlight (it stayed on WAR OF THE RING)
	AptButtonInst *wotr = starttest::firstButtonIn(p.at("Toggles.ButtonNav.Strategic"));
	REQUIRE(wotr);
	starttest::clickButton(p.fx, *wotr);
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.hasNote("unported", "OnlineStrategic"); }, 300), p.notes());
	p.dismissBox();
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.sh->currentPage() == "OnlineOpenPlay" && navLabel(p) == "_OpenPlay"; }, 300),
		navLabel(p) << " / " << p.sh->currentPage() << "\n" << p.notes());
}

TEST_CASE("onlineux1 retail: a refused join is said in the player's words, on lines that fit the box: another version's game, a wrong password (RotWK's "
		  "own text), a full game")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer other(*s, lobby.port, 84, u""), locked(*s, lobby.port, 85, u""), join(*s, lobby.port, 86, u"");
	std::string why;
	REQUIRE(other.client.setProfile(SimulationProfile::Faithful, "fedcba9876543210fedcba9876543210", &why));
	const std::vector<OnlinePlayer *> all{ &other, &locked, &join };
	for (OnlinePlayer *h : { &other, &locked })
	{
		signInAndOpenPlay(*h, h == &other ? u"Other" : u"Locked");
		REQUIRE(h->clickWhenShown("Controls.CreateGame"));
		REQUIRE(runUntil({ h }, [&] { return h->sh->gameNameWindow() && h->sh->gamePasswordWindow() && h->sh->openPlayState() == 3; }, 300));
		GadgetTextEntrySetText(h->sh->gamePasswordWindow(), h == &locked ? u"secret" : u"");
		REQUIRE(h->clickWhenShown("CreateGamePopup.Accept"));
		REQUIRE_MESSAGE(runUntil({ h }, [&] { return h->client.inGame() && h->sh->openPlayState() == 6; }, 300), h->notes());
	}
	join.fx.text.set("GUI:JoinFailedBadPassword", "Incorrect password: the game could not be joined. Check the password with the game's host.");
	signInAndOpenPlay(join, u"Joiner");
	REQUIRE(runUntil(all, [&] { return join.sh->gameRows().size() == 2; }, 300));
	auto rowOf = [&](const std::string &host) {
		for (size_t i = 0; i < join.sh->gameRows().size(); ++i)
		{
			if (join.sh->gameRows()[i].rfind(host, 0) == 0)
			{
				return (int)i;
			}
		}
		return -1;
	};
	// another version's game: why, and what to do; no raw profile digest
	GadgetListBoxSetSelected(join.sh->gameList(), rowOf("Other"));
	REQUIRE(join.clickWhenShown("Main.Controls.JoinGame"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return join.sh->messageBox() && join.sh->messageBox()->type() != AptMessageBox::TYPE_NONE; }, 300), join.notes());
	std::u16string said = join.sh->messageBox()->text();
	std::u16string flat = said;
	std::replace(flat.begin(), flat.end(), u'\n', u' ');
	CHECK_MESSAGE(flat.find(u"different OpenBFME version or different game files") != std::u16string::npos, OnlineLobbyClient::utf16ToUtf8(said));
	CHECK(said.find(u"fedcba98") == std::u16string::npos);
	CHECK(said.find(u"0123456789abcdef") == std::u16string::npos);
	CHECK(linesFit(said, 58));
	join.dismissBox();
	// a wrong password: RotWK's own text (GUI:JoinFailedBadPassword, the player's game text)
	GadgetListBoxSetSelected(join.sh->gameList(), rowOf("Locked"));
	REQUIRE(join.clickWhenShown("Main.Controls.JoinGame"));
	REQUIRE(runUntil(all, [&] { return join.sh->openPlayState() == 10 && join.sh->gamePasswordWindow(); }, 300));
	GadgetTextEntrySetText(join.sh->gamePasswordWindow(), u"wrong");
	join.fx.wm->invokeCallback("AptOnline::CustomMatch::AcceptJoinGame", "");
	REQUIRE_MESSAGE(runUntil(all, [&] { return join.sh->messageBox() && join.sh->messageBox()->type() != AptMessageBox::TYPE_NONE; }, 300), join.notes());
	std::u16string retailText; // the label's text (the test's game text: the real one is in the player's language archive)
	REQUIRE(join.fx.text.fetch("GUI:JoinFailedBadPassword", retailText));
	CHECK(join.sh->messageBox()->text() == AptOnlineShell::wrapLines(retailText, 58));
	CHECK(join.sh->messageBox()->text().find(u"wrong password") == std::u16string::npos);
	join.dismissBox();
	CHECK_FALSE(join.client.inGame());
}

TEST_CASE("onlineux1 retail: back in the online screens after an online game (the score screen's Continue): the screens resume on Open Play signed in, "
		  "the game is left (the lobby offers it for no rejoin), and even a lobby that still lists it (older than ONLINEUX-1) shows no REJOIN for it")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("local=127.0.0.1:9", ""); // the START is built without a word to the relay
	OnlinePlayer host(*s, lobby.port, 87, u""), join(*s, lobby.port, 88, u"");
	const std::vector<OnlinePlayer *> all{ &host, &join };
	signInAndOpenPlay(host, u"Host");
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	joinListed(join, all, u"Joiner");
	const std::string game = host.client.gameId();
	REQUIRE(runUntil(all, [&] { return join.sh->readyIcon(1) == "_enabledUnchecked"; }, 300)); // the joiner's room opens on its CHAT tab
	REQUIRE(join.click("~1.Ready"));
	REQUIRE(runUntil(all, [&] { return host.client.slotViews()[1].ready; }, 300));
	REQUIRE(host.clickWhenShown("LowerButtons_Host.PlayGame"));
	REQUIRE_MESSAGE(runUntil(all, [&] {
		return host.fx.services.countRequests(ShellAction::OnlineGameStart) == 1 && join.fx.services.countRequests(ShellAction::OnlineGameStart) == 1;
	}, 900), host.notes());
	CHECK(join.client.playedGame() == game);
	// the lobby lists the started game as the player's (a crash would come back to it): the answer an older lobby keeps giving after a leave
	join.client.fetchMine();
	REQUIRE(runUntil(all, [&] { return join.client.mine().size() == 1; }, 300));
	std::string mineBefore;
	Tamper keep = [&](const OnlineLobbyClient::HttpRequest &r, HttpAnswer &a) {
		if (r.path == "/v1/mine" && mineBefore.empty())
		{
			mineBefore = a.body;
		}
	};
	join.tamper = keep;
	join.client.fetchMine();
	REQUIRE(runUntil(all, [&] { return !mineBefore.empty(); }, 300));
	join.tamper = nullptr;
	// the game is played and over: the screens come back
	for (OnlinePlayer *p : all)
	{
		backAfterGame(*p);
	}
	// they resume where the player was, signed in: no login page, Open Play's list
	for (OnlinePlayer *p : all)
	{
		REQUIRE_MESSAGE(runUntil(all, [&] { return p->sh->currentPage() == "OnlineOpenPlay" && p->sh->openPlayState() == 2 && p->sh->gameList(); }, 600), p->notes());
		const std::vector<std::string> pages = p->sh->loadedPages();
		CHECK(std::find(pages.begin(), pages.end(), "OnlineLogin") == pages.end());
		CHECK(p->client.signedIn());
		CHECK(p->hasNote("online-game-finished", game));
		CHECK(p->hasNote("online-resume", "OnlineOpenPlay"));
		CHECK(p->client.playedGame().empty());
		CHECK_FALSE(p->client.inGame());
		CHECK(p->ctx.leftGames->count(game) == 1);
		CHECK(p->client.sentCount("leave") == 1);
	}
	// this lobby took the leave: /v1/mine no longer has it
	join.client.fetchMine();
	join.tick(20);
	CHECK(join.client.mine().empty());
	// a lobby that still lists the game (the answer kept from before): it is never offered for a rejoin, nor shown in the list
	join.tamper = [&](const OnlineLobbyClient::HttpRequest &r, HttpAnswer &a) {
		if (r.path == "/v1/mine")
		{
			a.body = mineBefore;
		}
	};
	join.fx.wm->invokeCallback("AptOnline::CustomMatch::Refresh", "");
	REQUIRE(runUntil(all, [&] { return join.client.mine().size() == 1; }, 300));
	join.tick(30);
	CHECK(std::find(join.sh->gameIds().begin(), join.sh->gameIds().end(), game) == join.sh->gameIds().end());
	CHECK(std::count_if(join.sh->boxesShown().begin(), join.sh->boxesShown().end(), [](const std::string &b) { return b.rfind("rejoin-offer", 0) == 0; }) == 0);
	// the lobby refuses a rejoin of a game left (an old client's or a crafted request)
	join.tamper = nullptr;
	join.client.rejoinGame(game);
	REQUIRE(runUntil(all, [&] { return join.hasNote("online-refused", "rejoin: left"); }, 300));
	// r2 (Sol 4): a new run of the program (its own set of left games is empty) with the same session: the lobby's /v1/mine names the game it left, so
	// the running list's game is no WATCH row either (the lobby refuses a player of the game as its spectator: 409 player)
	join.ctx.leftGames->clear();
	join.fx.wm->invokeCallback("AptOnline::CustomMatch::Refresh", "");
	REQUIRE(runUntil(all, [&] {
		return std::any_of(join.client.games().begin(), join.client.games().end(), [&](const OnlineLobbyClient::Listing &l) { return l.id == game && l.state == "started"; });
	}, 300));
	join.tick(30);
	CHECK(join.client.mine().empty());
	CHECK(std::find(join.sh->gameIds().begin(), join.sh->gameIds().end(), game) == join.sh->gameIds().end());
	CHECK(std::none_of(join.sh->gameRows().begin(), join.sh->gameRows().end(), [](const std::string &r) { return r.find("|watch") != std::string::npos; }));
	// hostile "left" lists are refused by name (closed set, size before use): not a list, a non-id, more than kMaxMine
	std::string many = "[";
	for (size_t i = 0; i <= OnlineLobbyClient::kMaxMine; ++i)
	{
		many += std::string(i ? "," : "") + "\"" + std::string(15, '0') + std::to_string(i % 10) + "\"";
	}
	many += "]";
	auto refusedMine = [&]() {
		return (size_t)std::count_if(join.fx.wm->notes().begin(), join.fx.wm->notes().end(),
			[](const WindowManagerNote &n) { return n.kind == "online-refused" && n.detail.find("mine: protocol") != std::string::npos; });
	};
	for (const std::string &bad : { std::string("\"x\""), std::string("[\"not-an-id\"]"), many })
	{
		const size_t refusedBefore = refusedMine();
		join.tamper = [&](const OnlineLobbyClient::HttpRequest &r, HttpAnswer &a) {
			if (r.path == "/v1/mine")
			{
				a.body = "{\"v\":1,\"games\":[],\"left\":" + bad + "}";
			}
		};
		join.client.fetchMine();
		CHECK_MESSAGE(runUntil(all, [&] { return refusedMine() == refusedBefore + 1; }, 300), bad.substr(0, 40));
	}
	join.tamper = nullptr;
}

TEST_CASE("onlineux1 r2 (Claude F1, Sol) retail: a rejoin or watch that could not go on (game.gd _online_game_failed -> OnlineShellContext::gameFailed) "
		  "is not a leave: back in the screens the player keeps the game, the lobby still offers it for a rejoin and grants one")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("local=127.0.0.1:9", "");
	OnlinePlayer host(*s, lobby.port, 91, u""), join(*s, lobby.port, 92, u"");
	const std::vector<OnlinePlayer *> all{ &host, &join };
	signInAndOpenPlay(host, u"Host");
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	joinListed(join, all, u"Joiner");
	const std::string game = host.client.gameId();
	REQUIRE(runUntil(all, [&] { return join.sh->readyIcon(1) == "_enabledUnchecked"; }, 300));
	REQUIRE(join.click("~1.Ready"));
	REQUIRE(runUntil(all, [&] { return host.client.slotViews()[1].ready; }, 300));
	REQUIRE(host.clickWhenShown("LowerButtons_Host.PlayGame"));
	REQUIRE_MESSAGE(runUntil(all, [&] {
		return host.fx.services.countRequests(ShellAction::OnlineGameStart) == 1 && join.fx.services.countRequests(ShellAction::OnlineGameStart) == 1;
	}, 900), host.notes());
	REQUIRE(join.client.playedGame() == game);
	// the joiner's game could not go on on its side (what game.gd does: AptMenuPlayer.online_game_failed, then the score screen's Continue)
	join.ctx.gameFailed(u"The game could not be rejoined: the relay keeps no log of this game: it cannot be rejoined");
	backAfterGame(join);
	REQUIRE_MESSAGE(runUntil(all, [&] { return join.sh->currentPage() == "OnlineOpenPlay" && join.sh->openPlayState() == 2 && join.sh->gameList(); }, 600), join.notes());
	REQUIRE(runUntil(all, [&] {
		return std::any_of(join.sh->boxesShown().begin(), join.sh->boxesShown().end(), [](const std::string &b) { return b.rfind("online-notice|The game could not be rejoined", 0) == 0; });
	}, 300));
	CHECK(join.client.sentCount("leave") == 0); // before r2: the shell's remaking sent POST /leave for it
	CHECK(join.client.playedGame().empty());
	CHECK(join.ctx.leftGames->count(game) == 0);
	CHECK_FALSE(join.hasNote("online-game-finished", game));
	// the lobby still has it as the joiner's running game: listed for a rejoin
	join.dismissBox();
	join.client.fetchMine();
	REQUIRE(runUntil(all, [&] { return join.client.mine().size() == 1 && join.client.mine()[0].id == game; }, 300));
	join.fx.wm->invokeCallback("AptOnline::CustomMatch::Refresh", "");
	join.tick(30);
	const auto at = std::find(join.sh->gameIds().begin(), join.sh->gameIds().end(), game);
	REQUIRE(at != join.sh->gameIds().end());
	CHECK(join.sh->gameRows()[(size_t)(at - join.sh->gameIds().begin())].find("|rejoin") != std::string::npos);
	// and grants a rejoin START (no "409 left")
	join.client.rejoinGame(game);
	REQUIRE_MESSAGE(runUntil(all, [&] { return join.fx.services.countRequests(ShellAction::OnlineGameStart) == 2 || join.hasNote("online-refused", "rejoin"); }, 300), join.notes());
	CHECK_FALSE(join.hasNote("online-refused", "rejoin"));
	CHECK(join.fx.services.countRequests(ShellAction::OnlineGameStart) == 2);
}

TEST_CASE("onlineux1 r2 (F2): a START that could not be played is no played game: dropStart forgets it, and no leave is sent for it later")
{
	OnlineLobbyClient c(clientOptions(4, 4));
	c.setPlayedGame("0123456789abcdef");
	c.dropStart();
	CHECK(c.playedGame().empty());
	CHECK(c.finishGame().empty());
	CHECK(c.sentCount("leave") == 0);
	CHECK(c.takeRequests().empty());
}

TEST_CASE("onlineux1 retail: the server browser: the Lobby Room drop-down lists the one room; the MAP panel shows the selected game's map; the "
		  "player's own map is the one of a game they create")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer a(*s, lobby.port, 89, u""), b(*s, lobby.port, 90, u""), viewer(*s, lobby.port, 91, u"");
	const std::vector<OnlinePlayer *> all{ &a, &b, &viewer };
	signInAndOpenPlay(viewer, u"Viewer");
	GameWindow *lobbies = viewer.sh->lobbiesWindow();
	REQUIRE(lobbies);
	CHECK(GadgetComboBoxGetText(lobbies) == u"OpenBFME");
	REQUIRE(GadgetComboBoxGetListBox(lobbies));
	CHECK(GadgetListBoxGetNumEntries(GadgetComboBoxGetListBox(lobbies)) == 1); // its list: the one room (it was the movie's empty list)
	const std::string own = viewer.sh->setup()->info().mapName;
	REQUIRE_FALSE(own.empty());
	// two games on two other maps (the lobby clients' own create: the map the host chose)
	std::vector<std::string> maps;
	for (const MapCacheEntry &m : viewer.sh->setup()->source().maps())
	{
		if (m.isMultiplayer && m.numPlayers >= 2 && m.name != own && maps.size() < 2)
		{
			maps.push_back(m.name);
		}
	}
	REQUIRE(maps.size() == 2);
	for (size_t i = 0; i < 2; ++i)
	{
		OnlinePlayer &h = i == 0 ? a : b;
		signIn(h, i == 0 ? u"Ahost" : u"Bhost");
		SkirmishGameInfo info = viewer.sh->setup()->info();
		info.mapName = maps[i];
		info.slots[0].state = SLOT_PLAYER;
		for (int sl = 1; sl < MAX_SLOTS; ++sl)
		{
			info.slots[sl] = SkirmishGameSlot();
			info.slots[sl].state = SLOT_OPEN;
		}
		h.client.createGame(i == 0 ? u"Agame" : u"Bgame", "", info, 0);
		REQUIRE_MESSAGE(runUntil(all, [&] { return h.client.inGame(); }, 300), h.notes());
	}
	viewer.fx.wm->invokeCallback("AptOnline::CustomMatch::Refresh", "");
	REQUIRE(runUntil(all, [&] { return viewer.sh->gameRows().size() == 2; }, 300));
	for (size_t i = 0; i < 2; ++i)
	{
		const std::string name = i == 0 ? "Agame" : "Bgame";
		int row = -1;
		for (size_t r = 0; r < viewer.sh->gameRows().size(); ++r)
		{
			row = viewer.sh->gameRows()[r].rfind(name, 0) == 0 ? (int)r : row;
		}
		REQUIRE_FALSE(row == -1); // the game is listed
		starttest::wheelAndClickRow(viewer.fx, viewer.sh->gameList(), row); // the player's click on the row
		REQUIRE_MESSAGE(runUntil(all, [&] { return viewer.sh->setup()->info().mapName == maps[i]; }, 120), viewer.sh->setup()->info().mapName);
	}
	// the player's own game is on the player's own map
	REQUIRE(viewer.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil(all, [&] { return viewer.sh->gameNameWindow() && viewer.sh->openPlayState() == 3; }, 300));
	REQUIRE(viewer.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return viewer.client.inGame() && viewer.sh->openPlayState() == 6; }, 300), viewer.notes());
	CHECK(viewer.client.gameInfo().mapName == own);
}

TEST_CASE("onlineux1 retail: the home page's ticker shows retail's Date / Time / Players Online / Games In Progress rows (RW 0x9C8694 / 0x9C881D) with the "
		  "lobby's counts, and no placeholder rows")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	NodeLobby lobby("", "");
	OnlinePlayer p(*s, lobby.port, 92, u"");
	const char *labels[] = { "APT:Date", "APT:Time", "APT:PlayersOnline", "APT:GamesInProgress" }; // the label table RW 0xDB9290, entries 0, 1, 4, 5
	const char *texts[] = { "Date", "Time", "Players Online", "Games In Progress" }; // the test's game text (the real one: the player's language archive)
	for (int i = 0; i < 4; ++i)
	{
		p.fx.text.set(labels[i], texts[i]);
	}
	signIn(p, u"Tic");
	REQUIRE(runUntil({ &p }, [&] { return p.sh->tickerRows().size() == 4; }, 120));
	std::u16string label;
	for (int i = 0; i < 4; ++i)
	{
		REQUIRE_MESSAGE(p.fx.text.fetch(labels[i], label), labels[i]);
		const std::string field = OnlineLobbyClient::utf16ToUtf8(label) + ":";
		const std::string *shown = p.fx.wm->aptText("APT:TickerField_" + std::to_string(i));
		REQUIRE(shown);
		CHECK(*shown == field);
	}
	// unknown counts read "...", then the host's counts
	CHECK(*p.fx.wm->aptText("APT:TickerValue_2") == "...");
	p.ctx.playersOnline = 7;
	p.ctx.gamesRunning = 3;
	p.tick(5);
	CHECK(*p.fx.wm->aptText("APT:TickerValue_2") == "7");
	CHECK(*p.fx.wm->aptText("APT:TickerValue_3") == "3");
	// the time row is the clock's HH:MM
	const std::string *clock = p.fx.wm->aptText("APT:TickerValue_1");
	REQUIRE(clock);
	CHECK(clock->size() == 5);
	CHECK((*clock)[2] == ':');
	// each row's clip shows those records: its Field and Value are given "$TickerField_<i>" / "$TickerValue_<i>", as the movie's TickerInfo class
	// (registerClass, unported: S-104) would; before, the clips kept their placeholder "Men of The West" / "99.99%"
	std::function<AptCharacterInst *(AptCharacterInst *, const std::string &)> find = [&](AptCharacterInst *c, const std::string &suffix) -> AptCharacterInst * {
		if (!c)
		{
			return nullptr;
		}
		const std::string path = c->targetPath();
		if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
		{
			return c;
		}
		AptSpriteInst *sp = c->asSprite();
		for (AptCharacterInst *k : sp ? sp->children() : std::vector<AptCharacterInst *>())
		{
			if (AptCharacterInst *f = find(k, suffix))
			{
				return f;
			}
		}
		return nullptr;
	};
	for (int i = 0; i < 4; ++i)
	{
		auto *field = dynamic_cast<AptTextInst *>(find(p.fx.wm->apt().level(p.sh->level()), ".TickerInfo_" + std::to_string(i) + ".Field"));
		auto *value = dynamic_cast<AptTextInst *>(find(p.fx.wm->apt().level(p.sh->level()), ".TickerInfo_" + std::to_string(i) + ".Value"));
		REQUIRE_MESSAGE(field, "ticker row " << i);
		REQUIRE_MESSAGE(value, "ticker row " << i);
		CHECK(field->text == "$TickerField_" + std::to_string(i));
		CHECK(value->text == "$TickerValue_" + std::to_string(i));
	}
	CHECK_FALSE(p.hasNote("online-ticker"));
	// the movie shows four rows: none of retail's other eight (ranks, faction win rates) is left to the movie's placeholder
	std::string fields;
	p.fx.wm->getExtern("OnlineHome::NumTickerFields", fields);
	CHECK(fields == "4");
}
#endif

#ifdef ONLINE4_POSIX
namespace
{
int comboItem(GameWindow *combo, int row)
{
	return (int)reinterpret_cast<std::intptr_t>(GadgetComboBoxGetItemData(combo, row));
}
struct HeroDir
{
	std::string path;
	HeroDir()
	{
		std::random_device rd;
		path = (std::filesystem::temp_directory_path() / ("openbfme-onlinemenu1-" + std::to_string(rd()))).u8string();
		std::filesystem::create_directories(std::filesystem::u8path(path));
	}
	~HeroDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(std::filesystem::u8path(path), ec);
	}
};
} // namespace

TEST_CASE("onlinemenu1 retail: the online staging room's Handicap and Hero work as the LAN room's: the joiner picks them in its own row's drop-downs, "
		  "every player's room shows them, and every START carries them (S-4076 / S-4552 were locked)")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	HeroDir dir;
	CreateAHeroHeroList heroes;
	heroes.load(s->mount->fs.get(), dir.path, nullptr);
	REQUIRE(heroes.size() == 8); // RotWK's eight system heroes (the Create-a-Hero screen's own list; no file of ours)
	CreateAHeroScreenContext cah;
	cah.system = &s->world->createAHeroSystem();
	cah.heroes = &heroes;
	NodeLobby lobby("local=127.0.0.1:9", ""); // the START is built without a word to the relay (as the positions test)
	OnlinePlayer host(*s, lobby.port, 81, u""), a(*s, lobby.port, 82, u"");
	const std::vector<OnlinePlayer *> all{ &host, &a };
	for (OnlinePlayer *p : all)
	{
		p->fx.environment.createAHero = &cah;
		p->fx.text.set("GUI:Random", "Random");
		p->fx.text.set("VALUE:Default", " (Default)");
	}
	CHECK_FALSE(host.hasNote("online-stops", "[S-4076]"));
	signInAndOpenPlay(host, u"Host");
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	joinListed(a, all, u"JoinerA");
	REQUIRE(runUntil(all, [&] { return a.sh->slotGadget(1, "Handicap") && a.sh->slotGadget(1, "Hero") && host.sh->slotGadget(1, "Handicap"); }, 300));
	// ---- Handicap: RotWK's combo (RW 0x840B9D): 0, -5 .. -95, the item data the value; the joiner picks -15 (row 3) in its own row ----
	GameWindow *handicap = a.sh->slotGadget(1, "Handicap");
	REQUIRE(GadgetComboBoxGetLength(handicap) == 20);
	for (int r = 0; r < 20; ++r)
	{
		CHECK(comboItem(handicap, r) == -5 * r);
	}
	// the host sees the joiner's row locked once its room view has the joiner (before, slot 1 was an open slot: the host's to fill)
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.client.slotViews()[1].state == "player" && host.sh->slotGadget(1, "Handicap") &&
		!gadgetEnabled(host.sh->slotGadget(1, "Handicap")); }, 300), host.notes());
	GadgetComboBoxSetSelectedPos(handicap, 3, false);
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.client.gameInfo().slots[1].handicap == -15 && host.sh->setup()->info().slots[1].handicap == -15; }, 300),
		a.notes());
	CHECK(a.client.gameInfo().slots[1].handicap == -15);
	CHECK(host.client.gameInfo().slots[0].handicap == 0);
	// a raw out-of-set handicap (a client that skips the combo): the lobby refuses it by name and the room keeps -15
	a.client.setOwnSlot(OnlineLobbyClient::kKeep, OnlineLobbyClient::kKeep, OnlineLobbyClient::kKeep, OnlineLobbyClient::kKeep, -7);
	REQUIRE(runUntil(all, [&] { return a.hasNote("online-refused", "slot: bad_field"); }, 300));
	for (OnlinePlayer *p : all)
	{
		p->tick(30);
	}
	CHECK(host.client.gameInfo().slots[1].handicap == -15);
	// ---- Hero: the joiner takes the Men, then a system hero the Men can use, from its own Hero combo ----
	int men = -1;
	for (int i = 0; i < (int)a.fx.setup.factions().size(); ++i)
	{
		men = a.fx.setup.factions()[(size_t)i].templateName == "FactionMen" ? i : men;
	}
	REQUIRE(men >= 0); // tolerance: a lookup that found FactionMen (any index of the faction list)
	GameWindow *faction = a.sh->slotGadget(1, "PlayerTemplate");
	for (int r = 0; r < GadgetComboBoxGetLength(faction); ++r)
	{
		if (comboItem(faction, r) == men)
		{
			GadgetComboBoxSetSelectedPos(faction, r, false);
		}
	}
	REQUIRE(runUntil(all, [&] { return host.client.gameInfo().slots[1].playerTemplate == men; }, 300));
	const CreateAHeroSystem &sys = s->world->createAHeroSystem();
	int chosen = -1;
	for (int i = 0; i < (int)heroes.size() && chosen < 0; ++i)
	{
		const CreateAHeroSubClass *sub = sys.subClass(heroes.at(i)->hero.classIndex, heroes.at(i)->hero.subClassIndex);
		if (sub && std::find(sub->usableFactions.begin(), sub->usableFactions.end(), 0) != sub->usableFactions.end())
		{
			chosen = i;
		}
	}
	REQUIRE(chosen >= 0); // tolerance: a lookup that found a system hero the Men can use (its index in the list)
	REQUIRE(runUntil(all, [&] { return a.sh->slotGadget(1, "Hero") && GadgetComboBoxGetLength(a.sh->slotGadget(1, "Hero")) == 10; }, 300));
	GameWindow *heroCombo = a.sh->slotGadget(1, "Hero");
	CHECK(gadgetEnabled(heroCombo));
	int row = -1;
	for (int r = 0; r < GadgetComboBoxGetLength(heroCombo); ++r)
	{
		row = comboItem(heroCombo, r) == chosen ? r : row;
	}
	REQUIRE(row >= 0); // tolerance: a lookup that found the hero's row in the combo
	GadgetComboBoxSetSelectedPos(heroCombo, row, false);
	const std::vector<std::uint8_t> wire = heroes.at(chosen)->hero.save();
	REQUIRE_MESSAGE(runUntil(all, [&] {
		const SkirmishGameSlot &h = host.client.gameInfo().slots[1];
		return h.hasCreateAHero && h.createAHero.save() == wire && host.sh->setup()->info().slots[1].hasCreateAHero;
	}, 300), a.notes());
	CHECK(host.client.slotViews()[1].heroProblem.empty());
	// the host's room shows the joiner's choice as text (RW 0x841A9E): a system hero's name and VALUE:Default
	REQUIRE(runUntil(all, [&] { return host.sh->slotGadget(1, "Hero") && GadgetComboBoxGetLength(host.sh->slotGadget(1, "Hero")) == 1; }, 300));
	CHECK(GadgetComboBoxGetText(host.sh->slotGadget(1, "Hero")) == heroes.at(chosen)->hero.name + u" (Default)");
	CHECK_FALSE(gadgetEnabled(host.sh->slotGadget(1, "Hero")));
	// ---- ready, start: both STARTs carry the handicap and the record as OnlineStart reads them (the joiner is on the CHAT tab, the Ready column's) ----
	REQUIRE_MESSAGE(runUntil(all, [&] { return a.sh->readyIcon(1) == "_enabledUnchecked"; }, 300), a.notes());
	// ---- a hero record the HOST's client cannot read (the host's room views rewritten: the lobby and the joiner keep the good one): the slot shows no
	// hero there and the host's Play Game is refused by name (the joiner's name in the room's chat), no START asked for ----
	host.tamper = [](const OnlineLobbyClient::HttpRequest &, HttpAnswer &ans) {
		ans.body = std::regex_replace(ans.body, std::regex("\"cah\":\"[^\"]+\""), "\"cah\":\"AAAA\"");
	};
	a.client.chat(u"a new room view for the host");
	REQUIRE_MESSAGE(runUntil(all, [&] { return !host.client.slotViews()[1].heroProblem.empty(); }, 300), host.notes());
	CHECK_FALSE(host.client.gameInfo().slots[1].hasCreateAHero);
	REQUIRE(a.click("~1.Ready"));
	REQUIRE(runUntil(all, [&] { return host.client.slotViews()[1].ready; }, 300));
	const size_t refusals = host.sh->startRefusals().size();
	REQUIRE(host.clickWhenShown("LowerButtons_Host.PlayGame"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.sh->startRefusals().size() == refusals + 1; }, 300), host.notes());
	CHECK(host.sh->startRefusals().back() == "create-a-hero");
	CHECK(host.client.sentCount("start") == 0);
	CHECK(std::count(host.sh->chatLines().begin(), host.sh->chatLines().end(),
		  "The game can't start: JoinerA's Create-a-Hero can't be used. They need to choose another hero or none.") == 1);
	// ---- the host's views read again (nothing rewritten): the record is the joiner's again and Play Game starts ----
	host.tamper = nullptr;
	a.client.chat(u"and another");
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.client.slotViews()[1].heroProblem.empty() && host.client.gameInfo().slots[1].hasCreateAHero; }, 300),
		host.notes());
	REQUIRE(runUntil(all, [&] { return host.client.slotViews()[1].ready; }, 300));
	REQUIRE(host.clickWhenShown("LowerButtons_Host.PlayGame"));
	REQUIRE_MESSAGE(runUntil(all, [&] {
		return host.fx.services.countRequests(ShellAction::OnlineGameStart) == 1 && a.fx.services.countRequests(ShellAction::OnlineGameStart) == 1;
	}, 900), host.notes());
	for (OnlinePlayer *p : all)
	{
		OnlineStart::Limits limits;
		limits.playerTemplateCount = s->world->playerTemplates().getPlayerTemplateCount();
		limits.colorCount = (int)s->settings.multiplayerColors.size();
		LobbyStart parsed;
		std::string error;
		ProfileIdentity identity;
		identity.digest = p->client.options().profileDigest;
		REQUIRE_MESSAGE(OnlineStart::parse(startOf(p->fx.services), identity, limits, parsed, &error), error);
		CHECK(parsed.game.game.slots[0].handicap == 0);
		CHECK(parsed.game.game.slots[1].handicap == -15);
		CHECK_FALSE(parsed.game.game.slots[0].hasCreateAHero);
		REQUIRE(parsed.game.game.slots[1].hasCreateAHero);
		CHECK(parsed.game.game.slots[1].createAHero.save() == wire);
	}
}
#endif

#ifdef ONLINE4_POSIX
namespace
{
// lane ONLINEMENU-1: a relay stand-in for the Connections screen: answers a probe (an unknown sender's LOG_REQUEST) NOT_BOUND as RelayCore does, or
// stays silent while `silent` is set
struct ProbeRelay
{
	UDP socket;
	NetAddress address;
	std::atomic<bool> stop{ false }, silent{ false };
	std::thread thread;
	ProbeRelay()
	{
		std::string error;
		REQUIRE_MESSAGE(socket.bind(0x7F000001u, 0, &error), error);
		address = socket.localAddress();
		address.ip = 0x7F000001u;
		thread = std::thread([this] {
			NetAddress from;
			std::vector<std::uint8_t> d;
			const std::vector<std::uint8_t> notBound = RelayProtocol::encodeEmpty(RelayProtocol::NOT_BOUND);
			while (!stop)
			{
				if (!socket.receiveFrom(from, d))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				if (!silent && d == RelayProtocol::encodeLogRequest(0))
				{
					socket.sendTo(from, notBound.data(), notBound.size());
				}
			}
		});
	}
	~ProbeRelay()
	{
		stop = true;
		thread.join();
	}
};
} // namespace

TEST_CASE("onlinemenu1 retail: the staging room's CONNECTIONS button opens RotWK's Connections grid with each player's link to the relay (the owner's "
		  "option 1): good links, a failed one after Refresh, empty cells for no player, names and numbers (S-4550)")
{
	ONLINE4_REQUIRE_NODE();
	OPENBFME_REQUIRE_START(s);
	ProbeRelay relay;
	NodeLobby lobby("lab=" + relay.address.text(), "");
	OnlinePlayer host(*s, lobby.port, 91, u""), a(*s, lobby.port, 92, u"");
	const std::vector<OnlinePlayer *> all{ &host, &a };
	signInAndOpenPlay(host, u"Host");
	// the owner (2026-10-20: "ping to server"): outside a room the player's ping is measured against the relay the lobby names (GET /v1/health), for
	// the corner only: nothing is reported to the lobby
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.lobbyRelay() == relay.address.text() && host.client.ownRelayKnown(); }, 1500),
		host.notes() << host.client.relayLookupError());
	CHECK(host.client.ownRelayRttMs() >= 1); // tolerance: a loopback round trip, at least the 1 ms the probe reports
	CHECK(host.client.sentCount("health") == 1);
	CHECK(host.client.sentCount("link") == 0);
	REQUIRE(host.clickWhenShown("Controls.CreateGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.sh->gameNameWindow() != nullptr && host.sh->openPlayState() == 3; }, 300));
	REQUIRE(host.clickWhenShown("CreateGamePopup.Accept"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.client.inGame() && host.sh->openPlayState() == 6; }, 300), host.notes());
	joinListed(a, all, u"JoinerA");
	// the room names the lobby's relay; both clients probe it on entering and report their links (no request of the screen's own)
	CHECK(host.client.roomRelay() == relay.address.text());
	auto linked = [&](OnlinePlayer &p, int slot) { return p.client.slotViews()[(size_t)slot].linkKnown; };
	REQUIRE_MESSAGE(runUntil(all, [&] { return linked(host, 0) && linked(host, 1) && linked(a, 0) && linked(a, 1); }, 1500), host.notes() << a.notes());
	for (int slot = 0; slot < 2; ++slot)
	{
		CHECK(host.client.slotViews()[(size_t)slot].rttMs >= 1); // tolerance: a loopback round trip, at least the 1 ms the probe reports
		CHECK(host.client.slotViews()[(size_t)slot].rttMs < 300); // tolerance: below GameSpy's good cut-off (a loopback answer)
		CHECK(host.client.slotViews()[(size_t)slot].lossPct == 0);
	}
	CHECK(host.client.sentCount("link") == 1);
	CHECK(a.client.sentCount("link") == 1);
	// Sol: a screen over the shell (Options) pauses the measurement; back on top nothing is measured or reported again before its minute is up
	host.fx.shell->push("Options.apt");
	host.tick(10);
	host.fx.shell->pop();
	runUntil(all, [&] { return false; }, 120); // two seconds of updates: a new measurement would have been reported by then (a loopback answers at once)
	CHECK(host.client.sentCount("link") == 1);
	auto isMs = [](const UnicodeString &t) { return t.size() > 3 && t.compare(t.size() - 3, 3, u" ms") == 0 && t[0] >= u'0' && t[0] <= u'9'; };
	// ---- the owner (2026-10-20: "the player can just see their ping on the side"): the chat tab's Players list has each player's ping beside the name
	// (the host's own: its ping to the relay; the joiner's: both pings added) ----
	REQUIRE(host.clickWhenShown("TabButtons.ChatTab"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.sh->chatPlayersWindow() && GadgetListBoxGetNumEntries(host.sh->chatPlayersWindow()) == 2; }, 300), host.notes());
	host.tick(60); // the lists are made again once the tab's clip has played in
	{
		GameWindow *players = host.sh->chatPlayersWindow();
		REQUIRE(players);
		CHECK(GadgetListBoxGetNumColumns(players) == 2);
		CHECK(GadgetListBoxGetText(players, 0, 0) == u"Host");
		CHECK_MESSAGE(isMs(GadgetListBoxGetText(players, 0, 1)), OnlineLobbyClient::utf16ToUtf8(GadgetListBoxGetText(players, 0, 1)));
		CHECK_MESSAGE(isMs(GadgetListBoxGetText(players, 1, 1)), OnlineLobbyClient::utf16ToUtf8(GadgetListBoxGetText(players, 1, 1)));
		Color shown = 0;
		GadgetListBoxGetTextAndColor(players, &shown, 1, 1);
		CHECK(shown == 0xFF6CD66Cu); // a loopback ping: the good band's green
	}
	CHECK(host.client.lobbyRttMs() >= 0); // tolerance: measured (not -1); its value is the test transport's timing
	// ---- the movie's own CONNECTIONS button: the grid shows good cells between the two players, nothing for the empty slots or a player's own ----
	const size_t boxes = host.sh->boxesShown().size();
	REQUIRE(host.clickWhenShown("LowerButtons_Host.Connections"));
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.sh->connectionCell(0, 1) == "_good"; }, 300), host.notes());
	CHECK(host.sh->boxesShown().size() == boxes); // no "not available" box any more
	CHECK(host.sh->connectionsOpen());
	// the owner (2026-10-20: the grid "makes like no sense"): an OpenBFME page covers it and says it in words: the lobby server's response time, the
	// player's ping to the relay, its loss, and each other player's ping to the player
	REQUIRE(host.sh->networkPageOpen());
	auto pageValue = [&](const std::string &key) {
		for (const OpenBFMEPageRow &r : host.sh->networkPage()->spec().rows)
		{
			if (r.key == key)
			{
				return r.valueText;
			}
		}
		return UnicodeString(u"(no row)");
	};
	auto pageLabel = [&](const std::string &key) {
		for (const OpenBFMEPageRow &r : host.sh->networkPage()->spec().rows)
		{
			if (r.key == key)
			{
				return r.label;
			}
		}
		return UnicodeString(u"(no row)");
	};
	REQUIRE_MESSAGE(runUntil(all, [&] { return isMs(pageValue("relay")); }, 300), OnlineLobbyClient::utf16ToUtf8(pageValue("relay")));
	CHECK(isMs(pageValue("lobby")));
	CHECK(pageValue("loss") == u"0%");
	CHECK(pageLabel("p1") == u"JoinerA");
	CHECK_MESSAGE(isMs(pageValue("p1")), OnlineLobbyClient::utf16ToUtf8(pageValue("p1")));
	CHECK(pageValue("p0") == u"(no row)"); // the player's own slot is the YOU column
	CHECK(host.sh->connectionCell(1, 0) == "_good");
	CHECK(host.sh->connectionCell(0, 0) == "_empty");
	// the effect in the movie: each cell clip (Connections.apt Main.Grid.<row>.<column>) plays its state's frame label, the movie's own frames
	auto cellShows = [&](int row, int column, const std::string &label) {
		AptCharacterInst *c = host.fx.wm->apt().resolvePath(host.fx.wm->apt().level(host.sh->level()),
			"OnlineOpenPlay.ConnectionScreen.Main.Grid." + std::to_string(row) + "." + std::to_string(column));
		AptSpriteInst *sp = c ? c->asSprite() : nullptr;
		if (!sp || sp->labelFrame(label) < 0)
		{
			return false;
		}
		const int from = sp->labelFrame(label);
		int to = sp->totalFrames();
		for (const char *other : { "_empty", "_waiting", "_connecting", "_good", "_warning", "_bad", "_failed" })
		{
			const int f = sp->labelFrame(other);
			if (f > from && f < to)
			{
				to = f;
			}
		}
		return sp->frame >= from && sp->frame < to;
	};
	CHECK(cellShows(0, 1, "_good"));
	CHECK(cellShows(1, 0, "_good"));
	CHECK(cellShows(0, 0, "_empty"));
	CHECK(cellShows(2, 3, "_empty"));
	CHECK(host.sh->connectionCell(1, 1) == "_empty");
	for (int i = 2; i < MAX_SLOTS; ++i)
	{
		CHECK_MESSAGE(host.sh->connectionCell(0, i) == "_empty", i);
		CHECK_MESSAGE(host.sh->connectionCell(i, 1) == "_empty", i);
	}
	auto text = [&](const std::string &name) {
		const std::string *t = host.fx.wm->aptText(name);
		return t ? *t : std::string("(unset)");
	};
	CHECK(text("APT:ConnectionPlayerName_0") == "Host");
	CHECK(text("APT:ConnectionPlayerName_1") == "JoinerA");
	CHECK(text("APT:ConnectionPlayerNum_0") == "1");
	CHECK(text("APT:ConnectionPlayerNum_1") == "2");
	CHECK(text("APT:ConnectionPlayerName_2").empty());
	CHECK(text("APT:ConnectionPlayerNum_2").empty());
	// ---- the relay stops answering; the screen's Refresh probes again: the host's link fails, every cell with the host is failed on both clients ----
	relay.silent = true;
	{
		// the page's REFRESH NOW (its one tab; the screen's own button under the page is out of reach), clicked with the pointer
		std::array<int, 4> tr{};
		REQUIRE(host.sh->networkPage()->rect("tab:0", tr));
		starttest::clickAt(host.fx, (tr[0] + tr[2]) / 2.0f, (tr[1] + tr[3]) / 2.0f);
	}
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.client.slotViews()[0].linkKnown && host.client.slotViews()[0].rttMs == -1 &&
		a.client.slotViews()[0].rttMs == -1; }, 1500), host.notes());
	CHECK(host.client.slotViews()[0].lossPct == 100);
	REQUIRE_MESSAGE(runUntil(all, [&] { return host.sh->connectionCell(0, 1) == "_failed"; }, 300), host.notes());
	CHECK(host.sh->connectionCell(1, 0) == "_failed");
	host.tick(5);
	CHECK(cellShows(0, 1, "_failed"));
	CHECK(host.client.sentCount("link") == 2);
	CHECK(pageValue("relay") == u"no answer");
	CHECK(pageValue("loss") == u"100%");
	CHECK(pageValue("p1") == u"no link");
	// ---- the grid's Kick buttons (Main.Grid.<row>.Kick) stay in the movie's own state with no hit area in the staging room: a player cannot press
	// them (the room's own per-slot Kick is the way to remove a player); nothing reaches the engine from them ----
	{
		std::vector<AptButtonInst *> all;
		host.buttons(host.fx.wm->apt().level(host.sh->level()), all);
		for (AptButtonInst *b : all)
		{
			if (b->targetPath().find("Main.Grid.") == std::string::npos || b->targetPath().find(".Kick") == std::string::npos)
			{
				continue;
			}
			float x0, y0, x1, y1, x, y;
			REQUIRE(b->contentBounds(x0, y0, x1, y1));
			b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
			CHECK_MESSAGE(host.fx.wm->apt().input().hitTestButtons(x, y) == nullptr, b->targetPath());
		}
	}
}
#endif

#ifdef ONLINE4_POSIX // OnlinePlayer (the online players above) is POSIX-only
TEST_CASE("onlinemenu1 retail: the FRIENDS page over the online screens (OpenBFME's, in the game's style): the button drawn beside MESSENGER and "
		  "pressed for real, the page's tabs, rows and activating list, Enter in its entry, closed when another screen covers the shell, and nothing "
		  "of the shell's left on the layer once it is gone")
{
	OPENBFME_REQUIRE_START(s);
	OnlinePlayer p(*s, 1, 95, u""); // no lobby needed: the page is the shell's own
	p.tick(30);
	AptGadgetLayer *layer = p.fx.wm->gadgetLayer();
	REQUIRE(layer);
	auto drawn = [&](const std::u16string &text) {
		GadgetDrawList out;
		layer->buildDrawList(out);
		return std::any_of(out.commands.begin(), out.commands.end(), [&](const GadgetDrawCommand &c) { return c.kind == GadgetDrawCommand::Kind::Text && c.text == text; });
	};
	CHECK_FALSE(drawn(u"FRIENDS (2)"));
	// ---- the button: drawn once the host names it, pressed with the pointer (the window manager's mouse filter is the shell's) ----
	p.sh->setFriendsButton(u"FRIENDS (2)");
	p.tick(2);
	REQUIRE(drawn(u"FRIENDS (2)"));
	const float bx = (AptOnlineShell::kFriendsButtonLeft + AptOnlineShell::kFriendsButtonRight) / 2.0f;
	const float by = (AptOnlineShell::kFriendsButtonTop + AptOnlineShell::kFriendsButtonBottom) / 2.0f;
	starttest::clickAt(p.fx, bx, by);
	std::vector<OpenBFMEPageEvent> ev = p.sh->takeFriendsEvents();
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == OpenBFMEPageEvent::Kind::Entry);
	CHECK(ev[0].key == "friends_button");
	starttest::clickAt(p.fx, AptOnlineShell::kFriendsButtonLeft - 40.0f, by); // beside it: nothing
	CHECK(p.sh->takeFriendsEvents().empty());
	// ---- the page: tabs, a text row, an entry, a choice, and a list below the rows whose lines can be double-clicked ----
	OpenBFMEPageSpec spec;
	spec.tabsHeader = u"FRIENDS";
	spec.tabs = { u"REQUESTS", u"ADD FRIEND", u"BACK" };
	spec.tab = 1;
	spec.panelHeader = u"ADD A FRIEND";
	OpenBFMEPageRow code;
	code.kind = OpenBFMEPageRow::Kind::Text;
	code.key = "code";
	code.label = u"Your friend code";
	code.valueText = u"0123-4567-89";
	OpenBFMEPageRow add;
	add.kind = OpenBFMEPageRow::Kind::Entry;
	add.key = "add_code";
	add.label = u"Friend's code (Enter)";
	add.maxValue = 12;
	OpenBFMEPageRow status;
	status.kind = OpenBFMEPageRow::Kind::Choice;
	status.key = "status";
	status.label = u"Show me as";
	status.items = { { u"Online", true }, { u"Away", true } };
	spec.rows = { code, add, status };
	spec.hasList = true;
	spec.list = { { u"Create a recovery code", u"" }, { u"Second line", u"" } };
	spec.listFromRow = 4;
	spec.listActivates = true;
	std::string err;
	REQUIRE_MESSAGE(p.sh->openFriendsPage(spec, &err), err);
	p.tick(2);
	OpenBFMEOptionsPage *page = p.sh->friendsPage();
	REQUIRE(page);
	CHECK(p.sh->friendsPageOpen());
	CHECK(drawn(u"ADD A FRIEND"));
	CHECK(drawn(u"Your friend code"));
	CHECK(drawn(u"FRIENDS (2)")); // the button stays
	std::array<int, 4> lr{}, er{};
	REQUIRE(page->rect("list", lr));
	REQUIRE(page->rect("add_code", er));
	CHECK(lr[1] == OpenBFMEOptionsPage::kRowTop + 4 * OpenBFMEOptionsPage::kRowPitch); // the list from row 4 (listFromRow), below the three rows
	CHECK(lr[3] < OpenBFMEOptionsPage::kHelpTop - 10); // tolerance: the font's line height sets the exact bottom; two lines: the list is as tall as its lines and one more, not the rest of the panel
	CHECK(er[2] - er[0] == OpenBFMEOptionsPage::kEntryWidth); // a code's entry keeps its width (a longer entry runs to the gadgets' right edge)
	// a click on a tab (the page takes it; the online page under it never sees it)
	std::array<int, 4> tr{};
	REQUIRE(page->rect("tab:2", tr));
	starttest::clickAt(p.fx, (tr[0] + tr[2]) / 2.0f, (tr[1] + tr[3]) / 2.0f);
	ev = p.sh->takeFriendsEvents();
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == OpenBFMEPageEvent::Kind::Tab);
	CHECK(ev[0].value == 2);
	// Enter in the entry (the gadget's own GEM_EDIT_DONE to its owner): the text comes with the Submit
	GameWindow *entry = page->gadget("add_code");
	REQUIRE(entry);
	GadgetTextEntrySetText(entry, u"ABCD-EFGH-JK");
	entry->manager().winSendSystemMsg(entry->winGetOwner(), GEM_EDIT_DONE, (WindowMsgData)entry, 0);
	ev = p.sh->takeFriendsEvents();
	REQUIRE_FALSE(ev.empty());
	CHECK(ev.back().kind == OpenBFMEPageEvent::Kind::Submit);
	CHECK(ev.back().key == "add_code");
	CHECK(ev.back().text == u"ABCD-EFGH-JK");
	// ONLINEMENU-1 (the friends video, 2026-10-20): a real click into the entry and typed characters reach it: the page's band kept every press
	// from the gadget layer, so a player could not type a friend code (the steps above send the gadget's messages directly)
	GadgetTextEntrySetText(entry, u"");
	starttest::clickAt(p.fx, (er[0] + er[2]) / 2.0f, (er[1] + er[3]) / 2.0f);
	CHECK(p.sh->takeFriendsEvents().empty()); // a click in the page is not the online page's under it, nor a tab
	for (char16_t c : std::u16string(u"WXYZ-1234"))
	{
		p.fx.wm->postTextInput(c);
	}
	p.tick(2);
	const auto typed = GadgetTextEntryGetText(entry);
	CHECK(std::u16string(typed.begin(), typed.end()) == u"WXYZ-1234");
	p.fx.wm->postTextInput(u'\r');
	p.tick(2);
	ev = p.sh->takeFriendsEvents();
	REQUIRE_FALSE(ev.empty());
	CHECK(ev.back().kind == OpenBFMEPageEvent::Kind::Submit);
	CHECK(ev.back().text == u"WXYZ-1234");
	// a list line double-clicked (ZH GadgetListBox's GLM_DOUBLE_CLICKED with the row) is the page's ListActivate with that row
	GameWindow *list = page->gadget("list");
	REQUIRE(list);
	list->manager().winSendSystemMsg(list->winGetOwner(), GLM_DOUBLE_CLICKED, (WindowMsgData)list, 1);
	ev = p.sh->takeFriendsEvents();
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == OpenBFMEPageEvent::Kind::ListActivate);
	CHECK(ev[0].value == 1);
	// the choice through its gadget
	REQUIRE_MESSAGE(page->choose("status", 1, &err), err);
	ev = p.sh->takeFriendsEvents();
	REQUIRE_FALSE(ev.empty());
	CHECK(ev.back().kind == OpenBFMEPageEvent::Kind::Value);
	CHECK(ev.back().value == 1);
	// ---- a screen over the shell (Options): the page closes (its gadgets would show through) and the host is told; back on top, the button works ----
	p.fx.shell->push("Options.apt");
	p.tick(10);
	CHECK_FALSE(p.sh->friendsPageOpen());
	ev = p.sh->takeFriendsEvents();
	REQUIRE_FALSE(ev.empty());
	CHECK(ev.back().kind == OpenBFMEPageEvent::Kind::Cancel);
	CHECK(ev.back().key == "covered");
	CHECK_FALSE(drawn(u"FRIENDS (2)")); // nothing of the shell's over the Options screen
	p.fx.shell->pop();
	p.tick(10);
	REQUIRE(drawn(u"FRIENDS (2)"));
	starttest::clickAt(p.fx, bx, by);
	ev = p.sh->takeFriendsEvents();
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].key == "friends_button");
	// ---- lane ONLINEMENU-1 (friends in the Messenger): the host's Yes / No question in GuiFX's own box, one at a time, its answer handed back once ----
	REQUIRE(p.sh->askYesNo("social1", u"Friend Game Invite", u"Ann has invited you to play a Open Play game. Do you want to jump to their game?"));
	CHECK_FALSE(p.sh->askYesNo("social2", u"Ignore", u"Ignore Bea?")); // the box is up
	CHECK_FALSE(p.sh->askYesNo("bad|key", u"T", u"t"));
	p.tick(10);
	p.dismissBox("Yes");
	CHECK(p.sh->takeAnswers() == std::vector<std::string>{ "social1|yes" });
	CHECK(p.sh->takeAnswers().empty());
	REQUIRE(p.sh->askYesNo("social2", u"Ignore", u"Ignore Bea?"));
	p.tick(10);
	p.dismissBox("No");
	CHECK(p.sh->takeAnswers() == std::vector<std::string>{ "social2|no" });
	// ---- the shell gone: its decorator is not left on the layer (it captured the shell) ----
	p.fx.shell->pop();
	p.tick(5);
	CHECK(layer->decoratorOwner() != static_cast<const void *>(p.sh));
}
#endif
