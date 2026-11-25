// OpenBFME MODGAP-2: TeleportToCasterSpecialPower (RW 0x8D1AEB, 0x8D1979): the stored units go to the target point; without a list the allies around the target come to the caster. Synthetic data.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/Module/StoreObjectsSpecialPower.h"
#include "GameLogic/Module/TeleportToCasterSpecialPower.h"
#include "GameLogic/Object/PartitionManager.h"

#include <cmath>

namespace
{
const char kObjects[] =
	"SpecialPower TestTeleportStore\n Enum = SPECIAL_SCREECH\n ReloadTime = 100\n ObjectFilter = NONE +INFANTRY SAME_PLAYER\nEnd\n"
	"SpecialPower TestTeleportGo\n Enum = SPECIAL_MTTROLL_BORED\n ReloadTime = 100\n ObjectFilter = NONE +INFANTRY SAME_PLAYER\nEnd\n"
	"Object TestCaster\n KindOf = INFANTRY SELECTABLE HERO\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AITag\n End\n"
	" Behavior = SpecialPowerModule StorePowerTag\n SpecialPowerTemplate = TestTeleportStore\n UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = SpecialPowerModule GoPowerTag\n SpecialPowerTemplate = TestTeleportGo\n UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = StoreObjectsSpecialPower StoreTag\n"
	"  SpecialPowerTemplate = TestTeleportStore\n Radius = 80\n StartAbilityRange = 100000\n"
	"  UnpackTime = 200\n PreparationTime = 200\n PackTime = 200\n IgnoreFacingCheck = Yes\n"
	" End\n"
	" Behavior = TeleportToCasterSpecialPower GoTag\n"
	"  SpecialPowerTemplate = TestTeleportGo\n Radius = 80\n MinDestinationRadius = 20\n MaxDestinationRadius = 60\n StartAbilityRange = 100000\n"
	"  UnpackTime = 200\n PreparationTime = 200\n PackTime = 200\n IgnoreFacingCheck = Yes\n"
	" End\nEnd\n"
	"Object TestFollower\n KindOf = INFANTRY SELECTABLE\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AITag\n End\n"
	"End\n";

struct TeleportWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *previous = TheSpecialPowerStore;
	TeleportWorld()
	{
		logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		const std::string error = w.load(kObjects);
		REQUIRE_MESSAGE(error.empty(), error);
	}
	~TeleportWorld() { TheSpecialPowerStore = previous; }
};

float distance2D(const Coord3D &a, const Coord3D &b)
{
	return std::hypot(a.x - b.x, a.y - b.y);
}
} // namespace

TEST_CASE("modgap2 teleport: the stored units teleport to the target point and the list clears (RW 0x8D1B2D, 0x8D1E93)")
{
	TeleportWorld w;
	Object *caster = w.unit("TestCaster", 'A', 300, 300);
	Object *a = w.unit("TestFollower", 'A', 330, 300);
	Object *b = w.unit("TestFollower", 'A', 340, 300);
	REQUIRE(caster);
	REQUIRE(a);
	REQUIRE(b);
	auto *store = dynamic_cast<StoreObjectsSpecialPower *>(caster->findModule("StoreObjectsSpecialPower"));
	auto *go = dynamic_cast<TeleportToCasterSpecialPower *>(caster->findModule("TeleportToCasterSpecialPower"));
	REQUIRE(store);
	REQUIRE(go);
	const Coord3D here{ 300, 300, 0 };
	store->initiateIntentToDoSpecialPower(store->abilityData()->m_specialPowerTemplate, nullptr, &here, 0, 0);
	w.frames(20);
	REQUIRE(store->storedObjects().size() == 2);
	const Coord3D there{ 700, 700, 0 };
	go->initiateIntentToDoSpecialPower(go->abilityData()->m_specialPowerTemplate, nullptr, &there, 0, 0);
	w.frames(20);
	CHECK(store->storedObjects().empty());
	CHECK(distance2D(*a->getPosition(), there) <= 60.0f); // tolerance: findPositionAround's ring is MinDestinationRadius .. MaxDestinationRadius
	CHECK(distance2D(*b->getPosition(), there) <= 60.0f); // tolerance: findPositionAround's ring is MinDestinationRadius .. MaxDestinationRadius
	CHECK(distance2D(*caster->getPosition(), here) == 0.0f); // the caster stays
}

TEST_CASE("modgap2 teleport: without a list the allies around the target come to the caster (RW 0x8D1B3F .. 0x8D1D0A)")
{
	TeleportWorld w;
	Object *caster = w.unit("TestCaster", 'A', 300, 300);
	Object *ally = w.unit("TestFollower", 'A', 700, 700);
	Object *enemy = w.unit("TestFollower", 'B', 710, 700);
	Object *far = w.unit("TestFollower", 'A', 900, 900);
	auto *go = dynamic_cast<TeleportToCasterSpecialPower *>(caster->findModule("TeleportToCasterSpecialPower"));
	REQUIRE(go);
	const Coord3D farPos = *far->getPosition();
	const Coord3D enemyPos = *enemy->getPosition();
	const Coord3D there{ 700, 700, 0 };
	go->initiateIntentToDoSpecialPower(go->abilityData()->m_specialPowerTemplate, nullptr, &there, 0, 0);
	w.frames(20);
	CHECK(distance2D(*ally->getPosition(), *caster->getPosition()) <= 60.0f); // tolerance: findPositionAround's ring is MinDestinationRadius .. MaxDestinationRadius
	CHECK(distance2D(*enemy->getPosition(), enemyPos) == 0.0f);
	CHECK(distance2D(*far->getPosition(), farPos) == 0.0f);
}
