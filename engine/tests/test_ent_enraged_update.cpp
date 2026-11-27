// OpenBFME. GPL-3.0.
// EntEnragedUpdate tests (both editions; RW 0x8B6866 / 0x8B697C / 0x8B66CE).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/EntEnragedUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

#include <string>

using namespace combattest;

namespace
{
std::string unitWith(const char *name, const char *kinds, int radius, const std::string &body, const std::string &fields, bool destroyDie = true)
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
		"  End\n" + body +
		(destroyDie ? "  Behavior = DestroyDie ModuleTag_Destroy\n  End\n" : "") + fields +
		"End\n";
}

const char kPlainBody[] = "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 1000\n  End\n";
const char kAI[] = "  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n";
const char kUnitKinds[] = "INFANTRY SELECTABLE SCORE";

const std::string kEntObjects =
	unitWith("Ent", kUnitKinds, 8, kPlainBody, std::string(kAI) +
		"  Behavior = EntEnragedUpdate ModuleTag_Rage\n"
		"    HatedObjectFilter = NONE ENEMIES +HatedMill\n"
		"    FriendlyDeadFilter = NONE +FallenFriend\n"
		"    ScanDistance = 100\n"
		"    EnragedTime = 1000\n"
		"    TimeUntilCanRageAgain = 2000\n"
		"    EnragedTransitionTime = 400\n"
		"  End\n") +
	unitWith("HatedMill", "STRUCTURE SELECTABLE", 8, kPlainBody, "") +
	unitWith("FallenFriend", kUnitKinds, 8, kPlainBody, "  Behavior = SlowDeathBehavior ModuleTag_Death\n    DeathTypes = ALL\n    DestructionDelay = 20000\n  End\n", false);

const unsigned kStatusEnraged = 0x36;

EntEnragedUpdate *entOf(Object *o) { return dynamic_cast<EntEnragedUpdate *>(o->findModule("EntEnragedUpdate")); }

void hit(Object *victim, float amount)
{
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_SLASH;
	info.m_input.m_deathType = DEATH_NORMAL;
	info.m_input.m_amount = amount;
	victim->attemptDamage(info);
}
} // namespace

TEST_CASE("modgap1 EntEnragedUpdate: hated enemy building, low health, dead friend, and xfer (RW 0x8B6866)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CombatWorld w(kEntObjects.c_str());
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		Object *ent = w.unit("Ent", 'A', 400, 400);
		REQUIRE(ent != nullptr);
		EntEnragedUpdate *rage = entOf(ent);
		REQUIRE(rage != nullptr);
		w.unit("HatedMill", 'B', 800, 800);
		w.frames(3);
		CHECK_FALSE(rage->enraged());
		Object *mill = w.unit("HatedMill", 'B', 450, 400);
		REQUIRE(mill != nullptr);
		w.frames(2);
		REQUIRE(rage->enraged());
		CHECK(ent->testStatus(kStatusEnraged));

		// xfer round-trip while enraged
		StateHasher h1;
		rage->crc(h1);
		XferSave save;
		rage->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		rage->xfer(load);
		StateHasher h2;
		rage->crc(h2);
		CHECK(h1.value() == h2.value());

		mill->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		w.frames(40);
		CHECK_FALSE(rage->enraged());
		CHECK_FALSE(ent->testStatus(kStatusEnraged));

		// Suppressed vs low health and forced
		rage->setSuppressed(true);
		hit(ent, 600.0f);
		w.frames(3);
		CHECK_FALSE(rage->enraged());
		rage->setSuppressed(false);
		w.frames(3);
		CHECK(rage->enraged());

		Object *other = w.unit("Ent", 'A', 700, 700);
		EntEnragedUpdate *forced = entOf(other);
		w.frames(2);
		REQUIRE_FALSE(forced->enraged());
		forced->setForced(true, false);
		w.frames(2);
		CHECK(forced->enraged());
	}
}
