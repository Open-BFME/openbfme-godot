// OpenBFME unit tests: LOCAL-8 hero respawn (issue #113, TOP20 #25).
//
// Editions: both (BFME2 1.06 and RotWK 2.01 are byte-for-byte identical across RespawnUpdate, delta RW - 0x4106 = BFME2).
//
// RotWK / BFME2 addresses:
//   RespawnUpdateModuleData::buildFieldParse: RW 0x8B3DD7 / BFME2 0x8AFCD1 (table RW 0xC6CE80 / BFME2 0xC561D0)
//     DeathAnimationTime (+0xFC, RW 0xC6CEA0 / BFME2 0xC561F0, parseDurationUnsignedInt RW 0x73A429; parsed-unused in both binaries)
//     AutoRespawnAtObjectFilter (+0x08, RW 0xC6CF10 / BFME2 0xC56260, parseObjectFilter RW 0x76392F)
//     ButtonImage (+0x118, RW 0xC6CF40 / BFME2 0xC56290, parseAsciiString RW 0x42EE5E)
//   RespawnUpdate::getDeadImage: RW 0x8B33E4 .. 0x8B34BC / BFME2 0x8AF2DE (primary vtable 0xC6C360 slot 14 / +0x38)
//   HeroRecord::HeroRecord(const Object*): RW 0x780DD4 (at RW 0x780EEE .. 0x780F06 stores getDeadImage in HeroRecord + 0x00)
//   HeroRecord::xfer: RW 0x7808FD (at RW 0x780A29 .. 0x780A79 transfers HeroRecord + 0x00 as AsciiString)
//   HeroRecord::getButtonImage: RW 0x7810BC / BFME2 0x77DDAC (at RW 0x7810CA .. 0x7810D5 returns HeroRecord + 0x00 when dead + 0xB0 is set)
//   Living World dead hero record setup: RW 0x6E2E18 (at RW 0x6E2F59 .. 0x6E2FB3 reads RespawnUpdateModuleData + 0x118 into HeroRecord + 0x00)

#include "doctest.h"

#include "ProdTestUtil.h"

#include "Common/PlayerHeroList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Xfer.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/HudContext.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/HeroSystem.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Object/PartitionManager.h"

namespace
{
const char kLocal8ButtonObjects[] =
	"Object Local8RecallHero\n"
	"  ButtonImage = HIHeroAlive\n"
	"  SelectPortrait = HPHeroPortrait\n"
	"  BuildCost = 1100\n"
	"  BuildTime = 20.0\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = RespawnBody ModuleTag_Body\n"
	"    MaxHealth = 400\n"
	"  End\n"
	"  Behavior = RespawnUpdate ModuleTag_Respawn\n"
	"    DeathAnim = DYING\n"
	"    DeathFX = None\n"
	"    DeathAnimationTime = 1133\n"
	"    RespawnAnim = LEVELED\n"
	"    RespawnAnimationTime = 2000\n"
	"    ButtonImage = HIHeroRecall\n"
	"    RespawnRules = AutoSpawn:No Cost:550 Time:40000 Health:100%\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Local8NoObjButtonHero\n"
	"  BuildCost = 900\n"
	"  BuildTime = 15.0\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = RespawnBody ModuleTag_Body\n"
	"    MaxHealth = 300\n"
	"  End\n"
	"  Behavior = RespawnUpdate ModuleTag_Respawn\n"
	"    DeathAnim = DYING\n"
	"    DeathFX = None\n"
	"    RespawnAnim = LEVELED\n"
	"    RespawnAnimationTime = 1500\n"
	"    ButtonImage = HIWormtongueOnly\n"
	"    RespawnRules = AutoSpawn:No Cost:450 Time:30000 Health:100%\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Local8Fortress\n"
	"  KindOf = STRUCTURE SELECTABLE CASTLE_KEEP\n"
	"  CommandSet = Local8FortressSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    MaxQueueEntries = 5\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
	"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
	"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
	"  End\n"
	"End\n";

const char kLocal8ButtonCommands[] =
	"CommandButton Command_Local8Revive0\n"
	"  Command = REVIVE\n"
	"  Options = CANCELABLE\n"
	"  InPalantir = Yes\n"
	"End\n"
	"CommandButton Command_Local8Revive1\n"
	"  Command = REVIVE\n"
	"  Options = CANCELABLE\n"
	"  InPalantir = Yes\n"
	"End\n"
	"CommandSet Local8FortressSet\n"
	"  InitialVisible = 6\n"
	"  1 = Command_Local8Revive0\n"
	"  2 = Command_Local8Revive1\n"
	"End\n";

void killHero(Object *victim)
{
	DamageInfo d;
	d.m_input.m_sourceID = 0;
	d.m_input.m_amount = 100000.0f;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	victim->attemptDamage(d);
}
} // namespace

TEST_CASE("LOCAL-8 respawn ButtonImage: dead hero record stores RespawnUpdate ButtonImage, ControlBar shows it on REVIVE button and falls back when unmapped, xfer and crc preserve it (RW 0x8B33E4 / 0x780F00 / 0x7810BC / 0x780A29)")
{
	prodtest::ProdWorld f;
	f.logic->settings().bodyThresholdsLoaded = true;
	f.logic->settings().unitDamagedThreshold = 0.7f;
	f.logic->settings().unitReallyDamagedThreshold = 0.35f;
	f.load(kLocal8ButtonObjects);
	f.load(kLocal8ButtonCommands);

	Player *alice = f.alice();
	REQUIRE(alice != nullptr);
	f.players.setLocalPlayer(alice);
	PlayerTemplate *pt = const_cast<PlayerTemplate *>(alice->getPlayerTemplate());
	REQUIRE(pt != nullptr);
	pt->m_buildableHeroesMP = { "Local8RecallHero", "Local8NoObjButtonHero" };

	Object *fortress = f.make("Local8Fortress", f.teamOf("Alice"));
	REQUIRE(fortress != nullptr);

	// Purchase records before any hero has spawned or died: !r.dead uses Object/ButtonImage
	alice->heroes().addPurchase(*f.w.get("Local8RecallHero"));
	alice->heroes().addPurchase(*f.w.get("Local8NoObjButtonHero"));
	REQUIRE(alice->heroes().size() == 2u);
	CHECK_FALSE(alice->heroes().at(0)->dead);
	CHECK(alice->heroes().at(0)->deadImage.empty());

	PinholeView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{ *f.logic, nullptr, view, ui, stream, &f.commands, MouseSettings{} };
	ControlBar bar(ctx);
	ui.selectObject(fortress->getID());
	bar.update();

	REQUIRE(bar.palantirButtons().size() == 2u);
	CHECK(bar.palantirButtons()[0].image == "HIHeroAlive");
	CHECK(bar.palantirButtons()[1].image == "");

	// Spawn both heroes and kill them: onDeath (RW 0x8B3744) -> addDeadHero (RW 0x780DD4) -> getDeadImage (RW 0x8B33E4)
	alice->heroes().clear();
	Object *hero0 = f.make("Local8RecallHero", f.teamOf("Alice"));
	Object *hero1 = f.make("Local8NoObjButtonHero", f.teamOf("Alice"));
	REQUIRE(hero0 != nullptr);
	REQUIRE(hero1 != nullptr);
	killHero(hero0);
	killHero(hero1);

	REQUIRE(alice->heroes().size() == 2u);
	CHECK(alice->heroes().at(0)->dead);
	CHECK(alice->heroes().at(0)->deadImage == "HIHeroRecall");
	CHECK(alice->heroes().at(1)->dead);
	CHECK(alice->heroes().at(1)->deadImage == "HIWormtongueOnly");

	// ControlBar::heroRecordImage (RW 0x7810BC at RW 0x7810CA..0x7810D5) returns r.deadImage for dead records
	bar.update();
	REQUIRE(bar.palantirButtons().size() == 2u);
	CHECK(bar.palantirButtons()[0].image == "HIHeroRecall");
	CHECK(bar.palantirButtons()[1].image == "HIWormtongueOnly");

	// RW 0x8B348F .. 0x8B34B1: when MappedImageCollection is attached and RespawnUpdate/ButtonImage is absent from it (e.g. EvilMenBlackRider's HIBlackRider_res),
	// falls back to the template's ButtonImage ("HIHeroAlive"); once the mapped image exists, returns "HIHeroRecall"
	MappedImageCollection images;
	{
		Image img;
		img.name = "HIHeroAlive";
		images.addImage(img);
		img.name = "HIWormtongueOnly";
		images.addImage(img);
	}
	ctx.images = &images;
	bar.update();
	REQUIRE(bar.palantirButtons().size() == 2u);
	CHECK(bar.palantirButtons()[0].image == "HIHeroAlive");
	CHECK(bar.palantirButtons()[1].image == "HIWormtongueOnly");

	{
		Image img;
		img.name = "HIHeroRecall";
		images.addImage(img);
	}
	bar.update();
	CHECK(bar.palantirButtons()[0].image == "HIHeroRecall");

	// State hash and xfer round-trip (RW 0x780A29 .. 0x780A79)
	const std::uint32_t hashBefore = f.logic->computeStateHash();
	alice->heroes().at(0)->deadImage = "HIHeroAlt";
	CHECK(f.logic->computeStateHash() != hashBefore);
	alice->heroes().at(0)->deadImage = "HIHeroRecall";
	CHECK(f.logic->computeStateHash() == hashBefore);

	XferSave saver;
	alice->heroes().xfer(saver);
	PlayerHeroList loaded;
	XferLoad loader(saver.data());
	loaded.xfer(loader);
	REQUIRE(loaded.size() == 2u);
	CHECK(loaded.at(0)->dead);
	CHECK(loaded.at(0)->deadImage == "HIHeroRecall");
	CHECK(loaded.at(1)->dead);
	CHECK(loaded.at(1)->deadImage == "HIWormtongueOnly");
}

TEST_CASE("LOCAL-8 respawn ButtonImage: Living World dead hero record populates deadImage from RespawnUpdateModuleData +0x118 (RW 0x6E2F82 .. 0x6E2FB3)")
{
	prodtest::ProdWorld f;
	f.load(kLocal8ButtonObjects);
	Player *alice = f.alice();
	REQUIRE(alice != nullptr);

	std::vector<HeroSystem::LivingWorldHero> wotrHeroes = {
		{ "Local8RecallHero", "hero_1", 50.0f, true },
		{ "Local8NoObjButtonHero", "hero_2", 0.0f, true },
	};
	std::vector<std::string> errors;
	HeroSystem::initLivingWorldPlayer(*f.logic, *alice, wotrHeroes, 1, errors);
	CHECK(errors.empty());
	REQUIRE(alice->heroes().size() == 2u);
	CHECK(alice->heroes().at(0)->dead);
	CHECK(alice->heroes().at(0)->deadImage == "HIHeroRecall");
	CHECK(alice->heroes().at(1)->dead);
	CHECK(alice->heroes().at(1)->deadImage == "HIWormtongueOnly");
}

namespace
{
const char kLocal8AutoRespawnObjects[] =
	"Object Local8KeepHero\n"
	"  BuildCost = 1500\n"
	"  BuildTime = 25.0\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = RespawnBody ModuleTag_Body\n"
	"    MaxHealth = 600\n"
	"  End\n"
	"  Behavior = RespawnUpdate ModuleTag_Respawn\n"
	"    DeathAnim = DYING\n"
	"    DeathFX = None\n"
	"    RespawnAnim = LEVELED\n"
	"    RespawnFX = None\n"
	"    RespawnAnimationTime = 2000\n"
	"    AutoRespawnAtObjectFilter = NONE +CASTLE_KEEP\n"
	"    ButtonImage = HIKeepHeroDead\n"
	"    RespawnRules = AutoSpawn:Yes Cost:0 Time:10000 Health:75%\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Local8NoneFilterHero\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = RespawnBody ModuleTag_Body\n"
	"    MaxHealth = 400\n"
	"  End\n"
	"  Behavior = RespawnUpdate ModuleTag_Respawn\n"
	"    DeathAnim = DYING\n"
	"    RespawnAnim = LEVELED\n"
	"    RespawnAnimationTime = 1000\n"
	"    AutoRespawnAtObjectFilter = NONE\n"
	"    RespawnRules = AutoSpawn:Yes Cost:0 Time:5000 Health:50%\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Local8Barracks\n"
	"  KindOf = STRUCTURE SELECTABLE FS_FACTORY\n"
	"End\n"
	"Object Local8Keep\n"
	"  KindOf = STRUCTURE SELECTABLE CASTLE_KEEP\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
	"    UnitCreatePoint = X:5.0 Y:10.0 Z:0.0\n"
	"    NaturalRallyPoint = X:25.0 Y:40.0 Z:0.0\n"
	"  End\n"
	"End\n";

int local8McBit(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::string(TheModelConditionNames[i]) == name)
		{
			return i;
		}
	}
	return -1;
}
} // namespace

TEST_CASE("LOCAL-8 respawn AutoRespawnAtObjectFilter: state 3 searches closest player object matching AutoRespawnAtObjectFilter, queries natural rally point, moves hero, restores health, enters spawn animation and completes (RW 0x8B307E / 0x8B3B08 / 0x8B310D)")
{
	prodtest::ProdWorld f;
	f.logic->settings().bodyThresholdsLoaded = true;
	f.logic->settings().unitDamagedThreshold = 0.7f;
	f.logic->settings().unitReallyDamagedThreshold = 0.35f;
	f.load(kLocal8AutoRespawnObjects);
	f.logic->partition().setRegion(0.0f, 0.0f, 2000.0f, 2000.0f);

	Object *hero = f.make("Local8KeepHero", f.teamOf("Alice"));
	Object *nearBarracks = f.make("Local8Barracks", f.teamOf("Alice"));
	Object *enemyKeep = f.make("Local8Keep", f.teamOf("Bob"));
	Object *aliceKeepFar = f.make("Local8Keep", f.teamOf("Alice"));
	Object *aliceKeepNear = f.make("Local8Keep", f.teamOf("Alice"));
	REQUIRE(hero != nullptr);
	REQUIRE(nearBarracks != nullptr);
	REQUIRE(enemyKeep != nullptr);
	REQUIRE(aliceKeepFar != nullptr);
	REQUIRE(aliceKeepNear != nullptr);

	const Coord3D heroPos{ 100.0f, 100.0f, 0.0f };
	const Coord3D barracksPos{ 120.0f, 100.0f, 0.0f }; // closer (dist 20), but not CASTLE_KEEP
	const Coord3D enemyKeepPos{ 110.0f, 100.0f, 0.0f }; // closer (dist 10), but wrong player (Bob)
	const Coord3D keepFarPos{ 800.0f, 100.0f, 0.0f };   // Alice's CASTLE_KEEP (dist 700)
	const Coord3D keepNearPos{ 300.0f, 200.0f, 0.0f };  // Alice's closest CASTLE_KEEP (dist ~223.6)
	hero->setPosition(&heroPos);
	nearBarracks->setPosition(&barracksPos);
	enemyKeep->setPosition(&enemyKeepPos);
	aliceKeepFar->setPosition(&keepFarPos);
	aliceKeepNear->setPosition(&keepNearPos);
	f.logic->partition().update();

	RespawnUpdate *ru = dynamic_cast<RespawnUpdate *>(hero->findModule("RespawnUpdate"));
	REQUIRE(ru != nullptr);

	// 1. searchForSuitableSpawnAtObject (RW 0x8B307E) picks Alice's closest CASTLE_KEEP
	CHECK(ru->searchForSuitableSpawnAtObject() == aliceKeepNear);

	// 2. AutoRespawnAtObjectFilter = NONE (e.g. TomBombadil) matches nothing and falls back to self (RW 0x8B30FD cmove)
	Object *noneHero = f.make("Local8NoneFilterHero", f.teamOf("Alice"));
	REQUIRE(noneHero != nullptr);
	noneHero->setPosition(&heroPos);
	f.logic->partition().update();
	RespawnUpdate *noneRu = dynamic_cast<RespawnUpdate *>(noneHero->findModule("RespawnUpdate"));
	REQUIRE(noneRu != nullptr);
	CHECK(noneRu->searchForSuitableSpawnAtObject() == noneHero);

	// 3. Kill hero (onDeath RW 0x8B3744 sets m_health = 0.75, DISABLED_PARALYZED, UNSELECTABLE), then enter STATE_AUTO_RESPAWN (3)
	killHero(hero);
	CHECK(hero->isEffectivelyDead());
	CHECK((hero->getDisabledMask() & (1u << 4)) == (1u << 4));
	CHECK(ru->healthFraction() == 0.75f);

	f.frames(2);
	const UnsignedInt startFrame = f.logic->getFrame();
	ru->setState(RespawnUpdate::STATE_AUTO_RESPAWN);
	ru->setRespawnAt(INVALID_ID);
	ru->setAutoRespawnTimer((std::int32_t)startFrame, 50);

	// getRespawnProgress (RW 0x8B310D): in state 3 returns (frame - m_34) / m_time and writes m_cost
	int outCost = -1;
	CHECK(ru->getRespawnProgress(&outCost) == 0.0f);
	CHECK(outCost == 0);

	// Xfer round-trip of RespawnUpdate in STATE_AUTO_RESPAWN
	{
		XferSave saver;
		ru->xfer(saver);
		XferLoad loader(saver.data());
		ru->xfer(loader);
		CHECK(ru->state() == RespawnUpdate::STATE_AUTO_RESPAWN);
		CHECK(ru->autoRespawnStartFrame() == (std::int32_t)startFrame);
		CHECK(ru->autoRespawnDuration() == 50);
	}

	// Wake RespawnUpdate on the next frame (getDisabledTypesToProcess RW 0x8B3313 = 0xFFFFFFFF lets it run while DISABLED_PARALYZED)
	ru->wakeFromInterface(UPDATE_SLEEP_NONE);
	f.frames(1);

	// RW 0x8B3B08 .. 0x8B3C05:
	// - spawnAt = searchForSuitableSpawnAtObject() -> aliceKeepNear (300, 200, 0)
	// - exit->getNaturalRallyPoint(&pos, true) -> (300 + 25, 200 + 40, 0) plus 2-cell exit offset along (20, 30) = (335.6, 256.96, 0)
	// - hero moved to pos, RespawnAnim (LEVELED) set, state = STATE_SPAWN_ANIMATION (4),
	//   health restored to 75% of 600 = 450, m_34 = -1, m_time = -1, wakes after RespawnAnimationTime (2000 ms = 10 frames)
	Coord3D nearRally{};
	REQUIRE(aliceKeepNear->getObjectExitInterface() != nullptr);
	REQUIRE(aliceKeepNear->getObjectExitInterface()->getNaturalRallyPoint(&nearRally, true));
	CHECK(ru->state() == RespawnUpdate::STATE_SPAWN_ANIMATION);
	CHECK(hero->getPosition()->x == nearRally.x);
	CHECK(hero->getPosition()->y == nearRally.y);
	CHECK(hero->getPosition()->x == 335.599976f); // one ulp below the decimal literal: rally point + exit offset in float
	CHECK(hero->getPosition()->y == 256.959961f); // one ulp below the decimal literal: rally point + exit offset in float
	CHECK(hero->testModelCondition(local8McBit("LEVELED")));
	CHECK(hero->getBodyModule()->getHealth() == 450.0f);
	CHECK(ru->autoRespawnStartFrame() == -1);
	CHECK(ru->autoRespawnDuration() == -1);
	CHECK(ru->getRespawnProgress() == 0.0f);

	// After RespawnAnimationTime (10 frames), STATE_SPAWN_ANIMATION (RW 0x8B3A5B) clears LEVELED, UNSELECTABLE and DISABLED_PARALYZED and enters STATE_ALIVE
	f.frames(10);
	CHECK(ru->state() == RespawnUpdate::STATE_ALIVE);
	CHECK_FALSE(hero->testModelCondition(local8McBit("LEVELED")));
	CHECK_FALSE(hero->testStatus(3));
	CHECK((hero->getDisabledMask() & (1u << 4)) == 0);

	// Explicit m_respawnAt (+0x30, RW 0x8B3B12..0x8B3B22) takes precedence over searchForSuitableSpawnAtObject
	Coord3D farRally{};
	REQUIRE(aliceKeepFar->getObjectExitInterface() != nullptr);
	REQUIRE(aliceKeepFar->getObjectExitInterface()->getNaturalRallyPoint(&farRally, true));
	ru->setState(RespawnUpdate::STATE_AUTO_RESPAWN);
	ru->setRespawnAt(aliceKeepFar->getID());
	ru->wakeFromInterface(UPDATE_SLEEP_NONE);
	f.frames(1);
	CHECK(ru->state() == RespawnUpdate::STATE_SPAWN_ANIMATION);
	CHECK(hero->getPosition()->x == farRally.x);
	CHECK(hero->getPosition()->y == farRally.y);
	CHECK(hero->getPosition()->x == 835.599976f); // one ulp below the decimal literal: rally point + exit offset in float
	CHECK(hero->getPosition()->y == 156.959961f); // one ulp below the decimal literal: rally point + exit offset in float

	// STATE_REFUNDED (1) returns progress 1.0f (RW 0x8B3123..0x8B312A)
	ru->setState(RespawnUpdate::STATE_REFUNDED);
	CHECK(ru->getRespawnProgress() == 1.0f);
}
