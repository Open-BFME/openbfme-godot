// OpenBFME. GPL-3.0.
// GiantBirdSlowDeathBehavior tests (both editions; RW 0x86511C / 0x865009 / 0x66A61A).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/GiantBirdSlowDeathBehavior.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/PartitionManager.h"

#include <string>

using namespace combattest;

namespace
{
std::string unitWith(const char *name, const char *kinds, int radius, const std::string &fields)
{
	return std::string("Object ") + name + "\n"
		"  KindOf = " + kinds + "\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = " + std::to_string(radius) + "\n"
		"  GeometryMinorRadius = " + std::to_string(radius) + "\n"
		"  GeometryHeight = 20\n"
		"  ArmorSet\n"
		"    Conditions = None\n"
		"    Armor = NoArmor\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 1000\n"
		"  End\n" + fields + "End\n";
}

const std::string kBirdObjects =
	unitWith("Flyer", "INFANTRY SELECTABLE SCORE", 8,
		"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n"
		"  Behavior = PhysicsBehavior ModuleTag_Physics\n  End\n"
		"  Behavior = GiantBirdSlowDeathBehavior ModuleTag_Death\n"
		"    DeathTypes = ALL\n"
		"    DestructionDelay = 20000\n"
		"    CrashAvoidKindOfs = STRUCTURE\n"
		"    CrashAvoidRadius = 200\n"
		"  End\n") +
	unitWith("Block", "STRUCTURE SELECTABLE", 30, "");
} // namespace

TEST_CASE("modgap1 GiantBirdSlowDeathBehavior: clear point ring search and crash flight in both editions (RW 0x66A61A / 0x86511C)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CombatWorld w(kBirdObjects.c_str());
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		Object *flyer = w.unit("Flyer", 'A', 300, 300);
		w.unit("Block", 'B', 300, 300);
		w.frames(1);

		KindOfMaskType kinds{};
		const int structure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
		REQUIRE(structure >= 0); // tolerance: KindOf index is non-negative when found
		kinds[(size_t)structure / 32] |= 1u << (structure % 32);
		const Coord3D in{ 300.0f, 300.0f, 0.0f };
		Coord3D out = in;
		REQUIRE(GiantBirdSlowDeathBehavior::findClearPoint(*flyer, 200.0f, in, out, kinds));
		CHECK(out.y == doctest::Approx(260.0f)); // tolerance: sin / cos of a float32 -PI
		CHECK(out.x == doctest::Approx(300.0f)); // tolerance: sin / cos of a float32 -PI
		Coord3D unchanged = in;
		CHECK_FALSE(GiantBirdSlowDeathBehavior::findClearPoint(*flyer, 30.0f, in, unchanged, kinds));
		CHECK(unchanged.y == in.y);

		auto *death = dynamic_cast<GiantBirdSlowDeathBehavior *>(flyer->findModule("GiantBirdSlowDeathBehavior"));
		REQUIRE(death != nullptr);
		flyer->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		REQUIRE(death->isSlowDeathActivated());
		CHECK(death->crashTarget().y == doctest::Approx(260.0f)); // tolerance: sin / cos of a float32 -PI
		PhysicsBehavior *physics = flyer->physicsModule();
		REQUIRE(physics != nullptr);
		CHECK(physics->isFlying());
		CHECK(physics->speed() == 30.0f);

		// xfer round-trip
		StateHasher h1;
		death->crc(h1);
		XferSave save;
		death->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		death->xfer(load);
		StateHasher h2;
		death->crc(h2);
		CHECK(h1.value() == h2.value());
	}
}
