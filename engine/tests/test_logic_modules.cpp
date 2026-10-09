// OpenBFME unit tests: the ported runtime modules, ActiveBody and HordeContain (lane LOGIC-1).
//
// ActiveBody expectations are the RotWK disassembly (RW 0x8C373F data defaults, 0x8C3841 constructor, 0x8C1B79 damage state; see
// GameLogic/Module/ActiveBody.h); HordeContain expectations are worked from HordeContainCore's documented rules (RW 0x877751 slot table,
// 0x873F30 member add, 0x873D48 rank fill-in, 0x875847 slot position) and the B1 / ZH sources cited in HordeContainRuntime.h.

#include "doctest.h"
#include "LogicTestUtil.h"

#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include <cmath>

using namespace logictest;

namespace
{
const char kObjects[] =
	"Object Fighter\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object Wounded\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"    MaxHealthDamaged = 100\n"
	"    MaxHealthReallyDamaged = 50\n"
	"    InitialHealth = 120\n"
	"    UseDefaultDamageSettings = No\n"
	"  End\n"
	"End\n"
	"Object TwoBodies\n"
	"  Body = ActiveBody ModuleTag_B1\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_B2\n"
	"    MaxHealth = 2\n"
	"  End\n"
	"End\n"
	"Object FighterHorde\n"
	"  Behavior = HordeContain ModuleTag_Horde\n"
	"    RankInfo = RankNumber:1 UnitType:Fighter Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:Fighter Position:X:30 Y:0 Leader 1 0 Position:X:30 Y:20 Leader 1 1 Position:X:30 Y:-20 Leader 1 2\n"
	"    InitialPayload = Fighter 6\n"
	"  End\n"
	"End\n"
	"Object OverfullHorde\n"
	"  Behavior = HordeContain ModuleTag_Horde\n"
	"    RankInfo = RankNumber:1 UnitType:Fighter Position:X:50 Y:0 Position:X:50 Y:20\n"
	"    InitialPayload = Fighter 4\n"
	"  End\n"
	"End\n"
	"Object LostPayloadHorde\n"
	"  Behavior = HordeContain ModuleTag_Horde\n"
	"    RankInfo = RankNumber:1 UnitType:Fighter Position:X:50 Y:0\n"
	"    InitialPayload = NoSuchUnit 2\n"
	"    InitialPayload = Fighter 1\n"
	"  End\n"
	"End\n"
	"Object JitteredHorde\n"
	"  Behavior = HordeContain ModuleTag_Horde\n"
	"    RandomOffset = X:5 Y:5\n"
	"    RankInfo = RankNumber:1 UnitType:Fighter Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    InitialPayload = Fighter 3\n"
	"  End\n"
	"End\n";

struct Fx : LogicWorld
{
	Fx()
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		LogicModules::registerAll(w.modules);
		const std::string err = w.load(kObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
	}
	std::vector<Object *> membersOf(Object *horde)
	{
		const ContainModuleInterface::ContainedItemsList *l = horde->getContain()->getContainedItemsList();
		return std::vector<Object *>(l->begin(), l->end());
	}
	HordeContain *hordeOf(Object *o) { return dynamic_cast<HordeContain *>(o->findModule("HordeContain")); }
};
} // namespace

TEST_CASE("ActiveBody: the module data defaults are the binary's (RW 0x8C373F)")
{
	LogicWorld lw;
	REQUIRE(lw.loadError.empty());
	LogicModules::registerAll(lw.w.modules);
	REQUIRE(lw.w.load("Object Bare\n  Body = ActiveBody ModuleTag_Body\n  End\nEnd\n").empty());
	const ThingTemplate *t = lw.w.get("Bare");
	const ThingTemplate::Nugget *n = t->behaviorModules().find("ModuleTag_Body");
	REQUIRE(n);
	const ActiveBodyModuleData *d = dynamic_cast<const ActiveBodyModuleData *>(n->data.get());
	REQUIRE(d);
	CHECK(d->m_maxHealth == 0.0f);
	CHECK(d->m_initialHealth == -1.0f); // RW 0xBD19DC
	CHECK(d->m_maxHealthDamaged == 0.0f);
	CHECK(d->m_maxHealthReallyDamaged == 0.0f);
	CHECK(d->m_recoveryTime == 0);
	CHECK(d->m_dodgePercent == 0.0f);
	CHECK(d->m_useDefaultDamageSettings);
	CHECK(d->m_grabDamage == 200.0f);  // RW 0xBE4170
	CHECK(d->m_cheerRadius == 200.0f);
	CHECK_FALSE(d->m_removeUpgradesOnDeath);
	CHECK_FALSE(d->m_burningDeathBehavior);
	CHECK(d->m_damageCreationList.empty());
}

TEST_CASE("ActiveBody: every field of the binary's table parses (RW 0xC71D68), the INI core rejects an unknown one")
{
	LogicWorld lw;
	LogicModules::registerAll(lw.w.modules);
	const std::string all =
		"Object Full\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 300.5\n    MaxHealthDamaged = 200\n    MaxHealthReallyDamaged = 100\n    InitialHealth = 250\n"
		"    RecoveryTime = 2000\n    DodgePercent = 25%\n    EnteringDamagedTransitionTime = 500\n    EnteringReallyDamagedTransitionTime = 1000\n"
		"    GrabObject = Rock\n    GrabFX = FX_Grab\n    GrabDamage = 50\n    GrabOffset = X:1 Y:2\n    UseDefaultDamageSettings = No\n"
		"    DamageCreationList = OCL_Chunk1 CATAPULT_ROCK FRONT_DESTROYED\n    DamageCreationList = OCL_Chunk2 CATAPULT_ROCK\n"
		"    HealingBuffFx = FX_Heal\n    DamagedAttributeModifier = ModA\n    ReallyDamagedAttributeModifier = ModB\n    CheerRadius = 80\n"
		"    RemoveUpgradesOnDeath = Yes\n    BurningDeathBehavior = Yes\n    BurningDeathFX = FX_Burn\n"
		"  End\n"
		"End\n";
	REQUIRE(lw.w.load(all) == "");
	const ActiveBodyModuleData *d = dynamic_cast<const ActiveBodyModuleData *>(lw.w.get("Full")->behaviorModules().find("ModuleTag_Body")->data.get());
	REQUIRE(d);
	CHECK(d->m_maxHealth == 300.5f);
	CHECK(d->m_initialHealth == 250.0f);
	CHECK(d->m_recoveryTime == 10);                  // 2000 ms * 0.005, ceil (RW 0x73A429)
	CHECK(d->m_dodgePercent == doctest::Approx(0.25f));
	CHECK(d->m_enteringDamagedTransitionTime == 3);  // 500 ms -> 2.5 -> 3
	CHECK(d->m_enteringReallyDamagedTransitionTime == 5);
	CHECK(d->m_grabObject == "Rock");
	CHECK(d->m_grabFX == "FX_Grab");
	CHECK(d->m_grabDamage == 50.0f);
	CHECK(d->m_grabOffset.x == 1.0f);
	CHECK(d->m_grabOffset.y == 2.0f);
	CHECK_FALSE(d->m_useDefaultDamageSettings);
	REQUIRE(d->m_damageCreationList.size() == 2);
	CHECK(d->m_damageCreationList[0] == std::vector<std::string>{ "OCL_Chunk1", "CATAPULT_ROCK", "FRONT_DESTROYED" });
	CHECK(d->m_cheerRadius == 80.0f);
	CHECK(d->m_removeUpgradesOnDeath);
	CHECK(d->m_burningDeathBehavior);
	CHECK(d->m_burningDeathFX == "FX_Burn");
	const std::string err = lw.w.load("Object Bad\n  Body = ActiveBody ModuleTag_Body\n    Nonsense = 1\n  End\nEnd\n");
	CHECK(err.find("Nonsense") != std::string::npos);
}

TEST_CASE("ActiveBody: the constructor sets health and the damage fractions as RW 0x8C3841 does; the damage state is RW 0x8C1B79")
{
	Fx f;
	// default damage settings: the data fractions are 0 (MaxHealthDamaged 0), so GameData's thresholds apply
	Object *fighter = f.make("Fighter");
	BodyModuleInterface *b = fighter->getBodyModule();
	REQUIRE(b);
	CHECK(b->getMaxHealth() == 100.0f);
	CHECK(b->getHealth() == 100.0f);       // InitialHealth -1 = the max health
	CHECK(b->getInitialHealth() == -1.0f); // the field keeps the data's value (RW +0x1C)
	CHECK(b->getDamageState() == BODY_PRISTINE);
	b->setInitialHealth(65);               // 65 <= 0.65 * 100: DAMAGED
	CHECK(b->getHealth() == 65.0f);
	CHECK(b->getDamageState() == BODY_DAMAGED);
	b->setInitialHealth(66);
	CHECK(b->getDamageState() == BODY_PRISTINE);
	b->setInitialHealth(40);               // 40 <= 0.4 * 100: REALLYDAMAGED
	CHECK(b->getDamageState() == BODY_REALLYDAMAGED);
	b->setInitialHealth(0);                // exactly 0: RUBBLE
	CHECK(b->getDamageState() == BODY_RUBBLE);
	// explicit fractions (MaxHealthDamaged 100 / MaxHealth 200 = 0.5, MaxHealthReallyDamaged 50 / 200 = 0.25) and an explicit InitialHealth 120
	Object *w = f.make("Wounded");
	BodyModuleInterface *wb = w->getBodyModule();
	CHECK(wb->getHealth() == 120.0f);
	CHECK(wb->getInitialHealth() == 120.0f);
	CHECK(wb->getDamageState() == BODY_PRISTINE); // 0.5 * 200 = 100 < 120
	wb->setInitialHealth(50);                     // 100 health: DAMAGED (0.5 * 200 >= 100)
	CHECK(wb->getDamageState() == BODY_DAMAGED);
	wb->setInitialHealth(25);                     // 50 health: REALLYDAMAGED (0.25 * 200 >= 50)
	CHECK(wb->getDamageState() == BODY_REALLYDAMAGED);
	// the caches: one body per object; a template that declares two (retail's ArnorArvedui does) runs with the LAST one (RW 0x69A3C8 stores without a test, ZH only asserts)
	Object *two = f.make("TwoBodies");
	CHECK(two->getBodyModule()->getMaxHealth() == 2.0f);
}

TEST_CASE("ActiveBody: an object that needs the GameData thresholds and has none loaded is an error, not a silent 0")
{
	Fx f;
	f.logic->settings().bodyThresholdsLoaded = false;
	CHECK_THROWS_AS(f.make("Fighter"), std::logic_error);
	f.make("Wounded"); // explicit fractions need nothing from GameData
}

TEST_CASE("HordeContain: a horde creates its InitialPayload members, each in its own object, on the first free slots in payload order")
{
	Fx f;
	f.logic->setClientHooks(&f.hooks);
	Object *horde = f.make("FighterHorde", f.teamOf("Alice"));
	HordeContain *hc = f.hordeOf(horde);
	REQUIRE(hc);
	CHECK(hc->payloadCreated());
	CHECK(hc->getSlotCount() == 6);
	CHECK(hc->unplacedMembers() == 0);
	const std::vector<Object *> m = f.membersOf(horde);
	REQUIRE(m.size() == 6);
	CHECK(horde->getID() == 1);                        // the horde is registered before onCreate makes its members
	for (size_t i = 0; i < m.size(); ++i)
	{
		CHECK(m[i]->getID() == 2 + i);
		CHECK(m[i]->getTemplate()->getName() == "Fighter");
		CHECK(m[i]->getContainedBy() == horde);
		CHECK(m[i]->getTeam() == horde->getTeam());
		CHECK(m[i]->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER")));
		CHECK(hc->getMemberSlot(m[i]) == (int)i);     // rank 1 slots 0..2, rank 2 slots 3..5
	}
	CHECK(f.logic->getObjectCount() == 7);
	CHECK(horde->getContain()->getContainCount() == 6);
	CHECK(horde->getContain()->getHordeContainInterface() != nullptr);
	// the members were each made with their own client hook call, the horde's after them (its initObject runs after onCreate)
	CHECK(f.log.indexOf("client:created#2") < f.log.indexOf("client:created#1"));
	// the horde sleeps (its member pass is not ported, S-149): its update module is in the sleeping vector
	CHECK(hc->friend_getPhaseInLogic() == -1);
	CHECK(hc->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
}

TEST_CASE("HordeContain: members stand at the slot offset rotated by the horde's angle and follow it when it moves (RW 0x875847)")
{
	Fx f;
	Object *horde = f.make("FighterHorde");
	const std::vector<Object *> m = f.membersOf(horde);
	Coord3D p{ 100.0f, 200.0f, 0.0f };
	horde->setOrientation(0.0f);
	horde->setPosition(&p);
	// slot 0 is (50, 0), slot 1 (50, 20), slot 2 (50, -20), slot 3 (30, 0): angle 0 is the identity rotation
	CHECK(m[0]->getPosition()->x == doctest::Approx(150.0f));
	CHECK(m[0]->getPosition()->y == doctest::Approx(200.0f));
	CHECK(m[1]->getPosition()->y == doctest::Approx(220.0f));
	CHECK(m[2]->getPosition()->y == doctest::Approx(180.0f));
	CHECK(m[3]->getPosition()->x == doctest::Approx(130.0f));
	const float halfPi = 1.57079632679f;
	horde->setOrientation(halfPi); // +x offsets point along +y
	CHECK(m[0]->getPosition()->x == doctest::Approx(100.0f).epsilon(1e-4));
	CHECK(m[0]->getPosition()->y == doctest::Approx(250.0f));
	CHECK(m[1]->getPosition()->x == doctest::Approx(80.0f)); // (50, 20) rotated: x' = -20, y' = 50
	CHECK(m[0]->getOrientation() == doctest::Approx(halfPi));
	Coord3D q{ 0.0f, 0.0f, 7.0f };
	horde->setPosition(&q);
	CHECK(m[0]->getPosition()->y == doctest::Approx(50.0f));
	// the z of a member is the ground height under it: 0 here (no terrain installed)
	CHECK_FALSE(f.logic->hasTerrain());
	CHECK(m[0]->getPosition()->z == 0.0f);
}

TEST_CASE("HordeContain: a payload that does not fit the slots keeps its extra members in the contain list without a slot")
{
	Fx f;
	Object *horde = f.make("OverfullHorde");
	HordeContain *hc = f.hordeOf(horde);
	CHECK(hc->getSlotCount() == 2);
	CHECK(horde->getContain()->getContainCount() == 4);
	CHECK(hc->unplacedMembers() == 2);
	const std::vector<Object *> m = f.membersOf(horde);
	CHECK(hc->getMemberSlot(m[0]) == 0);
	CHECK(hc->getMemberSlot(m[1]) == 1);
	CHECK(hc->getMemberSlot(m[2]) == -1);
	Coord3D p{ 10.0f, 20.0f, 0.0f };
	horde->setPosition(&p);
	CHECK(m[3]->getPosition()->x == 10.0f); // an unplaced member stands at the horde's position
}

TEST_CASE("HordeContain: an unknown payload template is reported, never skipped silently")
{
	Fx f;
	Object *horde = f.make("LostPayloadHorde");
	CHECK(horde->getContain()->getContainCount() == 1); // the second payload entry still happens
	const GameLogic::Report r = f.logic->report();
	bool found = false;
	for (const std::string &e : r.errors)
	{
		found = found || (e.find("NoSuchUnit") != std::string::npos && e.find("does not exist") != std::string::npos);
	}
	CHECK(found);
}

TEST_CASE("HordeContain: a member leaving the contain frees its slot and the rank fill-in moves the nearest deeper member up (RW 0x87370B, 0x873D48)")
{
	Fx f;
	Object *horde = f.make("FighterHorde");
	HordeContain *hc = f.hordeOf(horde);
	const std::vector<Object *> m = f.membersOf(horde);
	const ObjectID deadId = m[0]->getID();
	f.logic->destroyObject(m[0]); // slot 0, rank 1
	CHECK(horde->getContain()->getContainCount() == 5);
	// the lowest free slot is 0 (rank 1); the candidates are the rank 2 members (slots 3, 4, 5): slot 3 (30, 0) is the nearest to (50, 0)
	CHECK(hc->getMemberSlot(m[3]) == 0);
	CHECK(hc->getMemberSlot(m[1]) == 1);
	CHECK(hc->getMemberSlot(m[4]) == 4);
	CHECK(hc->freeSlotIndices() == std::vector<int>{ 3 });
	f.logic->processDestroyList();
	CHECK(f.logic->findObjectByID(deadId) == nullptr);
	CHECK(f.logic->getObjectCount() == 6);
}

TEST_CASE("HordeContain: destroying the horde destroys its members in the same pass (ZH OpenContain::onDelete, B1 HordeContainOnDelete)")
{
	Fx f;
	f.logic->setClientHooks(&f.hooks);
	Object *horde = f.make("FighterHorde");
	CHECK(f.logic->getObjectCount() == 7);
	f.logic->destroyObject(horde);
	CHECK(f.logic->destroyQueueSize() == 7); // the horde and its six members, queued by onDelete
	f.logic->processDestroyList();
	CHECK(f.logic->getObjectCount() == 0);
	CHECK(f.log.count("client:destroyed#1") == 1);
	for (int id = 2; id <= 7; ++id)
	{
		CHECK(f.log.count("client:destroyed#" + std::to_string(id)) == 1);
	}
}

TEST_CASE("HordeContain: RandomOffset draws two logic RNG values per slot, x then y, at the creation of the contain (RW 0x877751)")
{
	Fx f;
	f.logic->random().enableCallLog(true);
	Object *horde = f.make("JitteredHorde");
	// the slot draws come first (the horde's onObjectCreated), then the members' and the horde's own creation draws (1, 999)
	const auto &calls = f.logic->random().callLog();
	REQUIRE(calls.size() == 6 + 3 + 1); // 3 slots x (x draw, y draw), 3 members, the horde
	for (size_t i = 0; i < 6; ++i)
	{
		CHECK(calls[i].lo == -5);
		CHECK(calls[i].hi == 5);
		CHECK(calls[i].line == (i % 2 == 0 ? 1575 : 1579));
	}
	for (size_t i = 6; i < calls.size(); ++i)
	{
		CHECK(calls[i].line == 0x19A7);
	}
	CHECK(f.hordeOf(horde)->hasRandomOffset());
	// the same seed gives the same member positions; another seed another jitter
	auto positions = [](int seed) {
		Fx g;
		g.logic->random().seedRandom((unsigned)seed);
		Object *h = g.make("JitteredHorde");
		Coord3D p{ 100.0f, 100.0f, 0.0f };
		h->setPosition(&p);
		std::vector<float> out;
		for (Object *m : g.membersOf(h))
		{
			out.push_back(m->getPosition()->x);
			out.push_back(m->getPosition()->y);
		}
		return out;
	};
	CHECK(positions(1) == positions(1));
	CHECK(positions(1) != positions(2));
}

TEST_CASE("HordeContain: the state hash covers the members' positions and slots")
{
	Fx f;
	Object *horde = f.make("FighterHorde");
	const std::uint32_t before = f.logic->computeStateHash();
	Coord3D p{ 5.0f, 6.0f, 0.0f };
	f.membersOf(horde)[2]->setPosition(&p);
	CHECK(f.logic->computeStateHash() != before);
}
