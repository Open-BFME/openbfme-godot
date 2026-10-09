// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/GameLogic/Module/UpgradeModule.h: UpgradeMux,
// UpgradeModule, UpgradeModuleData).
//
// The upgrade module base of RotWK (lane UPGRADE-1). TARGET FACTS (RotWK game.dat, caveat S-001), read from CostModifierUpgrade (constructor RW 0x8B9B93: the
// UpgradeModule base RW 0x863B8F, the mux at module + 0x10, mux vtable RW 0xC6ED38) and the shared mux code at RW 0x8D2688 .. 0x8D2925:
//   * the data (table RW 0xC76AD8, extra 8): TriggeredBy (+0x0, an upgrade mask, RW 0x66F603), ConflictsWith (+0x90), RequiresAllTriggers (+0x12C),
//     RequiresAllConflictingTriggers (+0x12D), CustomAnimAndDuration (+0x120, RW 0x851412: `AnimState:<model condition> AnimTime:<duration>
//     [TriggerTime:<duration>]`, the durations parseDurationUnsignedInt RW 0x73A429; a missing AnimState / AnimTime is INIException 3), Permanent (+0x12E);
//   * the mux (+4 m_upgradeExecuted, constructor RW 0x8D26E6 false). Its vtable, in slot order:
//       0 isAlreadyUpgraded (RW 0x4986C4)          1 attemptUpgrade(mask) (RW 0x8D26BF: wouldUpgrade, then giveSelfUpgrade RW 0x855388, true)
//       2 wouldUpgrade(mask) (RW 0x8D26F3)          3 resetUpgrade(mask) (RW 0x8D2901: RW 0x8D278A, and when it reset, the custom anim condition is cleared)
//       4 (RW 0x9188EB, false)                      5 forceRefreshUpgrade (RW 0x8D26B3: the implementation again when executed)
//       6 postUpgradeCheck (per module; RW 0x63F3BF no-op for CostModifierUpgrade)   7 isPermanent (data + 0x136)
//       8 the removal (per module)                  9 setUpgradeExecuted (RW 0x5B462D)
//      10 upgradeImplementation (per module)       11 getUpgradeActivationMasks (RW 0x863B4F: data + 8, data + 0x98)
//      12, 15 (RW 0x863B76 -> RW 0x9F3A3C: no-op)   13 requiresAllTriggers (data + 0x134)   14 requiresAllConflictingTriggers (data + 0x135)
//   * wouldUpgrade (RW 0x8D26F3): false when the activation mask is empty or the upgrade executed; false when the mask holds the conflicting bits (all of
//     them with RequiresAllConflictingTriggers, else any); else the mask holds the activation bits (all with RequiresAllTriggers, else any);
//   * giveSelfUpgrade (RW 0x855388): slot 12, slot 15 (no-ops), the implementation, setUpgradeExecuted(true);
//   * resetUpgrade (RW 0x8D278A): when the mask holds any activation bit and the upgrade executed, it is no longer executed (true);
//   * the custom anim (RW 0x8D286F, set by 0x8D28F1 / cleared by 0x8D28F9 from the module implementations that use it): with a condition, set = an AnimTime
//     above 0 starts the SMCHelper timer (RW 0x68B581) else the condition is set on the object (RW 0x68B53C refreshes the drawable); clear = the condition
//     is cleared when set;
//   * removeUpgrade of a module (RW 0x8D2688, Object::removeUpgrade RW 0x691438 calls it after a successful slot 3): when not Permanent, the removal (slot 8)
//     then setUpgradeExecuted(false). (Slot 3 already cleared the executed flag, so a removal that tests isAlreadyUpgraded, like CostModifierUpgrade's RW
//     0x8B9BF2, does nothing on this path: retail behaviour, ported as is.)
// Object::updateUpgradeModules (RW 0x6936FE): the mask is the controlling player's completed upgrades (Player + 0x14C) | the object's (+0x28C) | its castle's
// (the castle member's castle object, RW 0x797BEF); every module with a mux that is not executed is asked attemptUpgrade(mask), then every mux postUpgradeCheck.

#pragma once

#include "Common/INI.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/BehaviorModule.h"

#include <string>
#include <vector>

class Object;
class StateHasher;

// the part every upgrade module's data starts with (RW 0xC76AD8)
class UpgradeModuleData : public ModuleData
{
public:
	UpgradeMaskType m_activationMask;   ///< TriggeredBy (+0x8)
	UpgradeMaskType m_conflictingMask;  ///< ConflictsWith (+0x98)
	std::vector<std::string> m_triggeredBy;   ///< the names as written (reports)
	std::vector<std::string> m_conflictsWith;
	bool m_requiresAllTriggers = false;            ///< +0x134
	bool m_requiresAllConflictingTriggers = false; ///< +0x135
	bool m_permanent = false;                      ///< +0x136
	// CustomAnimAndDuration (+0x128 .. +0x130)
	int m_customAnimCondition = -1;     ///< model condition bit (RW 0x4B3B5B), -1 when not given
	unsigned m_customAnimFrames = 0;    ///< AnimTime
	unsigned m_customTriggerFrames = 0; ///< TriggerTime

	static void buildBaseFieldParse(MultiIniFieldParse &p);
	static void buildFieldParse(MultiIniFieldParse &p) { buildBaseFieldParse(p); }
};

// RW mux vtable (see above)
class UpgradeMux
{
public:
	virtual ~UpgradeMux() = default;
	bool isAlreadyUpgraded() const { return m_upgradeExecuted; }                     // slot 0
	bool attemptUpgrade(const UpgradeMaskType &mask);                               // slot 1
	bool wouldUpgrade(const UpgradeMaskType &mask) const;                           // slot 2
	bool resetUpgrade(const UpgradeMaskType &mask);                                 // slot 3
	void forceRefreshUpgrade();                                                     // slot 5
	virtual void postUpgradeCheck() {}                                              // slot 6
	bool isPermanent() const { return muxData()->m_permanent; }                     // slot 7
	void setUpgradeExecuted(bool executed) { m_upgradeExecuted = executed; }        // slot 9
	// RW 0x8D2688: the removal unless Permanent, then not executed
	void removeUpgrade();
	// RW 0x855388
	void giveSelfUpgrade();
	// RW 0x8D28F1 / 0x8D28F9 (for the implementations that set the custom anim)
	void setCustomAnim(bool on);
	const UpgradeModuleData *muxData() const { return m_muxData; }

protected:
	UpgradeMux(Object *object, const UpgradeModuleData *data) : m_muxObject(object), m_muxData(data) {}
	virtual void upgradeImplementation() = 0;   // slot 10
	virtual void processUpgradeRemoval() {}     // slot 8
	void crcMux(StateHasher &h) const;

private:
	Object *m_muxObject;
	const UpgradeModuleData *m_muxData;
	bool m_upgradeExecuted = false; ///< mux + 4
};

// RW 0x863B8F: a behavior module with a mux
class UpgradeModule : public BehaviorModule, public UpgradeMux
{
public:
	UpgradeModule(Thing *thing, const UpgradeModuleData *data);
	UpgradeMux *getUpgrade() override { return this; }
	void crc(StateHasher &hasher) const override;
};
