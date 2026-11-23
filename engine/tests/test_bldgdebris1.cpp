// OpenBFME unit tests (lane BLDGDEBRIS-1): ActiveBody's DamageCreationList (parse RW 0x8C3F1F, the rock loop of doDamageFX RW 0x8C2F02) and the siege side
// damage (attemptDamage RW 0x8C3FA3 -> RW 0x8C3E70 -> the distributor RW 0x8C3CB8, internalChangeHealth RW 0x8C31A5, the side stage of setCorrectDamageState
// RW 0x8C2A5D, the DESTROYED_<side> model conditions of Object RW 0x6903B6). Synthetic data (combattest::CombatWorld) and one retail case on a RotWK skirmish
// (the civilian barracks ElvenBarracksBFME1, OCL_RBBarracks_Chunk1 .. 4); the retail case SKIPs loudly when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "HordeCompleteTestUtil.h"
#include "BuildTestUtil.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"

#include <string>
#include <vector>

using namespace combattest;

namespace
{
// DebrisHut: a STRUCTURE with the shape of the retail rows (civilianbuildings.ini): a stage-0 CATAPULT_ROCK list and one chunk per side. Max health 1000:
// the side cap / threshold is (1000 * 0.25) * 0.75 = 187.5
const char kDebrisObjects[] =
	"Object DebrisBit\n  KindOf = NO_COLLIDE\n  Geometry = Sphere\n  GeometryMajorRadius = 1\nEnd\n"
	"Object ChunkFront\n  KindOf = NO_COLLIDE\n  Geometry = Sphere\n  GeometryMajorRadius = 1\nEnd\n"
	"Object ChunkRight\n  KindOf = NO_COLLIDE\n  Geometry = Sphere\n  GeometryMajorRadius = 1\nEnd\n"
	"Object ChunkBack\n  KindOf = NO_COLLIDE\n  Geometry = Sphere\n  GeometryMajorRadius = 1\nEnd\n"
	"Object ChunkLeft\n  KindOf = NO_COLLIDE\n  Geometry = Sphere\n  GeometryMajorRadius = 1\nEnd\n"
	"Object DebrisHut\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n    MaxHealthDamaged = 750\n    MaxHealthReallyDamaged = 500\n"
	"    DamageCreationList = OCL_TestDebris CATAPULT_ROCK\n"
	"    DamageCreationList = OCL_TestTrebuchetDebris TREBUCHET_ROCK\n"
	"    DamageCreationList = OCL_TestFront CATAPULT_ROCK FRONT_DESTROYED\n"
	"    DamageCreationList = OCL_TestBack CATAPULT_ROCK BACK_DESTROYED\n"
	"    DamageCreationList = OCL_TestRight CATAPULT_ROCK RIGHT_DESTROYED\n"
	"    DamageCreationList = OCL_TestLeft CATAPULT_ROCK LEFT_DESTROYED\n"
	"    DamageCreationList = None CATAPULT_ROCK\n"
	"  End\n"
	"  Geometry = BOX\n  GeometryMajorRadius = 20\n  GeometryMinorRadius = 20\n  GeometryHeight = 20\n"
	"End\n";

const char kDebrisLists[] =
	"ObjectCreationList OCL_TestDebris\n  CreateObject\n    ObjectNames = DebrisBit\n    Count = 2\n  End\nEnd\n"
	"ObjectCreationList OCL_TestTrebuchetDebris\n  CreateObject\n    ObjectNames = DebrisBit\n    Count = 7\n  End\nEnd\n"
	"ObjectCreationList OCL_TestFront\n  CreateObject\n    ObjectNames = ChunkFront\n    Count = 1\n  End\nEnd\n"
	"ObjectCreationList OCL_TestRight\n  CreateObject\n    ObjectNames = ChunkRight\n    Count = 1\n  End\nEnd\n"
	"ObjectCreationList OCL_TestBack\n  CreateObject\n    ObjectNames = ChunkBack\n    Count = 1\n  End\nEnd\n"
	"ObjectCreationList OCL_TestLeft\n  CreateObject\n    ObjectNames = ChunkLeft\n    Count = 1\n  End\nEnd\n";

struct DebrisWorld : CombatWorld
{
	ObjectCreationListStore ocls{ w.keys };
	ObjectCreationListStore *savedOcl = TheObjectCreationListStore;
	DebrisWorld()
		: CombatWorld(kDebrisObjects)
	{
		TheObjectCreationListStore = &ocls;
		w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
		const std::string err = w.load(kDebrisLists, INI_LOAD_OVERWRITE, "debris_ocl.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~DebrisWorld()
	{
		logic->reset();
		TheObjectCreationListStore = savedOcl;
	}
	size_t count(const char *name) const
	{
		size_t n = 0;
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			n += !o->isDestroyed() && o->getTemplate()->getName() == name;
		}
		return n;
	}
};

DamageInfo siegeHit(float amount, const Object *source, int fxTrigger = 1, int type = DAMAGE_SIEGE)
{
	DamageInfo d;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	d.m_input.m_fxTrigger = fxTrigger;
	d.m_input.m_sourceID = source ? source->getID() : INVALID_ID;
	return d;
}

ActiveBody *bodyOf(Object *o)
{
	ActiveBody *b = dynamic_cast<ActiveBody *>(o->getBodyModule());
	REQUIRE(b != nullptr);
	return b;
}

bool hasCondition(const Object *o, const char *name)
{
	return o->testModelCondition(CombatNames::modelCondition(name));
}
} // namespace

TEST_CASE("BLDGDEBRIS-1 parse: DamageCreationList rows are { OCL, rock, side } entries (RW 0x8C3F1F); unknown rock / side names are INI errors")
{
	DebrisWorld w;
	const ActiveBodyModuleData *d =
		dynamic_cast<const ActiveBodyModuleData *>(w.w.get("DebrisHut")->behaviorModules().find("ModuleTag_Body")->data.get());
	REQUIRE(d);
	REQUIRE(d->m_damageCreationList.size() == 7);
	CHECK(d->m_damageCreationList[0].ocl == "OCL_TestDebris");
	CHECK(d->m_damageCreationList[0].rock == 1); // CATAPULT_ROCK (RW 0xDB2AF4)
	CHECK(d->m_damageCreationList[0].stage == 0); // absent
	CHECK(d->m_damageCreationList[1].rock == 2); // TREBUCHET_ROCK
	CHECK(d->m_damageCreationList[2].stage == 1); // FRONT_DESTROYED (RW 0xDB2B04)
	CHECK(d->m_damageCreationList[3].stage == 3); // BACK_DESTROYED
	CHECK(d->m_damageCreationList[4].stage == 2); // RIGHT_DESTROYED
	CHECK(d->m_damageCreationList[5].stage == 4); // LEFT_DESTROYED
	CHECK(d->m_damageCreationList[6].ocl.empty()); // "None" (RW 0x73A368)
	const std::string badSide = w.w.load("Object BadSide\n  Body = ActiveBody B\n    DamageCreationList = OCL_TestDebris CATAPULT_ROCK TOP_DESTROYED\n  End\nEnd\n",
		INI_LOAD_OVERWRITE, "bad.ini");
	CHECK_FALSE(badSide.empty());
	const std::string badRock = w.w.load("Object BadRock\n  Body = ActiveBody B\n    DamageCreationList = OCL_TestDebris PEBBLE\n  End\nEnd\n",
		INI_LOAD_OVERWRITE, "bad2.ini");
	CHECK_FALSE(badRock.empty());
}

TEST_CASE("BLDGDEBRIS-1 rock debris: a hit whose FX trigger matches a stage-0 row creates its OCL, every hit, any damage type (RW 0x8C2F75 .. 0x8C2FB8)")
{
	DebrisWorld w;
	Object *hut = w.unit("DebrisHut", 'B', 500, 500);
	Object *src = w.unit("Walker", 'A', 700, 500);
	REQUIRE(hut);
	REQUIRE(src);
	DamageInfo rock = siegeHit(10.0f, src);
	hut->attemptDamage(rock);
	CHECK(w.count("DebrisBit") == 2);
	DamageInfo again = siegeHit(10.0f, src);
	hut->attemptDamage(again); // no throttle
	CHECK(w.count("DebrisBit") == 4);
	DamageInfo plain = siegeHit(10.0f, src, 0); // FX trigger NONE: no row matches
	hut->attemptDamage(plain);
	CHECK(w.count("DebrisBit") == 4);
	DamageInfo trebuchet = siegeHit(10.0f, src, 2, DAMAGE_CRUSH); // TREBUCHET_ROCK: its own row; the damage type does not matter
	hut->attemptDamage(trebuchet);
	CHECK(w.count("DebrisBit") == 11);
	// the side rows (stage != 0) are never created by the rock loop
	CHECK(w.count("ChunkFront") + w.count("ChunkRight") + w.count("ChunkBack") + w.count("ChunkLeft") == 0);
}

TEST_CASE("BLDGDEBRIS-1 sides: SIEGE damage fills the side facing the source first, then the next by score, each up to (max * 0.25) * 0.75 (RW 0x8C3CB8)")
{
	DebrisWorld w;
	Object *hut = w.unit("DebrisHut", 'B', 500, 500);
	Object *front = w.unit("Walker", 'A', 700, 500); // +X: the object's X axis (angle 0)
	REQUIRE(hut);
	REQUIRE(front);
	ActiveBody *b = bodyOf(hut);
	DamageInfo first = siegeHit(100.0f, front);
	hut->attemptDamage(first);
	CHECK(b->sideDamage(0) == 100.0f);
	CHECK(b->sideDamage(1) == 0.0f);
	// 300 more: the front tops at 187.5, the rest goes to the next side. Scores from +X: [200, 0, -200, 0] -> order 0, 1, 3, 2 (ties keep index order)
	DamageInfo second = siegeHit(300.0f, front);
	hut->attemptDamage(second);
	CHECK(b->sideDamage(0) == 187.5f);
	CHECK(b->sideDamage(1) == 187.5f); // capped: 87.5 went to the front, 25 is left
	CHECK(b->sideDamage(3) == 25.0f);
	CHECK(b->sideDamage(2) == 0.0f);
	// a non-SIEGE hit adds nothing to the sides
	DamageInfo crush = siegeHit(50.0f, front, 0, DAMAGE_CRUSH);
	hut->attemptDamage(crush);
	CHECK(b->sideDamage(3) == 25.0f);
	CHECK(b->sideDamage(2) == 0.0f);
	// a source that is gone: no sides (RW 0x8C3E8C)
	DamageInfo orphan = siegeHit(50.0f, nullptr);
	hut->attemptDamage(orphan);
	CHECK(b->sideDamage(3) == 25.0f);

	// from -Y (the right side, s1 = Y . d): side 1 first
	Object *hut2 = w.unit("DebrisHut", 'B', 300, 300);
	Object *right = w.unit("Walker", 'A', 300, 100);
	ActiveBody *b2 = bodyOf(hut2);
	DamageInfo r = siegeHit(30.0f, right);
	hut2->attemptDamage(r);
	CHECK(b2->sideDamage(1) == 30.0f);
	CHECK(b2->sideDamage(0) == 0.0f);
}

TEST_CASE("BLDGDEBRIS-1 stage: while REALLYDAMAGED a full side sets the stage, DESTROYED_<side> and creates its chunk; healing lowers the values; RUBBLE clears")
{
	DebrisWorld w;
	Object *hut = w.unit("DebrisHut", 'B', 500, 500);
	Object *front = w.unit("Walker", 'A', 700, 500);
	REQUIRE(hut);
	REQUIRE(front);
	ActiveBody *b = bodyOf(hut);
	DamageInfo a = siegeHit(300.0f, front, 0); // 700: DAMAGED; front 187.5, right 112.5
	hut->attemptDamage(a);
	REQUIRE(b->getDamageState() == BODY_DAMAGED);
	CHECK(b->sideStage() == 0);
	DamageInfo c = siegeHit(250.0f, front, 0); // 450: REALLYDAMAGED. The state changed in this call: no side test (RW 0x8C2C78)
	hut->attemptDamage(c);
	REQUIRE(b->getDamageState() == BODY_REALLYDAMAGED);
	REQUIRE(hasCondition(hut, "REALLYDAMAGED"));
	CHECK(b->sideStage() == 0);
	CHECK(b->sideDamage(1) == 187.5f);
	CHECK(b->sideDamage(3) == 175.0f);
	CHECK(w.count("ChunkFront") == 0);
	DamageInfo d = siegeHit(1.0f, front, 0); // same state: the first full side with a clear flag, in index order
	hut->attemptDamage(d);
	CHECK(b->sideStage() == 1);
	CHECK(b->sideDestroyed(0));
	CHECK(w.count("ChunkFront") == 1);
	CHECK(hasCondition(hut, "DESTROYED_FRONT"));
	DamageInfo e = siegeHit(1.0f, front, 0);
	hut->attemptDamage(e);
	CHECK(b->sideStage() == 2);
	CHECK(b->sideDestroyed(1));
	CHECK(w.count("ChunkRight") == 1);
	CHECK(hasCondition(hut, "DESTROYED_RIGHT"));
	CHECK_FALSE(hasCondition(hut, "DESTROYED_FRONT"));
	// side 3 is at 178: below the threshold, and the flagged sides stay flagged: nothing more
	DamageInfo f = siegeHit(1.0f, front, 0);
	hut->attemptDamage(f);
	CHECK(b->sideStage() == 2);
	CHECK(w.count("ChunkFront") == 1);
	CHECK(w.count("ChunkRight") == 1);

	// healing: each value - (delta * 0.25) * 0.75, floored at 0 (RW 0x8C3221 .. 0x8C3262)
	const float left = b->sideDamage(3);
	hut->attemptHealing(40.0f, nullptr);
	CHECK(b->sideDamage(3) == left - 7.5f);
	CHECK(b->sideDamage(2) == 0.0f);

	// RUBBLE: the stage goes to 0 (a STRUCTURE, RW 0x8C2E46) and the four DESTROYED_<side> conditions are cleared (RW 0x690503)
	DamageInfo kill = siegeHit(1.0f, front, 0);
	kill.m_input.m_kill = true;
	hut->attemptDamage(kill);
	REQUIRE(b->getDamageState() == BODY_RUBBLE);
	CHECK(b->sideStage() == 0);
	for (const char *n : { "DESTROYED_FRONT", "DESTROYED_RIGHT", "DESTROYED_BACK", "DESTROYED_LEFT" })
	{
		CHECK_FALSE(hasCondition(hut, n));
	}
}

TEST_CASE("BLDGDEBRIS-1 stage: a destroyed side stays destroyed after a full repair (RW 0x8C2A5D never clears the flags), the next full side goes next")
{
	DebrisWorld w;
	Object *hut = w.unit("DebrisHut", 'B', 500, 500);
	Object *front = w.unit("Walker", 'A', 700, 500);
	ActiveBody *b = bodyOf(hut);
	DamageInfo a = siegeHit(300.0f, front, 0);
	hut->attemptDamage(a);
	DamageInfo c = siegeHit(250.0f, front, 0);
	hut->attemptDamage(c);
	DamageInfo d = siegeHit(1.0f, front, 0);
	hut->attemptDamage(d);
	REQUIRE(b->sideStage() == 1);
	REQUIRE(w.count("ChunkFront") == 1);
	b->internalChangeHealth(1000.0f); // over the maximum: the values go to 0, the flags stay (PRISTINE: the visual update clears DESTROYED_FRONT)
	CHECK(b->sideDamage(0) == 0.0f);
	CHECK(b->sideDestroyed(0));
	CHECK_FALSE(hasCondition(hut, "DESTROYED_FRONT"));
	// the same siege again: the front fills first and is full, but its flag is set: the right side (index 1) is the next stage
	DamageInfo e = siegeHit(300.0f, front, 0);
	hut->attemptDamage(e);
	DamageInfo f = siegeHit(250.0f, front, 0);
	hut->attemptDamage(f);
	DamageInfo g = siegeHit(1.0f, front, 0);
	hut->attemptDamage(g);
	CHECK(b->sideStage() == 2);
	CHECK(w.count("ChunkFront") == 1);
	CHECK(w.count("ChunkRight") == 1);
	CHECK(hasCondition(hut, "DESTROYED_RIGHT"));
}

TEST_CASE("BLDGDEBRIS-1 rubble: a building destroyed while ACTIVELY_BEING_CONSTRUCTED gets DESTROYED_WHILST_BEING_CONSTRUCTED for 3 s (RW 0x690432 .. 0x690458)")
{
	DebrisWorld w;
	Object *hut = w.unit("DebrisHut", 'B', 500, 500);
	Object *other = w.unit("DebrisHut", 'B', 300, 300);
	const int beingBuilt = CombatNames::modelCondition("ACTIVELY_BEING_CONSTRUCTED");
	const int whilst = CombatNames::modelCondition("DESTROYED_WHILST_BEING_CONSTRUCTED");
	hut->setModelConditionState(beingBuilt, true);
	DamageInfo kill = siegeHit(1.0f, nullptr, 0, DAMAGE_CRUSH);
	kill.m_input.m_kill = true;
	hut->attemptDamage(kill);
	REQUIRE(bodyOf(hut)->getDamageState() == BODY_RUBBLE);
	CHECK(hut->testModelCondition(whilst));
	DamageInfo kill2 = siegeHit(1.0f, nullptr, 0, DAMAGE_CRUSH);
	kill2.m_input.m_kill = true;
	other->attemptDamage(kill2); // a finished building: no
	CHECK_FALSE(other->testModelCondition(whilst));
}

TEST_CASE("BLDGDEBRIS-1 health: full health zeroes the sides; a revive from 0 sets each to cap - 1 (RW 0x8C31E5 .. 0x8C321F)")
{
	DebrisWorld w;
	Object *hut = w.unit("DebrisHut", 'B', 500, 500);
	Object *front = w.unit("Walker", 'A', 700, 500);
	ActiveBody *b = bodyOf(hut);
	DamageInfo a = siegeHit(100.0f, front, 0);
	hut->attemptDamage(a);
	REQUIRE(b->sideDamage(0) == 100.0f);
	b->internalChangeHealth(500.0f); // over the maximum: capped, the sides cleared
	CHECK(b->getHealth() == 1000.0f);
	CHECK(b->sideDamage(0) == 0.0f);
	b->internalChangeHealth(-1000.0f); // to 0
	REQUIRE(b->getHealth() == 0.0f);
	b->internalChangeHealth(10.0f); // previous health 0: x87 ((1000 * 0.25) * 0.75) - 1.0
	for (int i = 0; i < 4; ++i)
	{
		CHECK(b->sideDamage(i) == 186.5f);
	}
}

TEST_CASE("BLDGDEBRIS-1 save: ActiveBody xfer version 3 keeps the side stage, values and flags; version 2 loads with none")
{
	DebrisWorld w;
	Object *a = w.unit("DebrisHut", 'B', 500, 500);
	Object *bObj = w.unit("DebrisHut", 'B', 300, 300);
	Object *cObj = w.unit("DebrisHut", 'B', 700, 700);
	Object *front = w.unit("Walker", 'A', 700, 500);
	ActiveBody *ba = bodyOf(a);
	DamageInfo h1 = siegeHit(300.0f, front, 0);
	a->attemptDamage(h1);
	DamageInfo h2 = siegeHit(250.0f, front, 0);
	a->attemptDamage(h2);
	DamageInfo h3 = siegeHit(1.0f, front, 0);
	a->attemptDamage(h3);
	REQUIRE(ba->sideStage() == 1);
	XferSave saved;
	ba->xfer(saved);
	REQUIRE_FALSE(saved.data().empty());
	CHECK(saved.data()[0] == 3);
	ActiveBody *bb = bodyOf(bObj);
	XferLoad load(saved.data());
	bb->xfer(load);
	REQUIRE(load.atEnd());
	CHECK(bb->sideStage() == 1);
	CHECK(bb->sideDestroyed(0));
	CHECK(bb->sideDamage(1) == ba->sideDamage(1));
	XferSave again;
	bb->xfer(again);
	CHECK(again.data() == saved.data());
	// version 2: the same without the trailing stage (4) and the four value / flag pairs (4 x 5)
	std::vector<std::uint8_t> v2 = saved.data();
	v2[0] = 2;
	v2.resize(v2.size() - 24);
	ActiveBody *bc = bodyOf(cObj);
	XferLoad old(v2);
	bc->xfer(old);
	REQUIRE(old.atEnd());
	CHECK(bc->sideStage() == 0);
	CHECK(bc->sideDamage(0) == 0.0f);
	CHECK_FALSE(bc->sideDestroyed(0));
}

TEST_CASE("BLDGDEBRIS-1 hash: a body without side state hashes the words it hashed before; side state adds the stage word and the four value / flag pairs")
{
	DebrisWorld w;
	Object *hut = w.unit("DebrisHut", 'B', 500, 500);
	Object *front = w.unit("Walker", 'A', 700, 500);
	ActiveBody *b = bodyOf(hut);
	DamageInfo crush = siegeHit(10.0f, front, 0, DAMAGE_CRUSH);
	hut->attemptDamage(crush);
	std::vector<std::uint32_t> before;
	StateHasher h0(&before);
	b->crc(h0);
	DamageInfo siege = siegeHit(10.0f, front, 0);
	hut->attemptDamage(siege);
	REQUIRE(b->sideDamage(0) == 10.0f);
	std::vector<std::uint32_t> after;
	StateHasher h1(&after);
	b->crc(h1);
	REQUIRE(after.size() == before.size() + 9);
	CHECK(after[before.size() + 1] == 0x41200000u); // 10.0f, side 0
}

// Retail data: every weapon with an FXTrigger (CATAPULT_ROCK / TREBUCHET_ROCK) is a projectile launcher, and the warhead's temporary weapon (RW 0x6CF4D6)
// carries the warhead template's FXTrigger (generateDamageInfo RW 0x90E665 .. 0x90E66B: template + 0x10), NONE for every retail warhead: the stage-0 rows
// never fire in a retail game (a mod's direct-damage weapon with an FXTrigger fires them). The hit below carries CATAPULT_ROCK as such a weapon would.
TEST_CASE("BLDGDEBRIS-1 retail: a CATAPULT_ROCK hit on ElvenBarracksBFME1 throws OCL_BuildingDamageList01; its front breaks off as OCL_RBBarracks_Chunk1")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string error;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player *men = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	Player *mordor = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[1]);
	REQUIRE(men);
	REQUIRE(mordor);
	const ThingTemplate *tt = logic.things().findTemplate("ElvenBarracksBFME1");
	REQUIRE(tt);
	const ActiveBodyModuleData *d = nullptr;
	for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->behaviorModules().nuggets())
	{
		d = d ? d : dynamic_cast<const ActiveBodyModuleData *>(n.data.get());
	}
	REQUIRE(d);
	REQUIRE(d->m_damageCreationList.size() == 5); // civilianbuildings.ini: one stage-0 row, four chunks
	CHECK(d->m_damageCreationList[0].ocl == "OCL_BuildingDamageList01");
	CHECK(d->m_damageCreationList[1].ocl == "OCL_RBBarracks_Chunk1");
	CHECK(d->m_damageCreationList[1].stage == 1);
	REQUIRE(TheObjectCreationListStore);
	for (const ActiveBodyModuleData::DamageCreation &c : d->m_damageCreationList)
	{
		CHECK_MESSAGE(TheObjectCreationListStore->findObjectCreationList(c.ocl) != nullptr, c.ocl);
	}
	auto count = [&](const char *name) {
		size_t n = 0;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			n += !o->isDestroyed() && o->getTemplate()->getName() == name;
		}
		return n;
	};
	const Coord3D at{ 1500.0f, 1500.0f, logic.getGroundHeight(1500.0f, 1500.0f) };
	Object *barracks = logic.newObject(tt, men->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(barracks);
	barracks->setPosition(&at);
	barracks->setOrientation(0.0f);
	const ThingTemplate *ct = logic.things().findTemplate("MordorCatapult");
	REQUIRE(ct);
	Object *catapult = logic.newObject(ct, mordor->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(catapult);
	const Coord3D from{ at.x + 400.0f, at.y, at.z };
	catapult->setPosition(&from);
	testutil::completeHorde(catapult);
	ActiveBody *b = bodyOf(barracks);
	const size_t bits0 = count("BuildingDamageBits01");
	DamageInfo rock = siegeHit(100.0f, catapult);
	barracks->attemptDamage(rock);
	REQUIRE(rock.m_output.m_actualDamageDealt > 0.0f); // tolerance: the barracks armour's SIEGE percentage is retail data; the split is checked exactly below
	CHECK(count("BuildingDamageBits01") == bits0 + 5); // OCL_BuildingDamageList01: Count = 5
	CHECK(b->sideDamage(0) == rock.m_output.m_actualDamageDealt); // the front faces the catapult (+X) and takes the whole hit (below the 562.5 cap)
	CHECK(b->sideDamage(1) == 0.0f);
	// down to REALLYDAMAGED, then one more hit: the front (full first) breaks off
	for (int i = 0; i < 200 && b->getDamageState() != BODY_REALLYDAMAGED; ++i)
	{
		DamageInfo more = siegeHit(100.0f, catapult);
		barracks->attemptDamage(more);
	}
	REQUIRE(b->getDamageState() == BODY_REALLYDAMAGED);
	const size_t chunks0 = count("BuildingRBBarracksBitA");
	DamageInfo last = siegeHit(10.0f, catapult);
	barracks->attemptDamage(last);
	CHECK(b->sideStage() == 1);
	CHECK(count("BuildingRBBarracksBitA") == chunks0 + 1);
	CHECK(hasCondition(barracks, "DESTROYED_FRONT"));
}
