// OpenBFME. GPL-3.0.
// Derived from the BFME2 decomp (Open-BFME-2, Code/GameEngine/Source/GameLogic/Object/Upgrade/WeaponBonusUpgradeUpgradeImplementation.cpp); every
// address below is RotWK 2.01's, re-read on the retail image.
//
// WeaponBonusUpgrade (lane PORT-206, fork issue 488; editions: both RotWK 2.01 and BFME2 1.06):
// The upgrade that puts the PLAYER_UPGRADE weapon bonus condition (bit 5 of the mask at `Object + 0x39C`) on its object, so that a
// `WeaponBonus = PLAYER_UPGRADE <field> <percent>` row of the GameData set (GlobalData + 0xAD0, read by Weapon::computeBonus RW 0x6CA7CF /
// 0x6CA4B5) reaches the weapon of an upgraded object.
//
// TARGET FACTS (RotWK game.dat):
//   * the class chain: create RW 0x64FF7C -> constructor RW 0x8B97C0 (the UpgradeModule base RW 0x863B8F; the module vtable RW 0xC6EBD8, the mux at
//     module + 0x10 whose vtable is RW 0xC6EB90); data create RW 0x654B02, the field table RW 0xC76AD8 at extra 8 (the base upgrade rows only:
//     TriggeredBy, ConflictsWith, RequiresAllTriggers, RequiresAllConflictingTriggers — WeaponBonusUpgrade adds no row of its own, which the
//     port's registry golden records as an empty extra table too);
//   * the mux slots (RW 0xC6EB90): 8 the removal is RW 0x63F3BF (the shared no-op: retail never clears bit 5 again), 10 the implementation is
//     RW 0x8B980D: `mov eax,[ecx-8]` (the module's own object, the UpgradeModule's back pointer) / `or dword [eax+0x39C], 0x20` / `ret` — bit 5
//     (PLAYER_UPGRADE, the 6th name of the 22-name table RW 0xDA1740) is set straight into the object's weapon bonus mask;
//   * the object's mask is the one Weapon::computeBonus reads (RW 0x6CA7C4 `mov esi,[eax+0x39C]`), ORs with the caller's extra flags (RW 0x6CA7D5)
//     and hands to the set's entry adder (RW 0x6CA4B5 / 0x6CA434), so setting the bit is what makes the set's PLAYER_UPGRADE row apply;
//   * the UpgradeModule base already does the rest: Object::updateUpgradeModules (RW 0x6936FE) asks wouldUpgrade (RW 0x8D26F3) with the masks and
//     runs the implementation through giveSelfUpgrade (RW 0x855388); the port's UpgradeModule (lane UPGRADE-1) does the same, so this class adds
//     only the bit.
// PLAYER_UPGRADE IS NEVER CLEARED: retail's removal slot is the no-op RW 0x63F3BF and no other site in .text clears bit 5 (the only `and` on the
// mask's high byte are the veterancy reset RW 0x6958E4, PropagandaTowerBehavior's RW 0x884DC5 and the garrison parts' RW 0x87BFE8); the port keeps
// the bit for the object's lifetime, as retail does.
#pragma once

#include "GameLogic/Module/UpgradeModule.h"

class ModuleFactory;

class WeaponBonusUpgrade : public UpgradeModule
{
public:
	OBFME_MODULE_XFER(WeaponBonusUpgrade) // lane PORT-206: its own snapshot, UpgradeModule's state (the mux's executed flag); the bit lives in the
	                                      // object's mask, which Object::crc RW-hashes and Object::xfer carries
	WeaponBonusUpgrade(Thing *thing, const UpgradeModuleData *data) : UpgradeModule(thing, data) {}
	static void registerModule(ModuleFactory &modules); // RW 0x64FF7C -> ctor RW 0x8B97C0 (registered twice in retail, one class)
	void crc(StateHasher &h) const override; // UpgradeModule's: the mux's executed flag; the bit lives in the object's mask

protected:
	void upgradeImplementation() override; // RW 0x8B980D: `or dword [object+0x39C], 0x20`
	// the removal (mux slot 8) is RW 0x63F3BF, the shared no-op: no processUpgradeRemoval override, the base's does nothing

private:
	static constexpr int kPlayerUpgradeCondition = 5; ///< PLAYER_UPGRADE: RW 0x8B980D sets bit 5 (0x20)
};
