// OpenBFME unit tests. GPL-3.0.
// Lane ONLINEHUB-1 (docs/PLATFORM_API.md): RotWK's online TOURNAMENT page (OnlineQuickMatch.swf: Quick Match and the tournaments panel) and STATS page
// (Stats.swf: "Your stats" and the community's), GameClient/GUI/AptScreens/AptOnlineHub.h and AptStats.h, on the retail movies, against a real local
// lobby (services/online/lobby/src/server.mjs, Node.js) and the BACKEND'S OWN service (tools/platform_fake/fake_server.py, Python: the copy of the private
// repository's platform service, source commit 192a0fca), so the client and the real backend cannot drift. Every step goes through the movie's own buttons
// where the movie has one. The tests SKIP LOUDLY where Node.js, Python 3, the retail installs or POSIX processes are missing.

#include "doctest.h"
#include "OnlineTestWait.h"
#include "TestChild.h"

#include "Common/MiniJson.h"
#include "GameClient/GameRecords.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptOnlineHub.h"
#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/AptScreens/AptStats.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/OptionPreferences.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/PlatformApiClient.h"
#include "GameNetwork/Transport.h"

#include "ProcessTestUtil.h"
#include "StartShellFx.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
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
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#define HUB_POSIX 1
#endif

#ifdef HUB_POSIX
namespace
{
std::string dumpNotes(WindowManager &wm, std::size_t from)
{
	std::string out;
	for (std::size_t i = from; i < wm.notes().size(); ++i)
	{
		out += "  note " + wm.notes()[i].kind + ": " + wm.notes()[i].detail + "\n";
	}
	return out;
}

struct Answer
{
	int status = 0;
	std::string body, error;
};

// a plain HTTP/1.1 request to 127.0.0.1:port (the game's transport is Godot's HTTPRequest: scripts/online_http.gd)
Answer http(int port, const std::string &method, const std::string &path, const std::string &body, const std::string &token = std::string())
{
	Answer a;
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((std::uint16_t)port);
	addr.sin_addr.s_addr = htonl(0x7F000001u);
	if (fd < 0 || connect(fd, (sockaddr *)&addr, sizeof(addr)) != 0)
	{
		if (fd >= 0)
		{
			close(fd);
		}
		a.error = "connection refused";
		return a;
	}
	const timeval timeout{ 60, 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	std::string req = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Type: application/json\r\n";
	if (!token.empty())
	{
		req += "Authorization: Bearer " + token + "\r\n";
	}
	req += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
	std::size_t sent = 0;
	while (sent < req.size())
	{
		const ssize_t n = send(fd, req.data() + sent, req.size() - sent, 0);
		if (n <= 0)
		{
			break;
		}
		sent += (std::size_t)n;
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
		all.append(buf, (std::size_t)n);
	}
	close(fd);
	const std::size_t sp = all.find(' '), end = all.find("\r\n\r\n");
	if (sp == std::string::npos || end == std::string::npos)
	{
		a.error = "no HTTP answer";
		return a;
	}
	a.status = std::atoi(all.c_str() + sp + 1);
	std::string headers = all.substr(0, end);
	std::transform(headers.begin(), headers.end(), headers.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	std::string content = all.substr(end + 4);
	if (headers.find("transfer-encoding: chunked") != std::string::npos)
	{
		std::string out;
		std::size_t at = 0;
		for (;;)
		{
			const std::size_t eol = content.find("\r\n", at);
			if (eol == std::string::npos)
			{
				break;
			}
			const std::size_t len = std::strtoul(content.substr(at, eol - at).c_str(), nullptr, 16);
			if (len == 0)
			{
				break;
			}
			out += content.substr(eol + 2, len);
			at = eol + 2 + len + 2;
		}
		content = out;
	}
	a.body = content;
	return a;
}

int freePort()
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

std::string pathOf(const char *env, const char *fallback)
{
	if (const char *e = std::getenv(env))
	{
		return e;
	}
	return fallback;
}

std::string lobbyServer()
{
#ifdef OPENBFME_LOBBY_SERVER
	return pathOf("OPENBFME_LOBBY_SERVER", OPENBFME_LOBBY_SERVER);
#else
	return pathOf("OPENBFME_LOBBY_SERVER", "");
#endif
}

std::string fakePlatform()
{
#ifdef OPENBFME_FAKE_PLATFORM
	return pathOf("OPENBFME_FAKE_PLATFORM", OPENBFME_FAKE_PLATFORM);
#else
	return pathOf("OPENBFME_FAKE_PLATFORM", "");
#endif
}

bool haveTools()
{
	return std::system("node --version > /dev/null 2>&1") == 0 && std::system("python3 --version > /dev/null 2>&1") == 0 && std::filesystem::exists(lobbyServer())
		&& std::filesystem::exists(fakePlatform());
}

const char *kAdminToken = "screens-test-admin-token";
const char *kTicketKeyHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

JsonValue parseJson(const std::string &text)
{
	JsonValue v;
	std::string err;
	REQUIRE_MESSAGE(JsonValue::parse(text, v, &err), text);
	return v;
}

// a local service process on a free port until the test ends: the lobby (node server.mjs) or the platform (python3 -S fake_server.py: the backend's own
// service behind its HTTP fake, tools/platform_fake)
struct ServiceProcess
{
	pid_t pid = -1;
	int port = 0;
	std::filesystem::path dir;
	ServiceProcess(bool lobby, const std::string &relays = std::string())
	{
		dir = std::filesystem::temp_directory_path() / ("openbfme-hub-" + std::to_string(getpid()) + "-" + std::to_string(NetMilliseconds()) + (lobby ? "l" : "p"));
		std::filesystem::create_directories(dir / "data");
		port = freePort();
		std::vector<std::string> argv;
		if (lobby)
		{
			argv = { "node", lobbyServer() };
		}
		else
		{
			argv = { "python3", "-S", fakePlatform(), "--admin-token", kAdminToken, "--port=" + std::to_string(port) };
		}
		std::fflush(nullptr);
		pid = fork();
		if (pid == 0)
		{
			if (lobby)
			{
				setenv("TICKET_KEYS", (std::string("1:") + kTicketKeyHex + "\n").c_str(), 1);
				setenv("SESSION_KEY", "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf", 1);
				setenv("RELAYS", relays.c_str(), 1);
				setenv("LOBBY_BIND", ("127.0.0.1:" + std::to_string(port)).c_str(), 1);
				setenv("LOBBY_DATA", (dir / "data").string().c_str(), 1);
			}
			const std::string log = (dir / "service.log").string();
			if (!freopen(log.c_str(), "w", stdout) || !freopen(log.c_str(), "a", stderr))
			{
				_exit(126);
			}
			std::vector<char *> cargv;
			for (std::string &a : argv)
			{
				cargv.push_back(a.data());
			}
			cargv.push_back(nullptr);
			execvp(argv[0].c_str(), cargv.data());
			_exit(127);
		}
		const bool ready = onlinetest::waitUntil([&] { return http(port, "GET", "/v1/health", "").status == 200; });
		if (!ready)
		{
			stop();
		}
		std::ifstream serviceLog(dir / "service.log");
		const std::string output((std::istreambuf_iterator<char>(serviceLog)), std::istreambuf_iterator<char>());
		REQUIRE_MESSAGE(ready, "service did not become healthy: ", output);
	}
	void stop()
	{
		if (pid > 0)
		{
			kill(pid, SIGTERM);
			int st = 0;
			waitpid(pid, &st, 0);
			pid = -1;
		}
	}
	~ServiceProcess()
	{
		stop();
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
	// ---- the platform's test surface (fake_server.py) ----
	struct Entry
	{
		std::string method, path, body;
		int status = 0;
	};
	std::vector<Entry> log() const
	{
		std::vector<Entry> out;
		const JsonValue root = parseJson(http(port, "GET", "/__test/log", "").body);
		for (const JsonValue &e : root.get("log")->array)
		{
			out.push_back({ e.get("method")->string, e.get("path")->string, e.get("body")->string, (int)e.get("status")->number });
		}
		return out;
	}
	int count(const std::string &line) const // "METHOD /path?query"
	{
		int n = 0;
		for (const Entry &e : log())
		{
			n += (e.method + " " + e.path) == line ? 1 : 0;
		}
		return n;
	}
	int countPrefix(const std::string &prefix) const
	{
		int n = 0;
		for (const Entry &e : log())
		{
			n += (e.method + " " + e.path).rfind(prefix, 0) == 0 ? 1 : 0;
		}
		return n;
	}
	std::vector<std::string> bodies(const std::string &prefix) const // the request bodies of the lines starting with `prefix`
	{
		std::vector<std::string> out;
		for (const Entry &e : log())
		{
			if ((e.method + " " + e.path).rfind(prefix, 0) == 0)
			{
				out.push_back(e.body);
			}
		}
		return out;
	}
	std::int64_t advance(int seconds) const
	{
		return (std::int64_t)parseJson(http(port, "POST", "/__test/advance", "{\"seconds\":" + std::to_string(seconds) + "}").body).get("now")->number;
	}
	void post(const std::string &path, const std::string &body) const
	{
		REQUIRE(http(port, "POST", path, body).status == 200);
	}
	Answer admin(const std::string &path, const std::string &body) const { return http(port, "POST", path, body, kAdminToken); }
};

std::unique_ptr<ServiceProcess> makeLobby(const std::string &relays = "lab=127.0.0.1:9")
{
	return std::make_unique<ServiceProcess>(true, relays);
}
std::unique_ptr<ServiceProcess> makePlatform()
{
	return std::make_unique<ServiceProcess>(false);
}

const std::string kClientJson = "\"client\":{\"platform\":\"linux\",\"version\":\"0.4.5-test\"}";

// ---- tournaments made through the service's own admin route and its own registration ----
struct Entrant
{
	std::string install, nick;
};

std::string createTournament(const ServiceProcess &plat, const std::string &name, const std::string &mode, int maxPlayers, int startInS, const std::string &rules = "Best of one.")
{
	const std::int64_t now = plat.advance(0);
	const Answer a = plat.admin("/v1/admin/tournaments", "{\"v\":1,\"name\":\"" + name + "\",\"mode\":\"" + mode + "\",\"size\":\"1v1\",\"start\":" + std::to_string(now + startInS)
		+ ",\"max_players\":" + std::to_string(maxPlayers) + ",\"rules\":\"" + rules + "\"}");
	REQUIRE_MESSAGE(a.status == 201, a.body);
	return parseJson(a.body).get("id")->string;
}

void registerEntrant(const ServiceProcess &plat, const std::string &tid, const Entrant &e)
{
	const Answer a = http(plat.port, "POST", "/v1/tournaments/" + tid + "/register", "{\"v\":1,\"install\":\"" + e.install + "\",\"nick\":\"" + e.nick + "\"," + kClientJson + "}");
	REQUIRE_MESSAGE(a.status == 200, a.body);
}

// a tournament whose start has passed with `entrants` registered in that order: the service opened its first matches
std::string runningTournament(const ServiceProcess &plat, const std::string &name, const std::string &mode, const std::vector<Entrant> &entrants, int maxPlayers = 2)
{
	const std::string tid = createTournament(plat, name, mode, maxPlayers, 60);
	for (const Entrant &e : entrants)
	{
		registerEntrant(plat, tid, e);
	}
	plat.advance(61);
	return tid;
}

// a result of a finished two-player game as a client sends it (the service's own validation applies)
std::string resultBody(const std::string &install, int slot, const std::string &gameId, const std::string &factionA, const std::string &factionB, std::int64_t now, int winner = 0)
{
	const std::string nicks[2] = { "Hosta", "Joina" };
	return "{\"v\":1,\"install\":\"" + install + "\",\"nick\":\"" + nicks[slot] + "\"," + kClientJson + ",\"game\":{\"id\":\"" + gameId + "\",\"mode\":\"faithful\",\"map\":\"maps/x/x.map\","
		"\"players\":[{\"slot\":0,\"nick\":\"Hosta\",\"faction\":\"" + factionA + "\",\"team\":0,\"human\":true},{\"slot\":1,\"nick\":\"Joina\",\"faction\":\"" + factionB
		+ "\",\"team\":1,\"human\":true}],\"start\":" + std::to_string(now - 400) + ",\"end\":" + std::to_string(now - 100) + ",\"duration_s\":300,\"winner_team\":" + std::to_string(winner)
		+ "},\"me\":{\"slot\":" + std::to_string(slot) + "}}";
}

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

const std::string kInstallA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const std::string kInstallB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

// one player: the shell with OnlineShell.apt, its lobby client and its platform client, both pumped over HTTP each tick. The platform client's clock is
// a fake one (100 ms a tick), so the service's 5 second ticket polls take 50 ticks, not 5 seconds; the service's own clock moves only by advance()
struct HubPlayer
{
	OnlineLobbyClient client;
	OnlineShellContext ctx;
	PlatformApiClient platform;
	std::vector<GameRecord> records;
	OnlineHubContext hub;
	OptionPreferences prefs;
	starttest::ShellFx fx;
	AptOnlineShell *sh = nullptr;
	int lobbyPort = 0, platformPort = 0;
	std::uint64_t clockMs = 1000;
	HubPlayer(starttest::Shared &s, int lobby, int plat, std::uint32_t seed, const std::string &install, std::uint32_t searchTimeoutMs = 600000,
		const std::string &profile = "0123456789abcdef0123456789abcdef", bool uber = false)
		: client([&] {
			  OnlineLobbyClient::Options o;
			  o.profileDigest = profile;
			  o.playerTemplateCount = s.world->playerTemplates().getPlayerTemplateCount();
			  o.colorCount = (int)s.settings.multiplayerColors.size();
			  o.pollMs = 100;
			  return o;
		  }()),
		  platform([&] {
			  PlatformApiClient::Options o;
			  o.install = install;
			  o.clientPlatform = "linux";
			  o.clientVersion = "0.4.5-test";
			  o.searchTimeoutMs = searchTimeoutMs;
			  return o;
		  }()),
		  fx(s, seed), lobbyPort(lobby), platformPort(plat)
	{
		if (uber)
		{
			prefs.set(OptionPreferences::kUberPerformance, "yes");
		}
		ctx.client = &client;
		hub.platform = &platform;
		hub.records = &records;
		fx.environment.online = &ctx;
		fx.environment.onlineHub = &hub;
		fx.environment.options = &prefs;
		fx.environment.services = &fx.services;
		fx.shell->push("OnlineShell.apt");
		REQUIRE(fx.shell->errors().empty());
		sh = dynamic_cast<AptOnlineShell *>(fx.shell->top());
		REQUIRE(sh);
		sh->hub()->setClock([this] { return clockMs; });
	}
	void pump()
	{
		for (int i = 0; i < 8; ++i)
		{
			const auto rs = client.takeRequests();
			if (rs.empty())
			{
				break;
			}
			const Answer a = http(lobbyPort, rs[0].method, rs[0].path, rs[0].body, rs[0].token);
			client.deliver(rs[0].id, a.status, a.body, a.error);
		}
		platform.update(clockMs);
		for (int i = 0; i < 8; ++i)
		{
			const auto rs = platform.takeRequests();
			if (rs.empty())
			{
				break;
			}
			const Answer a = http(platformPort, rs[0].method, "/v1" + rs[0].path, rs[0].body);
			platform.deliver(rs[0].id, a.status, a.body, a.error);
		}
	}
	void tick(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			clockMs += 100;
			fx.tick(1);
			pump();
		}
	}
	AptOnlineHub &hubPage() { return *sh->hub(); }
	bool hasNote(const std::string &kind, const std::string &part = std::string()) const
	{
		return std::any_of(fx.wm->notes().begin(), fx.wm->notes().end(),
			[&](const WindowManagerNote &n) { return n.kind == kind && (part.empty() || n.detail.find(part) != std::string::npos); });
	}
	std::string notes() const { return dumpNotes(*fx.wm, 0); }
	AptCharacterInst *at(const std::string &path) { return fx.at(sh->level(), path); }
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
	// clicks the visible button whose path contains `part`, as a player's click; false (and the list of buttons) when there is none
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
		MESSAGE("no visible button '" << part << "'; the visible buttons:\n" << list);
		return false;
	}
	// the visible button whose path contains `part` when the pointer at its centre reaches it (as the ONLINE-4 tests' reachable)
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
	bool clickWhenShown(const std::string &part, int limit = 300)
	{
		for (int i = 0; i < limit; ++i)
		{
			if (reachable(part))
			{
				tick(5); // the clip's reveal
				if (reachable(part))
				{
					break;
				}
			}
			tick(1);
		}
		return click(part);
	}
	void boxButton(const char *button)
	{
		AptMessageBox *b = sh->messageBox();
		REQUIRE(b);
		b->hide(true);
		fx.wm->fscommand("_level" + std::to_string(b->level()) + ".MessageBox_On" + std::string("Button") + button, "");
		tick(10);
	}
	int starts() const { return fx.services.countRequests(ShellAction::OnlineGameStart); }
	std::string startJson() const
	{
		for (const ShellRequest &r : fx.services.requests)
		{
			if (r.action == ShellAction::OnlineGameStart)
			{
				return r.argument;
			}
		}
		return std::string();
	}
};

bool runUntil(std::vector<HubPlayer *> ps, const std::function<bool()> &done)
{
	return onlinetest::waitUntil([&] {
		for (HubPlayer *p : ps)
		{
			p->tick(1);
		}
		return done();
	}, std::chrono::milliseconds(5));
}

void signIn(HubPlayer &p, const std::u16string &name)
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
	}), p.notes());
	// lane ONLINEUX-1: typed as a player types it: the page's Login button follows the nickname (RW 0x97A668: enabled only with a printable
	// character, told by the combo's text change), so a name put in without typing leaves Login as the empty field left it
	GadgetComboBoxSetText(p.sh->nicknameWindow(), u"");
	starttest::typeText(p.fx, GadgetComboBoxGetEditBox(p.sh->nicknameWindow()), name);
	REQUIRE(p.clickWhenShown("Login.LogIn"));
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.client.signedIn() && p.sh->currentPage() == "OnlineHome"; }), p.notes());
	p.tick(30);
}

// the shell's nav button (Toggles.ButtonNav.<name>), as the player's click
void nav(HubPlayer &p, const std::string &button, const std::string &page)
{
	AptButtonInst *b = starttest::firstButtonIn(p.at("Toggles.ButtonNav." + button));
	REQUIRE_MESSAGE(b, button);
	starttest::clickButton(p.fx, *b);
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.sh->currentPage() == page; }), p.notes());
}

void openTournamentPage(HubPlayer &p)
{
	nav(p, "QuickMatch", "OnlineQuickMatch");
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.hubPage().sideCombo() && p.hubPage().sizeCombo() && p.hubPage().colorCombo(); }), p.notes());
	p.tick(20);
}

std::vector<std::string> listColumn(GameWindow *list, int column)
{
	std::vector<std::string> out;
	for (int i = 0; list && i < GadgetListBoxGetNumEntries(list); ++i)
	{
		out.push_back(OnlineLobbyClient::utf16ToUtf8(GadgetListBoxGetText(list, i, column)));
	}
	return out;
}
} // namespace

#define HUB_REQUIRE_TOOLS()                                                                                                                        \
	if (!haveTools())                                                                                                                              \
	{                                                                                                                                              \
		MESSAGE("SKIP LOUDLY: Node.js, Python 3, the lobby server or tools/platform_fake/fake_server.py is missing here, so the TOURNAMENT / STATS page " \
				"tests did not run");                                                                                                              \
		return;                                                                                                                                    \
	}

namespace
{
std::string boxText(HubPlayer &p)
{
	return p.sh->messageBox() ? OnlineLobbyClient::utf16ToUtf8(p.sh->messageBox()->text()) : std::string("(no box)");
}

// the row of the tournament panel named `name` (the list's order is the service's)
int tournamentRow(HubPlayer &p, const std::string &name)
{
	const auto &list = p.platform.tournaments();
	for (std::size_t i = 0; i < list.size(); ++i)
	{
		if (list[i].name == name)
		{
			return (int)i;
		}
	}
	return -1;
}

// the player's double click on a tournament: the first reads its detail, then the action
void selectAndWait(HubPlayer &p, const std::string &name)
{
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return tournamentRow(p, name) >= 0; }), p.notes()); // tolerance: a row index or a count; -1 / 1 is "not shown yet", the exact rows are asserted next
	p.hubPage().chooseTournament(tournamentRow(p, name));
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return p.platform.tournamentKnown() && p.platform.tournament().info.name == name && p.hubPage().detailRows().size() > 1; }), // tolerance: a row index or a count; -1 / 1 is "not shown yet", the exact rows are asserted next
		p.notes());
}

// the HTTP statuses of every request the platform's service saw: all successes
void checkEveryStatusWasASuccess(const ServiceProcess &plat)
{
	for (const ServiceProcess::Entry &e : plat.log())
	{
		const bool success = e.status == 200 || e.status == 201;
		CHECK_MESSAGE(success, e.method << " " << e.path << " -> " << e.status);
	}
}

std::string guestToken(int lobbyPort, const std::string &name)
{
	return parseJson(http(lobbyPort, "POST", "/v1/auth/guest", "{\"name\":\"" + name + "\"}").body).get("token")->string;
}
} // namespace

TEST_CASE("onlinehub1 retail: the TOURNAMENT page against the backend's own service: the tournaments panel, and a Quick Match from JOIN GAME to both players' START "
		  "in a private room nobody else can enter")
{
	HUB_REQUIRE_TOOLS();
	OPENBFME_REQUIRE_START(s);
	auto lobby = makeLobby();
	auto plat = makePlatform();
	// tournaments made through the service's own admin and registration routes: one upcoming (five others registered), one running (the two players
	// are in it: their match is open), one cancelled
	const std::string spring = createTournament(*plat, "Spring Cup", "faithful", 16, 86400, "Single elimination.");
	for (int i = 0; i < 5; ++i)
	{
		registerEntrant(*plat, spring, { std::string(31, 'c') + std::to_string(i), "Player" + std::to_string(i) });
	}
	const std::string off = createTournament(*plat, "Called Off", "faithful", 2, 86400);
	REQUIRE(plat->admin("/v1/admin/tournaments/" + off + "/cancel", "{\"v\":1}").status == 200);
	const std::string weekly = runningTournament(*plat, "Weekly 12", "faithful", { { kInstallA, "Hosta" }, { kInstallB, "Joina" } });
	plat->post("/__test/reset", "{}");
	HubPlayer host(*s, lobby->port, plat->port, 21, kInstallA);
	HubPlayer join(*s, lobby->port, plat->port, 22, kInstallB);
	signIn(host, u"Hosta");
	signIn(join, u"Joina");
	openTournamentPage(host);
	openTournamentPage(join);
	CHECK_FALSE(host.hasNote("unported", "OnlineQuickMatch")); // the page is no longer "not available"
	CHECK(host.sh->boxesShown().empty());

	// ---- the page's gadgets: Ladder = the size, Army = random + every playable faction, Color = random + every colour, ConnectionSpeed disabled ----
	AptOnlineHub &h = host.hubPage();
	CHECK(GadgetComboBoxGetLength(h.sizeCombo()) == 2);
	int playable = 0;
	for (const SkirmishFaction &f : host.fx.setup.factions())
	{
		playable += f.playableSide && !f.isObserver ? 1 : 0;
	}
	CHECK(GadgetComboBoxGetLength(h.sideCombo()) == 1 + playable);
	CHECK(GadgetImageComboBoxGetLength(h.colorCombo()) == 1 + (int)host.fx.setup.colors().size());
	CHECK(h.choices() == "1v1|random|-1");

	// ---- the tournaments panel: the service's list (every state), a tournament's rules and bracket ----
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return h.tournamentRows().size() == 3; }), host.notes());
	std::vector<std::string> rows = h.tournamentRows();
	std::sort(rows.begin(), rows.end());
	CHECK(rows == std::vector<std::string>{ "Called Off|Cancelled|0/2", "Spring Cup|Upcoming|5/16", "Weekly 12|Running|2/2" });
	REQUIRE(h.tournamentList());
	std::vector<std::string> shown = listColumn(h.tournamentList(), 0);
	std::sort(shown.begin(), shown.end());
	CHECK(shown == std::vector<std::string>{ "Called Off (1v1, Normal)", "Spring Cup (1v1, Normal)", "Weekly 12 (1v1, Normal)" });
	selectAndWait(host, "Weekly 12");
	const auto &d = h.detailRows();
	REQUIRE(d.size() == 5);
	CHECK(d[0].rfind("Running, starts ", 0) == 0);
	CHECK(d[0].find("; 2 of 2 places taken; you are registered") != std::string::npos);
	CHECK(d[1] == "Best of one.");
	CHECK(d[2] == "YOUR NEXT MATCH: round 1 against Joina (open): double-click the tournament to join");
	CHECK(d[3] == "ROUND 1");
	CHECK(d[4] == "Hosta vs Joina (open)");
	CHECK(listColumn(h.detailList(), 0) == d);
	selectAndWait(host, "Spring Cup");
	CHECK(h.detailRows()[1] == "Single elimination.");
	CHECK(h.detailRows().size() == 2);
	selectAndWait(host, "Called Off");
	CHECK(h.detailRows()[0].rfind("Cancelled, starts ", 0) == 0);

	// ---- Quick Match: Army = the first faction for the host, random for the joiner; JOIN GAME on both pages ----
	GadgetComboBoxSetSelectedPos(h.sideCombo(), 1, false);
	GadgetImageComboBoxSetSelectedPos(h.colorCombo(), 3);
	const std::string hostChoices = h.choices();
	REQUIRE(hostChoices.rfind("1v1|", 0) == 0);
	REQUIRE(host.clickWhenShown("JoinGame"));
	REQUIRE_MESSAGE(runUntil({ &host }, [&] { return host.platform.qmState() == PlatformApiClient::QmState::Searching; }), host.notes());
	CHECK(h.status().rfind("Searching for a 1 vs 1 game (Normal mode) ...", 0) == 0);
	REQUIRE(join.clickWhenShown("JoinGame"));
	// matched: the host (the older ticket) creates the private room by the match's code, the joiner joins it, both check nothing (a Quick Match), ready, and
	// the host starts
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return host.starts() == 1 && join.starts() == 1; }),
		host.notes() << "\n" << join.notes() << "\nhost status: " << h.status() << "\njoiner status: " << join.hubPage().status());
	CHECK(h.roomHost());
	CHECK_FALSE(join.hubPage().roomHost());
	CHECK(h.roomCheckedIn()); // a Quick Match has no check-in: nothing is waited for
	CHECK(h.matchId() == join.hubPage().matchId());
	CHECK(host.client.gameId() == join.client.gameId());
	// what the service saw: exactly two tickets (both with the client metadata), no cancel, no check-in, every answer a success
	const auto creates = plat->bodies("POST /v1/qm/tickets");
	REQUIRE(creates.size() == 2);
	CHECK(creates[0].find(kClientJson) != std::string::npos);
	CHECK(creates[0].find("\"install\":\"" + kInstallA + "\",\"nick\":\"Hosta\"") != std::string::npos);
	CHECK(creates[1].find("\"nick\":\"Joina\"") != std::string::npos);
	CHECK(plat->countPrefix("DELETE ") == 0);
	CHECK(plat->countPrefix("POST /v1/tournaments/") == 0);
	checkEveryStatusWasASuccess(*plat);
	// the room is private: not in the server browser, and an outsider who knows its id gets 403 room_code on /games/<id>/join and cannot spectate it
	{
		OnlineLobbyClient other(host.client.options());
		other.signInGuest(u"Other");
		other.refreshList();
		for (int i = 0; i < 6; ++i)
		{
			const auto rs = other.takeRequests();
			if (rs.empty())
			{
				break;
			}
			const Answer a = http(lobby->port, rs[0].method, rs[0].path, rs[0].body, rs[0].token);
			other.deliver(rs[0].id, a.status, a.body, a.error);
		}
		CHECK(other.games().empty());
	}
	const std::string outsider = guestToken(lobby->port, "Outsider");
	const Answer bypass = http(lobby->port, "POST", "/v1/games/" + host.client.gameId() + "/join", "{\"profile\":\"0123456789abcdef0123456789abcdef\"}", outsider);
	CHECK(bypass.status == 403);
	CHECK(parseJson(bypass.body).get("error")->string == "room_code");
	const Answer watch = http(lobby->port, "POST", "/v1/games/" + host.client.gameId() + "/spectate", "{}", outsider);
	CHECK(watch.status == 403);
	CHECK(parseJson(watch.body).get("error")->string == "private");
	// the room's setup: an official map for two, the host's army and colour, the match's teams
	const SkirmishGameInfo &info = host.client.gameInfo();
	const SkirmishGameSetup *model = host.sh->setup();
	REQUIRE(model);
	const MapCacheEntry *map = model->findMap(info.mapName);
	REQUIRE(map);
	CHECK(map->numPlayers == 2);
	CHECK(map->isOfficial);
	CHECK(info.slots[0].teamNumber == 0);
	CHECK(info.slots[1].teamNumber == 1);
	CHECK(host.fx.setup.factions()[(std::size_t)info.slots[0].playerTemplate].templateName == hostChoices.substr(4, hostChoices.rfind('|') - 4));
	CHECK(info.slots[0].color == 2); // the colour list's fourth entry: random, then colours 0, 1, 2
	CHECK(info.slots[1].playerTemplate == -1); // the joiner's random
	CHECK(host.startJson().find("\"kind\":\"openbfme-start\"") != std::string::npos);
	CHECK(host.platform.qmState() == PlatformApiClient::QmState::Idle);
}

TEST_CASE("menufix2 onlinehub retail: leaving the online menu destroys the tournaments panel's lists (they stayed on screen over later menus)")
{
	// lane MENUFIX-2: the menu inventory found "No tournaments are planned." and two scroll bars over Create-a-Hero's class screen: the panel's engine
	// gadgets outlived the online shell (AptOnlineHub's destructor did not destroy them)
	HUB_REQUIRE_TOOLS();
	OPENBFME_REQUIRE_START(s);
	auto lobby = makeLobby();
	auto plat = makePlatform();
	HubPlayer p(*s, lobby->port, plat->port, 31, kInstallA);
	signIn(p, u"Leaver");
	openTournamentPage(p);
	AptOnlineHub &h = p.hubPage();
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return h.tournamentList() != nullptr && h.detailList() != nullptr; }), p.notes());
	GameWindow *list = h.tournamentList();
	GameWindow *details = h.detailList();
	GameWindowManager &gwm = p.fx.wm->gadgetLayer()->gadgets();
	CHECK(gwm.winIsAlive(list));
	CHECK(gwm.winIsAlive(details));
	p.fx.shell->popImmediate(); // the online shell goes (as its MAIN MENU leaves it)
	p.fx.tick(30);
	CHECK(p.fx.shell->top() == nullptr);
	CHECK_FALSE(gwm.winIsAlive(list));
	CHECK_FALSE(gwm.winIsAlive(details));
}

TEST_CASE("onlinehub1 retail e2e: a Quick Match's START from both TOURNAMENT pages, played by two openbfme_peer processes through a local relay in lockstep")
{
	HUB_REQUIRE_TOOLS();
	OPENBFME_REQUIRE_START(s);
	const std::string peer = envOr("OPENBFME_PEER", kPeerExe), relayExe = envOr("OPENBFME_RELAY", kRelayExe);
	if (peer.empty() || relayExe.empty() || !std::filesystem::exists(peer) || !std::filesystem::exists(relayExe))
	{
		MESSAGE("SKIP LOUDLY: no openbfme_peer / openbfme_relay here (OPENBFME_PEER / OPENBFME_RELAY), so the Quick Match's START was not played");
		return;
	}
	const std::string base = "\"" + peer + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL") + "\"";
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme-hub1-e2e-" + std::to_string(getpid()));
	std::filesystem::create_directories(dir);
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
	const std::string relayBind = "127.0.0.1:" + std::to_string(relayPort), keys = (dir / "keys.txt").string(), logs = (dir / "relaylog").string(),
					  status = (dir / "relay-status.json").string();
	std::filesystem::create_directories(logs);
	std::fflush(nullptr);
	// lane DEVPORT-7: TestChild.h stops the relay with this scope, kills it with the test and keeps it off the test's output
	TestChild relay(relayExe, { "--bind", relayBind, "--keys", keys, "--log-dir", logs, "--status", status, "--stats-s", "0", "--idle-s", "60" },
		(dir / "relay.log").string());
	REQUIRE(relay.started());
	auto lobby = makeLobby("local=" + relayBind);
	auto plat = makePlatform();
	HubPlayer host(*s, lobby->port, plat->port, 71, kInstallA, 600000, profile);
	HubPlayer join(*s, lobby->port, plat->port, 72, kInstallB, 600000, profile);
	signIn(host, u"Hosta");
	signIn(join, u"Joina");
	openTournamentPage(host);
	openTournamentPage(join);
	GadgetComboBoxSetSelectedPos(host.hubPage().sideCombo(), 1, false);
	GadgetComboBoxSetSelectedPos(join.hubPage().sideCombo(), 2, false);
	REQUIRE(host.clickWhenShown("JoinGame"));
	REQUIRE(runUntil({ &host }, [&] { return host.platform.qmState() == PlatformApiClient::QmState::Searching; }));
	REQUIRE(join.clickWhenShown("JoinGame"));
	REQUIRE_MESSAGE(runUntil({ &host, &join }, [&] { return host.starts() == 1 && join.starts() == 1; }), host.notes() << "\n" << join.notes());
	const std::vector<std::string> starts = { host.startJson(), join.startJson() };
	CHECK(starts[0].find("\"slot\":0") != std::string::npos);
	CHECK(starts[1].find("\"slot\":1") != std::string::npos);
	CHECK(starts[0].find("\"relay\":\"" + relayBind + "\"") != std::string::npos);
	const int kFrames = 300;
	std::vector<std::string> files, reports;
	for (std::size_t i = 0; i < starts.size(); ++i)
	{
		files.push_back((dir / ("start-" + std::to_string(i) + ".json")).string());
		reports.push_back((dir / ("report-" + std::to_string(i) + ".txt")).string());
		std::ofstream(files[i]) << starts[i];
	}
	std::vector<std::thread> peers;
	std::vector<int> rcs(starts.size(), -1);
	for (std::size_t i = 0; i < starts.size(); ++i)
	{
		const std::string cmd = base + " --online-start \"" + files[i] + "\" --script --frames " + std::to_string(kFrames) + " --report \"" + reports[i] +
			"\" > \"" + (dir / ("peer-" + std::to_string(i) + ".log")).string() + "\" 2>&1";
		// DEVPORT-6: two peers at once; macOS's system() serializes them (the host played alone: the Mac gate's final-hash failure)
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
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}

TEST_CASE("onlinehub1 retail: Quick Match ends with a reason and the page comes back: the movie's Cancel and a new search that gets its own ticket, the search time, "
		  "the platform unreachable")
{
	HUB_REQUIRE_TOOLS();
	OPENBFME_REQUIRE_START(s);
	auto lobby = makeLobby();
	auto plat = makePlatform();
	SUBCASE("the Searching panel's Cancel deletes the ticket; the next search is a new ticket the service matches")
	{
		HubPlayer p(*s, lobby->port, plat->port, 31, kInstallA);
		HubPlayer q(*s, lobby->port, plat->port, 34, kInstallB);
		signIn(p, u"Hosta");
		signIn(q, u"Joina");
		openTournamentPage(p);
		openTournamentPage(q);
		REQUIRE(p.clickWhenShown("JoinGame"));
		REQUIRE(runUntil({ &p }, [&] { return p.platform.qmState() == PlatformApiClient::QmState::Searching; }));
		const std::string first = p.platform.qmTicket();
		p.tick(2);
		CHECK(p.hubPage().tournamentList()->winIsHidden()); // the movie's Searching pop-up has the page's middle: the panel steps aside
		REQUIRE(p.clickWhenShown("navSearching.Cancel"));
		REQUIRE_MESSAGE(runUntil({ &p }, [&] { return plat->count("DELETE /v1/qm/tickets/" + first) == 1; }), p.notes());
		CHECK(p.platform.qmState() == PlatformApiClient::QmState::Idle);
		CHECK(p.hubPage().status() == "The search was cancelled.");
		CHECK(p.sh->boxesShown().empty()); // the player's own cancel needs no box
		p.tick(2);
		CHECK_FALSE(p.hubPage().tournamentList()->winIsHidden());
		// at once again: a new ticket (the service made the cancelled one terminal), and the other player's search matches THIS one, not the cancelled
		REQUIRE(p.clickWhenShown("JoinGame"));
		REQUIRE(runUntil({ &p }, [&] { return p.platform.qmState() == PlatformApiClient::QmState::Searching; }));
		CHECK(p.platform.qmTicket() != first);
		REQUIRE(q.clickWhenShown("JoinGame"));
		REQUIRE_MESSAGE(runUntil({ &p, &q }, [&] { return p.hubPage().roomPhase() != "none" && q.hubPage().roomPhase() != "none"; }), p.notes() << "\n" << q.notes());
		CHECK(p.hubPage().matchId() == q.hubPage().matchId());
		CHECK(plat->bodies("POST /v1/qm/tickets").size() == 3);
		CHECK(plat->countPrefix("DELETE /v1/qm/tickets/") == 1);
		checkEveryStatusWasASuccess(*plat);
	}
	SUBCASE("no opponent within the search time: the ticket is deleted, the box says so")
	{
		HubPlayer p(*s, lobby->port, plat->port, 32, kInstallA, 3000);
		signIn(p, u"Waiter");
		openTournamentPage(p);
		REQUIRE(p.clickWhenShown("JoinGame"));
		REQUIRE_MESSAGE(runUntil({ &p }, [&] { return !p.sh->boxesShown().empty(); }), p.notes());
		CHECK(p.sh->boxesShown().back() == "quickmatch|No opponent was found. Try again later, or play an Open Play game.");
		REQUIRE(runUntil({ &p }, [&] { return plat->countPrefix("DELETE /v1/qm/tickets/") == 1; }));
		CHECK(p.platform.qmState() == PlatformApiClient::QmState::Idle);
	}
	SUBCASE("the ticket expires at the service (300 s without a poll counted on the service's clock): the box says so")
	{
		HubPlayer p(*s, lobby->port, plat->port, 35, kInstallA);
		signIn(p, u"Sleeper");
		openTournamentPage(p);
		REQUIRE(p.clickWhenShown("JoinGame"));
		REQUIRE(runUntil({ &p }, [&] { return p.platform.qmState() == PlatformApiClient::QmState::Searching; }));
		plat->advance(301);
		REQUIRE_MESSAGE(runUntil({ &p }, [&] { return !p.sh->boxesShown().empty(); }), p.notes());
		CHECK(p.sh->boxesShown().back() == "quickmatch|The search expired. Try again.");
	}
	SUBCASE("the platform cannot be reached: Online services unavailable, nothing waits")
	{
		HubPlayer p(*s, lobby->port, freePort(), 33, kInstallA);
		signIn(p, u"Offline");
		openTournamentPage(p);
		REQUIRE(runUntil({ &p }, [&] { return listColumn(p.hubPage().tournamentList(), 0).size() == 1 &&
			listColumn(p.hubPage().tournamentList(), 0)[0].rfind("Online services unavailable", 0) == 0; }));
		REQUIRE(p.clickWhenShown("JoinGame"));
		REQUIRE_MESSAGE(runUntil({ &p }, [&] { return !p.sh->boxesShown().empty(); }), p.notes());
		CHECK(p.sh->boxesShown().back() == "quickmatch|Online services unavailable: Quick Match is not available: connection refused");
		CHECK(p.platform.qmState() == PlatformApiClient::QmState::Idle);
	}
	SUBCASE("the service refuses the ticket: its refusal is told in the player's words")
	{
		HubPlayer p(*s, lobby->port, plat->port, 36, kInstallA);
		signIn(p, u"Refused");
		openTournamentPage(p);
		plat->post("/__test/fail", "{\"prefix\":\"/v1/qm\",\"status\":429,\"count\":1}");
		REQUIRE(p.clickWhenShown("JoinGame"));
		REQUIRE_MESSAGE(runUntil({ &p }, [&] { return !p.sh->boxesShown().empty(); }), p.notes());
		// the injected failure answers {"error":{"code":"injected"}}: a code the client does not know is shown as it came
		CHECK(p.sh->boxesShown().back() == "quickmatch|Online services unavailable: Quick Match is not available: The online services refused the request (injected).");
	}
}

TEST_CASE("onlinehub1 retail: tournaments: register and withdraw through the Yes / No box against the service's own rules")
{
	HUB_REQUIRE_TOOLS();
	OPENBFME_REQUIRE_START(s);
	auto lobby = makeLobby();
	auto plat = makePlatform();
	const std::string spring = createTournament(*plat, "Spring Cup", "faithful", 16, 86400, "Single elimination.");
	for (int i = 0; i < 5; ++i)
	{
		registerEntrant(*plat, spring, { std::string(31, 'c') + std::to_string(i), "Player" + std::to_string(i) });
	}
	HubPlayer a(*s, lobby->port, plat->port, 41, kInstallA);
	signIn(a, u"Hosta");
	plat->post("/__test/reset", "{}");
	openTournamentPage(a);
	AptOnlineHub &h = a.hubPage();
	REQUIRE(runUntil({ &a }, [&] { return h.tournamentRows().size() == 1; }));
	// the first double click reads the tournament, the second asks; No changes nothing, Yes registers
	h.tournamentAction(0);
	REQUIRE(runUntil({ &a }, [&] { return h.detailRows().size() == 2; }));
	CHECK(h.detailRows()[1] == "Single elimination.");
	CHECK(h.detailRows()[0].find("you are registered") == std::string::npos);
	h.tournamentAction(0);
	CHECK(boxText(a) == "Register for Spring Cup? (If you registered before, Yes keeps your place; No offers to withdraw.)");
	a.boxButton("No");
	CHECK(plat->countPrefix("POST /v1/tournaments/" + spring + "/register") == 0);
	h.tournamentAction(0);
	a.boxButton("Yes");
	REQUIRE_MESSAGE(runUntil({ &a }, [&] { return h.tournamentRows()[0] == "Spring Cup|Upcoming|6/16"; }), a.notes());
	const auto registers = plat->bodies("POST /v1/tournaments/" + spring + "/register");
	REQUIRE(registers.size() == 1);
	CHECK(registers[0] == "{\"v\":1,\"install\":\"" + kInstallA + "\",\"nick\":\"Hosta\"," + kClientJson + "}");
	CHECK(h.status() == "You are registered.");
	CHECK(a.platform.registeredIn(spring));
	REQUIRE(runUntil({ &a }, [&] { return !h.detailRows().empty() && h.detailRows()[0].find("you are registered") != std::string::npos; }));
	// the service holds it: a second registration of the same install and nickname is the same place
	CHECK(http(plat->port, "POST", "/v1/tournaments/" + spring + "/register", registers[0]).status == 200);
	CHECK(parseJson(http(plat->port, "GET", "/v1/tournaments", "").body).get("tournaments")->array[0].get("registered")->number == 6);
	// withdraw
	h.tournamentAction(0);
	CHECK(boxText(a) == "You are registered for Spring Cup. Withdraw?");
	a.boxButton("Yes");
	REQUIRE(runUntil({ &a }, [&] { return h.tournamentRows()[0] == "Spring Cup|Upcoming|5/16"; }));
	const auto withdraws = plat->bodies("DELETE /v1/tournaments/" + spring + "/register");
	REQUIRE(withdraws.size() == 1);
	CHECK(withdraws[0] == "{\"v\":1,\"install\":\"" + kInstallA + "\"," + kClientJson + "}");
	CHECK_FALSE(a.platform.registeredIn(spring));
	// registration closes at the start: the service's refusal is told in words (the page still shows the tournament as it read it)
	plat->advance(86400 + 1);
	h.tournamentAction(0);
	CHECK(boxText(a) == "Register for Spring Cup? (If you registered before, Yes keeps your place; No offers to withdraw.)");
	a.boxButton("Yes");
	REQUIRE_MESSAGE(runUntil({ &a }, [&] { return a.sh->boxesShown().back().rfind("tournament|The registration was refused", 0) == 0; }), a.notes());
	CHECK(a.sh->boxesShown().back() == "tournament|The registration was refused: Registration for this tournament is closed.");
	CHECK(h.status() == "The registration was refused: Registration for this tournament is closed.");
	CHECK_FALSE(a.platform.registeredIn(spring));
	const auto last = plat->log().back();
	CHECK(last.method + " " + last.path == "POST /v1/tournaments/" + spring + "/register");
	CHECK(last.status == 409);
}

TEST_CASE("onlinehub1 retail: tournaments: both players check in from the room screen, ready only afterwards, and START; check-in refused, the other mode and "
		  "no match each end with a reason and no room")
{
	HUB_REQUIRE_TOOLS();
	OPENBFME_REQUIRE_START(s);
	auto lobby = makeLobby();
	auto plat = makePlatform();
	SUBCASE("join your match: the first player listed hosts, both check in, then ready, then START")
	{
		const std::string tid = runningTournament(*plat, "Weekly 12", "faithful", { { kInstallA, "Hosta" }, { kInstallB, "Joina" } });
		HubPlayer a(*s, lobby->port, plat->port, 41, kInstallA);
		HubPlayer b(*s, lobby->port, plat->port, 42, kInstallB);
		signIn(a, u"Hosta");
		signIn(b, u"Joina");
		plat->post("/__test/reset", "{}");
		openTournamentPage(a);
		openTournamentPage(b);
		for (HubPlayer *p : { &a, &b })
		{
			selectAndWait(*p, "Weekly 12");
			p->hubPage().tournamentAction(tournamentRow(*p, "Weekly 12"));
			CHECK(boxText(*p) == "Join your round 1 match of Weekly 12 now?");
			p->boxButton("Yes");
		}
		REQUIRE_MESSAGE(runUntil({ &a, &b }, [&] { return a.starts() == 1 && b.starts() == 1; }), a.notes() << "\n" << b.notes());
		CHECK(a.hubPage().roomHost());
		CHECK_FALSE(b.hubPage().roomHost());
		CHECK(a.hubPage().tournamentId() == tid);
		CHECK(a.hubPage().matchId() == b.hubPage().matchId());
		CHECK(a.hubPage().matchId().rfind(tid + ".", 0) == 0);
		CHECK(a.hubPage().roomCheckedIn());
		CHECK(b.hubPage().roomCheckedIn());
		CHECK(a.client.gameId() == b.client.gameId());
		// the service saw each install's check-in, once, with its client metadata, on the match's own path
		const std::string path = "POST /v1/tournaments/" + tid + "/matches/" + a.hubPage().matchId() + "/checkin";
		const auto checkins = plat->bodies(path);
		REQUIRE(checkins.size() == 2);
		std::vector<std::string> sorted = checkins;
		std::sort(sorted.begin(), sorted.end());
		CHECK(sorted[0] == "{\"v\":1,\"install\":\"" + kInstallA + "\"," + kClientJson + "}");
		CHECK(sorted[1] == "{\"v\":1,\"install\":\"" + kInstallB + "\"," + kClientJson + "}");
		// both check-ins came before the room's start: the log has them, and the lobby's start came after (the clients only ready once checked in)
		checkEveryStatusWasASuccess(*plat);
		// the match is "ready" at the service (both sides checked in)
		const JsonValue detail = parseJson(http(plat->port, "GET", "/v1/tournaments/" + tid + "?install=" + kInstallA, "").body);
		CHECK(detail.get("matches")->array[0].get("state")->string == "ready");
	}
	SUBCASE("the check-in is refused (the 15 minutes passed after the page read the match): the room is left and the player told, no START")
	{
		const std::string tid = runningTournament(*plat, "Weekly 12", "faithful", { { kInstallA, "Hosta" }, { kInstallB, "Joina" } });
		HubPlayer a(*s, lobby->port, plat->port, 43, kInstallA);
		signIn(a, u"Hosta");
		plat->post("/__test/reset", "{}");
		openTournamentPage(a);
		selectAndWait(a, "Weekly 12");
		plat->advance(901); // nobody checked in: the service made the match disputed; this page still shows it open
		const int row = tournamentRow(a, "Weekly 12");
		a.hubPage().tournamentAction(row);
		a.boxButton("Yes");
		REQUIRE_MESSAGE(runUntil({ &a }, [&] { return !a.sh->boxesShown().empty() && a.hubPage().roomPhase() == "failed"; }), a.notes() << a.hubPage().status());
		CHECK(a.sh->boxesShown().back() == "quickmatch|The match could not start: the check-in was not taken: Check-in for this match is closed.");
		CHECK_FALSE(a.client.inGame()); // the room was left
		CHECK(a.starts() == 0);
		const auto checkins = plat->bodies("POST /v1/tournaments/" + tid + "/matches/");
		REQUIRE(checkins.size() == 1); // the one check-in, refused
		CHECK(plat->log().back().status == 409);
	}
	SUBCASE("the tournament is in the other mode: told, no room, no check-in")
	{
		runningTournament(*plat, "Uber Cup", "uber", { { kInstallA, "Hosta" }, { kInstallB, "Joina" } });
		HubPlayer a(*s, lobby->port, plat->port, 44, kInstallA);
		signIn(a, u"Hosta");
		plat->post("/__test/reset", "{}");
		openTournamentPage(a);
		selectAndWait(a, "Uber Cup");
		a.hubPage().tournamentAction(tournamentRow(a, "Uber Cup"));
		CHECK(boxText(a) == "This tournament is played in Uber performance mode. Turn it on in Options > Performance, then join your match.");
		CHECK(a.sh->boxesShown().back() == "tournament|mode uber");
		CHECK(a.hubPage().roomPhase() == "none");
		CHECK(plat->countPrefix("POST /v1/tournaments/") == 0);
		// and the other way round
		HubPlayer u(*s, lobby->port, plat->port, 45, kInstallB, 600000, "0123456789abcdef0123456789abcdef", true);
		signIn(u, u"Joina");
		runningTournament(*plat, "Normal Cup", "faithful", { { std::string(32, 'd'), "Dora" }, { kInstallB, "Joina" } });
		plat->post("/__test/reset", "{}");
		openTournamentPage(u);
		REQUIRE(runUntil({ &u }, [&] { return tournamentRow(u, "Normal Cup") >= 0; })); // tolerance: a row index or a count; -1 / 1 is "not shown yet", the exact rows are asserted next
		selectAndWait(u, "Normal Cup");
		u.hubPage().tournamentAction(tournamentRow(u, "Normal Cup"));
		CHECK(boxText(u) == "This tournament is played in Normal mode. Turn Uber performance mode off in Options > Performance, then join your match.");
		CHECK(u.hubPage().roomPhase() == "none");
		CHECK(u.platform.qmState() == PlatformApiClient::QmState::Idle);
		CHECK(plat->countPrefix("POST /v1/tournaments/") == 0);
	}
	SUBCASE("a running tournament the player is not in has no match to join")
	{
		runningTournament(*plat, "Their Cup", "faithful", { { std::string(32, 'd'), "Dora" }, { std::string(32, 'e'), "Edda" } });
		HubPlayer a(*s, lobby->port, plat->port, 46, kInstallA);
		signIn(a, u"Hosta");
		openTournamentPage(a);
		selectAndWait(a, "Their Cup");
		CHECK(a.hubPage().detailRows()[0].find("you are registered") == std::string::npos);
		a.hubPage().tournamentAction(tournamentRow(a, "Their Cup"));
		CHECK(a.hubPage().status() == "You have no match to play in Their Cup now.");
		CHECK(a.sh->boxesShown().empty());
		CHECK(a.hubPage().roomPhase() == "none");
	}
}

TEST_CASE("onlinehub1 review 2 retail: tournaments: something changed in between: the action follows the tournament as the service has it now, and a registration the game forgot can still be withdrawn")
{
	HUB_REQUIRE_TOOLS();
	OPENBFME_REQUIRE_START(s);
	auto lobby = makeLobby();
	auto plat = makePlatform();
	SUBCASE("upcoming when the list was read, running when the detail was refreshed: the double click offers Join, not Withdraw")
	{
		const std::string tid = createTournament(*plat, "Weekly 12", "faithful", 2, 120);
		registerEntrant(*plat, tid, { kInstallA, "Hosta" });
		registerEntrant(*plat, tid, { kInstallB, "Joina" });
		HubPlayer a(*s, lobby->port, plat->port, 51, kInstallA);
		signIn(a, u"Hosta");
		plat->post("/__test/reset", "{}");
		openTournamentPage(a);
		selectAndWait(a, "Weekly 12");
		const int row = tournamentRow(a, "Weekly 12");
		CHECK(a.hubPage().tournamentRows()[(std::size_t)row] == "Weekly 12|Upcoming|2/2");
		plat->advance(121); // the tournament starts; the page's list is still the one read before
		a.platform.fetchTournament(tid);
		REQUIRE_MESSAGE(runUntil({ &a }, [&] { return a.platform.tournament().info.state == "running"; }), a.notes());
		CHECK(a.hubPage().tournamentRows()[(std::size_t)row] == "Weekly 12|Upcoming|2/2");
		a.hubPage().tournamentAction(row);
		CHECK(boxText(a) == "Join your round 1 match of Weekly 12 now?");
		CHECK(a.sh->boxesShown().back().rfind("tournament|join ", 0) == 0);
		CHECK(plat->countPrefix("DELETE /v1/tournaments/") == 0);
	}
	SUBCASE("registered before a restart (the game no longer knows): No on the register box offers Withdraw, which takes the place; No again changes nothing")
	{
		const std::string spring = createTournament(*plat, "Spring Cup", "faithful", 16, 86400, "Single elimination.");
		registerEntrant(*plat, spring, { kInstallA, "Hosta" });
		registerEntrant(*plat, spring, { kInstallB, "Joina" });
		HubPlayer a(*s, lobby->port, plat->port, 52, kInstallA);
		signIn(a, u"Hosta");
		plat->post("/__test/reset", "{}");
		openTournamentPage(a);
		selectAndWait(a, "Spring Cup");
		AptOnlineHub &h = a.hubPage();
		CHECK_FALSE(a.platform.registeredIn(spring));
		h.tournamentAction(0);
		CHECK(boxText(a) == "Register for Spring Cup? (If you registered before, Yes keeps your place; No offers to withdraw.)");
		a.boxButton("No");
		CHECK(boxText(a) == "Withdraw from Spring Cup? (Nothing changes if you are not registered.)");
		a.boxButton("No");
		CHECK(plat->countPrefix("DELETE /v1/tournaments/") == 0);
		CHECK(plat->countPrefix("POST /v1/tournaments/" + spring + "/register") == 0);
		h.tournamentAction(0);
		a.boxButton("No");
		a.boxButton("Yes");
		REQUIRE_MESSAGE(runUntil({ &a }, [&] { return h.tournamentRows()[0] == "Spring Cup|Upcoming|1/16"; }), a.notes());
		const auto withdraws = plat->bodies("DELETE /v1/tournaments/" + spring + "/register");
		REQUIRE(withdraws.size() == 1);
		CHECK(withdraws[0] == "{\"v\":1,\"install\":\"" + kInstallA + "\"," + kClientJson + "}");
		CHECK(plat->countPrefix("POST /v1/tournaments/" + spring + "/register") == 0);
		CHECK(h.status() == "You withdrew.");
	}
}

TEST_CASE("onlinehub1 retail: the STATS page: the cards and Your stats from the recorded games, the community tab and its cards from the platform (never the "
		  "player's own numbers), the filters, unavailable said plainly")
{
	HUB_REQUIRE_TOOLS();
	OPENBFME_REQUIRE_START(s);
	auto lobby = makeLobby();
	auto plat = makePlatform();
	HubPlayer p(*s, lobby->port, plat->port, 51, kInstallA);
	// the records' map: the first map of the skirmish list (a real one of this install), as the platform's statistics name it too
	REQUIRE(p.sh->setup());
	REQUIRE(!p.sh->setup()->listedMaps().empty());
	const std::string fords = p.sh->setup()->source().maps()[(std::size_t)p.sh->setup()->listedMaps()[0]].name;
	// the community: one game of two players whose reports agree (Men beat Mordor): the service counted it
	const std::int64_t now = plat->advance(0);
	auto report = [&](const std::string &install, int slot) {
		std::string body = resultBody(install, slot, "agreed-game", "FactionMen", "FactionMordor", now);
		body.replace(body.find("maps/x/x.map"), 12, fords);
		return body;
	};
	REQUIRE(http(plat->port, "POST", "/v1/results", report(kInstallA, 0)).status == 201);
	REQUIRE(http(plat->port, "POST", "/v1/results", report(kInstallB, 1)).status == 201);
	plat->advance(0); // the statistics cache is made
	std::string recordMap = fords;
	auto game = [&recordMap](const std::string &id, const std::string &kind, const std::string &faction, int winner, std::int64_t end) {
		GameRecord r;
		r.id = id;
		r.kind = kind;
		r.mode = "faithful";
		r.map = recordMap;
		r.start = end - 600;
		r.end = end;
		r.durationS = 600;
		r.players = { { 0, "Statter", faction, 0, true }, { 1, "Other", "FactionElves", 1, kind != "skirmish" } };
		r.winnerTeam = winner;
		return r;
	};
	// three recorded games: Men won online, Men lost on LAN, Mordor won against the computer
	p.records = { game("g1", "online", "FactionMen", 0, 1000), game("g2", "lan", "FactionMen", 1, 2000), game("g3", "skirmish", "FactionMordor", 0, 3000) };
	signIn(p, u"Statter");
	nav(p, "Profile", "Stats");
	AptStats *st = nullptr;
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { st = p.hubPage().stats(); return st && st->statsList(); }), p.notes());
	p.tick(20);
	auto name = [&](const std::string &templ) {
		for (const SkirmishFaction &f : p.fx.setup.factions())
		{
			if (f.templateName == templ)
			{
				return OnlineLobbyClient::utf16ToUtf8(fetchOrMissing(&p.fx.text, f.displayLabel));
			}
		}
		return templ;
	};
	// the map's name as the server browser shows it (AptOnlineShell::mapTitle: the map cache's base display name)
	const std::string fordsName = OnlineLobbyClient::utf16ToUtf8(p.sh->setup()->mapBaseDisplayName(*p.sh->setup()->findMap(fords)));
	auto level = [&](int card) { return *p.fx.wm->aptText("Stats:LevelName_" + std::to_string(card)); };
	// the cards: the playable factions in their order (RotWK's seven)
	CHECK(st->cardTemplates() == std::vector<std::string>{ "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" });

	// ---- Your stats (OPEN PLAY): the player's own games on the cards and in the list ----
	p.fx.wm->invokeCallback("AptOnline::Stats::CurrentTab", "OpenPlay");
	p.fx.wm->invokeCallback("Stats::OnSelectFaction", "0");
	p.tick(5);
	CHECK(st->tab() == 1);
	CHECK(level(0) == "2 games, 50% won");
	CHECK(*p.fx.wm->aptText("Stats:PointsNextLevel_0") == "50% won");
	CHECK(level(4) == "1 game, 100% won");
	CHECK(level(1) == "No games");
	CHECK(st->rowLines() == std::vector<std::string>{ "YOUR GAMES (this computer)||", "Games|2|3", "Wins|1|2", "Losses|1|1", "Win rate|50%|67%",
								 "Longest win streak|1|1", "Current streak||1 win", "Favourite army||" + name("FactionMen"),
								 "Favourite map|" + fordsName + "|" + fordsName, "LAST GAMES||", fordsName + "|" + name("FactionMordor") + "|Won",
								 fordsName + "|" + name("FactionMen") + "|Lost", fordsName + "|" + name("FactionMen") + "|Won" });
	CHECK(listColumn(st->statsList(), 1)[1] == "2");

	// ---- the community (TOURNAMENT): the service's numbers: the rows AND the cards ----
	p.fx.wm->invokeCallback("AptOnline::Stats::CurrentTab", "Tournament");
	REQUIRE(st->modeFilter());
	REQUIRE(st->periodFilter());
	const std::vector<std::string> community = { "ALL PLAYERS: All modes, Last 30 days||", "Games counted||1", "Picked|1 (50%)|", "Wins|1 (100%)|",
		"Most picked army||" + name("FactionMen") + " (50%)", "Highest win rate||" + name("FactionMen") + " (100%)", "Average game length||5:00",
		"Against " + name("FactionMordor") + "|1 of 1 won (100%)|", "POPULAR MAPS||", fordsName + "||1 game", "Games against the computer||0" };
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return st->rowLines() == community; }), p.notes());
	CHECK(plat->count("GET /v1/stats/global?mode=&period=30d") == 1);
	// the cards show everyone's picks and wins, not the player's own 2 games of Men (and Mordor's 1)
	CHECK(level(0) == "1 pick, 100% won");
	CHECK(level(4) == "1 pick, 0% won");
	CHECK(level(1) == "No games");
	CHECK(*p.fx.wm->aptText("Stats:PointsNextLevel_4") == "0% won");
	// a filter asks again with its mode and period; until the answer comes the cards are empty, never the player's numbers
	st->setFilter(2, 0);
	CHECK(level(0).empty());
	CHECK(level(4).empty());
	REQUIRE(runUntil({ &p }, [&] { return plat->count("GET /v1/stats/global?mode=uber&period=7d") == 1; }));
	const std::vector<std::string> empty = { "ALL PLAYERS: Uber, Last 7 days||", "Games counted||0", "Picked|0|", "Wins|0|", "Most picked army||-", "Highest win rate||-",
		"Average game length||-", "Games against the computer||0" };
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return st->rowLines() == empty; }), p.notes());
	CHECK(level(0) == "No games");
	// the service stops answering: the cards empty, the list says so
	plat->post("/__test/fail", "{\"prefix\":\"/v1/stats\",\"status\":503,\"count\":1}");
	st->setFilter(1, 2);
	const std::vector<std::string> unavailable = { "ALL PLAYERS: Normal, All time||", "Online services unavailable: The online services refused the request (injected).||" };
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return st->rowLines() == unavailable; }), p.notes());
	CHECK(level(0).empty());
	// back to the player's own: their numbers again
	p.fx.wm->invokeCallback("AptOnline::Stats::CurrentTab", "OpenPlay");
	CHECK(level(0) == "2 games, 50% won");
	// ---- WAR OF THE RING ----
	p.fx.wm->invokeCallback("AptOnline::Stats::CurrentTab", "Strategic");
	CHECK(st->rowLines() == std::vector<std::string>{ "War of the Ring statistics are not kept: War of the Ring online is not available yet.||" });
	CHECK(level(0).empty());
	CHECK_FALSE(st->modeFilter());
}

TEST_CASE("onlinehub1 retail: the community statistics without the platform say so, the service's \"other\" row is never an army; the Skirmish profile page's "
		  "statistics are the recorded games")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 61);
	std::vector<GameRecord> records;
	GameRecord r;
	r.id = "s1";
	r.kind = "skirmish";
	r.mode = "faithful";
	r.map = "maps/x/x.map";
	r.end = 100;
	r.players = { { 0, "Me", "FactionDwarves", 0, true }, { 1, "AI", "FactionWild", 1, false } };
	r.winnerTeam = 0;
	records.push_back(r);
	OnlineHubContext hub;
	hub.records = &records;
	fx.environment.onlineHub = &hub;
	fx.environment.services = &fx.services;
	fx.shell->push("Skirmish.apt");
	REQUIRE(fx.shell->errors().empty());
	fx.tick(30);
	// the profile page's statistics button (AptSkirmish::OnStatsMenu): Stats.swf's registrations are made before the movie loads it
	fx.wm->invokeCallback("AptSkirmish::OnStatsMenu", "");
	CHECK(fx.wm->hasCommand("Stats::OnSelectFaction"));
	fx.wm->invokeCallback("Stats::OnSelectFaction", "2");
	fx.tick(2);
	const std::string *dwarves = fx.wm->aptText("Stats:LevelName_2");
	REQUIRE(dwarves);
	CHECK(*dwarves == "1 game, 100% won");
	fx.wm->invokeCallback("AptSkirmish::OnExitStatsScreen", "");
	CHECK_FALSE(fx.wm->hasCommand("Stats::OnSelectFaction"));
	// the community tab with no platform client: one line, no request, no filters, empty cards
	{
		AptStats online(*fx.wm, fx.environment, AptStats::Host::Online, nullptr);
		fx.wm->invokeCallback("AptOnline::Stats::CurrentTab", "Tournament");
		CHECK(online.rowLines() == std::vector<std::string>{ "ALL PLAYERS: All modes, Last 30 days||", "Online services unavailable||" });
		CHECK_FALSE(online.modeFilter());
		CHECK(fx.wm->aptText("Stats:LevelName_2")->empty());
	}
	// a platform answer with the service's "other" rows (everything past its named entries): never the most picked or strongest army, never a matchup of the
	// focused army; the maps' rest is "Other maps"
	PlatformApiClient::Options o;
	o.install = kInstallA;
	o.nick = "Me";
	o.clientPlatform = "linux";
	o.clientVersion = "t";
	PlatformApiClient platform(o);
	hub.platform = &platform;
	std::string err;
	REQUIRE(platform.fetchGlobalStats("", "30d", &err));
	const auto request = platform.takeRequests();
	REQUIRE(request.size() == 1);
	platform.deliver(request[0].id, 200,
		"{\"v\":1,\"as_of\":1,\"games\":50,\"avg_duration_s\":600,\"factions\":[{\"faction\":\"FactionMen\",\"picks\":10,\"wins\":6,\"pick_rate\":0.1,\"win_rate\":0.6},"
		"{\"faction\":\"other\",\"picks\":90,\"wins\":80,\"pick_rate\":0.9,\"win_rate\":0.888,\"overflow\":true}],\"matchups\":[{\"a\":\"FactionMen\",\"b\":\"FactionMordor\","
		"\"games\":4,\"a_wins\":2},{\"a\":\"other\",\"b\":\"other\",\"games\":9,\"a_wins\":5,\"overflow\":true}],\"maps\":[{\"map\":\"maps/a/a.map\",\"games\":30},"
		"{\"map\":\"other\",\"games\":20,\"overflow\":true}],\"vs_ai\":{\"games\":0,\"factions\":[],\"matchups\":[],\"maps\":[],\"avg_duration_s\":0}}", "");
	AptStats community(*fx.wm, fx.environment, AptStats::Host::Online, nullptr);
	fx.wm->invokeCallback("AptOnline::Stats::CurrentTab", "Tournament");
	fx.wm->invokeCallback("Stats::OnSelectFaction", "0");
	community.update();
	const std::vector<std::string> rows = community.rowLines();
	REQUIRE(rows.size() == 12);
	CHECK(rows[1] == "Games counted||50");
	CHECK(rows[4].find("Most picked army||") == 0);
	CHECK(rows[4].find("other") == std::string::npos);
	CHECK(rows[5].find("other") == std::string::npos);
	CHECK(rows[7] == "Against " + OnlineLobbyClient::utf16ToUtf8(fetchOrMissing(&fx.text, "INI:FactionMordor")) + "|2 of 4 won (50%)|");
	CHECK(rows[8] == "POPULAR MAPS||");
	CHECK(rows[9] == "a||30 games");
	CHECK(rows[10] == "Other maps||20 games");
	// the card of Men: its own picks (10, 60%), not the "other" row's
	CHECK(*fx.wm->aptText("Stats:LevelName_0") == "10 picks, 60% won");
}

TEST_CASE("onlineux1 retail: the TOURNAMENT page's lettering leaves with the online shell (it was left on the gadget layer, which outlives every screen, "
		  "and was called in the game after the shell was destroyed: a Quick Match player's game crashed); it draws nothing while the shell is not on top; "
		  "the page names Quick Match")
{
	HUB_REQUIRE_TOOLS();
	OPENBFME_REQUIRE_START(s);
	auto lobby = makeLobby();
	auto plat = makePlatform();
	HubPlayer p(*s, lobby->port, plat->port, 23, kInstallA);
	signIn(p, u"Lettered");
	openTournamentPage(p);
	AptGadgetLayer *layer = p.fx.wm->gadgetLayer();
	REQUIRE(layer);
	REQUIRE_MESSAGE(runUntil({ &p }, [&] { return layer->hasDecorator(); }), p.notes());
	CHECK(layer->decoratorOwner() == static_cast<const void *>(p.sh)); // lane ONLINEMENU-1: the shell owns the layer's one decorator (the hub's page and the FRIENDS page)
	GadgetDrawList drawn;
	layer->buildDrawList(drawn);
	CHECK_FALSE(drawn.commands.empty());
	// the page's line names the two halves (it read RotWK's "Tournament Play ... raise your rank in the Overall Online Ladder")
	const std::string *line = p.fx.wm->aptText("APT:TournamentModeDesc");
	REQUIRE(line);
	CHECK(line->rfind("QUICK MATCH (left)", 0) == 0);
	CHECK(line->find("TOURNAMENTS (right)") != std::string::npos);
	// the shell hidden under another screen (the game's loading screen, Options): the page's lettering draws nothing, and its lists are hidden
	REQUIRE(p.hubPage().tournamentList());
	CHECK_FALSE(p.hubPage().tournamentList()->winIsHidden());
	p.sh->runShutdown(nullptr);
	p.tick(2);
	CHECK(p.hubPage().tournamentList()->winIsHidden());
	CHECK(p.hubPage().detailList()->winIsHidden());
	GadgetDrawList hidden;
	layer->buildDrawList(hidden);
	CHECK(hidden.commands.size() < drawn.commands.size()); // tolerance: the layer's other gadgets may draw; the page's frame and texts are gone
	// the online shell goes (game.gd empties the shell for a game): its lettering goes with it, and another owner's is never taken
	p.fx.shell->clear();
	p.sh = nullptr;
	CHECK_FALSE(layer->hasDecorator());
	int calls = 0;
	const int other = 0;
	layer->setDecorator([&](GadgetDrawList &) { ++calls; }, &other);
	layer->clearDecorator(&calls); // not its owner
	GadgetDrawList again;
	layer->buildDrawList(again);
	CHECK(calls == 1);
	layer->clearDecorator(&other);
	CHECK_FALSE(layer->hasDecorator());
}
#endif
