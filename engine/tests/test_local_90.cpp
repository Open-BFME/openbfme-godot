// OpenBFME. SymbioticStructuresBody retail 2.01 delegation tests
// (RW vtable 0xC72200: getDamageState 0x008C4FF9, getGrabObjectName 0x008C505F,
// getDodgePercent 0x008C5250, isIndestructible 0x008C52DB, setIndestructible no-op 0x009F3A3C).

#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/StructureModules.h"

using namespace combattest;

namespace
{
const char *kSymbioticDelegateObjects = R"INI(
Armor KeepArmor
  Armor = DEFAULT 100%
End

Object Attacker
  KindOf = INFANTRY SELECTABLE CAN_ATTACK
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 200
  End
End

Object SymbioteKeep
  KindOf = STRUCTURE SELECTABLE
  ArmorSet
    Conditions = None
    Armor = KeepArmor
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 1000
    MaxHealthDamaged = 500
    MaxHealthReallyDamaged = 200
    DodgePercent = 25%
    GrabObject = HostChunk
  End
End

Object SymbioteProxy
  KindOf = STRUCTURE SELECTABLE
  Body = SymbioticStructuresBody ModuleTag_Body
    Symbiote = SymbioteKeep
    MaxHealth = 500
    DodgePercent = 80%
    GrabObject = ProxyOwnChunk
  End
End
)INI";

DamageInfo makeHit(Object *attacker, float amount, int damageType)
{
	DamageInfo d{};
	d.m_input.m_sourceID = attacker ? attacker->getID() : INVALID_ID;
	d.m_input.m_damageType = damageType;
	d.m_input.m_amount = amount;
	return d;
}
} // namespace

TEST_CASE("LOCAL-90 SymbioticStructuresBody getDamageState returns BODY_RUBBLE when unlinked and forwards symbiote state (RW 0x008C4FF9)")
{
	CombatWorld w(kSymbioticDelegateObjects);
	Object *attacker = w.unit("Attacker", 'A', 100, 100);
	Object *keep = w.unit("SymbioteKeep", 'B', 200, 200);
	Object *proxy = w.unit("SymbioteProxy", 'B', 210, 200);
	REQUIRE(attacker != nullptr);
	REQUIRE(keep != nullptr);
	REQUIRE(proxy != nullptr);

	SymbioticStructuresBody *proxyBody = dynamic_cast<SymbioticStructuresBody *>(proxy->getBodyModule());
	REQUIRE(proxyBody != nullptr);

	// RW 0x008C5017: push 3; pop eax -> BODY_RUBBLE when unlinked
	CHECK(proxyBody->getDamageState() == BODY_RUBBLE);

	proxyBody->setSymbiote(keep->getID());
	CHECK(proxyBody->getDamageState() == BODY_PRISTINE);

	DamageInfo d1 = makeHit(attacker, 600.0f, DAMAGE_SLASH);
	keep->attemptDamage(d1);
	CHECK(keep->getBodyModule()->getDamageState() == BODY_DAMAGED);
	CHECK(proxyBody->getDamageState() == BODY_DAMAGED);

	DamageInfo d2 = makeHit(attacker, 500.0f, DAMAGE_UNRESISTABLE);
	keep->attemptDamage(d2);
	CHECK(keep->getBodyModule()->getDamageState() == BODY_RUBBLE);
	CHECK(proxyBody->getDamageState() == BODY_RUBBLE);

	proxyBody->setSymbiote(INVALID_ID);
	CHECK(proxyBody->getDamageState() == BODY_RUBBLE);
}

TEST_CASE("LOCAL-90 SymbioticStructuresBody forwards grab object, dodge, and indestructible queries and ignores own setIndestructible (RW 0x008C505F..0x008C52DB)")
{
	CombatWorld w(kSymbioticDelegateObjects);
	Object *keep = w.unit("SymbioteKeep", 'B', 200, 200);
	Object *proxy = w.unit("SymbioteProxy", 'B', 210, 200);
	REQUIRE(keep != nullptr);
	REQUIRE(proxy != nullptr);

	BodyModuleInterface *keepBody = keep->getBodyModule();
	SymbioticStructuresBody *proxyBody = dynamic_cast<SymbioticStructuresBody *>(proxy->getBodyModule());
	REQUIRE(keepBody != nullptr);
	REQUIRE(proxyBody != nullptr);

	// Unlinked: return retail empty/zero/false defaults, ignoring the proxy's own INI fields.
	CHECK(proxyBody->getGrabObjectName().empty());
	CHECK(proxyBody->getDodgePercent() == 0.0f);
	CHECK_FALSE(proxyBody->isIndestructible());

	// RW vtable 0xC72200 +0x88 is 0x009F3A3C (no-op): setting indestructible on the proxy does nothing.
	proxyBody->setIndestructible(true);
	CHECK_FALSE(proxyBody->isIndestructible());
	// the body's own flag stays clear too (review: isIndestructible delegates, so only the base flag shows that the no-op override ran)
	CHECK_FALSE(proxyBody->ActiveBody::isIndestructible());

	// Linked: queries forward to the symbiote's body.
	proxyBody->setSymbiote(keep->getID());
	CHECK(proxyBody->getGrabObjectName() == "HostChunk");
	CHECK(proxyBody->getDodgePercent() == 0.25f);
	CHECK_FALSE(proxyBody->isIndestructible());

	keepBody->setIndestructible(true);
	CHECK(proxyBody->isIndestructible());
	keepBody->setIndestructible(false);
	CHECK_FALSE(proxyBody->isIndestructible());

	// Once the symbiote is destroyed, queries safely fall back to unlinked defaults.
	w.logic->destroyObject(keep);
	w.frames(1);
	CHECK(proxyBody->getDamageState() == BODY_RUBBLE);
	CHECK(proxyBody->getGrabObjectName().empty());
	CHECK(proxyBody->getDodgePercent() == 0.0f);
	CHECK_FALSE(proxyBody->isIndestructible());
}
