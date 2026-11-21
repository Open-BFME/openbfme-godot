// PORT-78: synthetic INI only. RW 0x85415C..0x85434B, see PORT78_RW_ANALYSIS.md.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/SpecialPower.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kIni[] = R"ini(
SpecialPower Port78Grab
 Enum = SPECIAL_GRAB_CHUNK
 ReloadTime = 0
End
Object Port78Actor
 KindOf = MONSTER SELECTABLE
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 20
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
 Behavior = TransportContain ModuleTag_Contain
  Slots = 1
  AllowEnemiesInside = Yes
  PassengerFilter = ALL
  ObjectStatusOfContained = UNSELECTABLE
 End
 Behavior = SpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = Port78Grab
 End
End
Object Port78Projectile
 KindOf = PROJECTILE
 Geometry = CYLINDER
 GeometryMajorRadius = 2
 GeometryHeight = 2
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 100
 End
End
Object Port78Vendor
 KindOf = CHUNK_VENDOR STRUCTURE IMMOBILE
 Geometry = BOX
 GeometryMajorRadius = 10
 GeometryMinorRadius = 10
 GeometryHeight = 20
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
  GrabObject = Port78Projectile
  GrabFX = FX_Port78
  GrabDamage = 125
  GrabOffset = X:7 Y:3
 End
End
Object Port78EmptyVendor
 KindOf = CHUNK_VENDOR STRUCTURE IMMOBILE
 Geometry = BOX
 GeometryMajorRadius = 10
 GeometryMinorRadius = 10
 GeometryHeight = 20
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
End
Object Port78Passenger
 KindOf = INFANTRY SELECTABLE
 TransportSlotCount = 1
 Geometry = CYLINDER
 GeometryMajorRadius = 2
 GeometryHeight = 10
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
End
)ini";
struct World : combattest::CombatWorld
{
 SpecialPowerStore powers;
 SpecialPowerStore *saved = TheSpecialPowerStore;
 World() : CombatWorld("", 100)
 {
  TheSpecialPowerStore = &powers;
  w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
  SpecialAbilityModules::registerAll(w.modules);
  SpecialPowerModules::registerAll(w.modules);
  combat().setAutoAcquireEnabled(false);
  const std::string err = w.load(kIni, INI_LOAD_OVERWRITE, "port78.ini");
  REQUIRE_MESSAGE(err.empty(), err);
 }
 ~World() { TheSpecialPowerStore = saved; }
};
// Exercise the real trigger without needing animation/AI timing.
struct Trigger : SpecialAbilityUpdate
{
 Trigger(Thing *o, const SpecialAbilityUpdateModuleData *d) : SpecialAbilityUpdate(o, d) {}
 using SpecialAbilityUpdate::triggerAbilityEffect;
};
const SpecialAbilityUpdateModuleData *data(Object *obj)
{
 for (const auto &m : obj->modules())
  if (auto *a = dynamic_cast<SpecialAbilityUpdate *>(m.get())) return a->abilityData();
 return nullptr;
}
void fire(Object *actor, Object *target)
{
 const auto *d = data(actor);
 REQUIRE(d);
 Trigger ability(actor, d);
 ability.initiateIntentToDoSpecialPower(d->m_specialPowerTemplate, target, nullptr, 0, 0);
 ability.triggerAbilityEffect();
}
}
TEST_CASE("PORT-78 vendor creates contained object, structural damage and rotated FX")
{
 World f;
 Object *actor = f.unit("Port78Actor", 'A', 100, 200);
 Object *vendor = f.unit("Port78Vendor", 'B', 300, 400);
 actor->setOrientation(1.57079632679f);
 REQUIRE(actor->getContain());
 f.frames(1); // Local 30 RW 0x8C4294: at frame 0 a sourceless hit retains the initial last-damage record.
 fire(actor, vendor);
 REQUIRE(actor->getContain()->getContainCount() == 1);
 Object *held = actor->getContain()->getContainedItemsList()->front();
 REQUIRE(held);
 CHECK(held->getTemplate() == f.logic->things().findTemplate("Port78Projectile"));
 CHECK(held->getProducerID() == actor->getID());
 CHECK(vendor->getBodyModule()->getHealth() == 875.0f); // RW 0x85422B structural hit
 int damageType = -1;
 CHECK(vendor->getBodyModule()->lastDamageRecordType(damageType));
 CHECK(damageType == DAMAGE_STRUCTURAL); // RW 0x854215
 const auto &events = f.logic->fxEvents().frameEvents();
 REQUIRE(events.size() == 1);
 CHECK(events[0].kind == FXEvent::POSITION_FX);
 CHECK(events[0].hasSecondaryPosition); // RW 0x85430B
 CHECK(events[0].secondaryPosition.x == events[0].position.x);
 CHECK(events[0].secondaryPosition.y == events[0].position.y);
 CHECK(events[0].secondaryPosition.z == events[0].position.z);
 CHECK(events[0].weaponSpeed == 100.0f); // RW 0x8542DC, 0xBD88D8
 CHECK(events[0].drawableMatrix); // RW 0x85432D -> 0x6765B9
 REQUIRE(events[0].fxList);
 CHECK(*events[0].fxList == "FX_Port78");
 // RW 0x85424C..0x854313: actor position + rotate((7,3), pi/2) = (97,207), not vendor position.
 CHECK(events[0].position.x == doctest::Approx(97).epsilon(0.0001)); // tolerance: pi/2 is represented as float32, and CRT trigonometry is approximate
 CHECK(events[0].position.y == doctest::Approx(207).epsilon(0.0001)); // tolerance: pi/2 is represented as float32, and CRT trigonometry is approximate
 CHECK(events[0].position.z == actor->getPosition()->z);
}
TEST_CASE("PORT-78 empty vendor still takes default damage; ordinary passenger keeps contain path")
{
 World f;
 Object *actor = f.unit("Port78Actor", 'A', 100, 200);
 Object *vendor = f.unit("Port78EmptyVendor", 'B', 300, 400);
 fire(actor, vendor);
 CHECK(vendor->getBodyModule()->getHealth() == 800.0f); // ctor RW 0x8C37D4 default 200
 CHECK(actor->getContain()->getContainCount() == 0);
 CHECK(f.logic->fxEvents().frameEvents().empty());
 Object *passenger = f.unit("Port78Passenger", 'B', 120, 200);
 fire(actor, passenger);
 CHECK(passenger->getBodyModule()->getHealth() == 1000.0f);
 CHECK(passenger->getContainedBy() == actor); // RW 0x854353..0x854397
}
