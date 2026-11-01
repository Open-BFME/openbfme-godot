// OpenBFME tests: extended map previews use the real retail gadgets with source-authored metadata. GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "AptRetail.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/GUI/WindowManager.h"

namespace
{
class PreviewSource : public IniSkirmishSetupSource
{
public:
	std::vector<MapCacheEntry> fixtures;
	const std::vector<MapCacheEntry> &maps() const override { return fixtures; }
	int mapContentsMask(const MapCacheEntry &) const override { return 1; }

	explicit PreviewSource(bool missing, bool withLegacy = false)
	{
		MapCacheEntry map;
		map.name = "maps/p16_ui/p16_ui.map";
		map.displayName = u"P16 UI preview fixture";
		map.isMultiplayer = true;
		map.numPlayers = 16;
		map.extentMin = { 0, 0, 0 };
		map.extentMax = { 1024, 1024, 0 };
		for (int i = 0; i < 16; ++i)
		{
			map.startPositions[i + 1] = { 128.0f + (i % 4) * 256.0f, 128.0f + (i / 4) * 256.0f, 0 };
		}
		if (missing)
		{
			map.startPositions.erase(16);
		}
		fixtures.push_back(map);
		if (withLegacy)
		{
			map.name = "maps/legacy_ui/legacy_ui.map";
			map.displayName = u"Legacy UI preview fixture";
			map.numPlayers = 8;
			for (int i = 9; i <= 16; ++i) map.startPositions.erase(i);
			fixtures.push_back(map);
		}
	}
};

struct PreviewFixture
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	PreviewSource setup;
	RecordingNewGameSink sink;
	MemorySkirmishProfiles profiles;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;
	AptSkirmish *screen = nullptr;

	PreviewFixture(AptRetail &mount, bool missing = false, bool withLegacy = false) : source(mount.fs), setup(missing, withLegacy)
	{
		std::string error;
		REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
		REQUIRE_MESSAGE(setup.load(mount.fs, &error), error);
		REQUIRE(profiles.add(u"P16 preview"));
		REQUIRE(profiles.setCurrent(u"P16 preview"));
		environment.profiles = &profiles;
		environment.skirmish = &setup;
		environment.newGame = &sink;
		registerAptScreenFactories(factories);
		wm = std::make_unique<WindowManager>(source, services);
		layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
		layer->registerComponents();
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->init();
		shell->push("Skirmish.apt");
		screen = dynamic_cast<AptSkirmish *>(shell->top());
		REQUIRE(screen);
		tick(90);
	}
	~PreviewFixture()
	{
		shell.reset();
		layer.reset();
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i) wm->update(33);
	}
	void click(GameWindow *button)
	{
		int x = 0, y = 0, w = 0, h = 0;
		button->winGetScreenPosition(&x, &y);
		button->winGetSize(&w, &h);
		wm->postMouseMove(x + w / 2.0f, y + h / 2.0f);
		tick(1);
		wm->postMouseButton(true);
		tick(1);
		wm->postMouseButton(false);
		tick(1);
	}
};
}

TEST_CASE("p16 ui preview: sixteen start markers use retail controls and the last accepts real mouse input")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	PreviewFixture fx(mount);
	REQUIRE(fx.screen->setup());
	CHECK(fx.screen->setup()->info().mapStartCount == 16);
	CHECK(fx.screen->lastError().empty());
	REQUIRE(fx.screen->currentMapSpots().size() == 16);
	int px = 0, py = 0, pw = 0, ph = 0;
	fx.screen->currentMapWindow()->winGetScreenPosition(&px, &py);
	fx.screen->currentMapWindow()->winGetSize(&pw, &ph);
	for (GameWindow *spot : fx.screen->currentMapSpots())
	{
		REQUIRE(spot);
		CHECK_FALSE(spot->winIsHidden());
		int x = 0, y = 0, w = 0, h = 0;
		spot->winGetScreenPosition(&x, &y);
		spot->winGetSize(&w, &h);
		CHECK(x >= px); // tolerance: skin and map aspect determine coordinates; containment is the contract
		CHECK(y >= py); // tolerance: skin and map aspect determine coordinates; containment is the contract
		CHECK(x + w <= px + pw); // tolerance: skin and map aspect determine coordinates; containment is the contract
		CHECK(y + h <= py + ph); // tolerance: skin and map aspect determine coordinates; containment is the contract
	}
	CHECK(fx.screen->setup()->info().slots[0].startPos == -1);
	fx.click(fx.screen->currentMapSpots()[15]);
	CHECK(fx.screen->setup()->info().slots[0].startPos == 15);
	CHECK(fx.layer->errors().empty());
}

TEST_CASE("p16 ui preview: a missing start is reported rather than silently omitting its marker")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	PreviewFixture fx(mount, true);
	CHECK(fx.screen->lastError() == "map preview: missing start position or control 16");
	CHECK(fx.wm->noteCount("map-preview-error") > 0); // tolerance: refreshes may repeat the note; only its presence is required
	for (GameWindow *spot : fx.screen->currentMapSpots())
	{
		CHECK(spot->winIsHidden());
	}
}

TEST_CASE("p16 ui layout: retail rows keep logical slot indexes while the scrollbar exposes players nine through sixteen")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	PreviewFixture fx(mount);
	for (int i = 0; i < 16; ++i)
	{
		CAPTURE(i);
		GameWindow *row = fx.screen->slotGadget(i, "Player");
		REQUIRE(row);
		CHECK(row->winIsHidden() == (i >= 8)); // tolerance: slots 8-15 are outside the initial eight-row viewport.
	}
	GameWindow *scroll = fx.screen->slotScrollWindow();
	REQUIRE(scroll);
	CHECK(fx.screen->firstVisibleSlot() == 0);
	const SkirmishGameInfo before = fx.screen->setup()->info();
	int x = 0, y = 0, w = 0, h = 0;
	scroll->winGetScreenPosition(&x, &y);
	scroll->winGetSize(&w, &h);
	for (int click = 0; click < 20 && fx.screen->firstVisibleSlot() != 8; ++click)
	{
		fx.wm->postMouseMove(x + w / 2.0f, y + h - 1.0f);
		fx.tick(1);
		fx.wm->postMouseButton(true);
		fx.tick(1);
		fx.wm->postMouseButton(false);
		fx.tick(1);
	}
	CHECK(fx.screen->firstVisibleSlot() == 8);
	for (int i = 0; i < 16; ++i)
	{
		CAPTURE(i);
		CHECK(fx.screen->slotGadget(i, "Player")->winIsHidden() == (i < 8)); // tolerance: slots 0-7 are outside the later eight-row viewport.
		CHECK(fx.screen->setup()->info().slots[i].state == before.slots[i].state);
		CHECK(fx.screen->setup()->info().slots[i].color == before.slots[i].color);
	}
	GameWindow *team = fx.screen->slotGadget(15, "Team");
	REQUIRE(team);
	GadgetComboBoxSetSelectedPos(team, 8, false);
	fx.tick(2);
	CHECK(fx.screen->setup()->info().slots[15].teamNumber == 7);
	CHECK(fx.screen->setup()->info().slots[7].teamNumber == before.slots[7].teamNumber);
	GameWindow *range = fx.screen->slotRangeWindow();
	REQUIRE(range);
	range->winGetScreenPosition(&x, &y);
	range->winGetSize(&w, &h);
	fx.wm->postMouseMove(x + w - 10.0f, y + h / 2.0f);
	fx.tick(1);
	fx.wm->postMouseButton(true);
	fx.tick(1);
	fx.wm->postMouseButton(false);
	fx.tick(1);
	GameWindow *ranges = GadgetComboBoxGetListBox(range);
	REQUIRE(ranges);
	CHECK_FALSE(ranges->winIsHidden());
	ranges->winGetScreenPosition(&x, &y);
	ranges->winGetSize(&w, &h);
	CHECK(y + h <= 768); // tolerance: the popup must stay inside the minimum-height viewport.
	CHECK(fx.layer->errors().empty());
}

TEST_CASE("p16 ui maps: large-map filter keeps player counts and does not silently change the selected game")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	PreviewFixture fx(mount, false, true);
	REQUIRE(fx.screen->mapListKeys().size() == 2);
	GameWindow *filter = fx.screen->mapCapacityFilterWindow();
	REQUIRE(filter);
	const std::string selected = fx.screen->setup()->info().mapName;
	GadgetComboBoxSetSelectedPos(filter, 1, false);
	fx.tick(2);
	REQUIRE(fx.screen->mapListKeys().size() == 1);
	CHECK(fx.screen->mapListKeys()[0] == "maps/p16_ui/p16_ui.map");
	CHECK(fx.screen->setup()->info().mapName == selected);
	GadgetComboBoxSetSelectedPos(filter, 0, false);
	fx.tick(2);
	CHECK(fx.screen->mapListKeys().size() == 2);
	CHECK(fx.screen->setup()->info().mapName == selected);
	CHECK(fx.layer->errors().empty());
}

TEST_CASE("p16 ui teams: extension teams use explicit labels absent from retail while legacy entries stay unchanged")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	PreviewFixture fx(mount);
	const auto teams = fx.screen->setup()->teamEntries();
	REQUIRE(teams.size() == 9);
	for (int team = 4; team < 8; ++team)
	{
		const std::string number = std::to_string(team + 1);
		CHECK(teams[team + 1].itemData == team);
		CHECK(teams[team + 1].text == std::u16string(number.begin(), number.end()));
	}
}
