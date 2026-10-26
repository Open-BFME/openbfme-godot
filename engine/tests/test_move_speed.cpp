// OpenBFME unit tests. GPL-3.0.
// F2: RotWK 0x5E4015 asks Object for the SPEED (8) product, innate=true, then 0x5E401E multiplies the resolved speed.
// Synthetic live objects exercise that host, the existing pool lifecycle and the normal move command; no retail data needed.
#include "doctest.h"
#include "MoveTestUtil.h"
#include "Common/JobSystem.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Object/AttributeModifierPool.h"

#include <cstdint>
#include <cmath>
#include <cstring>

using namespace movetest;

namespace
{
struct SpeedWorld : MoveWorld
{
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	SpeedWorld()
		: MoveWorld(100, 100,
			"ChildObject SpeedWalker Walker\n Behavior = AttributeModifierPoolUpdate Pool\n End\nEnd\n"
			"Object MemberOfHorde\n Behavior = AttributeModifierPoolUpdate Pool\n End\nEnd\n"
			"Object Horde\n Behavior = AttributeModifierPoolUpdate Pool\n End\nEnd\n")
	{
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		const std::string error = w.load(
			"ModifierList Half\n Category = SPELL\n Modifier = SPEED 50%\nEnd\n"
			"ModifierList Identity\n Category = SPELL\n Modifier = SPEED 100%\nEnd\n"
			"ModifierList Double\n Category = SPELL\n Modifier = SPEED 200%\nEnd\n"
			"ModifierList Innate\n Category = INNATE_ARMOR\n Modifier = SPEED 50%\nEnd\n"
			"ModifierList ArmorOnly\n Category = SPELL\n Modifier = ARMOR 200%\nEnd\n"
			"ModifierList A\n Category = SPELL\n Modifier = SPEED 1.1\nEnd\n"
			"ModifierList B\n Category = SPELL\n Modifier = SPEED 1.2\nEnd\n"
			"ModifierList C\n Category = SPELL\n Modifier = SPEED 1.3\nEnd\n");
		REQUIRE_MESSAGE(error.empty(), error);
		buildMap();
	}
	~SpeedWorld()
	{
		logic->reset();
		TheAttributeModifierStore = savedModifiers;
	}
	float maxSpeed(Object *o)
	{
		auto *ai = aiOf(o);
		return ai->curLocomotor()->getMaxSpeedForCondition(ai->locomotorHost());
	}
	AttributeModifierPool &pool(Object *o)
	{
		auto *p = static_cast<AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate"));
		REQUIRE(p != nullptr);
		return *p;
	}
};
std::uint32_t floatBits(float v)
{
	std::uint32_t bits;
	std::memcpy(&bits, &v, sizeof(bits));
	return bits;
}
} // namespace

TEST_CASE("move SPEED: 0.5, 1 and 2 reach the live host and straight-line motion")
{
	const char *list = "Half";
	float factor = 0.5f;
	SUBCASE("half") {}
	SUBCASE("identity") { list = "Identity"; factor = 1.0f; }
	SUBCASE("double") { list = "Double"; factor = 2.0f; }
	SpeedWorld w;
	Object *u = w.spawn("SpeedWalker", 105, 105);
	w.frames(3);
	REQUIRE(w.maxSpeed(u) == 11.0f);
	REQUIRE(u->addAttributeModifier(list, -1));
	auto *ai = w.aiOf(u);
	float product = 0;
	CHECK(ai->locomotorHost().speedAttributeModifier(product));
	CHECK(product == factor);
	CHECK(w.maxSpeed(u) == 11.0f * factor);
	CHECK(ai->curLocomotor()->getMaxAcceleration(ai->locomotorHost()) == (11.0f * factor) / 3.0f);
	CHECK(ai->curLocomotor()->getBraking(ai->locomotorHost()) == (11.0f * factor) / 3.0f);
	ai->aiMoveToPosition({ 805, 105, 0 }, CMD_FROM_PLAYER);
	w.frames(10); // beyond path admission and acceleration, far from braking
	CHECK(ai->curLocomotor()->speed() == 11.0f * factor);
	const Coord3D before = *u->getPosition();
	w.frames(1);
	// merge queue q26: on the archive the walker's path from (105, 105) bends slightly (y drifts 0.04 .. 0.17 a frame), so the frame's step is checked
	// by its length, not along x alone
	const float dx = u->getPosition()->x - before.x, dy = u->getPosition()->y - before.y;
	CHECK(std::sqrt(dx * dx + dy * dy) == doctest::Approx(11.0f * factor).epsilon(1e-4)); // tolerance: float rounding of the position update
}

TEST_CASE("move SPEED: absent pool, empty pool and a non-SPEED modifier leave default speed")
{
	SpeedWorld w;
	Object *bare = w.spawn("Walker", 105, 105);
	Object *u = w.spawn("SpeedWalker", 305, 105);
	w.frames(3);
	float product = 0;
	CHECK_FALSE(w.aiOf(bare)->locomotorHost().speedAttributeModifier(product));
	CHECK(w.maxSpeed(bare) == 11.0f);
	CHECK_FALSE(w.aiOf(u)->locomotorHost().speedAttributeModifier(product));
	CHECK(w.maxSpeed(u) == 11.0f);
	REQUIRE(u->addAttributeModifier("ArmorOnly", -1));
	CHECK_FALSE(w.aiOf(u)->locomotorHost().speedAttributeModifier(product));
	CHECK(w.maxSpeed(u) == 11.0f);
}

TEST_CASE("move SPEED: exact expiry frame removes the speed effect without waiting for pool cleanup")
{
	SpeedWorld w;
	Object *u = w.spawn("SpeedWalker", 105, 105);
	w.frames(3);
	REQUIRE(w.logic->getFrame() == 3);
	REQUIRE(u->addAttributeModifier("Double", 2)); // active at 3 and 4, expired at 5
	CHECK(w.maxSpeed(u) == 22.0f);
	w.frames(1);
	CHECK(w.maxSpeed(u) == 22.0f);
	w.frames(1);
	CHECK(w.logic->getFrame() == 5);
	float product = 0;
	CHECK_FALSE(w.aiOf(u)->locomotorHost().speedAttributeModifier(product));
	CHECK(w.maxSpeed(u) == 11.0f);
}

TEST_CASE("move SPEED: frame-zero suppression, innate inclusion and inclusive category disable boundary")
{
	SpeedWorld w;
	Object *u = w.spawn("SpeedWalker", 105, 105);
	REQUIRE(u->addAttributeModifier("Innate", -1));
	CHECK(w.maxSpeed(u) == 11.0f); // all categories start disabled through frame zero
	w.frames(3);
	CHECK(w.maxSpeed(u) == 5.5f);
	float product = 0;
	CHECK_FALSE(u->attributeModifierProduct(8, nullptr, false, product)); // positive control for innate=false
	CHECK(w.aiOf(u)->locomotorHost().speedAttributeModifier(product));
	CHECK(product == 0.5f);
	const int innate = w.modifiers.find("Innate")->m_category;
	w.pool(u).disableCategories(1u << innate, 5);
	CHECK(w.maxSpeed(u) == 11.0f);
	w.frames(2);
	CHECK(w.logic->getFrame() == 5);
	CHECK(w.maxSpeed(u) == 11.0f);
	w.frames(1);
	CHECK(w.maxSpeed(u) == 5.5f);
}

TEST_CASE("move SPEED: pool entry order rounds each product before multiplying the base speed")
{
	SpeedWorld w;
	Object *u = w.spawn("SpeedWalker", 105, 105);
	w.frames(3);
	REQUIRE(u->addAttributeModifier("A", -1));
	REQUIRE(u->addAttributeModifier("B", -1));
	REQUIRE(u->addAttributeModifier("C", -1));
	float product = 0;
	CHECK(w.aiOf(u)->locomotorHost().speedAttributeModifier(product));
	// IEEE binary32: ((1 * 1.1f) * 1.2f) * 1.3f, then product * 11.0f (RW 0x804FFF / 0x5E401E).
	CHECK(floatBits(product) == 0x3fdba5e3u);
	CHECK(floatBits(w.maxSpeed(u)) == 0x4197020cu);
}

TEST_CASE("move SPEED: horde propagation applies each object's own pool once")
{
	SpeedWorld w;
	Object *horde = w.spawn("Horde", 305, 305);
	w.frames(3);
	const auto members = w.membersOf(horde);
	REQUIRE(members.size() == 15);
	CHECK(w.maxSpeed(horde) == 10.0f);
	REQUIRE(horde->addAttributeModifier("Double", -1));
	CHECK(w.pool(horde).entryCount() == 1);
	CHECK(w.maxSpeed(horde) == 20.0f);
	for (Object *member : members)
	{
		CHECK(w.pool(member).entryCount() == 1);
		CHECK(w.maxSpeed(member) == 22.0f);
	}
	// A member-only debuff must neither reapply the horde product nor affect its siblings.
	REQUIRE(members.front()->addAttributeModifier("Half", -1));
	CHECK(w.maxSpeed(members.front()) == 11.0f);
	CHECK(w.maxSpeed(members.back()) == 22.0f);
	CHECK(w.maxSpeed(horde) == 20.0f);
}

TEST_CASE("move SPEED: maximum cap precedes the product and temporary cap follows it")
{
	SpeedWorld w;
	Object *u = w.spawn("SpeedWalker", 105, 105);
	w.frames(3);
	REQUIRE(u->addAttributeModifier("Double", -1));
	auto *loco = w.aiOf(u)->curLocomotor();
	loco->setMaxSpeedCap(6.0f);
	CHECK(w.maxSpeed(u) == 12.0f);
	loco->setTemporarySpeedCap(9.0f);
	CHECK(w.maxSpeed(u) == 9.0f);
}

TEST_CASE("move SPEED: save/load continues motion through suppression and expiry with identical frame hashes")
{
	SpeedWorld w;
	Object *u = w.spawn("SpeedWalker", 105, 105);
	const ObjectID id = u->getID();
	w.frames(3);
	REQUIRE(u->addAttributeModifier("Double", 8)); // expires at frame 11
	REQUIRE(u->addAttributeModifier("Innate", -1));
	w.pool(u).disableCategories(1u << w.modifiers.find("Innate")->m_category, 8);
	w.aiOf(u)->aiMoveToPosition({ 805, 105, 0 }, CMD_FROM_PLAYER);
	w.frames(3);
	REQUIRE(w.logic->getFrame() == 6);
	REQUIRE(w.maxSpeed(u) == 22.0f);
	const auto savedHash = w.logic->computeStateHash();
	std::vector<std::uint8_t> saved;
	std::string error;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(saved, &error), error);
	std::vector<std::uint32_t> hashes;
	std::vector<Coord3D> positions;
	std::vector<float> speeds;
	// Frame 7,8: innate suppressed; 9,10: both; 11 onward: only innate.
	const float expected[] = { 22, 22, 11, 11, 5.5f, 5.5f, 5.5f, 5.5f, 5.5f };
	for (float speed : expected)
	{
		w.frames(1);
		CHECK(w.maxSpeed(u) == speed);
		hashes.push_back(w.logic->computeStateHash());
		positions.push_back(*u->getPosition());
		speeds.push_back(w.aiOf(u)->curLocomotor()->speed());
	}
	REQUIRE_MESSAGE(w.logic->loadSnapshot(saved, &error), error);
	CHECK(w.logic->getFrame() == 6);
	CHECK(w.logic->computeStateHash() == savedHash);
	u = w.logic->findObjectByID(id); // objects are reconstructed by the load
	REQUIRE(u != nullptr);
	std::vector<std::uint8_t> again;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(again, &error), error);
	CHECK(again == saved);
	for (size_t i = 0; i < hashes.size(); ++i)
	{
		w.frames(1);
		CHECK(w.maxSpeed(u) == expected[i]);
		CHECK(w.logic->computeStateHash() == hashes[i]);
		CHECK(u->getPosition()->x == positions[i].x);
		CHECK(u->getPosition()->y == positions[i].y);
		CHECK(w.aiOf(u)->curLocomotor()->speed() == speeds[i]);
	}
}

TEST_CASE("move SPEED: repeated Uber traces agree at 1, 2 and 4 logic threads")
{
	struct RestoreThreads
	{
		int saved = JobSystem::logic().threadCount();
		~RestoreThreads() { JobSystem::logic().setThreadCount(saved); }
	} restore;
	std::vector<std::uint32_t> first;
	for (int threads : { 1, 2, 4 })
	{
		JobSystem::logic().setThreadCount(threads);
		SpeedWorld w;
		w.logic->setSimulationProfile(SimulationProfile::Uber);
		std::vector<Object *> units;
		for (int i = 0; i < 8; ++i)
		{
			units.push_back(w.spawn("SpeedWalker", 105, 105 + 40.0f * i));
		}
		w.frames(3);
		for (size_t i = 0; i < units.size(); ++i)
		{
			REQUIRE(units[i]->addAttributeModifier(i % 2 ? "Double" : "Half", 9));
			w.aiOf(units[i])->aiMoveToPosition({ 805, units[i]->getPosition()->y, 0 }, CMD_FROM_PLAYER);
		}
		std::vector<std::uint32_t> trace;
		for (int frame = 0; frame < 18; ++frame)
		{
			w.frames(1);
			trace.push_back(w.logic->computeStateHash());
			for (Object *u : units)
			{
				trace.push_back(floatBits(u->getPosition()->x));
				trace.push_back(floatBits(w.aiOf(u)->curLocomotor()->speed()));
				trace.push_back(floatBits(w.maxSpeed(u)));
			}
		}
		if (first.empty())
		{
			first = trace;
		}
		CHECK(trace == first);
		StateHasher digest;
		for (std::uint32_t value : trace)
		{
			digest.addU32(value);
		}
		MESSAGE("SPEED trace: threads=" << threads << " frames=18 digest=" << digest.value());
	}
}
