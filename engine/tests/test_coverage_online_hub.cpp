// Synthetic no-game coverage tests. GPL-3.0.
#include "doctest.h"

#include "AptPlayerTestUtil.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptOnlineHub.h"
#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameClient/GUI/WindowManager.h"

#include <algorithm>
#include <array>
#include <memory>
#include <sstream>

namespace
{
using namespace apttest;
const std::string kInstall = "0123456789abcdef0123456789abcdef";
const std::string kOther = "fedcba9876543210fedcba9876543210";
const std::string kTicket = "11111111111111111111111111111111";
const std::string kMatch = "33333333333333333333333333333333";
const std::string kCup = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const std::string kOtherCup = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const std::string kRoom = "ABCDEF0123456789";

struct Text : GameTextSource
{
	bool fetch(const std::string &key, std::u16string &out) const override
	{
		out = asciiToU16(key);
		return true;
	}
};

struct Setup : SkirmishSetupSource
{
	std::vector<SkirmishFaction> sides;
	std::vector<SkirmishColor> palette;
	std::vector<MapCacheEntry> levels;
	std::vector<int> cash{ 1000 };
	Setup()
	{
		SkirmishFaction men;
		men.templateName = "FactionMen";
		men.side = "Men";
		men.displayLabel = "INI:Men";
		men.playableSide = true;
		sides.push_back(men);
		SkirmishFaction observer;
		observer.templateName = "FactionObserver";
		observer.playableSide = true;
		observer.isObserver = true;
		sides.push_back(observer);
		palette.push_back({ "ColorBlue", "Color:Blue", 0x0000FFu, true });
		palette.push_back({ "ColorRed", "Color:Red", 0xFF0000u, true });
		MapCacheEntry map;
		map.name = "maps/known/known.map";
		map.displayName = u"Known Valley";
		map.isMultiplayer = true;
		map.isOfficial = true;
		map.numPlayers = 2;
		levels.push_back(map);
	}
	const std::vector<SkirmishFaction> &factions() const override { return sides; }
	const std::vector<SkirmishColor> &colors() const override { return palette; }
	const std::vector<MapCacheEntry> &maps() const override { return levels; }
	int defaultStartingCash() const override { return 1000; }
	const std::vector<int> &startingCashChoices() const override { return cash; }
	int mapContentsMask(const MapCacheEntry &) const override { return 1; }
	bool fileExists(const std::string &) const override { return false; }
	bool hasFileSystem() const override { return false; }
};

// The listbox skin exists only in this source. The production WindowScriptLoader parses it
// into both real engine gadgets; there is no filesystem, retail asset or substituted listbox.
std::string listSkin()
{
	return R"(FILE_VERSION = 2;
STARTLAYOUTBLOCK
  LAYOUTINIT = [None];
  LAYOUTUPDATE = [None];
  LAYOUTSHUTDOWN = [None];
ENDLAYOUTBLOCK
WINDOW
  WINDOWTYPE = SCROLLLISTBOX;
  SCREENRECT = UPPERLEFT: 0 0, BOTTOMRIGHT: 420 140, CREATIONRESOLUTION: 1024 768;
  NAME = "SyntheticList.wnd:";
  STATUS = ENABLED;
  STYLE = SCROLLLISTBOX+MOUSETRACK;
  SYSTEMCALLBACK = "Apt:None";
  INPUTCALLBACK = "Apt:None";
  TOOLTIPCALLBACK = "Apt:None";
  DRAWCALLBACK = "Apt:None";
  FONT = NAME: "Albertus MT", SIZE: 10, BOLD: 0;
  TEXTCOLOR = ENABLED: 255 255 255 255, ENABLEDBORDER: 0 0 0 255,
              DISABLED: 192 192 192 255, DISABLEDBORDER: 64 64 64 255,
              HILITE: 92 148 47 255, HILITEBORDER: 0 0 0 255;
  LISTBOXDATA = LENGTH: 128, AUTOSCROLL: 0, SCROLLIFATEND: 1,
                AUTOPURGE: 1, SCROLLBAR: 1, MULTISELECT: 0,
                COLUMNS: 3, COLUMNSWIDTH: 52, COLUMNSWIDTH: 30, COLUMNSWIDTH: 18, FORCESELECT: 1;
END
)";
}

// A genuine APT import and Construct action creates each native combo. In particular the
// _Init ref, rather than a test call to initGadget, fills the hub's size, army and colour.
struct Movies
{
	TestMovie library, shell;
	Movies()
	{
		library.addCharacter(0);
		const char *exports[] = { "HubCombo", "HubImage" };
		for (const char *name : exports)
		{
			const auto shape = library.addCharacter(library.addShape(0, 0, 220, 30, 1));
			TestMovie::Place rect;
			rect.characterId = shape;
			const auto sprite = library.addCharacter(library.addSprite({ { library.addPlaceItem(rect) } }));
			library.addExport(name, sprite);
		}
		for (int i = 0; i < 3; ++i) shell.addCharacter(0);
		shell.addImport("HubComponents", "HubCombo", 1);
		shell.addImport("HubComponents", "HubImage", 2);
		struct Gadget { const char *name; int kind; };
		const Gadget gadgets[] = { { "OnlineQuickMatch::Side", 1 }, { "OnlineQuickMatch::Ladder", 1 }, { "OnlineQuickMatch::NumOfPlayers", 1 },
			{ "OnlineQuickMatch::ConnectionSpeed", 1 }, { "OnlineQuickMatch::Color", 2 } };
		std::vector<std::uint32_t> frame;
		for (int i = 0; i < (int)std::size(gadgets); ++i)
		{
			Asm a = shell.program();
			a.pushString("_type").setStringVar(gadgets[i].kind == 1 ? "ComboBox" : "ImageComboBox");
			a.pushString("_Init").setStringVar("AptOnlineQuickMatch::InitGadgets");
			a.pushString("_WindowId").setStringVar(gadgets[i].name);
			a.op(APT_OP_END);
			TestMovie::Place p;
			p.characterId = gadgets[i].kind;
			p.depth = i + 1;
			p.flags |= APT_PLACE_HASNAME | APT_PLACE_HASCLIPACTION | APT_PLACE_HASMATRIX;
			p.name = std::string("Hub") + std::to_string(i);
			p.translation[0] = 8;
			p.translation[1] = 36.0f * i;
			p.events.push_back({ APT_CLIP_CONSTRUCT, 0, shell.commit(a) });
			frame.push_back(shell.addPlaceItem(p));
		}
		const auto script = program(shell, [](Asm &a) {
			a.defineFunction("HandleGameCall", { "name" }, [](Asm &b) {
				b.getStringVar("_root").pushString("hubCall").getStringVar("name").op(APT_OP_SETMEMBER);
			});
			a.defineFunction("LastHubCall", {}, [](Asm &b) {
				b.getStringVar("_root").getStringMember("hubCall").op(APT_OP_RETURN);
			});
		});
		const auto clip = shell.addCharacter(shell.addSprite({ { shell.addActionItem(script) } }));
		TestMovie::Place page;
		page.characterId = clip;
		page.depth = 10;
		page.flags |= APT_PLACE_HASNAME;
		page.name = "OnlineQuickMatch";
		frame.push_back(shell.addPlaceItem(page));
		shell.setRootFrames({ frame });
	}
};

PlatformApiClient::Options platformOptions()
{
	PlatformApiClient::Options o;
	o.install = kInstall;
	o.nick = "Guest";
	o.clientPlatform = "linux";
	o.clientVersion = "0.4.5-test";
	o.searchTimeoutMs = 60000;
	return o;
}
OnlineLobbyClient::Options lobbyOptions()
{
	OnlineLobbyClient::Options o;
	o.playerTemplateCount = 2;
	o.colorCount = 2;
	o.profileDigest = kInstall;
	return o;
}

std::string cupJson(const std::string &id = kCup, const std::string &name = "Spring Cup", const std::string &state = "upcoming", const std::string &mode = "faithful")
{
	return "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"mode\":\"" + mode
		+ "\",\"size\":\"1v1\",\"start\":1790000000,\"max_players\":8,\"state\":\"" + state + "\",\"registered\":3}";
}
std::string cups(const std::vector<std::string> &entries)
{
	std::string json = "{\"v\":1,\"tournaments\":[";
	for (const auto &entry : entries) json += (json.back() == '[' ? "" : ",") + entry;
	return json + "],\"next_cursor\":null}";
}
std::string matchId() { return kCup + ".000000000000000000000001"; }
std::string cupDetail(const std::string &state = "upcoming", const std::string &mode = "faithful", const std::string &players = "[null,null]", const std::string &matchState = "waiting", const std::string &reason = "")
{
	const std::string match = "{\"id\":\"" + matchId() + "\",\"round\":0,\"state\":\"" + matchState + "\",\"players\":" + players
		+ ",\"winner\":null" + (matchState == "open" ? ",\"room_code\":\"" + kRoom + "\"" : "") + "}";
	const std::string t = cupJson(kCup, "Spring Cup", state, mode);
	return "{\"v\":1," + t.substr(1, t.size() - 2) + ",\"rules\":\"Best of one.\\nNo rushing.\",\"reason\":\"" + reason
		+ "\",\"rounds\":[[\"" + matchId() + "\"]],\"matches\":[" + match + "]}";
}

struct HubFx
{
	MemorySource source;
	RecordingShellServices services;
	GadgetSkinData skins;
	Text text;
	Setup setup;
	RecordingNewGameSink sink;
	MemorySkirmishProfiles profiles;
	OnlineLobbyClient client{ lobbyOptions() };
	PlatformApiClient platform{ platformOptions() };
	OnlineShellContext online;
	OnlineHubContext onlineHub;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;
	AptOnlineShell *screen = nullptr;
	std::uint64_t ms = 1000;
	std::int64_t unixSeconds = 1789000000;

	HubFx(bool withPlatform = true, bool withSkin = true)
	{
		Movies movies;
		source.add("HubComponents", movies.library);
		source.add("OnlineShell", movies.shell);
		TestMovie empty;
		empty.setRootFrames({ {} });
		source.add("GuiFX", empty);
		if (withSkin)
		{
			const std::string wnd = listSkin();
			source.files["window/apt/listbox.wnd"] = { wnd.begin(), wnd.end() };
		}
		online.client = &client;
		onlineHub.platform = withPlatform ? &platform : nullptr;
		environment.gameText = &text;
		environment.skirmish = &setup;
		environment.newGame = &sink;
		environment.profiles = &profiles;
		environment.services = &services;
		environment.online = &online;
		environment.onlineHub = &onlineHub;
		wm = std::make_unique<WindowManager>(source, services);
		layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
		// Synthetic exports alone use these factories; all gadgets remain production
		// GameWindowManager gadgets and pass through the actual APT component lifecycle.
		for (const char *kind : { "HubCombo", "HubImage" })
		{
			wm->registerComponent(kind, [this, kind](AptComponentRequest &r) -> std::shared_ptr<GameWindow> {
				WinInstanceData data;
				GameWindow *parent = screen ? screen->ownerWindow() : nullptr;
				GameWindow *g = nullptr;
				if (std::string(kind) == "HubCombo")
				{
					data.m_style = GWS_COMBO_BOX;
					ComboBoxData cd;
					cd.isEditable = false;
					cd.maxChars = 32;
					cd.maxDisplay = 5;
					cd.listboxTemplate.listLength = 16;
					cd.listboxTemplate.columns = 1;
					g = layer->gadgets().gogoGadgetComboBox(parent, WIN_STATUS_ENABLED, (int)r.x0, (int)r.y0, 220, 30, &data, &cd, nullptr, false);
				}
				else
				{
					// ImageComboBox's production callback binds its real child list on first use.
					data.m_style = GWS_USER_WINDOW;
					g = layer->gadgets().winCreate(parent, WIN_STATUS_ENABLED, (int)r.x0, (int)r.y0, 120, 30, GadgetImageComboBoxSystem, &data);
					WinInstanceData list;
					list.m_style = GWS_SCROLL_LISTBOX;
					ListboxData ld;
					ld.listLength = 16;
					ld.columns = 1;
					GameWindow *child = layer->gadgets().gogoGadgetListBox(g, WIN_STATUS_ENABLED, 0, 30, 120, 80, &list, &ld, nullptr, false);
					REQUIRE(child);
				}
				return { g, [this](GameWindow *w) { if (w) layer->gadgets().winDestroy(w); } };
			});
		}
		registerAptScreenFactories(factories);
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->setShell(shell.get());
		wm->init();
		shell->push("OnlineShell.apt");
		screen = dynamic_cast<AptOnlineShell *>(shell->top());
		REQUIRE(screen);
		// Disable unrelated shell polls. Drive the hub/platform clock explicitly and
		// answer the room client's request queue without scheduling its chat polls.
		wm->removeUpdateListeners(screen);
		hub().setClock([this]() { return ms; });
		hub().setUnixClock([this]() { return unixSeconds; });
		platform.update(ms);
		hub().loadPage("OnlineQuickMatch");
		wm->update(16);
		wm->update(16);
		REQUIRE(hub().quickMatchUp());
		REQUIRE(hub().sideCombo());
		REQUIRE(hub().colorCombo());
		REQUIRE(hub().sizeCombo());
	}
	AptOnlineHub &hub() { return *screen->hub(); }
	void update() { hub().update(); }
	void advance(std::uint64_t elapsed) { ms += elapsed; platform.update(ms); update(); }
	void command(const std::string &name) { REQUIRE(wm->invokeCallback("AptOnline::OnlineQuickMatch::" + name, "")); }
	PlatformApiClient::HttpRequest platformRequest(const std::string &method, const std::string &path)
	{
		// A newly opened page asks for the list first. The responder makes every
		// incidental list deterministic, never discards an unanswered request.
		for (int i = 0; i < 12; ++i)
		{
			auto sent = platform.takeRequests();
			if (sent.empty()) break;
			REQUIRE(sent.size() == 1);
			if (sent.front().method == method && sent.front().path == path) return sent.front();
			if (sent.front().path == "/tournaments") platform.deliver(sent.front().id, 200, cups({}), "");
			else { FAIL_CHECK("unexpected platform request: " << sent.front().method << " " << sent.front().path); break; }
		}
		return {};
	}
	PlatformApiClient::HttpRequest answerPlatform(const std::string &method, const std::string &path, const std::string &json, int status = 200, const std::string &error = "")
	{
		const auto r = platformRequest(method, path);
		REQUIRE_MESSAGE(r.id, method, " ", path);
		platform.deliver(r.id, status, json, error);
		update();
		return r;
	}
	OnlineLobbyClient::HttpRequest lobbyRequest(const std::string &method, const std::string &path)
	{
		// Pump explicit room mutations only. The network client's independent periodic
		// room/chat polls are outside the hub and are deliberately not scheduled here.
		for (int i = 0; i < 12; ++i)
		{
			auto sent = client.takeRequests();
			if (sent.empty()) break;
			REQUIRE(sent.size() == 1);
			if (sent.front().method == method && sent.front().path == path) return sent.front();
			FAIL_CHECK("unexpected lobby request: " << sent.front().method << " " << sent.front().path);
			break;
		}
		return {};
	}
	void signIn()
	{
		client.signInGuest(u"Guest");
		const auto r = lobbyRequest("POST", "/v1/auth/guest");
		REQUIRE(r.id);
		client.deliver(r.id, 200, R"({"v":1,"token":"synthetic.token","account":{"id":"guest:1","name":"Guest","kind":"guest"}})", "");
		REQUIRE(client.signedIn());
		client.takeEvents(); // no shell polling; the deterministic fixture handles the sign-in
	}
	void search()
	{
		command("PlayGame");
		REQUIRE(platform.qmState() == PlatformApiClient::QmState::Starting);
		answerPlatform("POST", "/qm/tickets", "{\"v\":1,\"ticket\":\"" + kTicket + "\",\"poll_after_s\":1}", 201);
		REQUIRE(platform.qmState() == PlatformApiClient::QmState::Searching);
	}
	void list(const std::vector<std::string> &entries)
	{
		// The first page request is deliberately answered with the caller's data.
		answerPlatform("GET", "/tournaments", cups(entries));
	}
	void selectCup(const std::string &detail = cupDetail())
	{
		hub().chooseTournament(0);
		CHECK(hub().detailRows() == std::vector<std::string>{ "Asking the online services ..." });
		answerPlatform("GET", "/tournaments/" + kCup + "?install=" + kInstall, detail);
	}
	bool hasBox(const std::string &piece) const
	{
		return std::any_of(screen->boxesShown().begin(), screen->boxesShown().end(), [&](const std::string &s) { return s.find(piece) != std::string::npos; });
	}
};

// The same complete eight-slot game view the lobby accepts, synthesized in memory.
// Supplying both a room reply and follow-up slot/ready replies exercises the actual
// OnlineLobbyClient parser and request queue (not a fabricated client game state).
std::string roomView(bool host, bool bothReady = false, int seq = 1)
{
	std::ostringstream out;
	out << R"({"id":"0000000000000001","name":"Quick Match","private":true,"hostSlot":0,"seq":)" << seq
		<< R"(,"setup":{"map":"maps/known/known.map","startingCash":1000,"superweaponRestriction":0,"legendaryHeroes":1},"slots":[)";
	for (int i = 0; i < 8; ++i)
	{
		if (i) out << ',';
		const bool mine = i == (host ? 0 : 1);
		out << "{\"slot\":" << i << ",\"state\":\"" << (i < 2 ? "player" : "closed") << "\",\"name\":\""
			<< (mine ? "Guest" : i < 2 ? "Opponent" : "")
			<< "\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":" << (i < 2 ? i : -1)
			<< ",\"ready\":" << (i < 2 && bothReady ? "true" : "false") << ",\"you\":" << (mine ? "true" : "false") << '}';
	}
	out << "]}";
	return out.str();
}

std::string matchAnswer(bool host)
{
	return "{\"v\":1,\"state\":\"matched\",\"match\":{\"id\":\"" + kMatch + "\",\"room_code\":\"" + kRoom
		+ "\",\"host_install\":\"" + (host ? kInstall : kOther) + "\",\"mode\":\"faithful\",\"players\":[{\"nick\":\""
		+ (host ? "Guest" : "Opponent") + "\",\"team\":0},{\"nick\":\"" + (host ? "Opponent" : "Guest") + "\",\"team\":1}]}}";
}
void openRoom(HubFx &f, bool host)
{
	f.signIn();
	f.search();
	f.advance(1000);
	f.answerPlatform("GET", "/qm/tickets/" + kTicket, matchAnswer(host));
	const auto r = f.lobbyRequest("POST", host ? "/v1/games" : "/v1/rooms/" + kRoom + "/join");
	REQUIRE(r.id);
	f.client.deliver(r.id, 200, "{\"v\":1,\"game\":" + roomView(host) + "}", "");
	for (const auto &e : f.client.takeEvents())
	{
		if (e.kind == OnlineLobbyClient::Event::GameCreated || e.kind == OnlineLobbyClient::Event::GameJoined)
			CHECK(f.hub().lobbyEvent(e));
	}
	REQUIRE(f.hub().roomPhase() == "in_room");
}

OnlineLobbyClient::Event lobbyEvent(OnlineLobbyClient::Event::Kind kind, const std::string &request = "", const std::string &code = "", const std::u16string &text = {})
{
	OnlineLobbyClient::Event e{ kind };
	e.request = request;
	e.code = code;
	e.text = text;
	return e;
}
} // namespace

TEST_CASE("coverage1: synthetic APT constructs Quick Match gadgets and registers page commands")
{
	HubFx f;
	CHECK(AptOnlineHub::isHubPage("Stats"));
	CHECK(AptOnlineHub::isHubPage("OnlineQuickMatch"));
	CHECK_FALSE(AptOnlineHub::isHubPage("Other"));
	CHECK(f.hub().decorating());
	CHECK(f.hub().tournamentList());
	CHECK(f.hub().detailList());
	CHECK(f.hub().choices() == "1v1|random|-1");
	CHECK(f.hub().sideCombo() != nullptr);
	GadgetComboBoxSetSelectedPos(f.hub().sizeCombo(), 1, false);
	GadgetComboBoxSetSelectedPos(f.hub().sideCombo(), 1, false);
	GadgetImageComboBoxSetSelectedPos(f.hub().colorCombo(), 2);
	CHECK(f.hub().choices() == "2v2|FactionMen|1");
	f.command("StartSimple");
	f.command("WidenSearch");
	CHECK(f.hub().status().find("every player") != std::string::npos);
	f.hub().loadPage("OnlineQuickMatch");
	CHECK(f.hub().tournamentList());
	f.hub().unloadPage("OnlineQuickMatch");
	CHECK_FALSE(f.hub().quickMatchUp());
	CHECK(f.hub().tournamentList() == nullptr);
	CHECK(f.hub().sideCombo() == nullptr);
	CHECK_FALSE(f.wm->invokeCallback("AptOnline::OnlineQuickMatch::PlayGame", ""));
	f.hub().loadPage("OnlineQuickMatch");
	CHECK(f.hub().quickMatchUp());
	CHECK(f.hub().tournamentList());
}

TEST_CASE("coverage1: missing platform and missing synthetic panel skin report unavailable instead of starting")
{
	HubFx unavailable(false);
	CHECK(unavailable.hub().tournamentList());
	unavailable.command("PlayGame");
	CHECK(unavailable.hub().status().find("no platform service") != std::string::npos);
	CHECK(unavailable.hasBox("quickmatch|"));
	CHECK(unavailable.hub().roomPhase() == "none");
	HubFx noSkin(true, false);
	CHECK(noSkin.hub().tournamentList() == nullptr);
	CHECK(noSkin.hub().detailList() == nullptr);
	CHECK(noSkin.hub().quickMatchUp());
}

TEST_CASE("coverage1: search timeout reports no opponent and returns the platform to idle")
{
	HubFx f;
	f.search();
	f.advance(65000);
	CHECK(f.platform.qmState() == PlatformApiClient::QmState::Idle);
	CHECK(f.hub().status().find("No opponent") != std::string::npos);
	CHECK(f.hasBox("quickmatch|"));
	f.command("Cancel");
	CHECK(f.hub().status() == "The search was cancelled.");
	CHECK(f.hub().roomPhase() == "none");
}

TEST_CASE("coverage1: active search shows elapsed time and Cancel clears the queued search")
{
	HubFx f;
	f.search();
	CHECK(f.hub().tournamentList()->winIsHidden());
	f.advance(3100);
	CHECK(f.hub().status().find("0:03") != std::string::npos);
	f.command("Cancel");
	CHECK(f.platform.qmState() == PlatformApiClient::QmState::Idle);
	CHECK(f.hub().status() == "The search was cancelled.");
	f.update();
	CHECK_FALSE(f.hub().tournamentList()->winIsHidden());
}

TEST_CASE("coverage1: cancelled searches ignore late ticket creation and matched poll replies")
{
	for (bool cancelCreation : { true, false })
	{
		HubFx f;
		f.command("PlayGame");
		const auto create = f.platformRequest("POST", "/qm/tickets");
		REQUIRE(create.id);
		if (cancelCreation)
		{
			f.command("Cancel");
			f.platform.deliver(create.id, 201, "{\"v\":1,\"ticket\":\"" + kTicket + "\",\"poll_after_s\":1}", "");
		}
		else
		{
			f.platform.deliver(create.id, 201, "{\"v\":1,\"ticket\":\"" + kTicket + "\",\"poll_after_s\":1}", "");
			f.update();
			f.advance(1000);
			const auto poll = f.platformRequest("GET", "/qm/tickets/" + kTicket);
			REQUIRE(poll.id);
			f.command("Cancel");
			f.platform.deliver(poll.id, 200, matchAnswer(true), "");
		}
		f.update();
		CHECK(f.platform.qmState() == PlatformApiClient::QmState::Idle);
		CHECK(f.hub().roomPhase() == "none");
		CHECK(f.hub().matchId().empty());
		CHECK(f.hub().status() == "The search was cancelled.");
	}
}

TEST_CASE("coverage1: expired and malformed search answers surface errors to the player")
{
	for (const std::string &body : { std::string(R"({"v":1,"state":"expired"})"), std::string(R"({"v":1,"state":"matched","match":{}})") })
	{
		HubFx f;
		f.search();
		f.advance(1000);
		f.answerPlatform("GET", "/qm/tickets/" + kTicket, body);
		CHECK(f.hasBox("quickmatch|"));
		CHECK(f.hub().roomPhase() == "none");
		CHECK(f.platform.qmState() == PlatformApiClient::QmState::Idle);
	}
}

TEST_CASE("coverage1: found host chooses an official map and opens its private lobby room")
{
	HubFx f;
	f.signIn();
	f.search();
	f.advance(1000);
	f.answerPlatform("GET", "/qm/tickets/" + kTicket, matchAnswer(true));
	CHECK(f.hub().matchId() == kMatch);
	CHECK(f.hub().roomHost());
	CHECK(f.hub().roomPhase() == "creating");
	CHECK(f.hasBox("quickmatch|") == false);
	const auto r = f.lobbyRequest("POST", "/v1/games");
	REQUIRE(r.id);
	CHECK(r.body.find(kRoom) != std::string::npos);
	CHECK(r.body.find("maps/known/known.map") != std::string::npos);
	f.command("OnFoundMovieDone");
	CHECK(f.hub().roomPhase() == "creating");
	CHECK(f.hub().tournamentList()->winIsHidden());
	CHECK(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::Chat)));
	CHECK_FALSE(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::Refused, "list", "transport")));
	CHECK(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::Refused, "create", "error", u"room was refused")));
	CHECK(f.hub().roomPhase() == "failed");
	CHECK(f.hub().status().find("room was refused") != std::string::npos);
}

TEST_CASE("coverage1: found joiner retries an absent host only after the retry deadline")
{
	HubFx f;
	f.signIn();
	f.search();
	f.advance(1000);
	f.answerPlatform("GET", "/qm/tickets/" + kTicket, matchAnswer(false));
	CHECK_FALSE(f.hub().roomHost());
	CHECK(f.hub().roomPhase() == "joining");
	const std::string path = "/v1/rooms/" + kRoom + "/join";
	const auto r = f.lobbyRequest("POST", path);
	REQUIRE(r.id);
	f.client.deliver(r.id, 404, R"({"v":1,"error":"no_room","message":"The host has not opened the room yet."})", "");
	bool retried = false;
	for (const auto &e : f.client.takeEvents()) if (e.kind == OnlineLobbyClient::Event::Refused) retried = f.hub().lobbyEvent(e);
	CHECK(retried);
	CHECK(f.hub().roomPhase() == "joining");
	f.advance(AptOnlineHub::kJoinRetryMs - 1);
	CHECK(f.client.takeRequests().empty());
	f.advance(1);
	CHECK(f.lobbyRequest("POST", path).id != 0); // tolerance: request IDs are sequence numbers; only zero means no request
	CHECK(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::Refused, "poll", "transport")));
	CHECK(f.hub().roomPhase() == "joining");
	CHECK(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::Refused, "join", "denied", u"bad room")));
	CHECK(f.hub().roomPhase() == "failed");
}

TEST_CASE("coverage1: matched rooms fail on missing sign-in or official map and time out at ninety seconds")
{
	SUBCASE("no signed-in lobby")
	{
		HubFx f;
		f.search();
		f.advance(1000);
		f.answerPlatform("GET", "/qm/tickets/" + kTicket, matchAnswer(true));
		CHECK(f.hub().roomPhase() == "failed");
		CHECK(f.hub().status().find("not signed in") != std::string::npos);
	}
	SUBCASE("no official two-player map")
	{
		HubFx f;
		f.setup.levels[0].isOfficial = false;
		f.signIn();
		f.search();
		f.advance(1000);
		f.answerPlatform("GET", "/qm/tickets/" + kTicket, matchAnswer(true));
		CHECK(f.hub().roomPhase() == "failed");
		CHECK(f.hub().status().find("no official map") != std::string::npos);
	}
	SUBCASE("join at exact deadline")
	{
		HubFx f;
		f.signIn();
		f.search();
		f.advance(1000);
		f.answerPlatform("GET", "/qm/tickets/" + kTicket, matchAnswer(false));
		f.advance(AptOnlineHub::kRoomTimeoutMs - 1);
		CHECK(f.hub().roomPhase() == "joining");
		f.advance(1);
		CHECK(f.hub().roomPhase() == "failed");
		CHECK(f.hub().status().find("host did not open") != std::string::npos);
	}
}

TEST_CASE("coverage1: host readies after creating the room and starts when both players become ready")
{
	HubFx f;
	openRoom(f, true);
	CHECK(f.hub().roomCheckedIn()); // Quick Match needs no platform check-in
	CHECK(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::GameUpdated)));
	const auto ready = f.lobbyRequest("POST", "/v1/games/0000000000000001/ready");
	REQUIRE(ready.id);
	CHECK(ready.body.find("true") != std::string::npos);
	f.client.deliver(ready.id, 200, "{\"v\":1,\"game\":" + roomView(true, true, 2) + "}", "");
	bool updated = false;
	for (const auto &e : f.client.takeEvents())
		if (e.kind == OnlineLobbyClient::Event::GameUpdated) updated = f.hub().lobbyEvent(e);
	CHECK(updated);
	CHECK(f.hub().status().find("Every player is ready") != std::string::npos);
	CHECK(f.lobbyRequest("POST", "/v1/games/0000000000000001/start").id != 0); // tolerance: request IDs are sequence numbers; only zero means no request
	CHECK_FALSE(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::GameStart)));
	CHECK(f.hub().roomPhase() == "starting");
}

TEST_CASE("coverage1: joiner sends its own slot and readiness then handles a host departure")
{
	HubFx f;
	openRoom(f, false);
	CHECK(f.hub().roomCheckedIn());
	const auto slot = f.lobbyRequest("POST", "/v1/games/0000000000000001/slot");
	REQUIRE(slot.id);
	CHECK(slot.body.find("\"team\":1") != std::string::npos);
	f.client.deliver(slot.id, 200, "{\"v\":1,\"game\":" + roomView(false, false, 2) + "}", "");
	for (const auto &e : f.client.takeEvents())
		if (e.kind == OnlineLobbyClient::Event::GameUpdated) CHECK(f.hub().lobbyEvent(e));
	CHECK(f.lobbyRequest("POST", "/v1/games/0000000000000001/ready").id != 0); // tolerance: request IDs are sequence numbers; only zero means no request
	CHECK(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::Removed, "", "closed")));
	CHECK(f.hub().roomPhase() == "failed");
	CHECK(f.hub().status().find("host left") != std::string::npos);
	CHECK_FALSE(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::Chat)));
}

TEST_CASE("coverage1: tournaments fill real list and details, preserve selected id through a reorder")
{
	HubFx f;
	const auto rows = std::vector<std::string>{ cupJson(), cupJson(kOtherCup, "Winter Cup", "running") };
	f.list(rows);
	REQUIRE(f.hub().tournamentRows().size() == 2);
	CHECK(f.hub().tournamentRows()[0] == "Spring Cup|Upcoming|3/8");
	CHECK(f.hub().tournamentRows()[1] == "Winter Cup|Running|3/8");
	CHECK_FALSE(f.hub().gadgetMessage(nullptr, GLM_SELECTED, 0, 0));
	CHECK(f.hub().gadgetMessage(f.hub().detailList(), GLM_SELECTED, 0, 0));
	f.hub().chooseTournament(-1);
	f.hub().chooseTournament(42);
	CHECK(f.hub().selectedTournament() == -1);
	CHECK(f.hub().gadgetMessage(f.hub().tournamentList(), GLM_SELECTED, 0, 0));
	CHECK(f.hub().selectedTournament() == 0);
	f.answerPlatform("GET", "/tournaments/" + kCup + "?install=" + kInstall, cupDetail());
	REQUIRE(f.hub().detailRows().size() == 5);
	CHECK(f.hub().detailRows()[1] == "Best of one.");
	CHECK(f.hub().detailRows()[2] == "No rushing.");
	f.platform.listTournaments();
	f.list({ rows[1], rows[0] });
	CHECK(f.hub().selectedTournament() == 1);
}

TEST_CASE("coverage1: tournament refresh clears details when the selected tournament disappears")
{
	HubFx f;
	f.list({ cupJson(), cupJson(kOtherCup, "Winter Cup") });
	f.selectCup();
	REQUIRE(f.hub().selectedTournament() == 0);
	REQUIRE(f.hub().detailRows().size() == 5);
	f.platform.listTournaments();
	f.list({ cupJson(kOtherCup, "Winter Cup") });
	CHECK(f.hub().selectedTournament() == -1);
	CHECK(f.hub().detailRows().empty());
	CHECK(f.hub().detailList());
}

TEST_CASE("coverage1: empty, refused and malformed tournament lists keep a visible status row")
{
	HubFx f;
	f.list({});
	CHECK(f.hub().tournamentRows().empty());
	CHECK(f.hub().tournamentList());
	f.platform.listTournaments();
	f.answerPlatform("GET", "/tournaments", "{\"v\":1,\"tournaments\":{}}", 200);
	CHECK(f.hub().tournamentRows().empty());
	f.platform.listTournaments();
	f.answerPlatform("GET", "/tournaments", "", 0, "synthetic transport failure");
	CHECK(f.hub().tournamentList());
}

TEST_CASE("coverage1: tournament actions ask again for unknown detail and distinguish cancelled and finished")
{
	HubFx f;
	f.list({ cupJson(), cupJson(kOtherCup, "Winter Cup") });
	f.hub().tournamentAction(12);
	f.hub().tournamentAction(0);
	CHECK(f.hub().status().find("double-click it again") != std::string::npos);
	f.answerPlatform("GET", "/tournaments/" + kCup + "?install=" + kInstall, cupDetail("cancelled", "faithful", "[null,null]", "waiting", "maintenance"));
	CHECK(f.hub().detailRows()[0].find("maintenance") != std::string::npos);
	f.hub().tournamentAction(0);
	CHECK(f.hub().status().find("cancelled") != std::string::npos);
	f.hub().chooseTournament(0);
	f.answerPlatform("GET", "/tournaments/" + kCup + "?install=" + kInstall, cupDetail("finished"));
	f.hub().tournamentAction(0);
	CHECK(f.hub().status().find("finished") != std::string::npos);
}

TEST_CASE("coverage1: upcoming tournament confirmation registers and handles platform refusal")
{
	HubFx f;
	f.list({ cupJson() });
	f.selectCup();
	f.hub().tournamentAction(0);
	REQUIRE(f.screen->messageBox());
	CHECK(f.screen->messageBox()->type() == AptMessageBox::TYPE_YES_NO);
	CHECK(f.wm->invokeCallback("_level" + std::to_string(f.screen->messageBox()->level()) + ".MessageBox_OnButtonYes", ""));
	const auto r = f.platformRequest("POST", "/tournaments/" + kCup + "/register");
	REQUIRE(r.id);
	f.platform.deliver(r.id, 403, R"({"v":1,"error":{"code":"registration_closed"}})", "");
	f.update();
	CHECK(f.hub().status().find("registration was refused") != std::string::npos);
	CHECK(f.hasBox("tournament|"));
}

TEST_CASE("coverage1: panel draws its frame at rest and only search status under the popup")
{
	HubFx f;
	GadgetDrawList display;
	f.hub().drawPanel(display);
	CHECK(display.count(GadgetDrawCommand::Kind::Text) == 2);
	f.search();
	display.clear();
	f.hub().drawPanel(display);
	CHECK(display.count(GadgetDrawCommand::Kind::Text) == 1);
	CHECK(f.hub().tournamentList()->winIsHidden());
	f.hub().unloadPage("OnlineQuickMatch");
	CHECK_FALSE(f.hub().decorating());
	CHECK(f.hub().tournamentList() == nullptr);
	CHECK(f.platform.qmState() == PlatformApiClient::QmState::Idle);
}

TEST_CASE("coverage1: registered tournament prompts withdrawal and reports the service refusal")
{
	HubFx f;
	f.list({ cupJson() });
	f.selectCup();
	f.hub().tournamentAction(0);
	REQUIRE(f.screen->messageBox());
	const std::string yes = "_level" + std::to_string(f.screen->messageBox()->level()) + ".MessageBox_OnButtonYes";
	REQUIRE(f.wm->invokeCallback(yes, ""));
	const auto registerRequest = f.platformRequest("POST", "/tournaments/" + kCup + "/register");
	REQUIRE(registerRequest.id);
	f.platform.deliver(registerRequest.id, 200, R"({"v":1,"registered":true})", "");
	f.update();
	CHECK(f.hub().status() == "You are registered.");
	REQUIRE(f.platform.registeredIn(kCup));
	// The registration response asks for new details and a new listing.
	f.answerPlatform("GET", "/tournaments/" + kCup + "?install=" + kInstall, cupDetail());
	f.list({ cupJson() });
	// Reselecting the retained native list row refreshes its details once more.
	f.answerPlatform("GET", "/tournaments/" + kCup + "?install=" + kInstall, cupDetail());
	REQUIRE(f.platform.tournamentKnown());
	REQUIRE(f.platform.tournament().info.id == kCup);
	REQUIRE(f.hub().selectedTournament() == 0);
	f.hub().tournamentAction(0);
	CHECK(f.hasBox("tournament|withdraw " + kCup));
	CHECK(f.screen->messageBox()->type() == AptMessageBox::TYPE_YES_NO);
	CHECK(f.wm->invokeCallback("_level" + std::to_string(f.screen->messageBox()->level()) + ".MessageBox_OnButtonYes", ""));
	const auto withdraw = f.platformRequest("DELETE", "/tournaments/" + kCup + "/register");
	REQUIRE(withdraw.id);
	f.platform.deliver(withdraw.id, 403, R"({"v":1,"error":{"code":"not_registered"}})", "");
	f.update();
	CHECK(f.hub().status().find("withdrawal was refused") != std::string::npos);
	CHECK(f.hasBox("tournament|"));
}

TEST_CASE("coverage1: tournament match checks in after room creation and retries transport once")
{
	HubFx f;
	f.signIn();
	f.list({ cupJson(kCup, "Spring Cup", "running") });
	f.selectCup(cupDetail("running", "faithful", R"(["Guest","Opponent"])", "open"));
	CHECK(std::any_of(f.hub().detailRows().begin(), f.hub().detailRows().end(), [](const std::string &row) {
		return row.find("YOUR NEXT MATCH") != std::string::npos;
	}));
	f.hub().tournamentAction(0);
	REQUIRE(f.screen->messageBox());
	CHECK(f.wm->invokeCallback("_level" + std::to_string(f.screen->messageBox()->level()) + ".MessageBox_OnButtonYes", ""));
	CHECK(f.hub().tournamentId() == kCup);
	CHECK(f.hub().matchId() == matchId());
	CHECK(f.hub().roomPhase() == "creating");
	const auto create = f.lobbyRequest("POST", "/v1/games");
	REQUIRE(create.id);
	f.client.deliver(create.id, 200, "{\"v\":1,\"game\":" + roomView(true) + "}", "");
	bool entered = false;
	for (const auto &e : f.client.takeEvents())
		if (e.kind == OnlineLobbyClient::Event::GameCreated) entered = f.hub().lobbyEvent(e);
	CHECK(entered);
	CHECK(f.hub().roomPhase() == "in_room");
	CHECK_FALSE(f.hub().roomCheckedIn());
	CHECK(f.hub().lobbyEvent(lobbyEvent(OnlineLobbyClient::Event::GameUpdated)));
	const std::string checkPath = "/tournaments/" + kCup + "/matches/" + matchId() + "/checkin";
	const auto checkin = f.platformRequest("POST", checkPath);
	REQUIRE(checkin.id);
	f.platform.deliver(checkin.id, 0, "", "synthetic transport failure");
	f.update();
	CHECK(f.hub().roomPhase() == "in_room");
	f.advance(2999);
	CHECK(f.platform.takeRequests().empty());
	f.advance(1);
	f.answerPlatform("POST", checkPath, R"({"v":1,"state":"open"})");
	CHECK(f.hub().roomCheckedIn());
	CHECK(f.hub().status().find("Checked in") != std::string::npos);
	CHECK(f.lobbyRequest("POST", "/v1/games/0000000000000001/ready").id != 0); // tolerance: request IDs are sequence numbers; only zero means no request
}

TEST_CASE("coverage1: incompatible tournament mode refuses to join before opening a room")
{
	HubFx f;
	f.list({ cupJson(kCup, "Spring Cup", "running", "uber") });
	f.selectCup(cupDetail("running", "uber", R"(["Guest","Opponent"])", "open"));
	f.hub().tournamentAction(0);
	CHECK(f.hasBox("tournament|mode uber"));
	CHECK(f.hub().roomPhase() == "none");
	CHECK(f.client.takeRequests().empty());
}

TEST_CASE("coverage1: Stats page lifecycle and game-record refresh remain synthetic")
{
	HubFx f;
	CHECK(f.hub().stats() == nullptr);
	f.hub().loadPage("Stats");
	REQUIRE(f.hub().stats());
	AptStats *first = f.hub().stats();
	f.hub().loadPage("Stats");
	CHECK(f.hub().stats() == first);
	f.hub().recordsChanged();
	f.hub().update();
	f.hub().unloadPage("Stats");
	CHECK(f.hub().stats() == nullptr);
	f.hub().recordsChanged();
}
