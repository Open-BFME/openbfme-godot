// OpenBFME. MODGAP-1 tests, group 5 (MODULE_GAPS.md, stop S-5601): DeflectSpecialPower (RW 0x8C99DB initiate, 0x8C9A86 update, 0x8C9A3C onCollide) on synthetic data.
// The expected values are the binary's own: the model condition 158 DEFLECT_SPECIAL_POWER, the MOVING gate, the KindOf DEFLECT_BY_SPECIAL_POWER and the jam.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DeflectSpecialPower.h"
#include "GameLogic/Module/ProjectileModules.h"

using namespace combattest;

namespace
{
const char kDeflectObjects[] =
	"SpecialPower SpecialAbilityTestDeflect\n"
	"  Enum = SPECIAL_DEFLECT_PROJECTILES\n"
	"  ReloadTime = 0\n"
	"End\n"
	"Object DeflSpear\n"
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
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  Behavior = DeflectSpecialPower ModuleTag_Deflect\n"
	"    SpecialPowerTemplate = SpecialAbilityTestDeflect\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object DeflArrow\n"
	"  KindOf = PROJECTILE INERT DEFLECT_BY_SPECIAL_POWER NO_COLLIDE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_Flight\n"
	"    FirstHeight = 9\n"
	"    SecondHeight = 9\n"
	"    FirstPercentIndent = 20%\n"
	"    SecondPercentIndent = 90%\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryIsSmall = Yes\n"
	"  GeometryMajorRadius = 0.8\n"
	"End\n"
	"Object PlainArrow\n"
	"  KindOf = PROJECTILE INERT NO_COLLIDE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_Flight\n"
	"    FirstHeight = 9\n"
	"    SecondHeight = 9\n"
	"    FirstPercentIndent = 20%\n"
	"    SecondPercentIndent = 90%\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryIsSmall = Yes\n"
	"  GeometryMajorRadius = 0.8\n"
	"End\n";

struct DeflectWorld : CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *saved = TheSpecialPowerStore;
	DeflectWorld()
	{
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		const std::string err = w.load(kDeflectObjects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~DeflectWorld() { TheSpecialPowerStore = saved; }
	static DeflectSpecialPower *power(Object *o) { return DeflectSpecialPower::of(*o); }
	static bool shieldUp(Object *o) { return o->testModelCondition(CombatNames::modelCondition("DEFLECT_SPECIAL_POWER")); }
};
} // namespace

TEST_CASE("modgap1 DeflectSpecialPower: the power makes the unit pending, the next update makes it active with model condition DEFLECT_SPECIAL_POWER (RW 0x8C99DB, 0x8C9A86)")
{
	DeflectWorld w;
	Object *spear = w.unit("DeflSpear", 'A', 400, 400);
	DeflectSpecialPower *power = DeflectWorld::power(spear);
	REQUIRE(power != nullptr);
	CHECK_FALSE(power->isActive());
	CHECK_FALSE(power->pending());
	CHECK_FALSE(DeflectWorld::shieldUp(spear));
	power->doSpecialPower(0);
	CHECK(power->pending()); // at once, from initiate
	CHECK_FALSE(power->isActive());
	w.frames(3);
	CHECK(power->isActive());
	CHECK_FALSE(power->pending());
	CHECK(DeflectWorld::shieldUp(spear));
	w.frames(10);
	CHECK(power->isActive()); // a standing unit keeps the shield up
	CHECK(DeflectWorld::shieldUp(spear));
}

TEST_CASE("modgap1 DeflectSpecialPower: a moving unit drops the shield; the power waits while the unit walks (RW 0x8C9A86, the gate RW 0x68BE97)")
{
	SUBCASE("active, then the unit walks")
	{
		DeflectWorld w;
		Object *spear = w.unit("DeflSpear", 'A', 400, 400);
		DeflectSpecialPower *power = DeflectWorld::power(spear);
		power->doSpecialPower(0);
		w.frames(3);
		REQUIRE(power->isActive());
		w.select(w.playerOf('A')->getPlayerIndex(), { spear });
		w.moveTo(w.playerOf('A')->getPlayerIndex(), 600, 400);
		w.frames(8);
		CHECK_FALSE(power->isActive());
		CHECK_FALSE(DeflectWorld::shieldUp(spear));
		CHECK_FALSE(power->pending()); // it does not come back by itself
		w.frames(120);
		CHECK_FALSE(power->isActive());
	}
	SUBCASE("pending while the unit walks: the shield comes up when it stands")
	{
		DeflectWorld w;
		Object *spear = w.unit("DeflSpear", 'A', 400, 400);
		DeflectSpecialPower *power = DeflectWorld::power(spear);
		w.select(w.playerOf('A')->getPlayerIndex(), { spear });
		w.moveTo(w.playerOf('A')->getPlayerIndex(), 700, 400);
		w.frames(4);
		power->doSpecialPower(0);
		w.frames(3);
		CHECK_FALSE(power->isActive()); // still walking
		CHECK(power->pending());
		w.frames(100);
		CHECK(spear->getPosition()->x > 695.0f); // tolerance: locomotor stops within arrival radius of goal x=700
		// RW 0x68B34C reads the locomotor's current speed (+ 0x40), and a locomotor with no goal does not reset it: the unit that walked up to its goal keeps the
		// last speed (3.67 here), so the gate (above 0.1, RW 0xBD83D4) still says "moving" until the locomotor brakes to 0
		Locomotor *loco = spear->getAIUpdateInterface()->curLocomotor();
		REQUIRE(loco != nullptr);
		loco->setSpeed(0.0f);
		w.frames(3);
		CHECK(power->isActive()); // standing
		CHECK(DeflectWorld::shieldUp(spear));
	}
}

TEST_CASE("modgap1 DeflectSpecialPower: an active shield jams a DEFLECT_BY_SPECIAL_POWER projectile and no other (RW 0x8C9A3C, jam RW 0x85E54D)")
{
	DeflectWorld w;
	Object *spear = w.unit("DeflSpear", 'A', 400, 400);
	DeflectSpecialPower *power = DeflectWorld::power(spear);
	Object *deflectable = w.unit("DeflArrow", 'B', 410, 400);
	Object *plain = w.unit("PlainArrow", 'B', 410, 410);
	REQUIRE(deflectable != nullptr);
	REQUIRE(plain != nullptr);
	power->onCollide(deflectable, nullptr, nullptr); // not active yet
	CHECK_FALSE(deflectable->isEffectivelyDead());
	power->doSpecialPower(0);
	w.frames(3);
	REQUIRE(power->isActive());
	power->onCollide(plain, nullptr, nullptr); // no KindOf DEFLECT_BY_SPECIAL_POWER
	CHECK_FALSE(plain->isEffectivelyDead());
	power->onCollide(nullptr, nullptr, nullptr);
	power->onCollide(deflectable, nullptr, nullptr);
	CHECK(deflectable->isEffectivelyDead());
}

TEST_CASE("modgap1 DeflectSpecialPower: the AI units' contact pass reaches onCollide and leaves the other unit alone")
{
	DeflectWorld w;
	Object *spear = w.unit("DeflSpear", 'A', 400, 400);
	Object *other = w.unit("DeflSpear", 'A', 404, 400);
	DeflectWorld::power(spear)->doSpecialPower(0);
	w.frames(10);
	CHECK_FALSE(other->isEffectivelyDead());
	CHECK_FALSE(spear->isEffectivelyDead());
	CHECK(DeflectWorld::power(spear)->isActive());
	CHECK(spear->collideModulesKnown()); // pairFactsKnown: the UBER-2B pair skip still applies to the shield-wall units
}

TEST_CASE("modgap1 DeflectSpecialPower: the stop line names S-5601")
{
	const std::string line = DeflectSpecialPower::stopLine();
	CHECK(line.compare(0, 8, "[S-5601]") == 0);
	CHECK(line.find("NO_COLLIDE") != std::string::npos);
}
