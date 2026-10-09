// OpenBFME. GPL-3.0.
//
// Lane MODULES-2: area-scan behaviour modules on ThePartitionManager. RotWK only; ported from the binary (caveat S-001).
//
// TARGET FACTS (RotWK game.dat):
//   * LargeGroupBonusUpdate (17 templates: the Mordor, Wild and Angmar orc / goblin hordes). Data table RW 0xC639D8: UpdateRate (+8, duration), HordeMemberFilter
//     (+0xC, ObjectFilter), Count (+0x10, int), Radius (+0x14), AlliesOnly (+0x18, read by nothing here), RubOffRadius (+0x1C), FlagSubObjectNames (+0x20,
//     parseAsciiStringVector RW 0x42EED6; the client's sub objects), AttributeModifier (+0x2C). Constructor RW 0x893765: + 0x24 = now, + 0x28 active / + 0x29
//     reached = false, the first wake GameLogicRandomValue(1, UpdateRate) LargeGroupBonusUpdate.cpp line 0x6B. update RW 0x8938D9: an object whose KindOf has
//     neither INFANTRY nor CAVALRY (a horde) runs every frame and scans when UpdateRate frames passed since the last scan; an INFANTRY / CAVALRY object scans at
//     every call and sleeps UpdateRate. The scan (RW 0x893998): ThePartitionManager within Radius, distance type 3, unsorted, filters RW 0xC0F374 (the same
//     Object + 0x458 bit 3), RW 0xC10E20 (not effectively dead), RW 0xC10E14 (the same controlling player, RW 0x660AFE) and RW 0xC63870 (a horde with a member
//     the HordeMemberFilter allows, RW 0x660C72); the count is the sum over the hits' hordes of their members the filter allows (horde interface slot 0x180,
//     RW 0x8706DF). count >= Count - 1 (unsigned): reached and active. Else neither, unless a hit within RubOffRadius (RW 0x66137C: the 2D x87 distance) has a
//     LargeGroupBonusUpdate that is active and reached: then active (the bonus rubs off). A change of active adds the ModifierList (RW 0x68F1A8, duration 0)
//     or removes it (RW 0x68F259). A dead object sleeps forever.
//   * PassiveAreaEffectBehavior (17 templates: Galadriel's well / Gondor wells, elven statues, the create-a-hero buildings). Data RW 0x888080, table RW
//     0xC60B78: EffectRadius (+8, default 200), HealPercentPerSecond (+0xC, percent), PingDelay (+0x10, duration, default 15 frames), ModifierName (+0x14,
//     the append parser RW 0x42E59E), AllowFilter (+0x20), UpgradeRequired (+0x24), NonStackable (+0x28), AntiCategories (+0x2C, RW 0x89F32D), AntiFX (+0x30),
//     HealFX (+0x34). Constructor RW 0x887CF3: wake next frame. update RW 0x887DF7 (sleeps PingDelay, 1 when 0): an UpgradeRequired the object lacks (RW
//     0x66F5E5 / 0x691421) or a building under construction stops it there; a dead object sleeps forever; when PingDelay frames passed since the last scan
//     (+0x20), the scan RW 0x887F17 refills the target list (+0x24): ThePartitionManager within EffectRadius, type 0, sorted near to far, filters the
//     relationship RW 0xC11DC0 (ALLIES of the object), RW 0xC10E20, RW 0xC0F374 and the AllowFilter for the owner; not the object itself, not KindOf
//     IGNORED_IN_GUI / ARMY_OF_DEAD (RW 0x70C548), each id once. Every update then affects each live target (RW 0x887B72): the heal RW 0x887BAB (a target not
//     damaged within 4 seconds (RW 0x68C933), HealPercentPerSecond > 0, health below max, not NonStackable-blocked: MaxHealth * (percent / 5) * PingDelay through
//     attemptHealingFromSoleBenefactor (RW 0x690584) for PingDelay frames, then HealFX) and the modifiers RW 0x887C94 (every ModifierName for its own
//     duration (-1), then the pool's AntiCategories until now + PingDelay (RW 0x804FCC) and AntiFX).
//
// NOT PORTED / INFERENCE (stop S-1023, AreaScanModules::stopLines): LargeGroupBonusUpdate's FlagSubObjectNames (client sub objects) and the horde's second member
// list (slot 0x180 also counts the ids at HordeContain + 0x54 / + 0x150, S-486); PassiveAreaEffectBehavior's NonStackable (the body's last heal frame, body slot
// 0x48, as S-858: the heal stacks; noted) and the AntiCategories disable (RW 0x804FCC, XP-1's S-633: noted); the construction test is the object's
// UNDER_CONSTRUCTION status (RW 0x68C3E6 asks the first module answering behavior slot 0x80 first).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

#include <cstdint>
#include <list>
#include <string>
#include <utility>
#include <vector>

class ModuleFactory;
class StateHasher;

class LargeGroupBonusUpdateModuleData : public ModuleData
{
public:
	unsigned m_updateRate = 0;            ///< + 8 (frames)
	ObjectFilter m_hordeMemberFilter;     ///< + 0xC
	int m_count = 0;                      ///< + 0x10
	float m_radius = 0.0f;                ///< + 0x14
	bool m_alliesOnly = false;            ///< + 0x18
	float m_rubOffRadius = 0.0f;          ///< + 0x1C
	std::vector<std::string> m_flagSubObjectNames; ///< + 0x20
	std::string m_attributeModifier;      ///< + 0x2C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class LargeGroupBonusUpdate : public UpdateModule
{
public:
	LargeGroupBonusUpdate(Thing *thing, const LargeGroupBonusUpdateModuleData *data); ///< RW 0x893765
	UpdateSleepTime update() override;                                                 ///< RW 0x8938D9
	void crc(StateHasher &h) const override;
	bool active() const { return m_active; }
	bool reached() const { return m_reached; }
	unsigned lastCount() const { return m_lastCount; }

private:
	const LargeGroupBonusUpdateModuleData *m_data;
	UnsignedInt m_lastScan = 0; ///< + 0x24
	bool m_active = false;      ///< + 0x28
	bool m_reached = false;     ///< + 0x29
	unsigned m_lastCount = 0;   ///< the last scan's count (a test value, hashed)
	std::vector<std::pair<const Object *, unsigned>> m_scanCounts; ///< scratch of one scan (performance; not state)
};

class PassiveAreaEffectBehaviorModuleData : public ModuleData
{
public:
	float m_effectRadius = 200.0f;        ///< + 8 (RW 0xBE4170)
	float m_healPercentPerSecond = 0.0f;  ///< + 0xC
	unsigned m_pingDelay = 15;            ///< + 0x10
	std::vector<std::string> m_modifierNames; ///< + 0x14
	ObjectFilter m_allowFilter;           ///< + 0x20
	std::string m_upgradeRequired;        ///< + 0x24
	bool m_nonStackable = false;          ///< + 0x28
	std::uint32_t m_antiCategories = 0;   ///< + 0x2C
	std::string m_antiFX;                 ///< + 0x30
	std::string m_healFX;                 ///< + 0x34
	static void buildFieldParse(MultiIniFieldParse &p);
};

class PassiveAreaEffectBehavior : public UpdateModule
{
public:
	PassiveAreaEffectBehavior(Thing *thing, const PassiveAreaEffectBehaviorModuleData *data); ///< RW 0x887CF3
	UpdateSleepTime update() override;                                                        ///< RW 0x887DF7
	void crc(StateHasher &h) const override;
	const std::list<ObjectID> &targets() const { return m_targets; }
	unsigned long long heals() const { return m_heals; }
	unsigned long long modifiersApplied() const { return m_modifiers; }

private:
	void scan();                 ///< RW 0x887F17 (slot 0x34)
	void affect(Object &target); ///< RW 0x887B72 (slot 0x38): the heal RW 0x887BAB, the modifiers RW 0x887C94

	const PassiveAreaEffectBehaviorModuleData *m_data;
	UnsignedInt m_lastScan = 0;        ///< + 0x20
	std::list<ObjectID> m_targets;     ///< + 0x24
	unsigned long long m_heals = 0;     ///< heals given (hashed)
	unsigned long long m_modifiers = 0; ///< modifier lists applied (hashed)
};

namespace AreaScanModules
{
void registerAll(ModuleFactory &modules);
std::vector<std::string> stopLines();
} // namespace AreaScanModules
