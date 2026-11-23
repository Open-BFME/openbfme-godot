// OpenBFME unit tests: LOCAL-10 Upgrade DisplayName in UI and upgrade completion feedback (issue #115, TOP20 #15).
//
// RotWK addresses:
//   Table parse row: RW 0xC107F8 (DisplayName, INI::parseAsciiString RW 0x0042EE5E, offset +0x28 in UpgradeTemplate)
//   Reader 1 (ProductionUpdate upgrade completion): RW 0x8A2070 .. 0x8A223B
//     Checks us->isLocallyControlled() RW 0x68B749
//     Fetches "UPGRADE:UpgradeComplete" (RW 0xC67FA4) via GameText::fetch (RW 0x6E7AC0)
//     Fetches upgrade->m_displayName (+0x28) via GameText::fetch (RW 0x6E7AC0, empty string if null RW 0xBD0C3F)
//     Formats via RW 0xADF750
//     Outputs to TheInGameUI->message RW 0x8A211C
//     Creates radar event 2 (RADAR_EVENT_UPGRADE_COMPLETE, RW 0x8A213A)
//     Audio/Eva feedback: ResearchCompleteEvaEvent (+0x44), ResearchSound (+0x40), default Eva 4 (RW 0x8A21C9), UnitSpecificSound (+0x48)
//   Missing label behavior: GameText::fetch (RW 0x6E7AC0 .. 0x6E7AD9) formats "MISSING: '%hs'" (RW 0xC1B544)
//   Reader 2 (Living World): RW 0x00A07F56 .. 0x00A081C0 ("LW:UnitUpgradeListHeader", RW 0xC91FA4)

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "IniTestUtil.h"
#include "MapTestUtil.h"
#include "ProdTestUtil.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/Audio/AudioRequests.h"
#include "Common/BfmeGame.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Upgrade.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/InGameChat.h"
#include "GameClient/InGameHud.h"
#include "GameClient/InGameUIMessages.h"
#include "GameClient/LiveGame.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <map>
#include <memory>
#include <string>

namespace
{
using namespace maptest;
using namespace apttest;
using namespace prodtest;

class MockGameTextSource : public GameTextSource
{
public:
	std::map<std::string, std::u16string> table;

	bool fetch(const std::string &label, std::u16string &out) const override
	{
		auto it = table.find(label);
		if (it != table.end())
		{
			out = it->second;
			return true;
		}
		return false;
	}
};

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

struct Local10HudFixture
{
	initest::Fixture files;
	RetailObjectWorld world;
	ArchiveW3DFileSource w3dSource;
	WW3DAssetManager assets;
	MapObjectOptions mapOptions;
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

	explicit Local10HudFixture(GameTextSource *textSource = nullptr)
		: world(files.fsys), w3dSource(files.fsys), assets(w3dSource), game(world, files.fsys, assets, mapOptions),
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
		REQUIRE(world.load(&error));
		LiveGame::Options opts;
		opts.mapName = "synthetic";
		opts.mapScripts = 0;
		REQUIRE(game.load(opts, &error));
		TestMovie level0;
		level0.setRootFrames({ {} });
		movies.add("AptLevel0", level0);
		TestMovie pal;
		const auto first = program(pal, [](Asm &a) { fscmd(a, "AptPalantir::OnInitialized"); });
		const auto second = program(pal, [](Asm &a) { fscmd(a, "AptPalantir::OnBttnSpellStore"); });
		std::vector<std::uint32_t> opening = { pal.addActionItem(first) };
		pal.setRootFrames({ opening, { pal.addActionItem(second) } });
		movies.add("Palantir", pal);
		factories.registerFactory("Palantir.apt", [](AptScreenContext &c) {
			return std::make_unique<AptPalantir>(c.windows, c.shell);
		});
		windows.init();
		shell.push("Palantir.apt");
		windows.setMinimumFrameTime(false);
		windows.update(0);
		environment.gameText = textSource;
		InGameHud::Config cfg{ game, world, files.fsys, services, view, MouseSettings{}, meta, textSource, &windows, &shell };
		hud = std::make_unique<InGameHud>(cfg);
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
};
} // namespace

TEST_CASE("LOCAL-10 upgrade name: UpgradeTemplate parser stores DisplayName and getter returns it (RW 0xC107F8)")
{
	initest::Fixture fx;
	UpgradeCenter center;
	UpgradeCenter *saved = TheUpgradeCenter;
	center.init();
	center.registerBlock(fx.env.blocks);
	TheUpgradeCenter = &center;

	const char kIni[] =
		"Upgrade Upgrade_ForgedBlades\n"
		"  DisplayName = UPGRADE:GondorForgedBlades\n"
		"  Tooltip = TOOLTIP:GondorForgedBlades\n"
		"  Type = PLAYER\n"
		"  BuildTime = 10.0\n"
		"  BuildCost = 300\n"
		"End\n";
	std::string err = initest::loadError(fx.env, "upgrade.ini", kIni);
	REQUIRE_MESSAGE(err.empty(), err);

	const UpgradeTemplate *u = center.findUpgrade("Upgrade_ForgedBlades");
	REQUIRE(u != nullptr);
	CHECK(u->m_displayName == "UPGRADE:GondorForgedBlades");
	CHECK(u->displayName() == "UPGRADE:GondorForgedBlades");
	CHECK(u->tooltip() == "TOOLTIP:GondorForgedBlades");

	TheUpgradeCenter = saved;
}

TEST_CASE("LOCAL-10 upgrade name: ProductionUpdate completeUpgrade emits UpgradeCompleteNotice and radar event 2 (RW 0x8A2080 .. 0x8A223B)")
{
	ProdWorld pw;
	pw.activate();
	pw.load(kBarracksObjects);
	pw.load(
		"Upgrade Upgrade_TestPlayerUpgrade\n"
		"  Type = PLAYER\n"
		"  BuildTime = 0.2\n"
		"  BuildCost = 50\n"
		"  DisplayName = UPGRADE:TestPlayerUpgrade\n"
		"End\n");
	pw.upgrades.scan("Upgrade Upgrade_TestPlayerUpgrade\n  Type = PLAYER\nEnd\n", &pw.error);

	const UpgradeTemplate *testUpgrade = pw.upgradeCenter.findUpgrade("Upgrade_TestPlayerUpgrade");
	REQUIRE(testUpgrade != nullptr);

	Object *barracks = pw.make("Barracks", pw.teamOf("Alice"));
	REQUIRE(barracks != nullptr);
	const Coord3D pos{ 100.0f, 200.0f, 0.0f };
	barracks->setPosition(&pos);

	ProductionUpdateInterface *pu = barracks->getProductionUpdate();
	REQUIRE(pu != nullptr);

	Player *player = pw.alice();
	REQUIRE(player != nullptr);
	player->depositMoney(1000, true);

	CHECK(pw.logic->upgradeCompleteNotices().empty());
	CHECK(pw.logic->radarEventNotices().empty());

	REQUIRE(pu->queueUpgrade(testUpgrade));
	CHECK(player->hasUpgradeInProduction(testUpgrade));

	// Advance frames until upgrade completes
	int frames = 0;
	while (!player->hasUpgradeComplete(testUpgrade) && frames < 20)
	{
		pw.logic->runLogicFrame();
		++frames;
	}

	REQUIRE(player->hasUpgradeComplete(testUpgrade));

	// RW 0x8A2080 .. 0x8A223B: notice recorded in logic
	REQUIRE(pw.logic->upgradeCompleteNotices().size() == 1);
	const auto &notice = pw.logic->upgradeCompleteNotices().front();
	CHECK(notice.seq == 1);
	CHECK(notice.playerIndex == player->getPlayerIndex());
	CHECK(notice.producerID == barracks->getID());
	CHECK(notice.upgrade == testUpgrade);
	CHECK(notice.position.x == 100.0f);
	CHECK(notice.position.y == 200.0f);

	// RW 0x8A213A: radar event 2 (RADAR_EVENT_UPGRADE_COMPLETE) emitted at producer position
	bool foundRadarEvent2 = false;
	for (const auto &rn : pw.logic->radarEventNotices())
	{
		if (rn.type == 2 && rn.playerIndex == player->getPlayerIndex())
		{
			foundRadarEvent2 = true;
			CHECK(rn.source == GameLogic::RadarEventNotice::CONSTRUCTION);
			CHECK(rn.position.x == 100.0f);
			CHECK(rn.position.y == 200.0f);
		}
	}
	CHECK(foundRadarEvent2);
}

TEST_CASE("LOCAL-10 upgrade name: InGameHud presents upgrade completion with valid DisplayName")
{
	MockGameTextSource text;
	text.table["UPGRADE:UpgradeComplete"] = u"Upgrade Complete: %s";
	text.table["UPGRADE:GondorForgedBlades"] = u"Forged Blades";

	Local10HudFixture fx(&text);
	Player *localPlayer = fx.game.players().getLocalPlayer();
	REQUIRE(localPlayer != nullptr);

	UpgradeTemplate upgrade;
	upgrade.friend_setName("Upgrade_GondorForgedBlades");
	upgrade.m_displayName = "UPGRADE:GondorForgedBlades";

	CHECK(fx.hud->guiMessagesShown() == 0);

	// Note upgrade complete for local player
	fx.game.logic().noteUpgradeComplete(localPlayer->getPlayerIndex(), 1001, &upgrade, Coord3D{ 50.0f, 50.0f, 0.0f });

	fx.hud->update(0.033f);

	CHECK(fx.hud->guiMessagesShown() == 1);
	bool found = false;
	for (const auto &line : fx.hud->messageLines().lines())
	{
		if (line.used && line.text == u"Upgrade Complete: Forged Blades")
		{
			found = true;
			break;
		}
	}
	CHECK(found);
}

TEST_CASE("LOCAL-10 upgrade name: InGameHud missing DisplayName yields RotWK MISSING: '<label>' (RW 0x6E7AC0)")
{
	MockGameTextSource text;
	text.table["UPGRADE:UpgradeComplete"] = u"Upgrade Complete: %s";
	// "UPGRADE:UnknownUpgrade" is NOT in the text table

	Local10HudFixture fx(&text);
	Player *localPlayer = fx.game.players().getLocalPlayer();
	REQUIRE(localPlayer != nullptr);

	UpgradeTemplate upgrade;
	upgrade.friend_setName("Upgrade_Unknown");
	upgrade.m_displayName = "UPGRADE:UnknownUpgrade";

	fx.game.logic().noteUpgradeComplete(localPlayer->getPlayerIndex(), 1002, &upgrade, Coord3D{});
	fx.hud->update(0.033f);

	CHECK(fx.hud->guiMessagesShown() == 1);
	bool found = false;
	for (const auto &line : fx.hud->messageLines().lines())
	{
		if (line.used && line.text == u"Upgrade Complete: MISSING: 'UPGRADE:UnknownUpgrade'")
		{
			found = true;
			break;
		}
	}
	CHECK(found);
}

TEST_CASE("LOCAL-10 upgrade name: InGameHud empty DisplayName yields RotWK MISSING: '' (RW 0x6E7AC0 / 0x8A20C1)")
{
	MockGameTextSource text;
	text.table["UPGRADE:UpgradeComplete"] = u"Upgrade Complete: %s";

	Local10HudFixture fx(&text);
	Player *localPlayer = fx.game.players().getLocalPlayer();
	REQUIRE(localPlayer != nullptr);

	UpgradeTemplate upgrade;
	upgrade.friend_setName("Upgrade_NoName");
	upgrade.m_displayName = ""; // empty DisplayName

	fx.game.logic().noteUpgradeComplete(localPlayer->getPlayerIndex(), 1003, &upgrade, Coord3D{});
	fx.hud->update(0.033f);

	CHECK(fx.hud->guiMessagesShown() == 1);
	bool found = false;
	for (const auto &line : fx.hud->messageLines().lines())
	{
		if (line.used && line.text == u"Upgrade Complete: MISSING: ''")
		{
			found = true;
			break;
		}
	}
	CHECK(found);
}

TEST_CASE("LOCAL-10 upgrade name: InGameHud missing format label UPGRADE:UpgradeComplete yields RotWK MISSING: 'UPGRADE:UpgradeComplete'")
{
	MockGameTextSource text; // empty text table

	Local10HudFixture fx(&text);
	Player *localPlayer = fx.game.players().getLocalPlayer();
	REQUIRE(localPlayer != nullptr);

	UpgradeTemplate upgrade;
	upgrade.friend_setName("Upgrade_Blades");
	upgrade.m_displayName = "UPGRADE:Blades";

	fx.game.logic().noteUpgradeComplete(localPlayer->getPlayerIndex(), 1004, &upgrade, Coord3D{});
	fx.hud->update(0.033f);

	CHECK(fx.hud->guiMessagesShown() == 1);
	bool found = false;
	for (const auto &line : fx.hud->messageLines().lines())
	{
		if (line.used && line.text == u"MISSING: 'UPGRADE:UpgradeComplete'")
		{
			found = true;
			break;
		}
	}
	CHECK(found);
}

TEST_CASE("LOCAL-10 upgrade name: another player's completed upgrade produces no local HUD message (RW 0x68B749 isLocallyControlled)")
{
	MockGameTextSource text;
	text.table["UPGRADE:UpgradeComplete"] = u"Upgrade Complete: %s";
	text.table["UPGRADE:EnemyUpgrade"] = u"Enemy Upgrade";

	Local10HudFixture fx(&text);
	Player *localPlayer = fx.game.players().getLocalPlayer();
	REQUIRE(localPlayer != nullptr);

	UpgradeTemplate upgrade;
	upgrade.friend_setName("Upgrade_Enemy");
	upgrade.m_displayName = "UPGRADE:EnemyUpgrade";

	// Notice for another player (playerIndex + 1)
	fx.game.logic().noteUpgradeComplete(localPlayer->getPlayerIndex() + 1, 1005, &upgrade, Coord3D{});
	fx.hud->update(0.033f);

	CHECK(fx.hud->guiMessagesShown() == 0);
	for (const auto &line : fx.hud->messageLines().lines())
	{
		CHECK_FALSE(line.used);
	}
}
