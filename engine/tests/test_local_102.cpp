// OpenBFME. GPL-3.0.
// Local 102 (fork issue 772): ToppleUpdate (RW 0x0064ED68, RW 0x0064EDA0, RW 0x0068D58C, RW 0x008AB767, RW 0x008AB787, RW 0x008AB78D,
// RW 0x008AB7FB, RW 0x008AB804, RW 0x008AB849, RW 0x008AB8C6, RW 0x008AB8D7, RW 0x008AB974, RW 0x008AB990, RW 0x008AB9CB, RW 0x008ABA61,
// RW 0x008ABA7D, RW 0x008ABAAD, RW 0x008ABDBD, RW 0x008ABE65; DONOR BFME2 ToppleUpdate.cpp 0x008A8091 .. 0x008A878F, Rva004A82F5Topple.cpp)
// and retail 2.01 map-effect module citations (FireSpreadUpdate, FloodUpdate, PassiveAreaEffectBehavior, EvacuateDamage, ToppleUpdate, OathbreakersFadeAwayBehavior).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "StructureArena.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/ClientEvents.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AreaScanModules.h"
#include "GameLogic/Module/FlammableUpdate.h"
#include "GameLogic/Module/SpellEffectModules.h"
#include "GameLogic/Module/StructureToppleUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

#include <cmath>
#include <string>

using namespace combattest;

namespace
{
const char kToppleIni[] =
	"Object TestStump\n"
	"  KindOf = SHRUBBERY IMMOBILE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50.0\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5.0\n"
	"  GeometryHeight = 10.0\n"
	"End\n"
	"Object TestToppleDefaults\n"
	"  KindOf = SHRUBBERY IMMOBILE\n"
	"  CrushableLevel = 1\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50.0\n"
	"  End\n"
	"  Behavior = ToppleUpdate ModuleTag_Topple\n"
	"  End\n"
	"End\n"
	"Object TestToppleTree\n"
	"  KindOf = SHRUBBERY IMMOBILE\n"
	"  CrushableLevel = 1\n"
	"  Body = HighlanderBody ModuleTag_Body\n"
	"    MaxHealth = 50.0\n"
	"  End\n"
	"  Behavior = ToppleUpdate ModuleTag_Topple\n"
	"    ToppleFX = FX_ToppleTree\n"
	"    BounceFX = FX_TreeBounce\n"
	"    StumpName = TestStump\n"
	"    InitialVelocityPercent = 20%\n"
	"    InitialAccelPercent = 1%\n"
	"    BounceVelocityPercent = 30%\n"
	"    MinimumToppleSpeed = 1.0\n"
	"    KillWhenStartToppling = No\n"
	"    KillWhenFinishedToppling = Yes\n"
	"    KillStumpWhenToppled = Yes\n"
	"    ToppleLeftOrRightOnly = No\n"
	"    ReorientToppledRubble = Yes\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10.0\n"
	"  GeometryHeight = 60.0\n"
	"End\n"
	"Object TestToppleInstantKill\n"
	"  KindOf = SHRUBBERY IMMOBILE\n"
	"  CrushableLevel = 1\n"
	"  Body = HighlanderBody ModuleTag_Body\n"
	"    MaxHealth = 50.0\n"
	"  End\n"
	"  Behavior = ToppleUpdate ModuleTag_Topple\n"
	"    KillWhenStartToppling = Yes\n"
	"    ToppleLeftOrRightOnly = Yes\n"
	"    StumpName = TestStump\n"
	"    KillStumpWhenToppled = Yes\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10.0\n"
	"  GeometryHeight = 40.0\n"
	"End\n"
	"Object TestToppleLeftRight\n"
	"  KindOf = SHRUBBERY IMMOBILE\n"
	"  CrushableLevel = 1\n"
	"  Body = HighlanderBody ModuleTag_Body\n"
	"    MaxHealth = 50.0\n"
	"  End\n"
	"  Behavior = ToppleUpdate ModuleTag_Topple\n"
	"    ToppleLeftOrRightOnly = Yes\n"
	"    StumpName = TestStump\n"
	"    KillStumpWhenToppled = Yes\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10.0\n"
	"  GeometryHeight = 40.0\n"
	"End\n"
	"Object TestLightWalker\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  CrusherLevel = 1\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object TestHeavyCrusher\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  CrusherLevel = 2\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500.0\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n";

ToppleUpdate *toppleOf(Object *o)
{
	ToppleUpdate *t = dynamic_cast<ToppleUpdate *>(o->findModule("ToppleUpdate"));
	REQUIRE(t != nullptr);
	return t;
}
} // namespace

// RW 0x008AB849 (ctor defaults), RW 0x008AB8C6 (buildFieldParse, table RW 0xC6A490), DONOR BFME2 0x008A8173
TEST_CASE("LOCAL-102 ToppleUpdate: module data reads RotWK table RW 0xC6A490 and retail defaults")
{
	CombatWorld w(kToppleIni);
	Object *defObj = w.unit("TestToppleDefaults", 'A', 100, 100);
	const ToppleUpdateModuleData *d0 = toppleOf(defObj)->data();
	REQUIRE(d0 != nullptr);
	CHECK(d0->m_treeToppleFX.empty());
	CHECK(d0->m_bounceFX.empty());
	CHECK(d0->m_stumpName.empty());
	CHECK(d0->m_initialVelocityPercent == 0.2f); // RW 0xBDAD78
	CHECK(d0->m_initialAccelPercent == 0.01f);   // RW 0xBE5600
	CHECK(d0->m_bounceVelocityPercent == 0.2f);  // RW 0xBDAD78
	CHECK(d0->m_minimumToppleSpeed == 0.5f);     // RW 0xBD869C
	CHECK(d0->m_killWhenToppled);
	CHECK_FALSE(d0->m_killWhenStartToppled);
	CHECK_FALSE(d0->m_killStumpWhenToppled);
	CHECK_FALSE(d0->m_toppleLeftOrRightOnly);
	CHECK_FALSE(d0->m_reorientToppledRubble);

	Object *tree = w.unit("TestToppleTree", 'A', 200, 200);
	const ToppleUpdateModuleData *d1 = toppleOf(tree)->data();
	REQUIRE(d1 != nullptr);
	CHECK(d1->m_treeToppleFX == "FX_ToppleTree");
	CHECK(d1->m_bounceFX == "FX_TreeBounce");
	CHECK(d1->m_stumpName == "TestStump");
	CHECK(d1->m_initialVelocityPercent == doctest::Approx(0.2f)); // tolerance: parsePercentToReal 20 * 0.01f rounding
	CHECK(d1->m_initialAccelPercent == 0.01f);
	CHECK(d1->m_bounceVelocityPercent == doctest::Approx(0.3f)); // tolerance: parsePercentToReal 30 * 0.01f rounding
	CHECK(d1->m_minimumToppleSpeed == 1.0f);
	CHECK_FALSE(d1->m_killWhenStartToppled);
	CHECK(d1->m_killWhenToppled);
	CHECK(d1->m_killStumpWhenToppled);
	CHECK_FALSE(d1->m_toppleLeftOrRightOnly);
	CHECK(d1->m_reorientToppledRubble);
}

// RW 0x008AB9CB (onCollide), RW 0x0068D58C (Object::topple), RW 0x008AB7FB (isAbleToBeToppled), RW 0x008ABAAD (applyTopplingForce),
// RW 0x008ABDBD (xfer), RW 0x008ABE65 (update), RW 0x008AB990 (999999.0f DAMAGE_UNRESISTABLE / DEATH_TOPPLED); DONOR BFME2 ToppleUpdate.cpp
TEST_CASE("LOCAL-102 ToppleUpdate: onCollide and Object::topple topple away from a higher crusher, spawn StumpName, bounce, xfer and kill")
{
	CombatWorld w(kToppleIni);
	w.combat().setAutoAcquireEnabled(false);
	ClientEventRecorder events(*w.logic);
	w.logic->setClientHooks(&events);
	struct Detach { GameLogic &logic; ~Detach() { logic.setClientHooks(nullptr); } } detach{ *w.logic };

	Object *tree = w.unit("TestToppleTree", 'A', 300, 300);
	Object *light = w.unit("TestLightWalker", 'B', 250, 300);
	Object *heavy = w.unit("TestHeavyCrusher", 'B', 250, 300);
	ToppleUpdate *t = toppleOf(tree);
	REQUIRE(t->isAbleToBeToppled());
	CHECK(t->state() == TOPPLE_UPRIGHT);

	// RW 0x8AB9EE: CrusherLevel (1) <= CrushableLevel (1) does not topple
	t->onCollide(light, nullptr, nullptr);
	CHECK(t->state() == TOPPLE_UPRIGHT);
	CHECK(t->isAbleToBeToppled());

	// RW 0x8AB9EE .. 0x8ABA56: CrusherLevel (2) > CrushableLevel (1) topples east (+X) away from heavy at (250, 300)
	t->onCollide(heavy, nullptr, nullptr);
	REQUIRE(t->state() == TOPPLE_FALLING);
	CHECK_FALSE(t->isAbleToBeToppled());
	CHECK(t->toppleDirection().x == 1.0f);
	CHECK(t->toppleDirection().y == 0.0f);
	// speed 0 clamped to MinimumToppleSpeed (1.0): velocity = 0.2 * 1.0 = 0.2, accel = 0.01 * 1.0 = 0.01 (RW 0x8ABAD1, 0x8ABB46)
	CHECK(t->angularVelocity() == doctest::Approx(0.2f)); // tolerance: parsePercentToReal 20 * 0.01f * 1.0f rounding
	CHECK(t->angularAcceleration() == 0.01f);
	// floor(1.52170897 / (2 * 0.2)) = floor(3.80427) = 3 (RW 0x8ABC98 .. 0x8ABCD3)
	CHECK(t->numAngleDeltaX() == 3);
	CHECK(tree->testModelCondition(CombatNames::modelCondition("TOPPLED")));
	CHECK(w.logic->fxEvents().perSite().count("ToppleUpdate ToppleFX") == 1u);

	// StumpName spawned at tree's position (RW 0x8ABD0E .. 0x8ABD80)
	REQUIRE(t->stumpID() != INVALID_ID);
	Object *stump = w.logic->findObjectByID(t->stumpID());
	REQUIRE(stump != nullptr);
	CHECK(stump->getTemplate()->getName() == "TestStump");
	CHECK(stump->getPosition()->x == 300.0f);
	CHECK(stump->getPosition()->y == 300.0f);

	// A second topple call while falling is ignored (RW 0x0068D58C -> RW 0x008AB7FB)
	const Coord3D westDir{ -1.0f, 0.0f, 0.0f };
	tree->topple(&westDir, 5.0f, TOPPLE_OPTIONS_NO_BOUNCE);
	CHECK(t->toppleDirection().x == 1.0f);

	// Step until the first bounce (RW 0x8AC1EB .. 0x8AC260)
	int steps = 0;
	while (t->bounceVelocityDecay() == 0.0f && steps < 30)
	{
		w.logic->runLogicFrame();
		++steps;
	}
	REQUIRE(t->bounceVelocityDecay() == 0.025f); // RW 0xC67BB4
	CHECK(t->angularVelocity() < 0.0f); // tolerance: velocity sign reversal on first bounce (RW 0x8AC239 .. 0x8AC24E)
	CHECK(w.logic->fxEvents().perSite().count("ToppleUpdate BounceFX") == 1u);

	// xfer & crc round-trip mid-fall (RW 0x008ABDBD)
	StateHasher hBefore;
	t->crc(hBefore);
	XferSave save;
	t->xfer(save);
	w.logic->runLogicFrame();
	XferLoad load(save.data());
	t->xfer(load);
	CHECK(load.atEnd());
	StateHasher hAfter;
	t->crc(hAfter);
	CHECK(hBefore.value() == hAfter.value());

	// Run to TOPPLE_DOWN: kills HighlanderBody with 999999 UNRESISTABLE / DEATH_TOPPLED, reorients rubble by GeometryHeight (60) along +X, kills stump
	while (t->state() != TOPPLE_DOWN && steps < 200)
	{
		w.logic->runLogicFrame();
		++steps;
	}
	REQUIRE(t->state() == TOPPLE_DOWN);
	CHECK(tree->isEffectivelyDead());
	CHECK(stump->isEffectivelyDead());
	// ReorientToppledRubble (RW 0x8AC2EA .. 0x8AC373): top of 60-high tree fallen east (+X) moves X past 350
	CHECK(tree->getPosition()->x > 350.0f); // tolerance: rotated 60-unit height projected onto +X past 300 + 50 (RW 0x8AC2EA .. 0x8AC373)
}

// RW 0x008ABAAD (KillWhenStartToppling +0x25, ToppleLeftOrRightOnly +0x27, dead guard +0x458 & 1), RW 0x008ABE65 (pre-destroyed stump guard)
TEST_CASE("LOCAL-102 ToppleUpdate: KillWhenStartToppling, ToppleLeftOrRightOnly, dead-target and pre-destroyed-stump guards")
{
	CombatWorld w(kToppleIni);
	w.combat().setAutoAcquireEnabled(false);

	// 1. KillWhenStartToppling kills immediately and sleeps forever (RW 0x8ABAF9 .. 0x8ABB1A)
	Object *instant = w.unit("TestToppleInstantKill", 'A', 150, 150);
	ToppleUpdate *ti = toppleOf(instant);
	const Coord3D dir{ 1.0f, 1.0f, 0.0f };
	instant->topple(&dir, 1.0f, TOPPLE_OPTIONS_NONE);
	CHECK(instant->isEffectivelyDead());
	CHECK(ti->state() == TOPPLE_UPRIGHT);
	// Calling applyTopplingForce on an already dead object is a no-op (RW 0x8ABAC0)
	ti->applyTopplingForce(&dir, 2.0f, TOPPLE_OPTIONS_NONE);
	CHECK(ti->state() == TOPPLE_UPRIGHT);

	// 2. ToppleLeftOrRightOnly snaps direction to orientation +- PI/2 (RW 0x8ABBF3 .. 0x8ABC61); destroying stump before TOPPLE_DOWN is safe
	Object *lr = w.unit("TestToppleLeftRight", 'A', 400, 400);
	lr->setOrientation(0.0f); // facing +X; left/right are +Y (PI/2) and -Y (-PI/2)
	ToppleUpdate *tlr = toppleOf(lr);
	const Coord3D diag{ 0.7f, 0.7f, 0.0f };
	lr->topple(&diag, 1.0f, TOPPLE_OPTIONS_NO_BOUNCE);
	REQUIRE(tlr->state() == TOPPLE_FALLING);
	CHECK(std::fabs(tlr->toppleDirection().x) < 0.001f); // tolerance: cos(PI/2) float rounding (RW 0x8ABC3F)
	CHECK(tlr->toppleDirection().y == 1.0f);
	REQUIRE(tlr->stumpID() != INVALID_ID);
	if (Object *stump = w.logic->findObjectByID(tlr->stumpID()))
	{
		w.logic->destroyObject(stump);
	}
	w.frames(25);
	CHECK(tlr->state() == TOPPLE_DOWN);
	CHECK(lr->isEffectivelyDead());
}

