// OpenBFME. Port 64 (issue fork issue 176): RubbleRiseUpdate — a destroyed camp or castle piece rises back out of its rubble.
//
// Every expectation below is read off the RotWK disassembly (the addresses are in the PR and in StructureModules.h / .cpp): RW 0x8A85D0 update,
// RW 0x8A83F6 onDie, RW 0x8A83CE the reset, RW 0x8A847C doPhase, RW 0x656350 the data constructor, RW 0xC69408 the field table, RW 0x659DF0 the
// registration. Synthetic data only (combattest::CombatWorld), no retail files. A logic frame is 200 ms; the fixture's Gravity is -2.56 per frame
// squared (GameData Gravity -64 per second squared), so the rise velocity is (-gravity) * (1 - damping) * 20 per frame (RW 0x8A8764).
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Object/Object.h"

using namespace combattest;

namespace
{
// a rubble piece sunk 100 below the terrain (the module's target height is the terrain's own height, RW 0x8A87AD); a plain structure with no
// RubbleRiseUpdate next to it (the neighbour: an object without the module is unchanged); a piece with the default data and no lists at all
const char kRubble[] =
	"Armor StoneArmor\n"
	"  Armor = DEFAULT 100%\n"
	"End\n"
	"Object RubblePiece\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE SCORE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 60\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = StoneArmor\n"
	"  End\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"    MaxHealthDamaged = 300\n"
	"    MaxHealthReallyDamaged = 150\n"
	"  End\n"
	"  Behavior = RubbleRiseUpdate ModuleTag_Rise\n"
	"    MinRubbleRiseDelay = 0\n"
	"    MaxRubbleRiseDelay = 0\n"
	"    MinBurstDelay = 250\n"
	"    MaxBurstDelay = 800\n"
	"    BigBurstFrequency = 4\n"
	"    RubbleRiseDamping = 0.5\n"
	"    RubbleHeight = 4.0\n"
	"    MaxShudder = 0.6\n"
	"    FXList = INITIAL FX_StructureMediumPostCollapse\n"
	"    FXList = DELAY FX_Delay\n"
	"    FXList = BURST FX_Burst1 FX_Burst2\n"
	"    OCL = BURST OCL_Burst\n"
	"    FXList = FINAL FX_Final\n"
	"  End\n"
	"End\n"
	// the neighbour: the same piece with no RubbleRiseUpdate
	"Object PlainPiece\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE SCORE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 60\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n";

// the drawable stand-in (lane SMOOTH-1): RW's update asks Object::getDrawable (RW 0x70E013) before it rises and before it moves (RW 0x8A85EE / 0x8A87D1)
struct TestDrawable : ObjectClientHooks
{
	void objectCreated(Object &) override {}
	void objectDestroyed(Object &) override {}
};

// every FX event with the frame it was made in, counted by site and by the FX list it names
struct Recorder : FXEventSink
{
	std::vector<FXEvent> events;
	void onFXEvent(const FXEvent &e) override { events.push_back(e); }
	int count(const char *site) const
	{
		int n = 0;
		for (const FXEvent &e : events)
		{
			n += std::string(e.site) == site ? 1 : 0;
		}
		return n;
	}
	int countList(const char *name) const
	{
		int n = 0;
		for (const FXEvent &e : events)
		{
			n += (std::string(e.site) == "RubbleRise" && e.fxList != nullptr && *e.fxList == name) ? 1 : 0;
		}
		return n;
	}
};

int postRubble() { return CombatNames::modelCondition("POST_RUBBLE"); }
int postCollapse() { return CombatNames::modelCondition("POST_COLLAPSE"); }

// a damage input from `source` (the tests drive the body's die path directly, as test_structure_bodies does)
DamageInfo hit(const Object *source, float amount, int type)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	return d;
}

// spawns `name` at (x, y) with its z set to `z` afterwards (MoveWorld's spawn places everything at z 0), with a drawable when `hooks`
Object *piece(CombatWorld &w, const char *name, float x, float y, float z, TestDrawable *hooks)
{
	Object *o = w.unit(name, 'B', x, y);
	Coord3D p{ x, y, z };
	o->setPosition(&p);
	o->friend_bindToClient(hooks);
	return o;
}

// RW 0x8A8325: the constructor sets UPDATE_SLEEP_FOREVER, and the only wake is the module's own onDie (RW 0x8A83F0). So the piece is destroyed first
// (a lethal hit runs every die module) and the rubble condition arrives afterwards, as it does when a collapse leaves its rubble behind.
void destroy(CombatWorld &w, Object *piece)
{
	if (w.byId(piece->getID()) == nullptr)
	{
		return;
	}
	Object *attacker = w.unit("Swordsman", 'A', 200.0f, 300.0f);
	DamageInfo d = hit(attacker, 100000.0f, DAMAGE_UNRESISTABLE);
	piece->attemptDamage(d);
}
} // namespace

TEST_CASE("PORT-64: a structure with no RubbleRiseUpdate is unchanged by POST_RUBBLE")
{
	CombatWorld w(kRubble);
	Recorder rec;
	w.logic->fxEvents().setSink(&rec);
	TestDrawable hooks;
	Object *plain = piece(w, "PlainPiece", 300.0f, 300.0f, -100.0f, &hooks);
	CHECK(plain->findModule("RubbleRiseUpdate") == nullptr);
	destroy(w, plain);
	CHECK(plain->isEffectivelyDead());
	plain->setModelConditionState(postRubble(), true);
	w.frames(6);
	CHECK(plain->getPosition()->x == 300.0f);
	CHECK(plain->getPosition()->y == 300.0f);
	CHECK(plain->getPosition()->z == -100.0f);
	CHECK(plain->testModelCondition(postRubble()));
	CHECK_FALSE(plain->testModelCondition(postCollapse()));
	CHECK(rec.count("RubbleRise") == 0);
}

TEST_CASE("PORT-64: a rubble piece rises to the terrain's height on RW's schedule and swaps POST_RUBBLE for POST_COLLAPSE")
{
	CombatWorld w(kRubble);
	Recorder rec;
	w.logic->fxEvents().setSink(&rec);
	TestDrawable hooks;
	Object *o = piece(w, "RubblePiece", 300.0f, 300.0f, -100.0f, &hooks);
	RubbleRiseUpdate *rise = dynamic_cast<RubbleRiseUpdate *>(o->findModule("RubbleRiseUpdate"));
	REQUIRE(rise != nullptr);
	CHECK(rise->state() == RISESTATE_WAITING);
	CHECK(rise->data()->m_minBurstDelay == 2); // 250 ms = 2 frames
	CHECK(rise->data()->m_maxBurstDelay == 4); // 800 ms = 4 frames
	CHECK(rise->data()->m_bigBurstFrequency == 4);
	CHECK(rise->data()->m_rubbleRiseDamping == 0.5f);
	CHECK(rise->data()->m_maxShudder == 0.6f);
	CHECK(rise->data()->m_rubbleHeight == 4.0f); // parsed, and read by no slot of the class (RW 0xC69408 row +0x50)
	// the four phase lists of RW's name table 0xDB0ED0, and one pick per list by default (the data ctor RW 0x656350)
	CHECK(rise->data()->m_fx[RRPHASE_INITIAL].size() == 1);
	CHECK(rise->data()->m_fx[RRPHASE_DELAY].size() == 1);
	CHECK(rise->data()->m_fx[RRPHASE_BURST].size() == 2);
	CHECK(rise->data()->m_fx[RRPHASE_FINAL].size() == 1);
	CHECK(rise->data()->m_ocl[RRPHASE_BURST].size() == 1);
	CHECK(rise->data()->m_fxCount[RRPHASE_DELAY] == 1);
	CHECK(rise->data()->m_oclCount[RRPHASE_BURST] == 1);

	destroy(w, o);
	CHECK(o->isEffectivelyDead());
	CHECK(rise->state() == RISESTATE_WAITING);
	o->setModelConditionState(postRubble(), true);
	// the first update: INITIAL and BURST at once, the state rising, the velocity from (-gravity) * (1 - damping) * 20 (RW 0x8A8764)
	w.logic->runLogicFrame();
	REQUIRE(rise->state() == RISESTATE_RISING);
	CHECK(rise->riseVelocity() == doctest::Approx(25.6f).epsilon(0.0001)); // tolerance: x87 at 24 bits: 2.56 * 0.5 * 20
	CHECK(rise->targetHeight() == 0.0f);                                    // the synthetic terrain is flat at z 0 (RW 0x8A87AD)
	CHECK(rise->risePosition().x == 300.0f);
	CHECK(rise->risePosition().z == -100.0f);
	CHECK(rec.countList("FX_StructureMediumPostCollapse") == 1); // the INITIAL phase's single entry
	CHECK(rec.count("RubbleRise") == 2);                          // INITIAL one, BURST one of its two
	// z = the saved z + the height risen so far (RW 0x8A884D: saved z + currentHeight, SSE add)
	CHECK(o->getPosition()->z == -100.0f + rise->currentHeight());
	// 25.6 per frame up from -100 to the terrain at 0: four frames (25.6, 51.2, 76.8 and 102.4 which is past 100)
	int frames = 1;
	while (rise->state() == RISESTATE_RISING && frames < 20)
	{
		w.logic->runLogicFrame();
		++frames;
		if (rise->state() == RISESTATE_RISING)
		{
			CHECK(o->getPosition()->z == -100.0f + rise->currentHeight());
			CHECK(rise->currentHeight() == doctest::Approx(25.6f * (float)frames).epsilon(0.001)); // tolerance: x87 accumulation
		}
	}
	CHECK(frames == 4);
	REQUIRE(rise->state() == RISESTATE_DONE);
	// RW 0x8A88DF: the piece is put at (saved x, saved y, the target height) and POST_RUBBLE becomes POST_COLLAPSE (bits 59 and 60)
	CHECK(o->getPosition()->x == 300.0f);
	CHECK(o->getPosition()->y == 300.0f);
	CHECK(o->getPosition()->z == 0.0f);
	CHECK_FALSE(o->testModelCondition(postRubble()));
	CHECK(o->testModelCondition(postCollapse()));
	CHECK(rec.countList("FX_Final") == 1); // the FINAL phase fired once
	// RW 0x8A8A0E: UPDATE_SLEEP_FOREVER; a done module neither moves nor draws again
	const Coord3D rest = *o->getPosition();
	const size_t events = rec.events.size();
	w.frames(10);
	CHECK(rise->state() == RISESTATE_DONE);
	CHECK(o->getPosition()->x == rest.x);
	CHECK(o->getPosition()->y == rest.y);
	CHECK(o->getPosition()->z == rest.z);
	CHECK(rec.events.size() == events);
	CHECK(rec.countList("FX_Final") == 1);
}

TEST_CASE("PORT-64: the rubble rise's draws follow RW's order (INITIAL pick, BURST pick, BURST OCL pick, burst delay, shudder y then x)")
{
	CombatWorld w(kRubble);
	// lane TARGET-1: the idle target scan draws a one-off offset from the same generator; the scan is switched off so the log holds the rise alone
	w.combat().setAutoAcquireEnabled(false);
	TestDrawable hooks;
	Object *o = piece(w, "RubblePiece", 300.0f, 300.0f, -100.0f, &hooks);
	RubbleRiseUpdate *rise = dynamic_cast<RubbleRiseUpdate *>(o->findModule("RubbleRiseUpdate"));
	REQUIRE(rise != nullptr);
	destroy(w, o);
	w.logic->random().enableCallLog(true);
	w.logic->random().clearCallLog();
	o->setModelConditionState(postRubble(), true);
	w.logic->runLogicFrame();
	const std::vector<GameLogicRandom::Call> &log = w.logic->random().callLog();
	REQUIRE(log.size() == 6);
	// RW 0x8A8620 doPhase(INITIAL): one FX entry -> random(0, 0)
	CHECK_FALSE(log[0].real);
	CHECK(log[0].lo == 0);
	CHECK(log[0].hi == 0);
	// RW 0x8A8734 doPhase(BURST): the FX pass draws one index of two, the OCL pass one index of one
	CHECK_FALSE(log[1].real);
	CHECK(log[1].lo == 0);
	CHECK(log[1].hi == 1);
	CHECK_FALSE(log[2].real);
	CHECK(log[2].lo == 0);
	CHECK(log[2].hi == 0);
	// RW 0x8A8741: nextBurstFrame = now + random(MinBurstDelay, MaxBurstDelay) (line 171)
	CHECK_FALSE(log[3].real);
	CHECK(log[3].lo == 2);
	CHECK(log[3].hi == 4);
	// RW 0x8A87FC / 0x8A8822: two logic reals in [-MaxShudder, MaxShudder] (line 196), the first to y and the second to x
	CHECK(log[4].real);
	CHECK(log[4].rlo == doctest::Approx(-0.6f).epsilon(0.0001)); // tolerance: x87 at 24 bits
	CHECK(log[4].rhi == doctest::Approx(0.6f).epsilon(0.0001));  // tolerance: x87 at 24 bits
	CHECK(log[5].real);
	// the y of the piece carries the first draw, its x the second (RW 0x8A885D adds the first to the saved y)
	CHECK(o->getPosition()->y - 300.0f == doctest::Approx(log[4].rresult).epsilon(0.0001)); // tolerance: pc24 add of the drawn shudder
	CHECK(o->getPosition()->x - 300.0f == doctest::Approx(log[5].rresult).epsilon(0.0001)); // tolerance: pc24 add of the drawn shudder
}

TEST_CASE("PORT-64: no drawable leaves the module waiting (RW 0x8A85EE and 0x8A87D1 gate on Object::getDrawable)")
{
	CombatWorld w(kRubble);
	Recorder rec;
	w.logic->fxEvents().setSink(&rec);
	Object *o = piece(w, "RubblePiece", 300.0f, 300.0f, -100.0f, nullptr);
	REQUIRE(o->clientHooks() == nullptr);
	RubbleRiseUpdate *rise = dynamic_cast<RubbleRiseUpdate *>(o->findModule("RubbleRiseUpdate"));
	REQUIRE(rise != nullptr);
	destroy(w, o);
	o->setModelConditionState(postRubble(), true);
	w.frames(5);
	CHECK(rise->state() == RISESTATE_WAITING);
	CHECK(o->getPosition()->z == -100.0f);
	CHECK(rec.count("RubbleRise") == 0);
	// the drawable arrives: the next frame starts the rise from the same place
	TestDrawable hooks;
	o->friend_bindToClient(&hooks);
	w.logic->runLogicFrame();
	CHECK(rise->state() == RISESTATE_RISING);
	CHECK(rise->risePosition().z == -100.0f);
	CHECK(rec.count("RubbleRise") == 2);
}

TEST_CASE("PORT-64: onDie with an applicable DieMux resets the rise to waiting and wakes the module (RW 0x8A83F6 / 0x8A83CE)")
{
	CombatWorld w(kRubble);
	TestDrawable hooks;
	Object *o = piece(w, "RubblePiece", 300.0f, 300.0f, -100.0f, &hooks);
	RubbleRiseUpdate *rise = dynamic_cast<RubbleRiseUpdate *>(o->findModule("RubbleRiseUpdate"));
	REQUIRE(rise != nullptr);
	destroy(w, o);
	CHECK(rise->state() == RISESTATE_WAITING); // the die ran the reset, which is the module's only wake (RW 0x8A83F0)
	o->setModelConditionState(postRubble(), true);
	w.logic->runLogicFrame();
	REQUIRE(rise->state() == RISESTATE_RISING);
	REQUIRE(rise->currentHeight() == doctest::Approx(25.6f).epsilon(0.001)); // tolerance: x87 at 24 bits
	// a second die event (the module's slot is called directly: the body refuses a dead object) puts the state, the heights and the burst frame back
	rise->onDie(DieModuleInterface::Event{});
	CHECK(rise->state() == RISESTATE_WAITING);
	CHECK(rise->currentHeight() == 0.0f);
	CHECK(rise->riseVelocity() == 0.0f);
	CHECK(rise->targetHeight() == 0.0f);
	CHECK(rise->nextBurstFrame() == 0);
	// the reset woke the module again: POST_RUBBLE is still set, so the next frame rises from the spot the object is at now (RW 0x8A83CE does not
	// write the start position +0x3C; the update takes it again)
	const float zAfterReset = o->getPosition()->z;
	w.logic->runLogicFrame();
	CHECK(rise->state() == RISESTATE_RISING);
	CHECK(rise->risePosition().z == zAfterReset);
}
