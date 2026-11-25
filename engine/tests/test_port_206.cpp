// OpenBFME unit tests (lane PORT-206, fork issue 488; both editions): WeaponBonusUpgrade and the weapon bonus conditions of `Object + 0x39C`.
//
// The address of every expectation below was read on the retail RotWK 2.01 image (`game.dat`, md5 daed3668...):
//   * PLAYER_UPGRADE (bit 5) is set by WeaponBonusUpgrade's mux slot 10, RW 0x8B980D: `mov eax,[ecx-8]` (the module's own object, the UpgradeModule's
//     back pointer at module + 0) / `or dword [eax+0x39C], 0x20` / `ret`. The class chain: create RW 0x64FF7C -> constructor RW 0x8B97C0 (the
//     UpgradeModule base RW 0x863B8F, mux vtable RW 0xC6EB90), so the slot 10 at RW 0xC6EB90 + 0x28 is this class's implementation.
//   * the removal (mux slot 8) is RW 0x63F3BF, the shared no-op, and no site in .text clears bit 5 (the mask's only `and` forms are the garrison
//     parts' RW 0x87BFE8 (bit 0), the veterancy reset RW 0x6958E4 (bits 8..15 of the byte at +0x39D hold 9/10/11) and PropagandaTowerBehavior's
//     RW 0x884DC5 (bits 8 and 15)): the bit stays for the object's lifetime.
// GPL-3.0.

#include "doctest.h"

#include "LogicTestUtil.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/WeaponBonusUpgrade.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"

#include <string>

using namespace logictest;

namespace
{
// PLAYER_UPGRADE is the 6th name of the 22-name table RW 0xDA1740
constexpr int kPlayerUpgrade = 5;

std::string upgradeObjects()
{
	return "Upgrade Upgrade_Port206Bonus\n"
		   "  Type = OBJECT\n"
		   "End\n"
		   "Object Port206Swordsman\n"
		   "  KindOf = INFANTRY SELECTABLE\n"
		   "  Body = ActiveBody ModuleTag_Body\n"
		   "    MaxHealth = 100\n"
		   "  End\n"
		   "  Behavior = WeaponBonusUpgrade ModuleTag_Bonus\n"
		   "    TriggeredBy = Upgrade_Port206Bonus\n"
		   "  End\n"
		   "End\n"
		   "Object Port206Plain\n"
		   "  KindOf = INFANTRY SELECTABLE\n"
		   "  Body = ActiveBody ModuleTag_Body\n"
		   "    MaxHealth = 100\n"
		   "  End\n"
		   "End\n";
}

WeaponBonusUpgrade *bonusModuleOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (WeaponBonusUpgrade *u = dynamic_cast<WeaponBonusUpgrade *>(m.get()))
		{
			return u;
		}
	}
	return nullptr;
}

struct BonusWorld
{
	LogicWorld f;
	BonusWorld()
	{
		REQUIRE_MESSAGE(f.loadError.empty(), f.loadError);
		LogicModules::registerAll(f.w.modules);
		f.logic->settings().unitDamagedThreshold = 0.5f;
		f.logic->settings().unitReallyDamagedThreshold = 0.25f;
		f.logic->settings().bodyThresholdsLoaded = true;
		const std::string err = f.w.load(upgradeObjects(), INI_LOAD_OVERWRITE, "port206.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	Object *make(const char *name) { return f.make(name, f.teamOf("Alice")); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			f.logic->runLogicFrame();
		}
	}
};
} // namespace

// RW 0x8B980D: the object whose upgrade completes carries bit 5 after it, and every other bit of the mask is untouched.
TEST_CASE("PORT-206 a WeaponBonusUpgrade puts PLAYER_UPGRADE into the object's mask (RW 0x8B980D) and clears no other bit")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		BonusWorld w;
		w.f.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		Object *o = w.make("Port206Swordsman");
		REQUIRE(o);
		WeaponBonusUpgrade *module = bonusModuleOf(o);
		REQUIRE_MESSAGE(module, "WeaponBonusUpgrade must be a live module, not the UnportedModule stub");
		w.frames(2);

		// nothing before the upgrade: RW 0x8B980D runs only when updateUpgradeModules (RW 0x6936FE) accepts the upgrade
		CHECK(o->weaponBonusConditionMask() == 0u);
		CHECK_FALSE(module->isAlreadyUpgraded());

		// a bit the object carries from another writer survives the upgrade (RW 0x8B980D is an `or`, not a store)
		o->setWeaponBonusCondition(0, true); // GARRISONED, as GarrisonContain's RW 0x87BF01 sets it
		CHECK(o->weaponBonusConditionMask() == 1u);

		o->giveUpgrade("Upgrade_Port206Bonus");
		CHECK(module->isAlreadyUpgraded());
		CHECK(o->weaponBonusConditionMask() == ((1u << kPlayerUpgrade) | 1u)); // 0x21: RW 0x8B980D ORs 0x20 into RW 0x6CA7C4's mask

		// snapshot xfer round-trip while upgraded
		XferSave save;
		module->xfer(save);

		// the removal is retail's no-op RW 0x63F3BF: taking the upgrade away leaves the bit on the object
		o->removeUpgrade(Player::resolveUpgrade("Upgrade_Port206Bonus", true));
		CHECK(o->weaponBonusConditionMask() == ((1u << kPlayerUpgrade) | 1u));
		CHECK_FALSE(module->isAlreadyUpgraded());

		XferLoad load(save.data());
		module->xfer(load);
		CHECK(module->isAlreadyUpgraded());
	}
}

TEST_CASE("PORT-206 the neighbouring behaviour holds: an object without the module never gains the bit, and TriggeredBy still gates the upgrade")
{
	BonusWorld w;
	Object *plain = w.make("Port206Plain");
	REQUIRE(plain);
	CHECK(bonusModuleOf(plain) == nullptr);
	w.frames(2);
	CHECK(plain->weaponBonusConditionMask() == 0u);

	Object *o = w.make("Port206Swordsman");
	REQUIRE(o);
	o->giveUpgrade("Upgrade_Port206Bonus");
	CHECK(o->weaponBonusConditionMask() == (1u << kPlayerUpgrade));

	// the mask is simulation state: the object's hash covers it (Object::crc adds m_weaponBonusMask), so the upgrade moves the hash
	Object *second = w.make("Port206Swordsman");
	REQUIRE(second);
	w.frames(1);
	StateHasher h1;
	second->crc(h1);
	second->giveUpgrade("Upgrade_Port206Bonus");
	StateHasher h2;
	second->crc(h2);
	CHECK(h1.value() != h2.value());
}
