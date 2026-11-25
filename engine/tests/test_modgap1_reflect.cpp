// OpenBFME. ReflectDamage unit tests (RW 0x8BED43 onDamage, data RW 0x8BEC6E / 0xC70928; both editions).
// The expected values are the data's own: 30% of the INPUT amount back to the attacker, at least MinimumDamageToReflect, never for damage type REFLECTED.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/ReflectDamage.h"

using namespace combattest;

namespace
{
std::string thornUnit(const char *name, const char *fields)
{
	return std::string("Object ") + name + "\n"
		"  KindOf = INFANTRY SELECTABLE SCORE\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 8\n"
		"  GeometryMinorRadius = 8\n"
		"  GeometryHeight = 20\n"
		"  ArmorSet\n"
		"    Conditions = None\n"
		"    Armor = NoArmor\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 1000\n"
		"  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"  End\n"
		"  Behavior = DestroyDie ModuleTag_Destroy\n"
		"  End\n" + fields +
		"End\n";
}

const std::string kReflectObjects =
	thornUnit("ThornUnit",
		"  Behavior = ReflectDamage ModuleTag_Reflect\n"
		"    DamageTypesToReflect = NONE +SLASH +CRUSH\n"
		"    ReflectDamagePercentage = 30%\n"
		"    MinimumDamageToReflect = 2.0\n"
		"  End\n") +
	thornUnit("PlusOnlyUnit",
		"  Behavior = ReflectDamage ModuleTag_Reflect\n"
		"    DamageTypesToReflect = +SLASH +CRUSH\n"
		"    ReflectDamagePercentage = 30%\n"
		"    MinimumDamageToReflect = 2.0\n"
		"  End\n") +
	thornUnit("AllThornUnit",
		"  Behavior = ReflectDamage ModuleTag_Reflect\n"
		"    DamageTypesToReflect = ALL -STRUCTURAL\n"
		"    ReflectDamagePercentage = 50%\n"
		"    MinimumDamageToReflect = 1.0\n"
		"  End\n") +
	thornUnit("PlainUnit", "");

float hit(CombatWorld &w, Object *victim, Object *attacker, int type, float amount)
{
	DamageInfo info;
	info.m_input.m_sourceID = attacker ? attacker->getID() : 999999;
	info.m_input.m_damageType = type;
	info.m_input.m_deathType = DEATH_NORMAL;
	info.m_input.m_amount = amount;
	const float before = w.health(victim);
	victim->attemptDamage(info);
	return before - w.health(victim);
}
} // namespace

TEST_CASE("modgap1 ReflectDamage: the attacker takes 30% of the input amount back for a listed damage type, and nothing for an unlisted one (RW 0x8BED43)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CombatWorld w(kReflectObjects.c_str());
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		Object *thorn = w.unit("ThornUnit", 'A', 400, 400);
		Object *attacker = w.unit("PlainUnit", 'B', 420, 400);
		REQUIRE(thorn != nullptr);
		REQUIRE(attacker != nullptr);
		auto *rd = dynamic_cast<ReflectDamage *>(thorn->findModule("ReflectDamage"));
		REQUIRE(rd != nullptr);
		CHECK(hit(w, thorn, attacker, DAMAGE_SLASH, 100.0f) == 100.0f);
		CHECK(w.health(attacker) == doctest::Approx(970.0f)); // tolerance: 30% float32 parsePercentToReal
		CHECK(hit(w, thorn, attacker, DAMAGE_PIERCE, 100.0f) == 100.0f);
		CHECK(w.health(attacker) == doctest::Approx(970.0f)); // tolerance: 30% float32 parsePercentToReal
		CHECK(hit(w, thorn, attacker, DAMAGE_CRUSH, 50.0f) == 50.0f);
		CHECK(w.health(attacker) == doctest::Approx(955.0f)); // tolerance: 30% float32 parsePercentToReal

		XferSave save;
		rd->xfer(save);
		XferLoad load(save.data());
		rd->xfer(load);
	}
}

TEST_CASE("modgap1 ReflectDamage: a small hit reflects MinimumDamageToReflect, and a hit with no source object reflects nothing (RW 0x8BED90 comiss, 0x8BED67)")
{
	CombatWorld w(kReflectObjects.c_str());
	Object *thorn = w.unit("ThornUnit", 'A', 400, 400);
	Object *attacker = w.unit("PlainUnit", 'B', 420, 400);
	hit(w, thorn, attacker, DAMAGE_SLASH, 4.0f);
	CHECK(w.health(attacker) == 998.0f);
	CHECK(hit(w, thorn, nullptr, DAMAGE_SLASH, 100.0f) == 100.0f);
	CHECK(w.health(attacker) == 998.0f);
}

TEST_CASE("modgap1 ReflectDamage: damage of type REFLECTED is never reflected, so two thorned objects do not bounce a hit for ever (RW 0x8BED52)")
{
	CombatWorld w(kReflectObjects.c_str());
	Object *a = w.unit("AllThornUnit", 'A', 400, 400);
	Object *b = w.unit("AllThornUnit", 'B', 420, 400);
	hit(w, a, b, DAMAGE_SLASH, 100.0f);
	CHECK(w.health(a) == 900.0f);
	CHECK(w.health(b) == 950.0f);
	CHECK(hit(w, a, b, DAMAGE_REFLECTED, 100.0f) == 100.0f);
	CHECK(w.health(b) == 950.0f);
}

TEST_CASE("modgap1 ReflectDamage: DamageTypesToReflect = ALL -STRUCTURAL reflects every type but STRUCTURAL (RW 0x73A5C9)")
{
	CombatWorld w(kReflectObjects.c_str());
	Object *thorn = w.unit("AllThornUnit", 'A', 400, 400);
	Object *attacker = w.unit("PlainUnit", 'B', 420, 400);
	hit(w, thorn, attacker, DAMAGE_STRUCTURAL, 100.0f);
	CHECK(w.health(attacker) == 1000.0f);
	hit(w, thorn, attacker, DAMAGE_PIERCE, 100.0f);
	CHECK(w.health(attacker) == 950.0f);
}

// RW 0x73A5D5: the damage type flag parser starts from ALL (or ebx, -1) and "+X" only sets a bit.
TEST_CASE("modgap1 ReflectDamage: a list of + tokens without NONE reflects every damage type, as the retail parser starts from ALL (RW 0x73A5D5)")
{
	CombatWorld w(kReflectObjects.c_str());
	Object *thorn = w.unit("PlusOnlyUnit", 'A', 400, 400);
	Object *attacker = w.unit("PlainUnit", 'B', 420, 400);
	hit(w, thorn, attacker, DAMAGE_PIERCE, 100.0f);
	CHECK(w.health(attacker) == doctest::Approx(970.0f)); // tolerance: 30% float32 parsePercentToReal
}
