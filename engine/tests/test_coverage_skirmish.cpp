// Synthetic no-game coverage tests. GPL-3.0.

#include "AptPlayerTestUtil.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GUI/Skirmish/SkirmishGameRecord.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

using namespace apttest;

namespace
{
struct Labels : GameTextSource
{
	std::map<std::string, std::u16string> values;
	bool fetch(const std::string &key, std::u16string &out) const override
	{
		auto it = values.find(key);
		if (it == values.end()) return false;
		out = it->second;
		return true;
	}
	bool fetchMapLabel(const std::string &, const std::string &, std::u16string &) const override { return false; }
	Labels()
	{
		for (const char *s : { "Player", "Open", "Closed", "EasyAI", "MediumAI", "HardAI", "BrutalAI", "Random", "Blank", "???" })
			values[std::string("GUI:") + s] = asciiToU16(s);
		for (int i = 1; i <= 8; ++i) values["NUMBER:" + std::to_string(i)] = asciiToU16(std::to_string(i));
		for (int i = 0; i <= 4; ++i) values["Team:" + std::to_string(i)] = asciiToU16("Team " + std::to_string(i));
		values["TOOLTIP:StartPosition"] = u"free";
		values["TOOLTIP:StartPositionN"] = u"player %d";
		values["GUI:StartingMoneyFormat"] = u"$%d";
		values["GUI:ErrorStartingGame"] = u"Cannot start";
		values["GUI:CantFindMap"] = u"Unknown map";
		values["LAN:TooManyPlayers"] = u"Too many players: %d";
	}
};

struct Maps : SkirmishSetupSource
{
	std::vector<SkirmishFaction> factionEntries;
	std::vector<SkirmishColor> colorEntries;
	std::vector<MapCacheEntry> entries;
	std::set<std::string> files;
	const std::vector<SkirmishFaction> &factions() const override { return factionEntries; }
	const std::vector<SkirmishColor> &colors() const override { return colorEntries; }
	const std::vector<MapCacheEntry> &maps() const override { return entries; }
	int defaultStartingCash() const override { return 1500; }
	const std::vector<int> &startingCashChoices() const override { static const std::vector<int> cash{ 500, 1500 }; return cash; }
	int mapContentsMask(const MapCacheEntry &) const override { return 1 | 2; }
	bool fileExists(const std::string &file) const override { return files.count(file) != 0; }
	bool hasFileSystem() const override { return true; }
	Maps()
	{
		SkirmishFaction men;
		men.templateName = "FactionMen";
		men.side = "Men";
		men.displayLabel = "INI:FactionMen";
		men.startingBuilding = "Fortress";
		men.playableSide = true;
		factionEntries.push_back(men);
		colorEntries.push_back({ "Blue", "Color:Blue", 0x0000FFu, true });
		colorEntries.push_back({ "Red", "Color:Red", 0xFF0000u, true });
		for (auto item : { std::pair<const char *, int>{ "zulu", 2 }, { "alpha", 4 }, { "beta", 2 } })
		{
			MapCacheEntry m;
			m.name = std::string("maps/") + item.first + "/" + item.first + ".map";
			m.rawName = m.name;
			m.displayName = asciiToU16(item.first);
			m.description = u"description";
			m.isMultiplayer = m.isOfficial = true;
			m.numPlayers = item.second;
			m.fileCRC = 13;
			m.fileSize = 100;
			m.extentMax.x = 100;
			m.extentMax.y = 100;
			for (int n = 1; n <= m.numPlayers; ++n)
			{
				Coord3D pos{};
				pos.x = n * 20;
				pos.y = n * 20;
				m.startPositions[n] = pos;
			}
			entries.push_back(m);
		}
	}
};

// A real Skirmish.apt timeline, served only from memory. Its exported component sprites have Construct
// actions with the same _Init/_WindowId protocol as the movie. The component factory makes actual native
// gadgets; no WND, retail mount, test-only screen subclass or private-member access is required.
struct Movie
{
	TestMovie movie;
	std::vector<std::uint32_t> frame;
	Movie()
	{
		const std::uint32_t shape = movie.addCharacter(movie.addShape(0, 0, 200, 120, 0));
		std::map<std::string, std::uint32_t> sprites;
		for (const char *type : { "CoverageList", "CoverageCombo", "CoverageEntry", "CoverageMap" })
		{
			const std::uint32_t sprite = movie.addCharacter(movie.addSprite({ { movie.addPlaceItem(placeChar(shape, 1, "bounds")) } }));
			movie.addExport(type, sprite);
			sprites[type] = sprite;
		}
		int depth = 1;
		auto place = [&](const char *type, const std::string &name, const std::string &init) {
			TestMovie::Place p = placeChar(sprites.at(type), depth, "widget" + std::to_string(depth));
			++depth;
			const std::uint32_t code = program(movie, [&](Asm &a) {
				a.pushString("_type").setStringVar(type);
				a.pushString("_Init").setStringVar(init);
				a.pushString("_WindowId").setStringVar(name);
			});
			p.flags |= APT_PLACE_HASCLIPACTION;
			p.events.push_back({ APT_CLIP_CONSTRUCT, 0, code });
			frame.push_back(movie.addPlaceItem(p));
		};
		place("CoverageList", "Skirmish::SelectProfile", "AptSkirmish::InitGadgets");
		place("CoverageEntry", "Skirmish::CreatePersonaEntry", "AptSkirmish::InitGadgets");
		place("CoverageList", "MapList", "MpGameSetup::InitGadgets");
		place("CoverageList", "MapInfo", "AptMapPreview::MapGadgetInit");
		place("CoverageList", "MapDescription", "AptMapPreview::MapGadgetInit");
		place("CoverageMap", "CurrentMap", "AptMapPreview::MapGadgetInit");
		place("CoverageList", "Chat", "AptMpChat::InitGadgets");
		place("CoverageList", "ChatPlayers", "AptMpChat::InitGadgets");
		place("CoverageEntry", "ChatEntry", "AptMpChat::InitGadgets");
		for (int slot = 0; slot < 8; ++slot)
		{
			for (const char *leaf : { "Player", "PlayerTemplate", "Team", "Handicap", "Hero" })
			{
				if (slot >= 2 && std::string(leaf) != "Team" && !(slot == 2 && std::string(leaf) == "Player")) continue;
				place("CoverageCombo", std::to_string(slot) + "/" + leaf, "MpGameSetup::InitGadgets");
			}
		}
		for (const char *name : { "ShowMain", "CloseMain", "ButtonReset", "CloseProfilePopup", "PopupSelectBttnDisable", "PopupSelectBttnEnable", "SetReadyState" })
			frame.insert(frame.begin(), movie.addActionItem(program(movie, [name](Asm &a) {
				a.defineFunction(name, {}, [name](Asm &b) { b.pushString(name).op(APT_OP_TRACE); });
			})));
		frame.insert(frame.begin(), movie.addActionItem(program(movie, [](Asm &a) {
			a.defineFunction("ShowAddProfile", {}, [](Asm &b) { fscmd(b, "AptSkirmish::OnNewProfileMenu"); });
		})));
		frame.push_back(movie.addActionItem(program(movie, [](Asm &a) { fscmd(a, "AptSkirmish::OnInitialized"); a.op(APT_OP_STOP); })));
		movie.setRootFrames({ frame });
	}
};

struct Fixture
{
	MemorySource source;
	RecordingShellServices services;
	Labels labels;
	Maps maps;
	MemorySkirmishProfiles profiles;
	RecordingNewGameSink games;
	ShellEnvironment env;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	WindowManager wm;
	AptGadgetLayer layer;
	Shell shell;
	AptSkirmish *screen = nullptr;

	explicit Fixture(bool profile = true, bool pushNow = true)
		: wm(source, services), layer(wm, source, skins), shell(wm, factories, services, env)
	{
		if (profile) { profiles.add(u"Alpha"); profiles.setCurrent(u"Alpha"); }
		games.seed = 4242;
		env.gameText = &labels;
		env.skirmish = &maps;
		env.profiles = &profiles;
		env.newGame = &games;
		labels.values["INI:FactionMen"] = u"Men";
		labels.values["Color:Blue"] = u"Blue";
		labels.values["Color:Red"] = u"Red";
		TestMovie level0;
		level0.setRootFrames({ {} });
		source.add("AptLevel0", level0);
		Movie movie;
		source.add("Skirmish", movie.movie);
		factories.registerFactory("Skirmish.apt", [](AptScreenContext &c) { return std::make_unique<AptSkirmish>(c.windows, c.shell, c.environment); });
		for (const char *name : { "CoverageList", "CoverageCombo", "CoverageEntry", "CoverageMap" })
		{
			wm.registerComponent(name, [this](AptComponentRequest &r) -> std::shared_ptr<GameWindow> {
				GameWindowManager &g = layer.gadgets();
				WinInstanceData inst;
				GameWindow *w = nullptr;
				if (r.symbol == "CoverageList")
				{
					inst.m_style = GWS_SCROLL_LISTBOX;
					ListboxData data;
					data.columns = 1;
					data.listLength = 100;
					w = g.gogoGadgetListBox(nullptr, WIN_STATUS_ENABLED, 0, 0, 200, 120, &inst, &data, nullptr, false);
				}
				else if (r.symbol == "CoverageCombo")
				{
					inst.m_style = GWS_COMBO_BOX;
					ComboBoxData data;
					data.listboxTemplate.columns = 1;
					data.listboxTemplate.listLength = 32;
					data.maxDisplay = 8;
					w = g.gogoGadgetComboBox(nullptr, WIN_STATUS_ENABLED, 0, 0, 100, 24, &inst, &data, nullptr, false);
				}
				else if (r.symbol == "CoverageEntry")
				{
					inst.m_style = GWS_ENTRY_FIELD;
					EntryData data;
					data.maxTextLen = 110;
					w = g.gogoGadgetTextEntry(nullptr, WIN_STATUS_ENABLED, 0, 0, 150, 24, &inst, &data, nullptr, false);
				}
				else
				{
					inst.m_style = GWS_USER_WINDOW;
					w = g.winCreate(nullptr, WIN_STATUS_ENABLED, 0, 0, 200, 120, nullptr, &inst);
					for (int i = 7; w && i >= 0; --i)
					{
						WinInstanceData button;
						button.m_style = GWS_PUSH_BUTTON;
						g.gogoGadgetPushButton(w, WIN_STATUS_ENABLED, 0, 0, 15, 15, &button, nullptr, false);
					}
				}
				if (!w) return {};
				w->setAptInstanceName(r.instanceName);
				return { w, [this](GameWindow *window) { layer.gadgets().winDestroy(window); layer.gadgets().processDestroyList(); } };
			});
		}
		wm.init();
		if (pushNow) push();
	}
	~Fixture() { shell.clear(); }
	void push()
	{
		shell.push("Skirmish.apt");
		screen = dynamic_cast<AptSkirmish *>(shell.top());
		REQUIRE(screen != nullptr);
		REQUIRE(screen->setup() != nullptr);
		REQUIRE(screen->lastError().empty());
		for (int i = 0; i < 3; ++i) wm.update(33);
		REQUIRE(shell.errors().empty());
		for (const auto &error : wm.errors()) INFO(error);
		REQUIRE(wm.errors().empty());
		REQUIRE(layer.errors().empty());
		REQUIRE_FALSE(note("unknown-screen-ref"));
		REQUIRE_FALSE(note("component-no-init"));
		REQUIRE(screen->mapListWindow() != nullptr);
		REQUIRE(screen->currentMapWindow() != nullptr);
	}
	void command(const std::string &name, const std::string &arg = "") { REQUIRE(wm.invokeCallback(name, arg)); }
	void selectMap(const std::string &name)
	{
		const auto &keys = screen->mapListKeys();
		const auto found = std::find(keys.begin(), keys.end(), name);
		REQUIRE(found != keys.end());
		layer.gadgets().winSendSystemMsg(screen->ownerWindow(), GLM_SELECTED, msgData(screen->mapListWindow()),
			static_cast<WindowMsgData>(found - keys.begin()));
	}
	std::string provider(const std::string &name) { std::string value; REQUIRE(wm.getExtern(name, value) == AptExternResult::Value); return value; }
	bool note(const std::string &kind) const
	{
		return std::any_of(wm.notes().begin(), wm.notes().end(), [&](const WindowManagerNote &n) { return n.kind == kind; });
	}
};

void choose(Fixture &fx, GameWindow *combo, int value)
{
	REQUIRE(combo);
	int at = -1;
	for (int i = 0; i < GadgetComboBoxGetLength(combo); ++i)
		if (static_cast<int>(reinterpret_cast<std::intptr_t>(GadgetComboBoxGetItemData(combo, i))) == value) at = i;
	REQUIRE(at != -1); // exact not-found sentinel; each combo has its own row order
	GadgetComboBoxSetSelectedPos(combo, at, true);
	fx.layer.gadgets().winSendSystemMsg(fx.screen->ownerWindow(), GCM_SELECTED, msgData(combo), 0);
}
} // namespace

TEST_CASE("coverage1: synthetic Skirmish movie initializes handlers and profile popup")
{
	Fixture fx(false);
	CHECK(fx.screen->initializedCount() == 1);
	CHECK(fx.screen->newProfileMenuCount() == 1);
	CHECK(fx.screen->profileState() == 2);
	CHECK(fx.screen->profileAcceptDisabled());
	REQUIRE(fx.screen->profileEntryWindow());
	GadgetTextEntrySetText(fx.screen->profileEntryWindow(), u"  Beta  ");
	fx.layer.gadgets().winSendSystemMsg(fx.screen->ownerWindow(), GEM_UPDATE_TEXT, msgData(fx.screen->profileEntryWindow()), 0);
	CHECK_FALSE(fx.screen->profileAcceptDisabled());
	fx.command("AptSkirmish::OnAddProfileAccept");
	CHECK(fx.profiles.current() == u"Beta");
	CHECK(fx.screen->setup()->info().slots[0].name == u"Beta");
	CHECK(fx.screen->profileState() == 7);
	fx.command("AptSkirmish::OnNewProfileMenu");
	GadgetTextEntrySetText(fx.screen->profileEntryWindow(), u" beta ");
	fx.command("AptSkirmish::OnAddProfileAccept");
	CHECK(fx.profiles.names().size() == 1);
	CHECK(fx.note("profile-rejected"));
	fx.command("AptSkirmish::OnProfilePopupCancel");
	CHECK(fx.screen->profileAcceptDisabled());
}

TEST_CASE("coverage1: profile selection keeps the old lobby and reloads the new profile")
{
	Fixture fx;
	REQUIRE(fx.profiles.add(u"Bravo"));
	REQUIRE(fx.screen->setup()->info().mapName == fx.maps.entries[1].name);
	fx.command("AptSkirmish::OnChangeProfileMenu");
	GameWindow *list = fx.screen->profileListWindow();
	REQUIRE(list);
	CHECK(GadgetListBoxGetNumEntries(list) == 2);
	GadgetListBoxSetSelected(list, 1);
	fx.command("AptSkirmish::OnChangeProfile");
	CHECK(fx.profiles.current() == u"Bravo");
	CHECK(fx.profiles.profileValue(0, "GameInfo").empty()); // Bravo had no saved game
	CHECK(fx.screen->setup()->info().slots[0].name == u"Bravo");
	fx.command("AptSkirmish::OnChangeProfileMenu");
	GadgetListBoxSetSelected(list, 0);
	fx.command("AptSkirmish::OnChangeProfile");
	CHECK(fx.profiles.current() == u"Alpha");
	CHECK_FALSE(fx.profiles.profileValue(0, "GameInfo").empty());
	CHECK_FALSE(fx.screen->restoredGame().empty());
	CHECK(fx.screen->setup()->info().slots[0].name == u"Alpha");
	fx.command("AptSkirmish::OnDeleteProfileMenu");
	GadgetListBoxSetSelected(list, 0);
	fx.command("AptSkirmish::OnDeleteProfile");
	CHECK(fx.profiles.current() == u"Bravo");
	CHECK(fx.profiles.names().size() == 1);
	CHECK(fx.screen->setup()->info().slots[0].name == u"Bravo");
	CHECK_FALSE(fx.screen->restoredGame().empty());
}

TEST_CASE("coverage1: stale profile selection preserves the active lobby and popup")
{
	Fixture fx;
	REQUIRE(fx.profiles.add(u"Bravo"));
	fx.command("AptSkirmish::OnChangeProfileMenu");
	GameWindow *list = fx.screen->profileListWindow();
	REQUIRE(list);
	REQUIRE(GadgetListBoxGetNumEntries(list) == 2);
	GadgetListBoxSetSelected(list, 1);
	const int popup = fx.screen->profileState();
	const std::string map = fx.screen->setup()->info().mapName;
	REQUIRE(fx.profiles.remove(u"Bravo")); // another part of the application removed the listed profile
	fx.command("AptSkirmish::OnChangeProfile");
	CHECK(fx.profiles.current() == u"Alpha");
	CHECK(fx.screen->profileState() == popup);
	CHECK(fx.screen->setup()->info().mapName == map);
	CHECK(fx.screen->setup()->info().slots[0].name == u"Alpha");
	CHECK(fx.note("profile-rejected"));
	CHECK(GadgetListBoxGetNumEntries(list) == 1);
}

TEST_CASE("coverage1: invalid saved lobby record reports parse error and preserves default map")
{
	Fixture fx(true, false);
	fx.profiles.setProfileValue(0, "GameInfo", "M=bad;UNKNOWN=1;");
	fx.push();
	CHECK(fx.screen->restoredGame().empty());
	CHECK(fx.note("skirmish-kept-game"));
	CHECK(fx.screen->setup()->info().mapName == fx.maps.entries[1].name);
}

TEST_CASE("coverage1: map sorting selection and invalid rows use native listbox")
{
	Fixture fx;
	CHECK(fx.screen->mapListKeys() == std::vector<std::string>{ fx.maps.entries[2].name, fx.maps.entries[0].name, fx.maps.entries[1].name });
	fx.command("MpGameSetup::OnSortName");
	CHECK(fx.screen->mapListKeys().front() == fx.maps.entries[1].name);
	fx.command("MpGameSetup::OnSortName");
	CHECK(fx.screen->mapListKeys().front() == fx.maps.entries[0].name);
	GameWindow *list = fx.screen->mapListWindow();
	fx.selectMap(fx.maps.entries[0].name);
	CHECK(fx.screen->setup()->info().mapName == fx.maps.entries[0].name);
	CHECK(*fx.wm.aptText("APT:MapTitle") != " ");
	fx.layer.gadgets().winSendSystemMsg(fx.screen->ownerWindow(), GLM_SELECTED, msgData(list), static_cast<WindowMsgData>(999));
	CHECK(fx.note("map-list-row-without-data"));
	CHECK(fx.screen->setup()->info().mapName == fx.maps.entries[0].name);
}

TEST_CASE("coverage1: map preview reports absent art and uses synthetic art when available")
{
	Fixture fx(true, false);
	fx.maps.files.insert("maps\\alpha\\alpha_art.tga");
	fx.maps.files.insert("maps\\alpha\\alpha_pic.tga");
	fx.push();
	const auto *picture = fx.wm.renderPicture("AptMapPreview::Picture");
	REQUIRE(picture);
	CHECK(picture->file == "maps\\alpha\\alpha_pic.tga");
	CHECK(fx.screen->currentMapWindow()->winGetEnabledImage(0) == "maps_alpha_alpha");
	CHECK(fx.screen->currentMapWindow()->winGetEnabledImage(1) == "ScrollShroud");
	fx.selectMap(fx.maps.entries[2].name);
	picture = fx.wm.renderPicture("AptMapPreview::Picture");
	REQUIRE(picture);
	CHECK(picture->mappedImage == "MissingMap");
	CHECK(picture->file.empty());
}

TEST_CASE("coverage1: start spots cycle between human and AI and ignore unrelated gadgets")
{
	Fixture fx;
	const auto &spots = fx.screen->currentMapSpots();
	REQUIRE(spots.size() == 8);
	GameWindow *owner = fx.screen->ownerWindow();
	CHECK(fx.layer.gadgets().winSendSystemMsg(owner, GBM_SELECTED, msgData(fx.screen->profileListWindow()), 0) == MSG_IGNORED);
	fx.layer.gadgets().winSendSystemMsg(owner, GBM_SELECTED, msgData(spots[0]), 0);
	CHECK(fx.screen->setup()->info().slots[0].startPos == 0);
	fx.layer.gadgets().winSendSystemMsg(owner, GBM_SELECTED, msgData(spots[0]), 0);
	CHECK(fx.screen->setup()->info().slots[1].startPos == 0);
	fx.layer.gadgets().winSendSystemMsg(owner, GBM_SELECTED_RIGHT, msgData(spots[0]), 0);
	CHECK(fx.screen->setup()->info().slots[1].startPos == -1);
	CHECK(spots[0]->winGetText().empty());
}

TEST_CASE("coverage1: slot combos apply model values and reject out-of-range selection")
{
	Fixture fx;
	GameWindow *team = fx.screen->slotGadget(1, "Team");
	REQUIRE(team);
	choose(fx, team, 1);
	CHECK(fx.screen->setup()->info().slots[1].teamNumber == 1);
	GameWindow *handicap = fx.screen->slotGadget(1, "Handicap");
	REQUIRE(handicap);
	choose(fx, handicap, -25);
	CHECK(fx.screen->setup()->info().slots[1].handicap == -25);
	CHECK(fx.screen->slotGadget(8, "Team") == nullptr);
	CHECK(fx.screen->slotGadget(-1, "Team") == nullptr);
	CHECK(fx.screen->readyIcon(-1).empty());
	GadgetComboBoxSetSelectedPos(team, -1, false);
	fx.layer.gadgets().winSendSystemMsg(fx.screen->ownerWindow(), GCM_SELECTED, msgData(team), 0);
	CHECK(fx.screen->setup()->info().slots[1].teamNumber == 1);
}

TEST_CASE("coverage1: chat entry trims commands and ready and rules providers change with map type")
{
	Fixture fx;
	CHECK(fx.provider("MpGameSetup::HostMode") == "1");
	CHECK(fx.provider("AptMpGameRules::ShowChat") == "0");
	CHECK(fx.provider("AptMpGameRules::ShowClans") == "0");
	CHECK(fx.provider("AptMapPreview::GameMapType") == "OpenPlay");
	const int openCombos = std::stoi(fx.provider("MpGameRules::NumComboBoxes"));
	fx.command("MpGameSetup::OnReadyPress");
	CHECK(fx.screen->readyIcon(0).empty()); // new movie boxes are refreshed on the next update
	fx.wm.update(33);
	CHECK(fx.screen->readyIcon(0) == "_none");
	fx.command("MpGameSetup::OnReadyPress", "999");
	CHECK(fx.note("command-ignored"));
	GameWindow *entry = fx.screen->chatEntryWindow();
	REQUIRE(entry);
	GadgetTextEntrySetText(entry, u" \t ");
	fx.command("AptMpChat::Send");
	CHECK(GadgetTextEntryGetText(entry).empty());
	GadgetTextEntrySetText(entry, u" hello ");
	fx.layer.gadgets().winSendSystemMsg(fx.screen->ownerWindow(), GEM_EDIT_DONE, msgData(entry), 0);
	CHECK(GadgetTextEntryGetText(entry).empty());
	CHECK(fx.note("unported-command"));
	CHECK(fx.wm.setExtern("AptMapPreview::GameMapType", "Strategic"));
	CHECK(fx.provider("AptMapPreview::GameMapType") == "Strategic");
	CHECK(std::stoi(fx.provider("MpGameRules::NumComboBoxes")) != openCombos);
	CHECK(fx.screen->slotGadget(6, "Team")->winIsHidden());
	CHECK(fx.wm.setExtern("AptMapPreview::GameMapType", "OpenPlay"));
	CHECK_FALSE(fx.screen->slotGadget(6, "Team")->winIsHidden());
}

TEST_CASE("coverage1: deferred start posts one deterministic message and ignores repeat")
{
	Fixture fx;
	fx.command("AptSkirmish::StartGame");
	CHECK(fx.games.messages.empty());
	fx.wm.update(33);
	REQUIRE(fx.games.messages.size() == 1);
	CHECK(fx.games.messages[0].game.seed == 4242);
	CHECK(fx.games.messages[0].game.mapName == fx.maps.entries[1].name);
	CHECK(fx.screen->profileState() == 11);
	CHECK(std::find(fx.wm.traces().begin(), fx.wm.traces().end(), "CloseMain") != fx.wm.traces().end());
	CHECK(fx.note("skirmish-random-unresolved"));
	fx.command("AptSkirmish::StartGame");
	fx.wm.update(33);
	CHECK(fx.games.messages.size() == 1);
	CHECK(fx.note("start-ignored"));
}

TEST_CASE("coverage1: rejecting too many players resets the start button")
{
	Fixture fx;
	fx.selectMap(fx.maps.entries[2].name); // a two-person map
	GameWindow *player = fx.screen->slotGadget(2, "Player");
	REQUIRE(player);
	choose(fx, player, SLOT_EASY_AI); // two-person map, three occupied slots
	fx.command("AptSkirmish::StartGame");
	CHECK(fx.games.messages.empty());
	fx.wm.update(33);
	CHECK(fx.games.messages.empty());
	CHECK(fx.screen->profileState() == 7);
	CHECK(fx.note("start-refused"));
	CHECK_FALSE(fx.games.refusals.empty());
	CHECK(std::find(fx.wm.traces().begin(), fx.wm.traces().end(), "ButtonReset") != fx.wm.traces().end());
}

TEST_CASE("coverage1: a map missing a start position hides every previously visible start spot and reports the preview error")
{
	Fixture fx;
	const auto &spots = fx.screen->currentMapSpots();
	REQUIRE(spots.size() == 8);
	CHECK_FALSE(spots[0]->winIsHidden());
	CHECK_FALSE(spots[1]->winIsHidden());
	fx.maps.entries[1].startPositions.erase(1);
	fx.selectMap(fx.maps.entries[1].name);
	CHECK(spots[0]->winIsHidden());
	CHECK(spots[1]->winIsHidden()); // the archive's P16 preview refuses the whole map (no partial preview)
	CHECK(fx.note("map-preview-error"));
}
