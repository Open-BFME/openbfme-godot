// Synthetic, no-game coverage of the InGameHud entry points. GPL-3.0.
#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "IniTestUtil.h"
#include "MapTestUtil.h"

#include "GameClient/InGameHud.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/LiveGame.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/MessageStream/MetaEvent.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace maptest;
using namespace apttest;

// The game is deliberately a bare 16x16 map with only the default neutral player,
// no objects, retail mounts, scripted events or renderer. Inputs use an in-memory BIG.
// The INI values are only the mandatory settings needed by LiveGame::load; they are
// not purported to be values extracted from a retail installation.
const char kGameData[] = R"(
GameData
 ForceModelsToFollowTimeOfDay = No
 ForceModelsToFollowWeather = No
 UnitDamagedThreshold = 0.5
 UnitReallyDamagedThreshold = 0.25
 DefaultStartingCash = 100
 TerrainResourceCellSize = 10
 GoodCommandPoints = 10 100
 EvilCommandPoints = 10 100
 GoodCommandPointsAI = 10 100
 EvilCommandPointsAI = 10 100
 GoodCommandPointsBonus = 0
 EvilCommandPointsBonus = 0
 GoodCommandPointLimit = 100
 EvilCommandPointLimit = 100
 PowerLimit = 100
 ResourceBonusMultiplier = 1
 ResourceMultiplierLimit = 1
 MultiPlayMoneyMult = MP1:1.0
 GoodCommandPointsMP2 = 10 100
 GoodCommandPointsMP3 = 10 100
 GoodCommandPointsMP4 = 10 100
 GoodCommandPointsMP5 = 10 100
 GoodCommandPointsMP6 = 10 100
 GoodCommandPointsMP7 = 10 100
 GoodCommandPointsMP8 = 10 100
 EvilCommandPointsMP2 = 10 100
 EvilCommandPointsMP3 = 10 100
 EvilCommandPointsMP4 = 10 100
 EvilCommandPointsMP5 = 10 100
 EvilCommandPointsMP6 = 10 100
 EvilCommandPointsMP7 = 10 100
 EvilCommandPointsMP8 = 10 100
 PartitionCellSize = 10
 UnlookPersistDuration = 1000
 ShroudColor = R:0 G:0 B:0
 FogAlpha = 100
 ShroudAlpha = 200
 StealthFriendlyOpacity = 50%
 MaxPathfindCellsPerFrame = 100
 MaxCellsAdjustDestination = 100
 MaxCellsAdjustHordeMeleeDestination = 100
 MaxCellsPatchPath = 100
 MaxCellsFindPathLimit = 100
 MaxCellsFindAttackPath = 100
 MaxCellsAdjustToMeleeDestination = 100
 MaxCellsFindMeleeEngagementLocation = 100
 MaxCellsAdjustToPossibleDestination = 100
 MaxCellsToExamineTowardsGoal = 100
 MovementPenaltyDamageState = DAMAGED
End
)";
const char kAIData[] = R"(
AIData
 EnableRepulsors = No
 RepulsedDistance = 1
 WadeWaterDepth = 1
 DeepWaterDepth = 2
 HordesWaitForHordes = No
 MeleeApproachTolerance = 1
 MeleeApproachDist = 1
 CastleSiegeStandBackDistance = 1
 AttackUsesLineOfSight = No
End
)";
const char kMultiplayer[] = R"(
MultiplayerSettings
 InitialCreditsVeryLow = 100
 InitialCreditsLow = 100
 InitialCreditsMedium = 100
 InitialCreditsHigh = 100
 InitialCreditsVeryHigh = 100
 UseShroud = No
End
)";

std::string tinyMap()
{
 Toc t;
 W h;
 h.i32(16).i32(16).i32(0).i32(1).i32(16).i32(16).i32(256);
 for (int i = 0; i < 256; ++i) h.u16(0);
 const Bytes chunks = cat(chunk(t, "HeightMapData", 5, h.b), chunk(t, "WorldInfo", 1, D(t).i("weather", 0).bytes()));
 const Bytes map = file(t, chunks);
 return std::string(map.begin(), map.end());
}

struct HudFx
{
 initest::Fixture files;
 RetailObjectWorld world;
 ArchiveW3DFileSource w3dSource;
 WW3DAssetManager assets;
 LiveGame game;
 PinholeView view;
 MetaMap meta;
 MemorySource movies;
 RecordingShellServices services;
 ShellEnvironment environment;
 AptScreenFactoryTable factories;
 WindowManager windows;
 Shell shell;
 std::unique_ptr<InGameHud> hud;
 std::string error;

 explicit HudFx(bool withRadar = false)
  : world(files.fsys), w3dSource(files.fsys), assets(w3dSource), game(world, files.fsys, assets, MapObjectOptions{}),
    windows(movies, services), shell(windows, factories, services, environment)
 {
  files.mount({ { "Data\\INI\\Default\\SoundEffects.ini", "AudioEvent DefaultSoundEffect\n Volume = 100\n Type = ui everyone\n Type = +DEFAULT\nEnd\n" },
   { "Maps\\synthetic\\synthetic.map", tinyMap() },
   { "Data\\INI\\GameData.ini", kGameData },
   { "Data\\INI\\Default\\AIData.ini", kAIData },
   { "Data\\INI\\Multiplayer.ini", kMultiplayer },
   { "Data\\INI\\InGameUI.ini", "InGameUI\n SelectNearestBuilderCycleTimeOut = 1000\nEnd\n" },
   { "Data\\Scripts\\Scripts.lua", "-- Synthetic map: no scripted events.\n" },
   { "Data\\Scripts\\ScriptEvents.xml", "<SageLuaScriptSection>\n</SageLuaScriptSection>\n" } });
  if (!world.load(&error)) return;
  LiveGame::Options opts;
  opts.mapName = "synthetic";
  opts.mapScripts = 0;
  if (!game.load(opts, &error)) return;
  TestMovie level0;
  level0.setRootFrames({ {} });
  movies.add("AptLevel0", level0);
  // A real synthetic known-screen movie, not a missing-movie test: the first
  // frame initializes the Palantir, the second presses its store button.
  TestMovie pal;
  const auto first = program(pal, [](Asm &a) { fscmd(a, "AptPalantir::OnInitialized"); });
  const auto second = program(pal, [](Asm &a) { fscmd(a, "AptPalantir::OnBttnSpellStore"); });
  std::vector<std::uint32_t> opening = { pal.addActionItem(first) };
  if (withRadar)
  {
   // A native radar placeholder with synthetic authored bounds. Its construct
   // action uses the same _type protocol as the movie, without retail art.
   const auto box = pal.addCharacter(pal.addShape(0.0f, 0.0f, 200.0f, 100.0f, 1));
   const auto sprite = pal.addCharacter(pal.addSprite({ { pal.addPlaceItem(placeChar(box, 1)) } }));
   auto radar = placeChar(sprite, 1, "Radar");
   radar.flags |= APT_PLACE_HASMATRIX | APT_PLACE_HASCLIPACTION;
   radar.translation[0] = 100.0f;
   radar.translation[1] = 50.0f;
   const auto tag = program(pal, [](Asm &a) { a.pushString("_type").setStringVar("AptPalantir::RenderRadar"); });
   radar.events.push_back({ APT_CLIP_CONSTRUCT, 0, tag });
   opening.push_back(pal.addPlaceItem(radar));
  }
  pal.setRootFrames({ opening, { pal.addActionItem(second) } });
  movies.add("Palantir", pal);
  factories.registerFactory("Palantir.apt", [](AptScreenContext &c) {
   return std::make_unique<AptPalantir>(c.windows, c.shell);
  });
  windows.init();
  shell.push("Palantir.apt");
  // Loading runs frame zero's initialization action. Do not advance to the
  // store-button frame until boot has installed the real HUD callbacks.
  windows.setMinimumFrameTime(false);
  windows.update(0);
  hud = std::make_unique<InGameHud>(InGameHud::Config{ game, world, files.fsys, services, view, MouseSettings{}, meta, nullptr, &windows, &shell });
 }
 bool ready() const { return hud != nullptr; }
 bool boot() { return hud->boot(&error); }
 void tick() { windows.update(33); }
};

bool hasError(const InGameHud &hud, const std::string &word)
{
 const auto &errors = hud.errors();
 return std::any_of(errors.begin(), errors.end(), [&word](const std::string &s) { return s.find(word) != std::string::npos; });
}
} // namespace

TEST_CASE("coverage1: drag-selection rectangle follows the hint and disappears at release")
{
 InGameUI ui;
 IconUIOp op;
 CHECK_FALSE(InGameHud::selectionRegionOp(ui, op));
 IRegion2D rect{};
 rect.lo.x = 17; rect.lo.y = 23; rect.hi.x = 40; rect.hi.y = 52;
 ui.setAreaSelectHint(rect);
 REQUIRE(InGameHud::selectionRegionOp(ui, op));
 CHECK(op.kind == IconUIOp::OPEN_RECT);
 CHECK(op.x == 17.0f); CHECK(op.y == 23.0f);
 CHECK(op.w == 23.0f); CHECK(op.h == 29.0f);
 CHECK(op.width == 2.0f); CHECK(op.color == 0xBBFFBB33u);
 ui.endAreaSelectHint();
 CHECK_FALSE(InGameHud::selectionRegionOp(ui, op));
}

TEST_CASE("coverage1: synthetic Palantir movie initializes and presses the spell-store handler")
{
 HudFx f;
 REQUIRE_MESSAGE(f.ready(), f.error);
 REQUIRE_MESSAGE(f.boot(), f.error);
 REQUIRE(f.hud->palantir() != nullptr);
 CHECK(f.hud->palantir()->initialized());
 CHECK(f.hud->spellStoreRequests() == 0);
 f.tick();
 CHECK(f.hud->spellStoreRequests() == 1);
 CHECK(f.hud->spellStore() == nullptr);
 CHECK(f.hud->spellStoreError() == "the player has no PlayerTemplate"); // real movie handler rejects the neutral player
}

TEST_CASE("coverage1: boot rejects an absent shell Palantir rather than silently owning a new screen")
{
 HudFx f;
 REQUIRE_MESSAGE(f.ready(), f.error);
 f.shell.popImmediate();
 CHECK_FALSE(f.hud->boot(&f.error));
 CHECK(f.error.find("Palantir.apt") != std::string::npos);
 CHECK(hasError(*f.hud, "Palantir.apt"));
 CHECK(f.hud->palantir() == nullptr);
}

TEST_CASE("coverage1: script input disable and enable hide the pointer and restore the input gate")
{
 HudFx f;
 REQUIRE_MESSAGE(f.ready(), f.error);
 REQUIRE_MESSAGE(f.boot(), f.error);
 CHECK(f.hud->input().ui().getInputEnabled());
 f.hud->scriptInput(false);
 CHECK_FALSE(f.hud->input().ui().getInputEnabled());
 CHECK(f.hud->scriptMouseHidden());
 f.hud->scriptInput(true);
 CHECK(f.hud->input().ui().getInputEnabled());
 CHECK_FALSE(f.hud->scriptMouseHidden());
}

TEST_CASE("coverage1: modal mouse routing prevents a world drag and releases the modal gate")
{
 HudFx f;
 REQUIRE_MESSAGE(f.ready(), f.error);
 REQUIRE_MESSAGE(f.boot(), f.error);
 f.hud->setWindowSize(640, 480);
 f.hud->setModal(true);
 CHECK(f.hud->modal());
 f.hud->mouseMove(120, 120, 0);
 f.hud->mouseButton(HudInput::Button::Left, true, 120, 120, 0, 1000);
 f.hud->mouseMove(420, 310, 0);
 f.hud->mouseButton(HudInput::Button::Left, false, 420, 310, 0, 1010);
 CHECK(f.hud->input().stream().pending() == 0); // no world events reached the input stream
 f.hud->update(0.0);
 CHECK_FALSE(f.hud->input().ui().isSelecting());
 IconUIOp op;
 CHECK_FALSE(f.hud->selectionRegionOp(op));
 f.hud->setModal(false);
 CHECK_FALSE(f.hud->modal());
 f.hud->mouseMove(410, 300, 0);
 CHECK(f.hud->input().stream().pending() == 1); // the world receives the pointer again
 f.hud->update(0.0);
 CHECK(f.hud->input().stream().pending() == 0);
}

TEST_CASE("coverage1: modal keys do not become held world movement keys when the menu closes")
{
 HudFx f;
 REQUIRE_MESSAGE(f.ready(), f.error);
 REQUIRE_MESSAGE(f.boot(), f.error);
 constexpr int kMovementKey = KEY_UP; // the world's upward-scroll key (LookAtTranslator)
 f.hud->key(kMovementKey, 0);
 REQUIRE(f.hud->input().isKeyDown(kMovementKey));
 f.hud->setModal(true);
 REQUIRE_FALSE(f.hud->input().isKeyDown(kMovementKey));
 // Regression: previously key() forwarded this press into HudInput even
 // while modal; closing the menu left the game's movement key held down.
 f.hud->key(kMovementKey, 0);
 CHECK_FALSE(f.hud->input().isKeyDown(kMovementKey));
 f.hud->setModal(false);
 CHECK_FALSE(f.hud->input().isKeyDown(kMovementKey));
 f.hud->key(kMovementKey, 0);
 CHECK(f.hud->input().isKeyDown(kMovementKey));
 f.hud->key(kMovementKey, KEY_STATE_UP);
 CHECK_FALSE(f.hud->input().isKeyDown(kMovementKey));
}

TEST_CASE("coverage1: native radar maps window pixels and gates view movement until radar and input are enabled")
{
 SUBCASE("absent clip and malformed window size")
 {
  HudFx f;
  REQUIRE_MESSAGE(f.ready(), f.error);
  REQUIRE_MESSAGE(f.boot(), f.error);
  f.hud->setWindowSize(0, -100);
  CHECK(f.hud->windowWidth() == 1);
  CHECK(f.hud->windowHeight() == 1);
  float square[4] = {};
  CHECK_FALSE(f.hud->radarSquare(square));
  bool point = true;
  float wx = 77.0f, wy = 88.0f;
  CHECK_FALSE(f.hud->radarPoint(0, 0, point, wx, wy));
  CHECK_FALSE(point);
  CHECK(wx == 77.0f); CHECK(wy == 88.0f);
  CHECK_FALSE(f.hud->radarClick(HudInput::Button::Left, 0, 0, 0));
 }
 SUBCASE("native placeholder and script-input gate")
 {
  HudFx f(true);
  REQUIRE_MESSAGE(f.ready(), f.error);
  REQUIRE_MESSAGE(f.boot(), f.error);
  f.hud->setWindowSize(2048, 1536);
  float square[4] = {};
  REQUIRE(f.hud->radarSquare(square));
  CHECK(square[0] == 300.0f); CHECK(square[1] == 100.0f);
  CHECK(square[2] == 500.0f); CHECK(square[3] == 300.0f);
  bool point = true;
  float wx = -1.0f, wy = -1.0f;
  CHECK(f.hud->radarPoint(400, 200, point, wx, wy));
  CHECK_FALSE(point); // the clip is present, but radar is not yet forced on
  CHECK(wx == -1.0f); CHECK(wy == -1.0f);
  f.hud->update(1.0 / 30.0);
  REQUIRE(f.hud->radarPoint(400, 200, point, wx, wy));
  REQUIRE(point);
  CHECK(wx == f.hud->radar().extentWidth() * 0.5f);
  CHECK(wy == f.hud->radar().extentHeight() * 0.5f);
  CHECK(f.hud->radarClick(HudInput::Button::Left, 400, 200, 1));
  CHECK(f.view.position().x == wx); CHECK(f.view.position().y == wy);
  f.hud->scriptInput(false);
  CHECK(f.hud->radarClick(HudInput::Button::Left, 300, 100, 2));
  CHECK(f.view.position().x == wx); CHECK(f.view.position().y == wy);
  f.hud->scriptInput(true);
  CHECK(f.hud->radarClick(HudInput::Button::Left, 300, 100, 3));
  CHECK(f.view.position().x == 0.0f);
  CHECK(f.view.position().y == f.hud->radar().extentHeight());
  CHECK_FALSE(f.hud->radarPoint(299, 100, point, wx, wy));
  CHECK_FALSE(point);
 }
}

TEST_CASE("coverage1: neutral player without a template rejects direct store and spell presses without ghost purchases")
{
 HudFx f;
 REQUIRE_MESSAGE(f.ready(), f.error);
 REQUIRE_MESSAGE(f.boot(), f.error);
 std::string why;
 CHECK_FALSE(f.hud->openSpellStore(&why));
 CHECK(why == "the player has no PlayerTemplate");
 CHECK(f.hud->spellStore() == nullptr);
 CHECK_FALSE(f.hud->pressSpell(-1));
 CHECK_FALSE(f.hud->pressSpell(999));
 f.hud->closeSpellStore();
 CHECK(f.hud->spellStore() == nullptr);
}

TEST_CASE("coverage1: missing command store reports the rejected spell-store request")
{
 HudFx f;
 REQUIRE_MESSAGE(f.ready(), f.error);
 REQUIRE_MESSAGE(f.boot(), f.error);
 const auto context = f.hud->enterContext();
 CommandStore *saved = TheCommandStore;
 TheCommandStore = nullptr;
 std::string why = "unchanged";
 const bool opened = f.hud->openSpellStore(&why);
 TheCommandStore = saved; // restore the real world context before any assertion can throw
 CHECK_FALSE(opened);
 CHECK(why == "the spell store needs a command store");
 CHECK(f.hud->spellStore() == nullptr);
}

TEST_CASE("coverage1: HUD update builds decals once then reuses them until state changes")
{
 HudFx f;
 REQUIRE_MESSAGE(f.ready(), f.error);
 REQUIRE_MESSAGE(f.boot(), f.error);
 CHECK(f.hud->selectionDecalsVersion() == 0);
 f.hud->update(1.0 / 30.0);
 CHECK(f.hud->selectionDecalsVersion() == 1);
 CHECK(f.hud->selectionDecals().empty());
 f.hud->update(1.0 / 30.0);
 CHECK(f.hud->selectionDecalsVersion() == 1);
 auto settings = f.hud->selectionDecalSettings();
 settings.showSelectedUnitMarker = !settings.showSelectedUnitMarker;
 f.hud->setSelectionDecalSettings(settings);
 f.hud->update(1.0 / 30.0);
 CHECK(f.hud->selectionDecalsVersion() == 2);
 f.hud->update(1.0 / 30.0);
 CHECK(f.hud->selectionDecalsVersion() == 2);
 CHECK(f.hud->skippedUpdates() == 0);
}
