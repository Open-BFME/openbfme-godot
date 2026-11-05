// OpenBFME. KINDFIX-1 tests (both editions, same rule):
//   * ReplaceSelfUpgrade::wouldUpgrade's overlap exemption is KindOf 156 WALL_HUB (RotWK RW 0x8BB4D9 .. 0x8BB4F3, template + 0x118 bit 28; donor Open-BFME-2
//     ReplaceSelfUpgrade.cpp:360, BFME2 0x8B7110): a WALL_HUB piece may overlap an existing wall piece that is not a hub;
//   * ClickReactionBehavior::selectableForClick tests UNSELECTABLE on the HORDE producer (RW 0x6937E6; donor Object_isMassSelectable.cpp:81-84, BFME2 0x692FAC).
//   * RespawnUpdate's exit at the end of the spawn animation also runs for an AI in state 16, AI_GUARD (RW 0x8B3ABC .. 0x8B3AD0; donor RespawnUpdate.cpp:473,
//     BFME2 0x8AF92F). Synthetic data (combattest::CombatWorld), no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ClickReactionBehavior.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/ReplaceSelfUpgrade.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>

using namespace combattest;

namespace
{
const char kKindFixObjects[] =
	"Object KFSegmentToHub\n"
	"  KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n"
	"  Geometry = BOX\n  GeometryMajorRadius = 10\n  GeometryMinorRadius = 10\n  GeometryHeight = 20\n"
	"  Behavior = ReplaceSelfUpgrade ModuleTag_RS\n"
	"    TriggeredBy = Upgrade_KF\n"
	"    ReplaceWith = KFHub\n"
	"  End\n"
	"End\n"
	"Object KFSegmentToTower\n"
	"  KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n"
	"  Geometry = BOX\n  GeometryMajorRadius = 10\n  GeometryMinorRadius = 10\n  GeometryHeight = 20\n"
	"  Behavior = ReplaceSelfUpgrade ModuleTag_RS\n"
	"    TriggeredBy = Upgrade_KF\n"
	"    ReplaceWith = KFTower\n"
	"  End\n"
	"End\n"
	"Object KFHub\n"
	"  KindOf = STRUCTURE IMMOBILE WALL_HUB\n"
	"  Geometry = BOX\n  GeometryMajorRadius = 10\n  GeometryMinorRadius = 10\n  GeometryHeight = 20\n"
	"End\n"
	"Object KFTower\n"
	"  KindOf = STRUCTURE IMMOBILE WALL_UPGRADE\n"
	"  Geometry = BOX\n  GeometryMajorRadius = 10\n  GeometryMinorRadius = 10\n  GeometryHeight = 20\n"
	"End\n"
	"Object KFHorde\n"
	"  KindOf = HORDE SELECTABLE\n"
	"End\n"
	"Object KFSoldier\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"End\n"
	"Object KFHero\n"
	"  KindOf = INFANTRY SELECTABLE HERO\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n  GeometryMinorRadius = 8\n  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = RespawnUpdate ModuleTag_Respawn\n"
	"    DeathAnim = DYING\n"
	"    DeathFX = None\n"
	"    RespawnAnim = LEVELED\n"
	"    RespawnAnimationTime = 2000\n"
	"    InitialSpawnAnim = EMOTION_CELEBRATING\n"
	"    InitialSpawnAnimationTime = 1000\n"
	"    RespawnRules = AutoSpawn:No Cost:1000 Time:60000 Health:100%\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object KFFortress\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
	"    UnitCreatePoint = X:0.0 Y:-30.0 Z:0.0\n"
	"    NaturalRallyPoint = X:0.0 Y:-60.0 Z:0.0\n"
	"    ExitDelay = 0\n"
	"  End\n"
	"End\n";

struct KindFixWorld : CombatWorld
{
	KindFixWorld()
	{
		const std::string upErr = w.load("Upgrade Upgrade_KF\n  Type = OBJECT\nEnd\n", INI_LOAD_OVERWRITE, "kindfix1_upgrade.ini");
		REQUIRE_MESSAGE(upErr.empty(), upErr);
		const std::string err = w.load(kKindFixObjects, INI_LOAD_OVERWRITE, "kindfix1.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	// the segment's ReplaceSelfUpgrade asked with its own TriggeredBy mask (RW 0x8BB23F)
	bool wouldUpgrade(Object *segment)
	{
		ReplaceSelfUpgrade *rs = dynamic_cast<ReplaceSelfUpgrade *>(segment->findModule("ReplaceSelfUpgrade"));
		REQUIRE(rs != nullptr);
		const UpgradeModuleData *d = nullptr;
		for (const ThingTemplate::Nugget &n : segment->getTemplate()->getFinalOverride()->behaviorModules().nuggets())
		{
			if (n.name == "ReplaceSelfUpgrade")
			{
				d = dynamic_cast<const UpgradeModuleData *>(n.data.get());
			}
		}
		REQUIRE(d != nullptr);
		REQUIRE(d->m_activationMask.any());
		return rs->wouldUpgrade(d->m_activationMask);
	}
};
} // namespace

TEST_CASE("kindfix1 walls: the overlap exemption is WALL_HUB (RW 0x8BB4D9): a hub piece over a tower is allowed, a tower over a tower or a hub over a hub is refused")
{
	SUBCASE("free ground")
	{
		KindFixWorld w;
		Object *seg = w.unit("KFSegmentToTower", 'A', 500.0f, 500.0f);
		REQUIRE(seg);
		CHECK(w.wouldUpgrade(seg));
	}
	SUBCASE("hub piece over a tower")
	{
		KindFixWorld w;
		REQUIRE(w.unit("KFTower", 'A', 505.0f, 500.0f));
		Object *seg = w.unit("KFSegmentToHub", 'A', 500.0f, 500.0f);
		REQUIRE(seg);
		CHECK(w.wouldUpgrade(seg));
	}
	SUBCASE("tower piece over a tower")
	{
		KindFixWorld w;
		REQUIRE(w.unit("KFTower", 'A', 505.0f, 500.0f));
		Object *seg = w.unit("KFSegmentToTower", 'A', 500.0f, 500.0f);
		REQUIRE(seg);
		CHECK_FALSE(w.wouldUpgrade(seg));
	}
	SUBCASE("hub piece over a hub")
	{
		KindFixWorld w;
		REQUIRE(w.unit("KFHub", 'A', 505.0f, 500.0f));
		Object *seg = w.unit("KFSegmentToHub", 'A', 500.0f, 500.0f);
		REQUIRE(seg);
		CHECK_FALSE(w.wouldUpgrade(seg));
	}
}

TEST_CASE("kindfix1 selection: a soldier of a horde is refused by selectableForClick when the HORDE is UNSELECTABLE (RW 0x6937E6)")
{
	KindFixWorld w;
	Object *horde = w.unit("KFHorde", 'A', 500.0f, 500.0f);
	Object *soldier = w.unit("KFSoldier", 'A', 510.0f, 500.0f);
	REQUIRE((horde && soldier));
	soldier->setProducer(horde);
	CHECK(ClickReactionBehavior::selectableForClick(*soldier));
	const unsigned unselectable = (unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
	horde->setStatus(unselectable, true);
	CHECK_FALSE(ClickReactionBehavior::selectableForClick(*soldier));
	horde->setStatus(unselectable, false);
	CHECK(ClickReactionBehavior::selectableForClick(*soldier));
}

TEST_CASE("kindfix1 hero: a revived hero whose AI is guarding leaves through its producer's door when the spawn animation ends (RW 0x8B3AC6 .. 0x8B3AD0)")
{
	for (const bool guard : { true, false })
	{
		CAPTURE(guard);
		KindFixWorld w;
		Object *fort = w.unit("KFFortress", 'A', 1000.0f, 1000.0f);
		Object *hero = w.unit("KFHero", 'A', 600.0f, 600.0f);
		REQUIRE((fort && hero));
		hero->setProducer(fort);
		AIUpdateInterface *ai = hero->getAIUpdateInterface();
		REQUIRE(ai != nullptr);
		if (guard)
		{
			// guarding a point 300 away: the guard machine walks back to it (a guard sub-state that is not idle)
			const Coord3D at{ 900.0f, 600.0f, 0.0f };
			REQUIRE(ai->aiGuardPosition(at, 0, CMD_FROM_AI));
			REQUIRE(ai->stateId() == AI_GUARD);
		}
		else
		{
			const Coord3D to{ 200.0f, 600.0f, 0.0f };
			ai->aiMoveToPosition(to, CMD_FROM_AI);
			REQUIRE(ai->stateId() != AI_GUARD);
		}
		w.frames(1);
		REQUIRE_FALSE(ai->isIdle());
		RespawnUpdate *ru = dynamic_cast<RespawnUpdate *>(hero->findModule("RespawnUpdate"));
		REQUIRE(ru != nullptr);
		ru->setState(RespawnUpdate::STATE_SPAWN_ANIMATION);
		ru->wakeFromInterface(UPDATE_SLEEP_NONE);
		w.frames(1);
		CHECK(ru->state() == RespawnUpdate::STATE_ALIVE);
		// the producer's exit puts the hero at its create point (0, -30): (1000, 970); a moving hero stays out (not idle, not guarding)
		const bool atDoor = std::fabs(hero->getPosition()->x - 1000.0f) < 1.0f && std::fabs(hero->getPosition()->y - 970.0f) < 1.0f;
		CHECK(atDoor == guard);
	}
}
