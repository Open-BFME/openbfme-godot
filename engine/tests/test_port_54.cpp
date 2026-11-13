// PORT-54: synthetic CostModifierUpgrade and Palantir movie, no game files.
#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "EconTestUtil.h"
#include "IniTestUtil.h"
#include "MapTestUtil.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/InGameHud.h"
#include "GameClient/LiveGame.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/PalantirCommandUI.h"
#include "GameLogic/Module/UpgradeModules.h"
#include "GameLogic/Object/Object.h"

using namespace apttest;
namespace
{
const char objects[] = R"(
Object SyntheticForge
 KindOf = SELECTABLE STRUCTURE
 Behavior = CostModifierUpgrade Tag_Discount
  StartsActive = Yes
  UpgradeDiscount = Yes
  LabelForPalantirString = TEST:Discount
  Percentage = -10%
  Percentage = -20%
 End
End
Object SyntheticHearth
 KindOf = SELECTABLE STRUCTURE
 Behavior = CostModifierUpgrade Tag_Cost
  StartsActive = Yes
  ObjectFilter = ANY +INFANTRY
  LabelForPalantirString = TEST:Cost
  Percentage = -4%
  Percentage = -8%
 End
End
Object SyntheticOtherFilter
 KindOf = SELECTABLE STRUCTURE
 Behavior = CostModifierUpgrade Tag_Other
  StartsActive = Yes
  ObjectFilter = ANY +CAVALRY
  Percentage = -40%
 End
End
Object SyntheticUnspecified
 KindOf = SELECTABLE STRUCTURE
 Behavior = CostModifierUpgrade Tag_NoLabel
  UpgradeDiscount = Yes
 End
End
Object SyntheticPlain
 KindOf = SELECTABLE STRUCTURE
End
Object SyntheticUnit
 KindOf = SELECTABLE INFANTRY
 Behavior = CostModifierUpgrade Tag_Unit
  StartsActive = Yes
  UpgradeDiscount = Yes
  LabelForPalantirString = TEST:Discount
  Percentage = -10%
 End
End
)";

struct Fixture
{
 econtest::Fx game{objects};
 PinholeView view;
 InGameUI ui;
 MessageStream stream;
 HudContext context{*game.logic, nullptr, view, ui, stream, nullptr, {}, {}};
 ControlBar bar{context};
 MemorySource movies;
 RecordingShellServices services;
 ShellEnvironment environment;
 AptScreenFactoryTable factories;
 WindowManager windows{movies, services};
 Shell shell{windows, factories, services, environment};
 int shows = 0, hides = 0;
 Fixture()
 {
  game.players.setLocalPlayer(game.player("Alice"));
  ui.setLogic(game.logic.get());
  ui.setMaxSelectCount(50);
  TestMovie level0;
  level0.setRootFrames({{} });
  movies.add("AptLevel0", level0);
  TestMovie pal;
  auto actions = program(pal, [](Asm &a) {
   for (const char *name : {"SetPalantirFrameState", "SetPlayerButtonsState", "EnablePlayerMagicButton", "SetPlayerFaction", "ShowCommandInterface", "HideCommandInterface", "HideRankInterface"})
    a.defineFunction(name, {}, [](Asm &) {});
   a.defineFunction("ShowCostModifierUpgradeInterface", {}, [](Asm &f) { fscmd(f, "Test:ShowCost"); });
   a.defineFunction("HideCostModifierUpgradeInterface", {}, [](Asm &f) { fscmd(f, "Test:HideCost"); });
   fscmd(a, "AptPalantir::OnInitialized");
  });
  pal.setRootFrames({{pal.addActionItem(actions)}});
  movies.add("Palantir", pal);
  factories.registerFactory("Palantir.apt", [](AptScreenContext &c) { return std::make_unique<AptPalantir>(c.windows, c.shell); });
  windows.init();
  windows.registerCommand("Test:ShowCost", [this](const std::string &) { ++shows; });
  windows.registerCommand("Test:HideCost", [this](const std::string &) { ++hides; });
  shell.push("Palantir.apt", true);
  windows.update(33);
  shell.update();
 }
 AptPalantir *palantir() { return dynamic_cast<AptPalantir *>(shell.top()); }
 Object *make(const char *name, float x)
 {
  Object *o = game.makeAt(name, "Alice", x, x);
  o->friend_onBuildComplete(); // RW 0x8B9D0F: activate StartsActive modules
  return o;
 }
 PalantirCommandUI::CostModifierInfo info(Object *o) { return PalantirCommandUI::costModifierInfo(context, o->getID()); }
 void select(Object *o)
 {
  ui.deselectAll(false);
  ui.selectObject(o->getID());
  const auto cost = PalantirCommandUI::costModifierInfo(context, ui.firstSelected());
  AptPalantir::LocalState state;
  state.context = true;
  state.contextObject = ui.firstSelected();
  state.costVisible = cost.visible;
  state.costUpgradeDiscount = cost.upgradeDiscount;
  state.costAmount = cost.amount;
  // Synthetic GameText values; this is the same data passed by InGameHud::update.
  if (cost.label == "TEST:Discount") state.costLabelFormat = "Discount %d%%";
  if (cost.label == "TEST:Cost") state.costLabelFormat = "Cost %d%%";
  if (cost.label == ">UNSPECIFIED<") state.costLabelFormat = "MISSING: '>UNSPECIFIED<'";
  palantir()->setLocalState(state);
  palantir()->sync(bar);
 }
};
} // namespace

TEST_CASE("PORT-54 selected upgrade discount is formatted into the Palantir section")
{
 Fixture f;
 REQUIRE(f.palantir());
 REQUIRE(f.palantir()->initialized());
 Object *plain = f.make("SyntheticPlain", 1);
 f.select(plain);
 CHECK(f.windows.aptText("APT:CostModifierUpgrade") == nullptr); // RW 0x92FC25: no module
 CHECK(f.shows == 0);
 Object *forge = f.make("SyntheticForge", 2);
 f.select(forge);
 REQUIRE(f.windows.aptText("APT:CostModifierUpgrade"));
 CHECK(*f.windows.aptText("APT:CostModifierUpgrade") == "Discount -10%"); // RW 0x92F735, 0x92FCCD
 CHECK(f.shows == 1);
 f.make("SyntheticForge", 3);
 f.select(forge);
 CHECK(*f.windows.aptText("APT:CostModifierUpgrade") == "Discount -20%"); // RW 0x6B2D20, 0x92F73A
 CHECK(f.shows == 1);
 f.select(plain);
 CHECK(f.hides == 1); // RW 0x92F9B2 on context change
}

TEST_CASE("PORT-54 object filters, missing labels and the structure gate follow RW")
{
 Fixture f;
 Object *hearth = f.make("SyntheticHearth", 1);
 f.make("SyntheticOtherFilter", 2);
 f.make("SyntheticHearth", 3);
 const auto cost = f.info(hearth);
 CHECK(cost.visible);
 CHECK_FALSE(cost.upgradeDiscount);
 CHECK(cost.amount == 8); // RW 0x6AD9A9–0x6AD9D0: equal filter IDs only, then index 1
 f.select(hearth);
 REQUIRE(f.windows.aptText("APT:CostModifierUpgrade"));
 CHECK(*f.windows.aptText("APT:CostModifierUpgrade") == "Cost 8%");
 Object *noLabel = f.make("SyntheticUnspecified", 4);
 const auto unspecified = f.info(noLabel);
 CHECK(unspecified.visible); // RW 0x92FBAB tests module presence, not whether it executed
 CHECK(unspecified.label == ">UNSPECIFIED<"); // RW 0x8B9E0D
 f.select(noLabel);
 CHECK(*f.windows.aptText("APT:CostModifierUpgrade") == "MISSING: '>UNSPECIFIED<'");
 Object *unit = f.make("SyntheticUnit", 5);
 CHECK_FALSE(f.info(unit).visible); // RW 0x92FBC0: not STRUCTURE
 f.select(unit);
 CHECK(f.hides == 2);
}

// Minimal in-memory map and settings for the actual InGameHud update path.
namespace
{
using namespace maptest;
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


std::string hudMap()
{
 Toc t; W h;
 h.i32(16).i32(16).i32(0).i32(1).i32(16).i32(16).i32(256);
 for (int i = 0; i < 256; ++i) h.u16(0);
 D side(t); side.s("playerName", "Player_1").b("playerIsHuman", true);
 W sl; sl.u8(1).i32(1).raw(side.bytes()).i32(0);
 D team(t); team.s("teamName", "teamPlayer_1").s("teamOwner", "Player_1").b("teamIsSingleton", true);
 Bytes chunks = cat(chunk(t, "HeightMapData", 5, h.b), chunk(t, "WorldInfo", 1, D(t).i("weather", 0).bytes()));
 chunks = cat(chunks, chunk(t, "SidesList", 6, sl.b));
 chunks = cat(chunks, chunk(t, "Teams", 1, W().i32(1).raw(team.bytes()).b));
 Bytes map = file(t, chunks);
 return std::string(map.begin(), map.end());
}

struct HudText : GameTextSource
{
 bool fetch(const std::string &label, std::u16string &out) const override
 {
  if (label != "TEST:Discount") return false;
  out = u"Discount %d%%";
  return true;
 }
};
struct HudFixture
{
 initest::Fixture files; RetailObjectWorld world; ArchiveW3DFileSource w3dSource; WW3DAssetManager assets;
 MapObjectOptions mapOptions; LiveGame game; PinholeView view; MetaMap meta;
 MemorySource movies; RecordingShellServices services; ShellEnvironment environment; AptScreenFactoryTable factories;
 WindowManager windows; Shell shell; HudText text; std::unique_ptr<InGameHud> hud; std::string error;
 int shows = 0, hides = 0;
 HudFixture() : world(files.fsys), w3dSource(files.fsys), assets(w3dSource), game(world, files.fsys, assets, mapOptions),
  windows(movies, services), shell(windows, factories, services, environment)
 {
  files.mount({ { "Data\\INI\\Default\\SoundEffects.ini", "AudioEvent DefaultSoundEffect\n Volume = 100\n Type = ui everyone\n Type = +DEFAULT\nEnd\n" },
   { "Data\\INI\\Default\\SubsystemLegendExpansion1.ini", "LoadSubsystem TheThingFactory\n InitFile = Data\\INI\\SyntheticObjects.ini\nEnd\n" },
   { "Data\\INI\\SyntheticObjects.ini", objects },
   { "Maps\\synthetic\\synthetic.map", hudMap() },
   { "Data\\INI\\GameData.ini", kGameData }, { "Data\\INI\\Default\\AIData.ini", kAIData },
   { "Data\\INI\\Multiplayer.ini", kMultiplayer },
   { "Data\\INI\\InGameUI.ini", "InGameUI\n SelectNearestBuilderCycleTimeOut = 1000\nEnd\n" },
   { "Data\\Scripts\\Scripts.lua", "-- no scripts\n" }, { "Data\\Scripts\\ScriptEvents.xml", "<SageLuaScriptSection>\n</SageLuaScriptSection>\n" } });
  REQUIRE_MESSAGE(world.load(&error), error);
  LiveGame::Options opts; opts.mapName = "synthetic"; opts.mapScripts = 0;
  REQUIRE_MESSAGE(game.load(opts, &error), error);
  TestMovie level0; level0.setRootFrames({{}}); movies.add("AptLevel0", level0);
  TestMovie pal; auto actions = program(pal, [](Asm &a) {
   for (const char *name : {"SetPalantirFrameState", "SetPlayerButtonsState", "EnablePlayerMagicButton", "SetPlayerFaction", "ShowCommandInterface", "HideCommandInterface", "HideRankInterface"})
    a.defineFunction(name, {}, [](Asm &) {});
   a.defineFunction("ShowCostModifierUpgradeInterface", {}, [](Asm &f) { fscmd(f, "Test:HudShowCost"); });
   a.defineFunction("HideCostModifierUpgradeInterface", {}, [](Asm &f) { fscmd(f, "Test:HudHideCost"); });
   fscmd(a, "AptPalantir::OnInitialized");
  });
  pal.setRootFrames({{pal.addActionItem(actions)}}); movies.add("Palantir", pal);
  factories.registerFactory("Palantir.apt", [](AptScreenContext &c) { return std::make_unique<AptPalantir>(c.windows, c.shell); });
  windows.init();
  windows.registerCommand("Test:HudShowCost", [this](const std::string &) { ++shows; });
  windows.registerCommand("Test:HudHideCost", [this](const std::string &) { ++hides; });
  shell.push("Palantir.apt", true); windows.setMinimumFrameTime(false); windows.update(0);
  environment.gameText = &text;
  hud = std::make_unique<InGameHud>(InGameHud::Config{ game, world, files.fsys, services, view, MouseSettings{}, meta, &text, &windows, &shell });
  REQUIRE_MESSAGE(hud->boot(&error), error);
 }
};
}

TEST_CASE("PORT-54 InGameHud selects and localizes the active discount")
{
 HudFixture f;
 Player *local = f.game.players().getLocalPlayer();
 REQUIRE(local); REQUIRE(local->getDefaultTeam());
 Object *plain = f.game.createObject("SyntheticPlain", local->getPlayerIndex(), Coord3D{10, 10, 0}, 0, &f.error);
 REQUIRE_MESSAGE(plain, f.error);
 f.hud->input().ui().selectObject(plain->getID());
 f.hud->update(0.033f);
 CHECK(f.windows.aptText("APT:CostModifierUpgrade") == nullptr);
 CHECK(f.shows == 0);
 Object *forge = f.game.createObject("SyntheticForge", local->getPlayerIndex(), Coord3D{20, 20, 0}, 0, &f.error);
 REQUIRE_MESSAGE(forge, f.error); forge->friend_onBuildComplete();
 f.hud->input().ui().deselectAll(false); f.hud->input().ui().selectObject(forge->getID());
 f.hud->update(0.033f);
 REQUIRE(f.windows.aptText("APT:CostModifierUpgrade"));
 CHECK(*f.windows.aptText("APT:CostModifierUpgrade") == "Discount -10%");
 CHECK(f.shows == 1);
 f.hud->input().ui().deselectAll(false); f.hud->update(0.033f);
 CHECK(f.hides == 1);
}
