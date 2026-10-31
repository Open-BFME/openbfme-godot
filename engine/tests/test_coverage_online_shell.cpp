// Synthetic no-game coverage tests. GPL-3.0.
#include "doctest.h"

#include "AptPlayerTestUtil.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameNetwork/OnlineLobbyClient.h"

#include <algorithm>
#include <memory>
#include <sstream>

namespace
{
using namespace apttest;

struct SyntheticText : GameTextSource
{
	bool fetch(const std::string &key, std::u16string &out) const override
	{
		out = asciiToU16(key);
		return true;
	}
};

struct SyntheticSetup : SkirmishSetupSource
{
	std::vector<SkirmishFaction> sides;
	std::vector<SkirmishColor> palette;
	std::vector<MapCacheEntry> levels;
	std::vector<int> cash{ 1000 };
	SyntheticSetup()
	{
		MapCacheEntry m;
		m.name = "maps/known/known.map";
		m.displayName = u"Known Valley";
		m.isMultiplayer = true;
		m.numPlayers = 2;
		levels.push_back(m);
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

// Exported component symbols and Construct actions are entirely in-memory. The real WindowManager
// discovers each placeholder, calls its public component factory and invokes the page's real InitGadgets
// screen ref. Native gadgets are created by the production GameWindowManager, not substituted objects.
struct OnlineMovies
{
	TestMovie library, shell;
	OnlineMovies()
	{
		library.addCharacter(0);
		auto symbol = [&](const char *name) {
			const auto shape = library.addCharacter(library.addShape(0, 0, 220, 30, 1));
			TestMovie::Place rect;
			rect.characterId = shape;
			const auto sprite = library.addCharacter(library.addSprite({ { library.addPlaceItem(rect) } }));
			library.addExport(name, sprite);
		};
		symbol("CoverageList");
		symbol("CoverageCombo");
		symbol("CoverageEntry");
		for (int i = 0; i < 4; ++i) shell.addCharacter(0);
		shell.addImport("CoverageGadgets", "CoverageList", 1);
		shell.addImport("CoverageGadgets", "CoverageCombo", 2);
		shell.addImport("CoverageGadgets", "CoverageEntry", 3);
		struct Gadget { const char *name; const char *ref; int id; };
		const Gadget gadgets[] = {
			{ "OnlineLogin::Nickname", "AptOnlineLogin::InitGadgets", 2 },
			{ "OnlineLogin::Password", "AptOnlineLogin::InitGadgets", 3 },
			{ "OnlineHome::MessageOfTheDay", "AptOnlineHome::InitGadgets", 1 },
			{ "GameList", "AptOnlineCustomMatch::InitGadgets", 1 },
			{ "Lobbies", "AptOnlineCustomMatch::InitGadgets", 2 },
			{ "GameName", "AptOnlineCustomMatch::InitGadgets", 3 },
			{ "GamePassword", "AptOnlineCustomMatch::InitGadgets", 3 },
		};
		std::vector<std::uint32_t> frame;
		frame.push_back(shell.addActionItem(program(shell, [](Asm &a) {
			a.defineFunction("ReadProvider", { "key" }, [](Asm &b) {
				b.getStringVar("extern").getStringVar("key").op(APT_OP_GETMEMBER).op(APT_OP_RETURN);
			});
			a.defineFunction("WriteAdvanced", { "value" }, [](Asm &b) {
				b.getStringVar("extern").pushString("OnlineAdvMode").getStringVar("value").op(APT_OP_SETMEMBER);
			});
			a.defineFunction("LoadScreen", { "name" }, [](Asm &b) {
				b.getStringVar("name").op(APT_OP_TRACE);
			});
			a.defineFunction("LastLoaded", {}, [](Asm &b) {
				b.getStringVar("_root").getStringMember("lastLoaded").op(APT_OP_RETURN);
			});
		})));
		for (int i = 0; i < (int)std::size(gadgets); ++i)
		{
			const Gadget &g = gadgets[i];
			Asm a = shell.program();
			a.pushString("_type").setStringVar(g.id == 1 ? "ListBox" : g.id == 2 ? "ComboBox" : "TextEntry");
			a.pushString("_Init").setStringVar(g.ref);
			a.pushString("_WindowId").setStringVar(g.name);
			a.op(APT_OP_END);
			const auto construct = shell.commit(a);
			TestMovie::Place p;
			p.characterId = g.id;
			p.depth = i + 1;
			p.flags |= APT_PLACE_HASNAME | APT_PLACE_HASCLIPACTION | APT_PLACE_HASMATRIX;
			p.name = "Gadget" + std::to_string(i);
			p.translation[0] = 10;
			p.translation[1] = 40.0f * i;
			p.events.push_back({ APT_CLIP_CONSTRUCT, 0, construct });
			frame.push_back(shell.addPlaceItem(p));
		}
		// A synthetic page clip: the shell's engine->movie calls have a real target.
		const auto pageCode = program(shell, [&](Asm &a) {
			a.defineFunction("OnStartLobby", {}, [](Asm &) {});
			a.defineFunction("ClosePassword", {}, [](Asm &) {});
			a.defineFunction("GotoAndPlay", { "label" }, [](Asm &) {});
			a.defineFunction("EnterPassword", {}, [](Asm &) {});
			a.defineFunction("PopUpConnectingOpen", {}, [](Asm &) {});
			a.defineFunction("PopUpConnectingClose", {}, [](Asm &) {});
		});
		const auto page = shell.addCharacter(shell.addSprite({ { shell.addActionItem(pageCode) } }));
		TestMovie::Place pagePlace;
		pagePlace.characterId = page;
		pagePlace.depth = 10;
		pagePlace.flags |= APT_PLACE_HASNAME;
		pagePlace.name = "OnlineOpenPlay";
		frame.push_back(shell.addPlaceItem(pagePlace));
		const auto loginCode = program(shell, [](Asm &a) {
			a.defineFunction("HandleGameCall", { "name" }, [](Asm &b) {
				b.getStringVar("_root").pushString("loginCall").getStringVar("name").op(APT_OP_SETMEMBER);
			});
		});
		const auto login = shell.addCharacter(shell.addSprite({ { shell.addActionItem(loginCode) } }));
		pagePlace.characterId = login;
		pagePlace.depth = 11;
		pagePlace.name = "OnlineLogin";
		frame.push_back(shell.addPlaceItem(pagePlace));
		shell.setRootFrames({ frame });
	}
};

// A deterministic hand-off: the lobby client queues HTTP but never performs I/O; every answer here
// is a literal or synthesized response, delivered with fixed logical time (never a socket or sleep).
struct OnlineFx
{
	MemorySource source;
	RecordingShellServices services;
	GadgetSkinData skins;
	SyntheticText text;
	SyntheticSetup setup;
	RecordingNewGameSink sink;
	MemorySkirmishProfiles profiles;
	OnlineLobbyClient client;
	OnlineShellContext context;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;
	AptOnlineShell *screen = nullptr;
	std::vector<std::u16string> remembered;

	static OnlineLobbyClient::Options options()
	{
		OnlineLobbyClient::Options o;
		o.playerTemplateCount = 2;
		o.colorCount = 2;
		o.profileDigest = "0123456789abcdef0123456789abcdef";
		return o;
	}
	OnlineFx() : client(options())
	{
		OnlineMovies movies;
		source.add("CoverageGadgets", movies.library);
		source.add("OnlineShell", movies.shell);
		TestMovie empty;
		empty.setRootFrames({ {} });
		source.add("AptLevel0", empty);
		source.add("GuiFX", empty); // message boxes use a synthetic movie, too
		context.client = &client;
		context.rememberName = [this](const std::u16string &name) { remembered.push_back(name); };
		environment.gameText = &text;
		environment.skirmish = &setup;
		environment.newGame = &sink;
		environment.profiles = &profiles;
		environment.services = &services;
		environment.online = &context;
		wm = std::make_unique<WindowManager>(source, services);
		layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
		// Only the synthetic movie's three invented exports are registered here. This test-side
		// factory uses public native gadget APIs; the shell's screen refs and handlers are unmodified.
		for (const char *kind : { "CoverageList", "CoverageCombo", "CoverageEntry" })
		{
			wm->registerComponent(kind, [this, kind](AptComponentRequest &request) -> std::shared_ptr<GameWindow> {
				WinInstanceData data;
				GameWindow *parent = screen ? screen->ownerWindow() : nullptr;
				GameWindow *g = nullptr;
				if (std::string(kind) == "CoverageList")
				{
					data.m_style = GWS_SCROLL_LISTBOX;
					ListboxData ld;
					ld.listLength = 64;
					ld.columns = 7;
					ld.columnWidthPercentage = { 4, 5, 5, 36, 33, 10, 7 };
					g = layer->gadgets().gogoGadgetListBox(parent, WIN_STATUS_ENABLED, (int)request.x0, (int)request.y0, 220, 30, &data, &ld, nullptr, false);
				}
				else if (std::string(kind) == "CoverageCombo")
				{
					data.m_style = GWS_COMBO_BOX;
					ComboBoxData cd;
					cd.isEditable = true;
					cd.entryTemplate.maxTextLen = 32;
					cd.maxChars = 32;
					cd.maxDisplay = 5;
					cd.listboxTemplate.listLength = 16;
					cd.listboxTemplate.columns = 1;
					g = layer->gadgets().gogoGadgetComboBox(parent, WIN_STATUS_ENABLED, (int)request.x0, (int)request.y0, 220, 30, &data, &cd, nullptr, false);
				}
				else
				{
					data.m_style = GWS_ENTRY_FIELD;
					EntryData ed;
					ed.maxTextLen = 32;
					g = layer->gadgets().gogoGadgetTextEntry(parent, WIN_STATUS_ENABLED, (int)request.x0, (int)request.y0, 220, 30, &data, &ed, nullptr, false);
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
		// Register page callbacks before the synthetic movie's first frame creates components.
		page("OnlineLogin");
		page("OnlineOpenPlay");
		page("OnlineLogin");
		tick(2);
		REQUIRE(screen->nicknameWindow());
		REQUIRE(screen->gameList());
		REQUIRE(screen->lobbiesWindow());
		REQUIRE(wm->isAptWindowLoaded(screen->level()));
	}
	void page(const std::string &name) { REQUIRE(wm->invokeCallback("AptOnline::ShellLoadScreen", name)); }
	void command(const std::string &name, const std::string &arg = "") { REQUIRE(wm->invokeCallback(name, arg)); }
	void tick(int n = 1) { for (int i = 0; i < n; ++i) wm->update(16); }
	OnlineLobbyClient::HttpRequest next(const std::string &path, const std::string &method = "GET")
	{
		for (int i = 0; i < 32; ++i)
		{
			client.update(100);
			const auto sent = client.takeRequests();
			if (sent.empty()) continue;
			REQUIRE(sent.size() == 1);
			const auto &r = sent.front();
			if (r.path == path && r.method == method) return r;
			if (r.method == "GET" && (r.path == "/v1/games?running=1" || r.path == "/v1/games" || r.path == "/v1/mine"))
				client.deliver(r.id, 200, "{\"v\":1,\"games\":[],\"left\":[]}", "");
			else if (r.path == "/v1/health") client.deliver(r.id, 200, "{\"relays\":[]}", "");
			else if (r.path == "/v1/motd") client.deliver(r.id, 200, "{\"v\":1,\"motd\":null}", "");
			else
			{
				FAIL_CHECK("unexpected synthetic HTTP request: " << r.method << " " << r.path);
				break;
			}
		}
		return {};
	}
	void answer(const std::string &path, const std::string &json, const std::string &method = "GET", int status = 200)
	{
		const auto r = next(path, method);
		REQUIRE_MESSAGE(r.id != 0, "no queued request for ", method, " ", path); // tolerance: request IDs are sequence numbers; only zero means no request
		client.deliver(r.id, status, json, "");
		tick();
	}
	void signIn()
	{
		GadgetComboBoxSetText(screen->nicknameWindow(), u"Guest");
		command("AptOnline::Login::Login");
		answer("/v1/auth/guest", R"({"v":1,"token":"synthetic.token","account":{"id":"guest:1","name":"Guest","kind":"guest"}})", "POST");
		REQUIRE(client.signedIn());
	}
	bool hasBox(const std::string &piece) const
	{
		return std::any_of(screen->boxesShown().begin(), screen->boxesShown().end(), [&](const std::string &s) { return s.find(piece) != std::string::npos; });
	}
};

std::string listing(const char *id, const char *name, int players, int open, bool password, const char *state = "open", const char *map = "maps/known/known.map")
{
	return std::string("{\"id\":\"") + id + "\",\"name\":\"" + name + "\",\"host\":\"Guest\",\"map\":\"" + map
		+ "\",\"players\":" + std::to_string(players) + ",\"openSlots\":" + std::to_string(open) + ",\"password\":" + (password ? "true" : "false") + ",\"state\":\"" + state + "\"}";
}
std::string view(int own = 0, int seq = 1)
{
	std::ostringstream out;
	out << R"({"id":"0000000000000001","name":"Test room","private":false,"hostSlot":0,"seq":)" << seq
		<< R"(,"setup":{"map":"maps/known/known.map","startingCash":1000,"superweaponRestriction":0,"legendaryHeroes":1},"slots":[)";
	for (int i = 0; i < 8; ++i)
	{
		if (i) out << ',';
		out << "{\"slot\":" << i << ",\"state\":\"" << (i == 0 || i == own ? "player" : "open") << "\",\"name\":\""
			<< (i == 0 ? "Guest" : i == own ? "Visitor" : "")
			<< "\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":false,\"you\":" << (i == own ? "true" : "false") << '}';
	}
	out << "]}";
	return out.str();
}
} // namespace

TEST_CASE("coverage1: page lifecycle refuses an unavailable page repeatedly without changing resume state")
{
	OnlineFx f;
	CHECK(f.screen->currentPage() == "OnlineLogin");
	CHECK(f.wm->hasProvider("OnlineAdvMode"));
	std::string value;
	REQUIRE(f.wm->invokeAS(f.screen->level(), "ReadProvider", { "OnlineAdvMode" }, &value));
	CHECK(value == "0");
	REQUIRE(f.wm->invokeAS(f.screen->level(), "WriteAdvanced", { "true" }));
	REQUIRE(f.wm->invokeAS(f.screen->level(), "ReadProvider", { "OnlineAdvMode" }, &value));
	CHECK(value == "1");
	REQUIRE(f.wm->invokeAS(f.screen->level(), "WriteAdvanced", { "not-true" }));
	REQUIRE(f.wm->invokeAS(f.screen->level(), "ReadProvider", { "OnlineAdvMode" }, &value));
	CHECK(value == "0");
	CHECK(f.wm->hasScreenRef("AptOnlineCustomMatch::InitGadgets"));
	// Unloading a page drops its registrations; reloading registers it and updates the resume page.
	f.command("AptOnline::ShellUnloadScreen", "OnlineOpenPlay");
	f.page("OnlineOpenPlay");
	CHECK(f.context.lastPage == "OnlineOpenPlay");
	f.page("OnlineStrategic");
	CHECK(f.screen->currentPage() == "OnlineOpenPlay");
	CHECK(f.context.lastPage == "OnlineOpenPlay");
	const size_t boxes = f.screen->boxesShown().size();
	f.page("OnlineStrategic"); // regression: an already-refused name was treated as a loaded page
	CHECK(f.screen->currentPage() == "OnlineOpenPlay");
	CHECK(f.context.lastPage == "OnlineOpenPlay");
	CHECK(f.screen->boxesShown().size() == boxes + 1);
	CHECK(f.hasBox("not available: OnlineStrategic"));
	f.command("AptOnline::ShellUnloadScreen", "OnlineOpenPlay");
	CHECK_FALSE(f.wm->hasCommand("GameSorter::OnSortName"));
	CHECK_FALSE(f.wm->hasScreenRef("AptOnlineCustomMatch::InitGadgets"));
	f.page("OnlineOpenPlay");
	CHECK(f.wm->hasCommand("GameSorter::OnSortName"));
}

TEST_CASE("coverage1: nickname edits, blank login and refused guest sign-in recover")
{
	OnlineFx f;
	GameWindow *name = f.screen->nicknameWindow();
	GadgetComboBoxSetText(name, u"  ");
	f.screen->nicknameEdited();
	CHECK(f.screen->loginButtonState() == 0);
	f.command("AptOnline::Login::Login");
	CHECK(f.hasBox("GSNoLoginInfoNickname"));
	GadgetComboBoxSetText(name, u"  Guest  ");
	f.screen->nicknameEdited();
	CHECK(f.screen->loginButtonState() == 1);
	f.command("AptOnline::Login::Login");
	CHECK(f.screen->loginButtonState() == 0);
	f.command("AptOnline::Login::Login"); // while in flight it is not sent twice
	f.answer("/v1/auth/guest", R"({"v":1,"error":"rate_limited","message":"wait"})", "POST", 429);
	CHECK(f.screen->loginButtonState() == 1);
	CHECK(f.hasBox("wait"));
	f.command("AptOnline::Login::Login");
	const auto r = f.next("/v1/auth/guest", "POST");
	REQUIRE(r.id);
	CHECK(r.body.find("\"name\":\"Guest\"") != std::string::npos);
	f.client.deliver(r.id, 200, R"({"v":1,"token":"synthetic.token","account":{"name":"Guest"}})", "");
	f.tick();
	CHECK(f.client.signedIn());
	CHECK(std::find(f.wm->traces().begin(), f.wm->traces().end(), "OnlineHome") != f.wm->traces().end());
	CHECK(f.context.rememberedName == u"Guest");
	CHECK(f.remembered == std::vector<std::u16string>{ u"Guest" });
}

TEST_CASE("coverage1: home MOTD pending, multiline answer, empty and transport refusal")
{
	OnlineFx f;
	f.page("OnlineHome");
	f.tick(); // deferred component InitGadgets now binds the MOTD list
	CHECK(f.screen->motdLines() == std::vector<std::string>{ "..." });
	CHECK(f.wm->hasProvider("OnlineHome::NumTickerFields"));
	REQUIRE(f.screen->tickerRows().size() == 4);
	f.answer("/v1/motd", R"({"v":1,"motd":"First\nSecond"})");
	CHECK(f.screen->motdLines() == std::vector<std::string>{ "First", "Second" });
	f.client.fetchMotd();
	f.answer("/v1/motd", R"({"v":1,"motd":null})");
	REQUIRE(f.screen->motdLines().size() == 1);
	CHECK(f.screen->motdLines()[0] != "...");
	f.client.fetchMotd();
	const auto failed = f.next("/v1/motd");
	REQUIRE(failed.id);
	f.client.deliver(failed.id, 0, "", "synthetic timeout");
	f.tick();
	REQUIRE(f.screen->motdLines().size() == 1);
	// A failed refresh retains the previously known empty MOTD instead of
	// replacing valid cached content with a transport error.
	CHECK(f.screen->motdLines()[0] != "...");
	CHECK(std::any_of(f.wm->notes().begin(), f.wm->notes().end(), [](const WindowManagerNote &note) {
		return note.kind == "online-refused" && note.detail.find("synthetic timeout") != std::string::npos;
	}));
	f.command("AptOnline::ShellUnloadScreen", "OnlineHome");
	CHECK_FALSE(f.wm->hasProvider("OnlineHome::NumTickerFields"));
}

TEST_CASE("coverage1: browser sorts synthetic games, keeps selection and excludes games left")
{
	OnlineFx f;
	f.signIn();
	f.page("OnlineOpenPlay");
	f.client.refreshList(true);
	const std::string a = listing("0000000000000001", "Zulu", 2, 0, true);
	const std::string b = listing("0000000000000002", "Alpha", 1, 1, false);
	const std::string c = listing("0000000000000003", "Started", 2, 0, false, "started", "maps/missing/missing.map");
	f.answer("/v1/games?running=1", "{\"v\":1,\"games\":[" + a + "," + b + "," + c + "]}");
	REQUIRE(f.screen->gameIds().size() == 3);
	REQUIRE(f.screen->gameRows().size() == 3);
	CHECK(f.screen->gameRows()[2].find("watch") != std::string::npos);
	GadgetListBoxSetSelected(f.screen->gameList(), 1);
	f.command("GameSorter::OnSortName");
	f.tick();
	CHECK(f.screen->gameIds()[0] == "0000000000000002");
	int selected = -1;
	GadgetListBoxGetSelected(f.screen->gameList(), &selected);
	CHECK(selected == 0);
	f.command("GameSorter::OnSortName");
	f.tick();
	CHECK(f.screen->sortDescending());
	CHECK(f.screen->gameIds()[0] == "0000000000000001");
	f.command("GameSorter::OnSortPlayers");
	f.tick();
	CHECK(f.screen->sortColumn() == 2);
	CHECK_FALSE(f.screen->sortDescending());
	f.context.leftGames->insert("0000000000000001");
	f.command("AptOnline::CustomMatch::Refresh");
	f.answer("/v1/games?running=1", "{\"v\":1,\"games\":[" + a + "," + b + "," + c + "]}");
	CHECK(f.screen->gameIds().size() == 2);
	CHECK(std::find(f.screen->gameIds().begin(), f.screen->gameIds().end(), "0000000000000001") == f.screen->gameIds().end());
}

TEST_CASE("coverage1: browser join rejects missing, full and blank password then queues a locked join")
{
	OnlineFx f;
	f.signIn();
	f.page("OnlineOpenPlay");
	f.command("AptOnline::CustomMatch::JoinGame");
	f.tick();
	CHECK(f.hasBox("NoGameSelected"));
	f.client.refreshList(true);
	const std::string full = listing("0000000000000001", "Full", 2, 0, false);
	const std::string locked = listing("0000000000000002", "Locked", 1, 1, true);
	f.answer("/v1/games?running=1", "{\"v\":1,\"games\":[" + full + "," + locked + "]}");
	f.tick(); // state 1 marks the list dirty; let state 2 fill it before selecting
	REQUIRE(f.screen->gameIds() == std::vector<std::string>{ "0000000000000001", "0000000000000002" });
	GadgetListBoxSetSelected(f.screen->gameList(), 0);
	f.command("AptOnline::CustomMatch::JoinGame");
	f.tick();
	CHECK(f.hasBox("JoinFailedRoomFull"));
	f.tick(); // state 1 returns to the idle browser before the next Join command
	GadgetListBoxSetSelected(f.screen->gameList(), 1);
	f.command("AptOnline::CustomMatch::JoinGame");
	f.tick();
	CHECK(f.screen->openPlayState() == 10);
	f.command("AptOnline::CustomMatch::AcceptJoinGame");
	CHECK(f.screen->openPlayState() == 1);
	CHECK(f.hasBox("JoinFailedBadPassword"));
	f.tick();
	GadgetListBoxSetSelected(f.screen->gameList(), 1);
	f.command("AptOnline::CustomMatch::JoinGame");
	f.tick();
	REQUIRE(f.screen->gamePasswordWindow());
	GadgetTextEntrySetText(f.screen->gamePasswordWindow(), u"pass");
	f.command("AptOnline::CustomMatch::AcceptJoinGame");
	CHECK(f.screen->openPlayState() == 11);
	const auto r = f.next("/v1/games/0000000000000002/join", "POST");
	REQUIRE(r.id);
	CHECK(r.body.find("pass") != std::string::npos);
	f.client.deliver(r.id, 403, R"({"v":1,"error":"password","message":"bad password"})", "");
	f.tick();
	CHECK(f.screen->openPlayState() == 2);
	CHECK(f.hasBox("bad password"));
}

TEST_CASE("coverage1: create dialog restores fields after malformed lobby answer and can enter staging")
{
	OnlineFx f;
	f.signIn();
	f.page("OnlineOpenPlay");
	f.command("AptOnline::OnOpenCreateDialog");
	CHECK(f.screen->openPlayState() == 3);
	REQUIRE(f.screen->gameNameWindow());
	CHECK(GadgetTextEntryGetText(f.screen->gameNameWindow()) == u"Guest");
	GadgetTextEntrySetText(f.screen->gameNameWindow(), u"  Test room  ");
	f.command("AptOnline::CustomMatch::CreateGame");
	f.tick();
	CHECK(f.screen->openPlayState() == 5);
	const auto bad = f.next("/v1/games", "POST");
	REQUIRE(bad.id);
	CHECK(bad.body.find("Test room") != std::string::npos);
	f.client.deliver(bad.id, 200, R"({"v":1,"game":{}})", ""); // invalid view, not a room
	f.tick();
	CHECK(f.screen->openPlayState() == 3);
	CHECK((f.screen->gameNameWindow()->winGetStatus() & WIN_STATUS_ENABLED) == WIN_STATUS_ENABLED);
	f.command("AptOnline::CustomMatch::CreateGame");
	f.tick();
	f.answer("/v1/games", "{\"v\":1,\"game\":" + view() + "}", "POST");
	CHECK(f.screen->openPlayState() == 6);
	CHECK(f.client.inGame());
	REQUIRE(f.screen->setup());
	CHECK(f.screen->setup()->info().mapName == "maps/known/known.map");
	f.command("AptOnline::CustomMatch::Cancel");
	CHECK(f.screen->openPlayState() == 1);
	CHECK_FALSE(f.client.inGame());
}

TEST_CASE("coverage1: watch and rejoin refuse malformed START and return to the list")
{
	OnlineFx f;
	f.signIn();
	f.page("OnlineOpenPlay");
	f.client.refreshList(true);
	f.answer("/v1/games?running=1", "{\"v\":1,\"games\":[" + listing("0000000000000003", "Running", 2, 0, false, "started") + "]}");
	GadgetListBoxSetSelected(f.screen->gameList(), 0);
	f.command("AptOnline::CustomMatch::JoinGame");
	f.tick();
	CHECK(f.screen->openPlayState() == 13);
	CHECK(f.screen->connectingShown());
	f.answer("/v1/games/0000000000000003/spectate", R"({"v":1,"start":null})", "POST");
	CHECK_FALSE(f.screen->connectingShown());
	CHECK(f.screen->openPlayState() == 2);
	CHECK(f.hasBox("rejoin-refused"));
	f.screen->startRunning("0000000000000003", true);
	CHECK(f.screen->openPlayState() == 13);
	f.answer("/v1/games/0000000000000003/rejoin", R"({"v":1,"error":"no_game","message":"ended"})", "POST", 404);
	CHECK(f.screen->openPlayState() == 2);
	CHECK_FALSE(f.screen->connectingShown());
	CHECK(f.hasBox("rejoin-refused|ended"));
}

TEST_CASE("coverage1: question, unavailable commands and player-facing refusal text")
{
	OnlineFx f;
	CHECK(f.screen->playerMessage("join", "password", u"secret") == u"GUI:JoinFailedBadPassword");
	CHECK(f.screen->playerMessage("join", "full", u"secret") == u"GUI:JoinFailedRoomFull");
	CHECK(f.screen->playerMessage("join", "no_game", u"secret") == u"GUI:HostLeft");
	f.context.lobbyUrl = "https://lobby.example.test/v1";
	CHECK(f.screen->playerMessage("list", "transport", u"hidden").find(u"lobby.example.test") != std::u16string::npos);
	CHECK(AptOnlineShell::wrapLines(u"a bc def", 3) == u"a\nbc\ndef");
	f.command("AptOnline::Login::Register");
	CHECK(f.hasBox("not available: Register"));
	REQUIRE(f.screen->messageBox());
	f.screen->messageBox()->hide(true);
	CHECK(f.screen->askYesNo("invite", u"Invitation", u"Join the game?"));
	CHECK(f.screen->asking("invite"));
	CHECK_FALSE(f.screen->askYesNo("second", u"Other", u"Again?"));
	REQUIRE(f.screen->messageBox());
	CHECK(f.screen->messageBox()->type() == AptMessageBox::TYPE_YES_NO);
	CHECK(f.screen->messageBox()->title() == u"Invitation");
	f.command("_level" + std::to_string(f.screen->messageBox()->level()) + ".MessageBox_OnButtonNo");
	CHECK_FALSE(f.screen->asking("invite"));
	CHECK(f.screen->takeAnswers() == std::vector<std::string>{ "invite|no" });
}
