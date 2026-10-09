// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (UpgradeModule / UpgradeMux are the model).
//
// The two upgrade modules of the economy (lane ECON-1): CommandPointsUpgrade (RW create proc 0x650688, data 0x654638) and CostModifierUpgrade (create 0x650078, data
// 0x6500B0). Lane UPGRADE-1: both are UpgradeModules (GameLogic/Module/UpgradeModule.h): the upgrade system triggers them through their mux; giveUpgrade() /
// takeUpgrade() run the implementation / the removal directly (the economy tests), and CostModifierUpgrade also runs itself when its building completes (StartsActive).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * both data classes start with the upgrade table RW 0xC76AD8 (extra 8): TriggeredBy (RW 0x66F603, a list of upgrade names), ConflictsWith (+0x90), RequiresAllTriggers
//     (+0x12C), RequiresAllConflictingTriggers (+0x12D), CustomAnimAndDuration (RW 0x851412, `Anim:<model condition> Time:<duration>`), Permanent (+0x12E);
//   * CommandPointsUpgrade table RW 0xC6FDA0: CommandPoints (parseInt, +0x138), RequiredObject (ObjectFilter, +0x13C, unset). Its upgrade implementation (module vtable
//     slot 10, RW 0x8BC7F5) adds the record {CommandPoints, the object's id, RequiredObject} to its controlling player's command points (RW 0x6A81E3); the removal
//     (slot 8, RW 0x8BC821), when the upgrade is executed, removes the first record with that value and id (RW 0x6A8033) and clears the executed flag;
//   * CostModifierUpgrade table RW 0xC6EF48: ObjectFilter (+0x138, the data constructor sets it to NONE with empty masks, RW 0x8B9E08), Percentage (RW 0x8BA26F: each
//     line appends one percentage as a fraction, +0x13C), UpgradeDiscount (+0x148, false), ApplyToTheseUpgrades (+0x150, a list of names), StartsActive (+0x149, false), Slaughter
//     (+0x14A, false), LabelForPalantirString (+0x14C). The implementation (slot 10, RW 0x8B9CD9) adds, to the controlling player, a CostModifier {filter, percentages,
//     object id, Slaughter} (RW 0x6AD845) or, with UpgradeDiscount, an UpgradeDiscount entry keyed by the object's template name (RW 0x6B2C67); the removal (slot 8,
//     RW 0x8B9BF2) takes the same entry out (RW 0x6AE74C / 0x6B1992) when the upgrade is executed; onBuildComplete (create interface, RW 0x8B9D0F) runs the implementation
//     and marks the upgrade executed when StartsActive; onCapture (RW 0x8B9C4D) moves an executed upgrade's entry from the old owner to the new one.
//   * the cost the entries give is Player::getProductionCostChangeBasedOnKindOf (RW 0x6AD8A7, Economy::productionCostChange): the entries that apply, in list order, each
//     take the percentage at the index of how many applied before them.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <memory>
#include <string>
#include <vector>

struct ObjectFilter;
class Player;

class CommandPointsUpgradeModuleData : public UpgradeModuleData
{
public:
	int m_commandPoints = 0;                                   // +0x138
	std::shared_ptr<const ObjectFilter> m_requiredObject;      // +0x13C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CostModifierUpgradeModuleData : public UpgradeModuleData
{
public:
	CostModifierUpgradeModuleData(); // the NONE filter
	std::shared_ptr<const ObjectFilter> m_objectFilter;        // +0x138
	std::vector<float> m_percentage;                           // +0x13C
	bool m_upgradeDiscount = false;                            // +0x148
	bool m_startsActive = false;                               // +0x149
	bool m_slaughter = false;                                  // +0x14A
	std::string m_labelForPalantirString;                      // +0x14C
	std::vector<std::string> m_applyToTheseUpgrades;           // +0x150
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CommandPointsUpgrade : public UpgradeModule
{
public:
	CommandPointsUpgrade(Thing *thing, const CommandPointsUpgradeModuleData *data);
	void giveUpgrade();   ///< the implementation unless executed (then executed)
	void takeUpgrade();   ///< the removal (slot 8, RW 0x8BC821: only when executed)
	const CommandPointsUpgradeModuleData *data() const { return m_data; }

protected:
	void upgradeImplementation() override;   // RW 0x8BC7F5
	void processUpgradeRemoval() override;   // RW 0x8BC821

private:
	const CommandPointsUpgradeModuleData *m_data;
};

class CostModifierUpgrade : public UpgradeModule, public CreateModuleInterface
{
public:
	CostModifierUpgrade(Thing *thing, const CostModifierUpgradeModuleData *data);
	CreateModuleInterface *getCreate() override { return this; }
	void giveUpgrade();   ///< the implementation unless executed (then executed)
	void takeUpgrade();   ///< the removal (slot 8, RW 0x8B9BF2: only when executed)
	// the module's vtable slot 8 (RW 0xC6EE3C + 0x20 = 0x8B9B3C: add ecx, 0x10; jmp [mux vtable + 0x20]) is the UpgradeMux's removal: deleting the object takes the upgrade
	// (the entry leaves the owner while the object still has one: Object::friend_onDestroy runs onDelete before the modules go). CommandPointsUpgrade's slot 8 is a no-op (RW 0x63F3BF)
	void onDelete() override { takeUpgrade(); }
	// CreateModuleInterface
	void onCreate() override {}
	void onBuildComplete() override;
	void onCapture(Player *oldOwner, Player *newOwner) override;
	const CostModifierUpgradeModuleData *data() const { return m_data; }

protected:
	void upgradeImplementation() override;   // RW 0x8B9CD9
	void processUpgradeRemoval() override;   // RW 0x8B9BF2

private:
	void addTo(Player &player);
	void removeFrom(Player &player);
	const CostModifierUpgradeModuleData *m_data;
};
