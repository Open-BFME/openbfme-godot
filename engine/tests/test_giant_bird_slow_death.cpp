// OpenBFME unit tests. GPL-3.0.
// GiantBirdSlowDeathBehavior (RW 0x86511C): a flyer falls to the ground ahead of it, beside the objects of CrashAvoidKindOfs. Synthetic data (combattest::CombatWorld), no
// retail files.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Module/GiantBirdSlowDeathBehavior.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
const char kGiantBirdAI[] = "GiantBirdAIUpdate";
const char kFields[] =
	"    DeathTypes = ALL\n"
	"    SinkDelay = 15000\n"
	"    SinkRate = 1.0\n"
	"    DestructionDelay = 20000\n"
	"    Sound = INITIAL BirdVoiceDie\n"
	"    Sound = HIT_GROUND BirdSlam\n"
	"    CrashAvoidKindOfs = STRUCTURE\n"
	"    CrashAvoidRadius = 200\n"
	"    CrashAvoidStrength = 0.3\n"
	"    NeedToMaintainFlailingHeight = Yes\n";

std::string bird(const std::string &name, const std::string &ai, const std::string &fields, const std::string &physics = "")
{
	std::string s = "Object " + name + "\n"
					"  KindOf = MONSTER SELECTABLE CAN_ATTACK GIANT_BIRD\n"
					"  Geometry = CYLINDER\n"
					"  GeometryMajorRadius = 20\n"
					"  GeometryHeight = 20\n"
					"  Body = ActiveBody ModuleTag_Body\n"
					"    MaxHealth = 100\n"
					"  End\n";
	if (!ai.empty())
	{
		s += "  Behavior = " + ai + " ModuleTag_AI\n  End\n"
			 "  LocomotorSet\n"
			 "    Locomotor = WalkerLoco\n"
			 "    Condition = SET_NORMAL\n"
			 "    Speed = 30\n"
			 "  End\n";
	}
	s += "  Behavior = PhysicsBehavior ModuleTag_Physics\n" + physics + "  End\n"
		 "  Behavior = GiantBirdSlowDeathBehavior ModuleTag_Death\n" + fields + "  End\n"
		 "End\n";
	return s;
}

const char kBlock[] =
	"Object Block\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryHeight = 10\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n";

struct BirdWorld : CombatWorld
{
	BirdWorld()
	{
		combat().setAutoAcquireEnabled(false);
		logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f); // the terrain's extent (RW 0x62FCCD)
		const std::string err = w.load(bird("Bird", kGiantBirdAI, kFields) + bird("Plain", kGiantBirdAI, "") + bird("Statue", "", kFields) +
				bird("Walker", "AIUpdateInterface", kFields) + bird("Bouncer", kGiantBirdAI, kFields, "    AllowBouncing = Yes\n    BounceCount = 1\n") + kBlock,
			INI_LOAD_OVERWRITE, "birdobjects.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	static GiantBirdSlowDeathBehavior *death(Object *o)
	{
		GiantBirdSlowDeathBehavior *d = dynamic_cast<GiantBirdSlowDeathBehavior *>(o->findModule("GiantBirdSlowDeathBehavior"));
		REQUIRE(d);
		return d;
	}
	// an object in the air over (x, y)
	Object *flyer(const char *name, float x, float y, float z)
	{
		Object *o = unit(name, 'A', x, y);
		const Coord3D p{ x, y, z };
		o->setPosition(&p);
		return o;
	}
	Coord3D killed(Object *o)
	{
		o->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		REQUIRE(death(o)->isSlowDeathActivated());
		return death(o)->crashPlace();
	}
	std::uint64_t hits() const
	{
		const auto &per = logic->fxEvents().perSite();
		const auto it = per.find("SlowDeath Sound HIT_GROUND");
		return it == per.end() ? 0u : (std::uint64_t)it->second;
	}
	static void moving(Object *o, bool on) { o->setModelConditionState(CombatNames::modelCondition("MOVING"), on); }
	static float distance(const Coord3D &a, float x, float y) { return std::sqrt((a.x - x) * (a.x - x) + (a.y - y) * (a.y - y)); }
};
} // namespace

TEST_CASE("GiantBirdSlowDeath: the module data's defaults (RW 0x865076) and fields (RW 0xC59430 after SlowDeathBehavior's)")
{
	BirdWorld e;
	const GiantBirdSlowDeathBehaviorModuleData *d = e.death(e.unit("Plain", 'A', 300, 300))->birdData();
	CHECK(d->m_fxHitGround.empty());
	CHECK(d->m_oclHitGround.empty());
	CHECK(d->m_delayFromGroundToFinalDeath == 0.0f);
	CHECK(d->m_crashAvoidKindOfs == KindOfMaskType{});
	CHECK(d->m_crashAvoidRadius == 800.0f);
	CHECK(d->m_crashAvoidStrength == 0.1f);
	CHECK_FALSE(d->m_needToMaintainFlailingHeight);
	d = e.death(e.unit("Bird", 'A', 400, 400))->birdData();
	KindOfMaskType structure{};
	structure[0] = 1u << 7;
	CHECK(d->m_crashAvoidKindOfs == structure);
	CHECK(d->m_crashAvoidRadius == 200.0f);
	CHECK(d->m_crashAvoidStrength == 0.3f);
	CHECK(d->m_needToMaintainFlailingHeight);
	CHECK(d->m_destructionDelay == 100u); // the slow death's own fields
	CHECK(d->m_sounds[SDPHASE_HIT_GROUND] == std::vector<std::string>{ "BirdSlam" });
}

TEST_CASE("GiantBirdSlowDeath: the crash place (RW 0x66A61A) is the first free spot on the rings, never the centre; no ring within the radius: none")
{
	BirdWorld e;
	Object *b = e.unit("Bird", 'A', 500, 500);
	const KindOfMaskType kinds = e.death(b)->birdData()->m_crashAvoidKindOfs;
	const float r = CombatQueries::boundingSphereRadius(*b);
	REQUIRE(r > 10.0f); // tolerance: only that the minimum radius does not apply
	const Coord3D centre{ 500.0f, 500.0f, 7.0f };
	Coord3D out{ -1.0f, -1.0f, -1.0f };
	// the first ring starts at the angle -pi: x by the sine (0), y by the cosine (-1)
	REQUIRE(GiantBirdSlowDeathBehavior::findClearCrashPlace(*b, 200.0f, centre, kinds, out));
	CHECK(std::fabs(out.x - 500.0f) < 0.001f); // tolerance: the sine of the float -pi is not 0
	CHECK(out.y == 500.0f - r);
	CHECK(out.z == 7.0f);
	// a ring of the object's own radius must fit into the radius
	out = Coord3D{ -1.0f, -1.0f, -1.0f };
	CHECK_FALSE(GiantBirdSlowDeathBehavior::findClearCrashPlace(*b, r - 0.5f, centre, kinds, out));
	CHECK(out.x == -1.0f);
	CHECK(GiantBirdSlowDeathBehavior::findClearCrashPlace(*b, r, centre, kinds, out));
	// a building on the first spot: the next one that keeps both radii clear
	Object *block = e.unit("Block", 'B', 500, 500 - r);
	e.frames(1);
	const float clear = CombatQueries::boundingSphereRadius(*block) + r;
	REQUIRE(GiantBirdSlowDeathBehavior::findClearCrashPlace(*b, 200.0f, centre, kinds, out));
	const float gap = e.distance(out, 500.0f, 500.0f - r);
	CHECK(gap >= clear - 0.01f); // tolerance: the float distance of the test
	CHECK(gap < 200.0f);
	// the building does not count for another kind
	KindOfMaskType other = kinds;
	other[0] |= 1u << 8;
	REQUIRE(GiantBirdSlowDeathBehavior::findClearCrashPlace(*b, 200.0f, centre, other, out));
	CHECK(out.y == 500.0f - r);
	// beyond the reach of the rings every spot is taken
	e.unit("Block", 'B', 500, 500 + r);
	e.unit("Block", 'B', 500 + r, 500);
	e.unit("Block", 'B', 500 - r, 500);
	e.frames(1);
	CHECK_FALSE(GiantBirdSlowDeathBehavior::findClearCrashPlace(*b, r + 1.0f, centre, kinds, out));
}

TEST_CASE("GiantBirdSlowDeath: a standing bird falls beside itself at 30 per frame and hits the ground once, the body goes after DestructionDelay")
{
	BirdWorld e;
	Object *b = e.flyer("Bird", 500, 500, 120);
	const ObjectID id = b->getID();
	const float r = CombatQueries::boundingSphereRadius(*b);
	e.moving(b, false);
	const Coord3D place = e.killed(b);
	const PhysicsBehavior *phys = PhysicsBehavior::find(*b);
	REQUIRE(phys);
	CHECK(place.y == 500.0f - r);
	CHECK(place.z == 0.0f);
	CHECK(phys->isFlying());
	CHECK(phys->landing().y == place.y);
	CHECK(phys->speed() == 30.0f);
	CHECK(e.hits() == 0u);
	bool stop = false; // S-5606: the fall notes the fields no code of the class reads
	for (const std::string &line : e.logic->report().stops)
	{
		stop = stop || line.rfind("[S-5606] GiantBirdSlowDeathBehavior", 0) == 0;
	}
	CHECK(stop);
	e.runUntil([&] { return e.hits() > 0u; }, 30);
	CHECK(e.hits() == 1u);
	CHECK(e.distance(*b->getPosition(), place.x, place.y) < 0.01f); // tolerance: the last point of the curve
	CHECK(b->testModelCondition(CombatNames::modelCondition("RUBBLE")));
	e.frames(20);
	CHECK(e.hits() == 1u);
	e.runUntil([&] { return e.byId(id) == nullptr; }, 100);
	CHECK(e.byId(id) == nullptr);
}

TEST_CASE("GiantBirdSlowDeath: a moving bird falls its speed * 5 ahead when that is above 50, and may bounce (RW 0x86517A .. 0x8651DF)")
{
	BirdWorld e;
	auto place = [&](float speed, bool &bounces) {
		Object *b = e.flyer("Bird", 400, 500, 120);
		b->setOrientation(0.0f);
		dynamic_cast<GiantBirdAIUpdate &>(*b->getAIUpdateInterface()).flight().speed = speed;
		e.moving(b, true);
		const Coord3D p = e.killed(b);
		const std::uint64_t before = e.hits();
		e.runUntil([&] { return !PhysicsBehavior::find(*b)->isFlying(); }, 60);
		e.frames(2);
		bounces = e.hits() > before + 1u;
		e.logic->destroyObject(b);
		e.frames(1);
		return p;
	};
	bool bounces = false;
	const float r = [&] { return CombatQueries::boundingSphereRadius(*e.unit("Plain", 'A', 100, 100)); }();
	Coord3D p = place(20.0f, bounces);
	CHECK(p.x == 500.0f); // 100 ahead
	CHECK(p.y == 500.0f - r);
	CHECK(bounces);
	// 10 * 5 is not above 50
	p = place(10.0f, bounces);
	CHECK(std::fabs(p.x - 400.0f) < 0.001f); // tolerance: the sine of the float -pi is not 0
	CHECK_FALSE(bounces);
	p = place(10.5f, bounces);
	CHECK(p.x == 452.5f);
}

TEST_CASE("GiantBirdSlowDeath: without an AI the body falls straight down; a moving object without a bird's AI does not fall (RW 0x865186)")
{
	BirdWorld e;
	Object *statue = e.flyer("Statue", 300, 300, 90);
	const Coord3D p = e.killed(statue);
	CHECK(p.x == 300.0f);
	CHECK(p.y == 300.0f);
	CHECK(PhysicsBehavior::find(*statue)->isFlying());
	Object *walker = e.flyer("Walker", 600, 600, 0);
	e.moving(walker, true);
	e.killed(walker);
	CHECK_FALSE(PhysicsBehavior::find(*walker)->isFlying());
	Object *standing = e.flyer("Walker", 700, 700, 0);
	e.moving(standing, false);
	e.killed(standing);
	CHECK(PhysicsBehavior::find(*standing)->isFlying());
}

TEST_CASE("PhysicsBehavior: a paralysed object flies on (RW 0x792C6A: DISABLED_PARALYZED), one that is held stays")
{
	BirdWorld e;
	for (const unsigned type : { 4u, 3u })
	{
		Object *statue = e.flyer("Statue", 300, 300, 90);
		const Coord3D landing{ 400.0f, 300.0f, 0.0f };
		PhysicsBehavior::find(*statue)->flyTo(landing, 10.0f, 10.0f);
		statue->setDisabled(type, 0x3FFFFFFFu);
		const float x = statue->getPosition()->x;
		e.frames(30);
		CHECK((statue->getPosition()->x > x) == (type == 4u)); // tolerance: flew on or not
		CHECK(PhysicsBehavior::find(*statue)->isFlying() == (type == 3u));
		e.logic->destroyObject(statue);
		e.frames(1);
	}
}

TEST_CASE("GiantBirdSlowDeath: a bounce makes the HIT_GROUND phase pending again (RW 0x865012 .. 0x865018)")
{
	BirdWorld e;
	Object *b = e.flyer("Bouncer", 500, 500, 120);
	e.moving(b, false);
	e.killed(b);
	e.runUntil([&] { return e.hits() > 1u; }, 80);
	CHECK(e.hits() == 2u);
	e.frames(30);
	CHECK(e.hits() == 2u); // BounceCount 1
}

TEST_CASE("GiantBirdSlowDeath: the snapshot keeps the crash place and the slow death's state (RW 0x86502B), the hash takes it")
{
	BirdWorld e;
	Object *b = e.flyer("Bird", 500, 500, 120);
	e.moving(b, false);
	const Coord3D place = e.killed(b);
	GiantBirdSlowDeathBehavior *d = e.death(b);
	XferSave saved;
	d->xfer(saved);
	Object *o = e.flyer("Bird", 300, 300, 0);
	GiantBirdSlowDeathBehavior *other = e.death(o);
	XferLoad load(saved.data());
	other->xfer(load);
	CHECK(other->crashPlace().x == place.x);
	CHECK(other->crashPlace().y == place.y);
	CHECK(other->isSlowDeathActivated());
	CHECK(other->destructionFrame() == d->destructionFrame());
	// the hash takes the crash place: two states of one module that differ in nothing else
	auto hash = [](const GiantBirdSlowDeathBehavior &m) {
		StateHasher h;
		m.crc(h);
		return h.value();
	};
	const std::uint32_t before = hash(*d);
	std::vector<std::uint8_t> moved = saved.data();
	moved.back() ^= 1; // the z of the crash place is the last word
	XferLoad movedLoad(moved);
	d->xfer(movedLoad);
	CHECK(hash(*d) != before);
	XferLoad again(saved.data());
	d->xfer(again);
	CHECK(hash(*d) == before);
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad shortLoad(truncated);
	CHECK_THROWS_AS(other->xfer(shortLoad), XferException);
}

TEST_CASE("GiantBirdSlowDeath: a save from before the port (the unported shell's version 1 block) still loads, no death begun and asleep; a version 3 is refused")
{
	BirdWorld e;
	Object *b = e.flyer("Bird", 500, 500, 120);
	GiantBirdSlowDeathBehavior *d = e.death(b);
	REQUIRE_FALSE(d->isSlowDeathActivated());
	// the block UnportedUpdateModule::xfer wrote for this module: version 1, the UpdateModule block (asleep for good), the call count
	UnportedUpdateModule shell(b, d->getModuleData(), MODULEINTERFACE_UPDATE);
	shell.update();
	XferSave old;
	shell.xfer(old);
	REQUIRE(old.data().front() == 1);
	XferLoad load(old.data());
	d->xfer(load);
	CHECK(load.atEnd()); // the call count is read, nothing is left over
	CHECK_FALSE(d->isSlowDeathActivated());
	CHECK(d->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
	XferSave now;
	d->xfer(now);
	CHECK(now.data().front() == 2);
	std::vector<std::uint8_t> future = now.data();
	future.front() = 3;
	XferLoad bad(future);
	CHECK_THROWS_AS(d->xfer(bad), XferException);
}
