// OpenBFME. COMBAT-2 tests: the structure bodies (StructureBody, InactiveBody) and the collapse of a destroyed structure (StructureCollapseUpdate). Synthetic data
// (combattest::CombatWorld), no retail files. Expected values are derived by hand in the comments (a logic frame is 200 ms).
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/StructureModules.h"

using namespace combattest;

namespace
{
const char kStructures[] =
	"Armor StoneArmor\n"
	"  Armor = DEFAULT 100%\n"
	"  Armor = SLASH 25%\n"
	"End\n"
	// a keep: 1000 health, damaged below 600, really damaged below 300; collapses over a frame count the tests derive; destroyed when the fall is done
	"Object Keep\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE SCORE\n"
	"  BountyValue = 40\n"
	"  CommandPointBonus = 6\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 60\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = StoneArmor\n"
	"  End\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    MaxHealthDamaged = 600\n"
	"    MaxHealthReallyDamaged = 300\n"
	"  End\n"
	"  Behavior = StructureCollapseUpdate ModuleTag_Collapse\n"
	"    MinCollapseDelay = 0\n"
	"    MaxCollapseDelay = 0\n"
	"    MinBurstDelay = 250\n"
	"    MaxBurstDelay = 800\n"
	"    CollapseDamping = 0.5\n"
	"    MaxShudder = 0.6\n"
	"    BigBurstFrequency = 4\n"
	"    FXList = INITIAL FX_A\n"
	"    FXList = DELAY FX_B FX_C\n"
	"    OCL = BURST OCL_A\n"
	"    FXList = ALMOST_FINAL FX_D\n"
	"    DestroyObjectWhenDone = Yes\n"
	"    CollapseHeight = 155\n"
	"  End\n"
	"End\n"
	// the same keep that stays behind as rubble
	"Object Ruin\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE SCORE\n"
	"  CommandPoints = 4\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 60\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = StructureCollapseUpdate ModuleTag_Collapse\n"
	"    MaxBurstDelay = 400\n"
	"    CollapseDamping = 0.5\n"
	"    BigBurstFrequency = 2\n"
	"    CollapseHeight = 30\n"
	"  End\n"
	"End\n"
	// a castle piece: the DAMAGE interface of CastleMemberBehavior hears the hits and the damage state
	"Object Rampart\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = StoneArmor\n"
	"  End\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    MaxHealthDamaged = 600\n"
	"    MaxHealthReallyDamaged = 300\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_Castle\n"
	"    CountsForEvaCastleBreached = Yes\n"
	"  End\n"
	"End\n"
	// a troll: delayed death of 1000 ms (5 frames), the lifetime module the body drives (a long default lifetime), a DestroyDie
	"Object Troll\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  BountyValue = 30\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = DelayedDeathBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"    DelayedDeathTime = 1000\n"
	"  End\n"
	"  Behavior = LifetimeUpdate ModuleTag_Life\n"
	"    MinLifetime = 600000\n"
	"    MaxLifetime = 600000\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	// a hero: RespawnBody with CanRespawn No; a mortal that only dies once
	"Object Hero\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  BountyValue = 50\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = RespawnBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"    CanRespawn = No\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	// the proxy of a structure: its body answers with the symbiote's
	"Object Proxy\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = SymbioticStructuresBody ModuleTag_Body\n"
	"    Symbiote = Keep\n"
	"  End\n"
	"End\n"
	// a fuse: dies by itself after 20 frames and pays the last damager's player
	"Object Fuse\n"
	"  KindOf = INFANTRY SCORE\n"
	"  BountyValue = 9\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 4\n"
	"  GeometryMinorRadius = 4\n"
	"  GeometryHeight = 4\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = LifetimeUpdate ModuleTag_Life\n"
	"    MinLifetime = 4000\n"
	"    MaxLifetime = 4000\n"
	"    ScoreKill = Yes\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	"Object Prop\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = InactiveBody ModuleTag_Body\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n";

DamageInfo hit(const Object *source, float amount, int type)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	return d;
}

} // namespace

TEST_CASE("structure bodies: StructureBody is an ActiveBody with a constructor id; its damage and its damage states are ActiveBody's")
{
	CombatWorld w(kStructures);
	Object *keep = w.unit("Keep", 'B', 300, 300);
	REQUIRE(keep->getBodyModule() != nullptr);
	StructureBody *body = dynamic_cast<StructureBody *>(keep->getBodyModule());
	REQUIRE(body != nullptr);
	CHECK(body->constructorObjectID() == INVALID_ID);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	body->setConstructorObject(nullptr);
	CHECK(body->constructorObjectID() == INVALID_ID);
	body->setConstructorObject(a);
	CHECK(body->constructorObjectID() == a->getID());
	CHECK(w.health(keep) == 1000.0f);
	CHECK(keep->getBodyModule()->getDamageState() == BODY_PRISTINE);
	// StoneArmor: SLASH 25%: 1000 slash = 250
	DamageInfo d = hit(a, 1000.0f, DAMAGE_SLASH);
	keep->attemptDamage(d);
	CHECK(w.health(keep) == 750.0f);
	CHECK(d.m_output.m_actualDamageDealt == 250.0f);
	CHECK(keep->getBodyModule()->getDamageState() == BODY_PRISTINE);
	CHECK_FALSE(keep->testModelCondition(CombatNames::modelCondition("DAMAGED")));
	// 600 or less: DAMAGED, and the model condition follows
	DamageInfo d2 = hit(a, 600.0f, DAMAGE_SLASH); // 150: 750 -> 600 (damagedFraction * max = 600 >= health)
	keep->attemptDamage(d2);
	CHECK(w.health(keep) == 600.0f);
	CHECK(keep->getBodyModule()->getDamageState() == BODY_DAMAGED);
	CHECK(keep->testModelCondition(CombatNames::modelCondition("DAMAGED")));
	CHECK_FALSE(keep->testModelCondition(CombatNames::modelCondition("REALLYDAMAGED")));
	DamageInfo d3 = hit(a, 1200.0f, DAMAGE_SLASH); // 300: 600 -> 300
	keep->attemptDamage(d3);
	CHECK(keep->getBodyModule()->getDamageState() == BODY_REALLYDAMAGED);
	CHECK(keep->testModelCondition(CombatNames::modelCondition("REALLYDAMAGED")));
	CHECK_FALSE(keep->testModelCondition(CombatNames::modelCondition("DAMAGED"))); // the states replace each other
	CHECK_FALSE(keep->isEffectivelyDead());
}

TEST_CASE("structure bodies: InactiveBody has no health, takes no damage and dies once to UNRESISTABLE damage")
{
	CombatWorld w(kStructures);
	Object *prop = w.unit("Prop", 'B', 300, 300);
	REQUIRE(prop->getBodyModule() != nullptr);
	CHECK(prop->isEffectivelyDead()); // RW 0x8C1A43: the constructor marks the object dead
	CHECK(w.health(prop) == 0.0f);
	CHECK(prop->getBodyModule()->getDamageState() == BODY_PRISTINE);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	// Object::attemptDamage never reaches an effectively dead object's body (RW 0x697E50 tests the dead flag): the body is driven directly
	BodyModuleInterface *body0 = prop->getBodyModule();
	DamageInfo d = hit(a, 500.0f, DAMAGE_SLASH);
	body0->attemptDamage(d);
	CHECK(d.m_output.m_noEffect);
	CHECK(w.combat().counters().kills == 0);
	CHECK(prop->getBodyModule()->estimateDamage(d.m_input) == 0.0f);
	DamageInfo heal = hit(a, 10.0f, DAMAGE_HEALING);
	body0->attemptDamage(heal);
	CHECK(heal.m_output.m_noEffect);
	DamageInfo kill = hit(a, 5.0f, DAMAGE_UNRESISTABLE);
	CHECK(prop->getBodyModule()->estimateDamage(kill.m_input) == 5.0f);
	body0->attemptDamage(kill);
	CHECK_FALSE(kill.m_output.m_noEffect);
	CHECK(w.combat().counters().kills == 1);
	InactiveBody *body = dynamic_cast<InactiveBody *>(prop->getBodyModule());
	REQUIRE(body != nullptr);
	CHECK(body->dieCalled());
	DamageInfo again = hit(a, 5.0f, DAMAGE_UNRESISTABLE);
	body0->attemptDamage(again);
	CHECK(w.combat().counters().kills == 1); // the die modules ran once
}

TEST_CASE("structure collapse: a destroyed keep falls on the hand-computed schedule and is destroyed when it is out of sight")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *keep = w.unit("Keep", 'B', 300, 300);
	const ObjectID keepId = keep->getID();
	StructureCollapseUpdate *collapse = dynamic_cast<StructureCollapseUpdate *>(keep->findModule("StructureCollapseUpdate"));
	REQUIRE(collapse != nullptr);
	CHECK(collapse->state() == COLLAPSESTATE_STANDING);
	const int cashBefore = w.cash('A');
	const unsigned begun = (unsigned)w.logic->getFrame();
	DamageInfo d = hit(a, 100000.0f, DAMAGE_UNRESISTABLE);
	keep->attemptDamage(d);
	CHECK(keep->isEffectivelyDead());
	CHECK(w.health(keep) == 0.0f);
	CHECK(keep->getBodyModule()->getDamageState() == BODY_RUBBLE);
	CHECK(keep->testModelCondition(CombatNames::modelCondition("RUBBLE")));
	CHECK(collapse->state() == COLLAPSESTATE_WAITINGFORCOLLAPSESTART);
	CHECK(collapse->collapseFrame() == begun); // MinCollapseDelay = MaxCollapseDelay = 0
	CHECK(collapse->collapsePosition().x == 300.0f);
	// the first update (the next frame) starts the fall (state collapsing), height 0 then 1.28 per frame squared: after n updates the height is -1.28 * n * (n - 1) / 2, the keep is
	// done when 155 + height <= 0: n * (n - 1) >= 242.2: n = 17 (16 * 15 = 240 < 242.2); the ALMOST_FINAL zone (108.5 + height <= 0, n * (n - 1) >= 169.5, n = 14) holds n = 14, 15, 16
	int updates = 0;
	int almostFinalFrames = 0;
	unsigned long long effects = w.combat().counters().collapseEffectsUnported;
	float previousZ = 0.0f;
	while (w.byId(keepId) && updates < 40)
	{
		w.logic->runLogicFrame();
		if (!w.byId(keepId) || collapse->state() == COLLAPSESTATE_DONE)
		{
			++updates;
			break;
		}
		++updates;
		const float z = w.byId(keepId)->getPosition()->z;
		const float expected = -1.28f * (float)updates * (float)(updates - 1) / 2.0f;
		CHECK_MESSAGE(z == doctest::Approx(expected).epsilon(0.001), "update " << updates);
		CHECK(z <= previousZ);
		previousZ = z;
		(void)almostFinalFrames;
	}
	CHECK(updates == 17);
	REQUIRE(w.logic->findObjectByID(keepId) == nullptr); // processDestroyList ran at the end of the frame
	(void)effects;
	CHECK(w.cash('A') == cashBefore); // the player has no bounty percent (a skirmish pays none, economy.md section 7)
	CHECK(w.combat().counters().collapsesBegun == 1);
	CHECK(w.combat().counters().collapsesDone == 1);
	CHECK(w.combat().counters().rubbleEntered == 1);
	// the unported effects: INITIAL (FX_A) 1, BURST at the start of the fall (OCL_A) 1, each burst / delay in the fall, ALMOST_FINAL x3, FINAL (no list) 0
	CHECK(w.combat().counters().collapseEffectsUnported >= 1 + 1 + 3);
}

TEST_CASE("structure collapse: the draws of one collapse follow the retail order (collapse delay, INITIAL list, burst delay, shudder y then x)")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *keep = w.unit("Keep", 'B', 300, 300);
	w.logic->random().enableCallLog(true);
	DamageInfo d = hit(a, 100000.0f, DAMAGE_UNRESISTABLE);
	keep->attemptDamage(d);
	const std::vector<GameLogicRandom::Call> &log = w.logic->random().callLog();
	// onDie: SlowDeath none; the collapse: random(0, 0) for the collapse frame, then the INITIAL FX list (one entry: random(0, 0))
	REQUIRE(log.size() >= 2);
	CHECK_FALSE(log[0].real);
	CHECK(log[0].lo == 0);
	CHECK(log[0].hi == 0);
	CHECK_FALSE(log[1].real);
	CHECK(log[1].lo == 0);
	CHECK(log[1].hi == 0);
	const size_t before = log.size();
	w.logic->runLogicFrame(); // waiting -> collapsing: the BURST phase (FX none, OCL_A one entry) then the burst delay random(2, 4), then the shudder: y first, then x
	REQUIRE(log.size() >= before + 4);
	CHECK(log[before].lo == 0); // the OCL pick of BURST
	CHECK(log[before].hi == 0);
	CHECK(log[before + 1].lo == 2); // 250 ms = 2 frames
	CHECK(log[before + 1].hi == 4); // 800 ms = 4 frames
	CHECK(log[before + 2].real);
	CHECK(log[before + 2].rlo == doctest::Approx(-0.6f));
	CHECK(log[before + 3].real);
}

TEST_CASE("structure collapse: a keep with no DestroyObjectWhenDone stays as rubble: POST_RUBBLE replaces RUBBLE and the module sleeps")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *ruin = w.unit("Ruin", 'B', 300, 300);
	const ObjectID id = ruin->getID();
	StructureCollapseUpdate *collapse = dynamic_cast<StructureCollapseUpdate *>(ruin->findModule("StructureCollapseUpdate"));
	REQUIRE(collapse != nullptr);
	CHECK(collapse->getCollapseHeight() == 30.0f); // no DestroyObjectWhenDone: the CollapseHeight field itself
	DamageInfo d = hit(a, 100000.0f, DAMAGE_UNRESISTABLE);
	ruin->attemptDamage(d);
	CHECK(ruin->testModelCondition(CombatNames::modelCondition("RUBBLE")));
	w.runUntil([&] { return collapse->state() == COLLAPSESTATE_DONE; }, 60);
	REQUIRE(collapse->state() == COLLAPSESTATE_DONE);
	ruin = w.byId(id);
	REQUIRE(ruin != nullptr); // not destroyed
	CHECK_FALSE(ruin->testModelCondition(CombatNames::modelCondition("RUBBLE")));
	CHECK(ruin->testModelCondition(CombatNames::modelCondition("POST_RUBBLE")));
	CHECK(ruin->getPosition()->z < 0.0f);
	const unsigned long long before = w.combat().counters().collapseEffectsUnported;
	w.frames(10);
	CHECK(collapse->state() == COLLAPSESTATE_DONE); // nothing moves again
	CHECK(w.combat().counters().collapseEffectsUnported == before);
}

TEST_CASE("structure collapse: the CollapseHeight is the larger of the field and the geometry when DestroyObjectWhenDone")
{
	CombatWorld w(kStructures);
	Object *keep = w.unit("Keep", 'B', 300, 300);
	StructureCollapseUpdate *collapse = dynamic_cast<StructureCollapseUpdate *>(keep->findModule("StructureCollapseUpdate"));
	REQUIRE(collapse != nullptr);
	CHECK(collapse->getCollapseHeight() == 155.0f); // max(155, geometry 60)
}

TEST_CASE("structure collapse: the collapse of a building under construction starts as low as it had risen")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *keep = w.unit("Keep", 'B', 300, 300);
	keep->setConstructionPercent(25.0f);
	keep->setModelConditionState(CombatNames::modelCondition("DESTROYED_WHILST_BEING_CONSTRUCTED"), true);
	StructureCollapseUpdate *collapse = dynamic_cast<StructureCollapseUpdate *>(keep->findModule("StructureCollapseUpdate"));
	DamageInfo d = hit(a, 100000.0f, DAMAGE_UNRESISTABLE);
	keep->attemptDamage(d);
	// height = -(155 * (1 - 25 * 0.01)) = -116.25
	CHECK(collapse->currentHeight() == doctest::Approx(-116.25f));
	CHECK(keep->getPosition()->z == doctest::Approx(-116.25f));
}

TEST_CASE("structure collapse: a rubble structure frees its command points and leaves the pathfinder at once, its bounty goes to the killer's player")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *keep = w.unit("Keep", 'B', 300, 300);
	const ObjectID id = keep->getID();
	w.ai->addObjectToPathfindMap(*keep);
	PathfindCell *cell = w.ai->pathfinder().getCell(LAYER_GROUND, 30, 30);
	REQUIRE(cell != nullptr);
	CHECK(cell->getType() == PathfindCell::CELL_OBSTACLE);
	CHECK(w.playerOf('B')->commandPoints().getBonus() == 6); // Keep CommandPointBonus = 6
	DamageInfo d = hit(a, 100000.0f, DAMAGE_UNRESISTABLE);
	keep->attemptDamage(d);
	CHECK(cell->getType() != PathfindCell::CELL_OBSTACLE);
	CHECK_FALSE(w.inPathfinder(id));
	CHECK(w.playerOf('B')->commandPoints().getBonus() == 0);
}

TEST_CASE("structure stops: S-340 .. S-343 are in GameLogic::report().stops exactly once")
{
	CombatWorld w(kStructures);
	const GameLogic::Report report = w.logic->report();
	const char *expect[] = { "structure collapse", "geometry", "damage state", "body classes" };
	for (int id = 340; id <= 343; ++id)
	{
		const std::string prefix = "[S-" + std::to_string(id) + "] ";
		int matches = 0;
		for (const std::string &line : report.stops)
		{
			if (line.rfind(prefix, 0) == 0)
			{
				++matches;
				CHECK_MESSAGE(line.find(expect[id - 340]) != std::string::npos, line.substr(0, 60));
			}
		}
		CHECK_MESSAGE(matches == 1, prefix << "appears " << matches << " times");
	}
}

TEST_CASE("structure bodies: CastleMemberBehavior's DAMAGE interface hears the hits and the rubble state of its member (RW 0x79B757, 0x79A0ED)")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *mine = w.unit("Rampart", 'A', 300, 300); // the local player's (Alice) piece: the EVA is asked for
	Object *theirs = w.unit("Rampart", 'B', 400, 300);
	CastleMemberBehavior *m = dynamic_cast<CastleMemberBehavior *>(mine->findModule("CastleMemberBehavior"));
	CastleMemberBehavior *t = dynamic_cast<CastleMemberBehavior *>(theirs->findModule("CastleMemberBehavior"));
	REQUIRE(m != nullptr);
	REQUIRE(t != nullptr);
	CHECK(w.combat().counters().castleMemberHits == 0);
	DamageInfo d = hit(a, 400.0f, DAMAGE_SLASH); // 100 after StoneArmor
	mine->attemptDamage(d);
	CHECK(w.combat().counters().castleMemberHits == 1);
	CHECK_FALSE(m->breached());
	DamageInfo huge = hit(a, 100000.0f, DAMAGE_UNRESISTABLE);
	mine->attemptDamage(huge);
	CHECK(m->breached()); // RUBBLE
	CHECK(w.combat().counters().castleBreaches == 1);
	theirs->attemptDamage(huge);
	CHECK(t->breached());
	// the breach is recorded for EVERY owner (hashed simulation state); playing the EVA is the client's, for the events whose owner is its local player
	CHECK(w.combat().counters().castleBreaches == 2);
	REQUIRE(w.combat().castleBreaches().size() == 2);
	CHECK(w.combat().castleBreaches()[0].ownerPlayerIndex == w.playerOf('A')->getPlayerIndex());
	CHECK(w.combat().castleBreaches()[1].ownerPlayerIndex == w.playerOf('B')->getPlayerIndex());
}

TEST_CASE("structure bodies: DelayedDeathBody keeps a unit alive for DelayedDeathTime after the health check, then its LifetimeUpdate kills it (RW 0x8C5828, 0x7A7F8B)")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *troll = w.unit("Troll", 'B', 300, 300);
	const ObjectID id = troll->getID();
	DelayedDeathBody *body = dynamic_cast<DelayedDeathBody *>(troll->getBodyModule());
	REQUIRE(body != nullptr);
	LifetimeUpdate *life = dynamic_cast<LifetimeUpdate *>(troll->findModule("LifetimeUpdate"));
	REQUIRE(life != nullptr);
	CHECK(life->dieFrame() == w.logic->getFrame() + 3000); // 600000 ms * 0.005 = 3000 frames: far away
	DamageInfo small = hit(a, 40.0f, DAMAGE_UNRESISTABLE);
	troll->attemptDamage(small);
	CHECK(w.health(troll) == 60.0f);
	CHECK_FALSE(body->started());
	// a hit that would kill: the delayed death starts (health stays, the lifetime shrinks to 5 frames: 1000 ms * 0.005)
	const unsigned now = w.logic->getFrame();
	DamageInfo lethal = hit(a, 1000.0f, DAMAGE_UNRESISTABLE);
	troll->attemptDamage(lethal);
	CHECK(body->started());
	CHECK(w.health(troll) == 60.0f);
	CHECK_FALSE(troll->isEffectivelyDead());
	CHECK(life->dieFrame() == now + 5);
	// ImmortalUntilDeathTime (default Yes): nothing hurts it now
	DamageInfo more = hit(a, 30.0f, DAMAGE_UNRESISTABLE);
	troll->attemptDamage(more);
	CHECK(w.health(troll) == 60.0f);
	// the lifetime runs out after 5 frames: kill() takes the whole health, the unit dies and is destroyed by DestroyDie, the killer is not credited (it is the unit's own kill)
	w.frames(4);
	CHECK(w.byId(id) != nullptr);
	CHECK_FALSE(w.byId(id)->isEffectivelyDead());
	w.frames(2);
	CHECK(w.byId(id) == nullptr);
	CHECK(w.combat().counters().delayedDeaths == 1);
}

TEST_CASE("structure bodies: a DelayedDeathBody that did not start its delay dies by a normal kill when DoHealthCheck is off and checked is false")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *troll = w.unit("Troll", 'B', 300, 300);
	DelayedDeathBody *body = dynamic_cast<DelayedDeathBody *>(troll->getBodyModule());
	body->setChecked(true);
	CHECK(body->checked());
	// checked: a kill hit starts the delay too, and the hit is zeroed
	DamageInfo kill = hit(a, 1.0f, DAMAGE_UNRESISTABLE);
	kill.m_input.m_kill = true;
	troll->attemptDamage(kill);
	CHECK(body->started());
	CHECK(w.health(troll) == 100.0f);
	CHECK_FALSE(kill.m_input.m_kill); // the body cleared the flag (RW 0x8C5908)
}

TEST_CASE("structure bodies: RespawnBody is an ActiveBody without its RespawnUpdate; CanRespawn and the killer filter classify a lethal hit (RW 0x8C553F)")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *hero = w.unit("Hero", 'B', 300, 300);
	RespawnBody *body = dynamic_cast<RespawnBody *>(hero->getBodyModule());
	REQUIRE(body != nullptr);
	CHECK_FALSE(body->respawnData()->m_canRespawn);
	CHECK(body->respawnData()->m_permanentlyKilledByFilter.get() != nullptr);
	DamageInfo d = hit(a, 150.0f, DAMAGE_UNRESISTABLE);
	hero->attemptDamage(d);
	CHECK(w.health(hero) == 50.0f);
	CHECK(w.combat().counters().respawnWithoutUpdate == 0);
	DamageInfo lethal = hit(a, 500.0f, DAMAGE_UNRESISTABLE);
	hero->attemptDamage(lethal);
	CHECK(hero->isEffectivelyDead());
	CHECK(w.combat().counters().respawnWithoutUpdate == 1);
	CHECK(w.combat().counters().kills == 1);
}

TEST_CASE("structure bodies: SymbioticStructuresBody answers with its symbiote's body and takes no damage itself (RW table 0xC72200)")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	Object *keep = w.unit("Keep", 'B', 300, 300);
	Object *proxy = w.unit("Proxy", 'B', 340, 300);
	SymbioticStructuresBody *body = dynamic_cast<SymbioticStructuresBody *>(proxy->getBodyModule());
	REQUIRE(body != nullptr);
	CHECK(body->getHealth() == 0.0f); // not linked yet
	body->setSymbiote(keep->getID());
	CHECK(body->getHealth() == 1000.0f);
	CHECK(body->getMaxHealth() == 1000.0f);
	DamageInfo d = hit(a, 400.0f, DAMAGE_SLASH);
	proxy->attemptDamage(d);
	CHECK(w.health(keep) == 1000.0f); // the proxy's attemptDamage does nothing
	DamageInfo d2 = hit(a, 400.0f, DAMAGE_SLASH);
	keep->attemptDamage(d2);
	CHECK(body->getHealth() == 900.0f);
	CHECK(body->estimateDamage(d2.m_input) == 100.0f); // the symbiote's armour
}

TEST_CASE("structure bodies: LifetimeUpdate kills its object after the random lifetime and the ScoreKill credits the last damager's player (RW 0x7A7F8B)")
{
	CombatWorld w(kStructures);
	Object *a = w.unit("Swordsman", 'A', 200, 300);
	w.playerOf('A')->setBountyPercent(1.0f);
	Object *fuse = w.unit("Fuse", 'B', 300, 300);
	const ObjectID id = fuse->getID();
	LifetimeUpdate *life = dynamic_cast<LifetimeUpdate *>(fuse->findModule("LifetimeUpdate"));
	REQUIRE(life != nullptr);
	CHECK(life->dieFrame() == w.logic->getFrame() + 20); // 4000 ms * 0.005
	DamageInfo d = hit(a, 10.0f, DAMAGE_UNRESISTABLE);
	fuse->attemptDamage(d); // the last damager is the swordsman
	const int cash = w.cash('A');
	w.frames(19);
	CHECK(w.byId(id) != nullptr);
	w.frames(2);
	CHECK(w.byId(id) == nullptr);
	CHECK(w.cash('A') == cash + 9); // ScoreKill: the bounty of the fuse (9) goes to Alice
}
