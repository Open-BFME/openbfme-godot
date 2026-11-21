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
//
// Lane PORT-65 (issue fork issue 177, class SpellRechargeModifierUpgrade; port65-spell-recharge-rw.md): the spell book's recharge, the same shape of module.
//   * create RW 0x650104 (it pushes the class's module proc RW 0x8B9F57 and its data proc RW 0x8BA1AB into the factory, which registers "SpellRechargeModifierUpgrade",
//     RW 0x65013C; the name string RW 0xC0AFC4 is used at RW 0x65A21A, the registry's module area). Constructor RW 0x8B9F57 on the UpgradeModule base RW 0x863B8F:
//     module vtable RW 0xC6F020, the mux at module + 0x10 (RW 0xC6EFD8), the create interface RW 0xC6EFC8, the behavior interface RW 0xC6ED80, the draw / client
//     interface RW 0xC6FD08 (an unconverted null base) and the + 0x20 flag (set to 1, RW 0x8B9F90); the vtable's slot 8 (removal) is RW 0x8B9FE0, slot 9 onCapture
//     RW 0x8BA05E, slot 10 the implementation RW 0x8BA139, slot 0x1C the scalar-deleting destructor RW 0x8B9F9A; the create interface's slot 1 (onBuildComplete)
//     is RW 0x8B9FB6.
//   * data RW 0x8BA1AB (data vtable RW 0xC6F050): the upgrade table RW 0xC76AD8 (extra 8, TriggeredBy / ConflictsWith / Permanent as CostModifierUpgrade's) and
//     the table RW 0xC6F0D0: Percentage (+ 0x138, a std::vector<float> the parse RW 0x8BA26F appends parsePercentToReal to, one per line), StartsActive (+ 0x144,
//     RW 0x42E558, false), LabelForPalantirString (+ 0x148, RW 0x42EE5E, an empty string). The table of LabelForPalantirString alone sits at RW 0xC6F0F0.
//   * the implementation (mux slot 10, RW 0x8BA139): the controlling player (+ 0x71C) counts one more SpellRechargeModifierUpgrade (RW 0x6AAAE0) and the
//     player's recharge discount (+ 0x718) becomes the Percentage at the index count - 1, kept at 0 when that is not positive and at the vector's last index
//     when it is above it (RW 0x8BA010 .. 0x8BA055 -> RW 0x6AAAEE). That discount is what a power with RESPECT_RECHARGE_TIME_DISCOUNT multiplies its reload by
//     (RW 0x896EC7: 1.0 + the discount); CostModifierUpgrade's percentages are used the same way by its own list index.
//   * the removal (mux slot 8, RW 0x8B9FE0): only when the upgrade is executed (mux slot 0); the count drops by one (RW 0x6AAAE7) and the discount is the
//     Percentage at the new index by the same clamp. The executed flag is not cleared here (the mux's removeUpgrade RW 0x8D2688 does that).
//   * onBuildComplete (create interface slot 1, RW 0x8B9FB6): with StartsActive the implementation runs and the upgrade is marked executed (mux slot 9);
//     the + 0x20 flag is set to 1 either way.
//   * onCapture (RW 0x8BA05E): the old owner (when given) counts one fewer and re-applies the discount for its new count, the new owner (when given) counts one
//     more and applies it for its count, then the upgrade is marked executed. It does not test the executed flag.
//   * xfer (RW 0x8B9D39, version (1, 2)): the version, the + 0x20 byte at version 2 and above (RW 0x8B9D57, Xfer slot 0x90 = xferBool), then
//     UpgradeModule's transfer (the mux's executed flag RW 0x8D27E8). The count and the discount are the PLAYER's state (PlayerScience + 0x71C / + 0x718,
//     transferred by the Player, RW 0x6B27C0), not the module's.
//   * the state hash: Player + 0x718 is hashed already (PlayerScience::crc), Player + 0x71C is added there with this lane; the module's own state is the mux's
//     executed flag, hashed by UpgradeModule::crc.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <memory>
#include <string>
#include <vector>

struct ObjectFilter;
class Player;

class SpellRechargeModifierUpgradeModuleData : public UpgradeModuleData
{
public:
	std::vector<float> m_percentage;                           // +0x138 (each Percentage line appends one, RW 0x8BA26F)
	bool m_startsActive = false;                               // +0x144
	std::string m_labelForPalantirString;                      // +0x148 (stored only: the Palantir display of the label is fork issue 164's section)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SpellRechargeModifierUpgrade : public UpgradeModule, public CreateModuleInterface
{
public:
	OBFME_MODULE_XFER(SpellRechargeModifierUpgrade) // RW 0x8B9D39 (1, 2): UpgradeModule's, the + 0x20 byte
	SpellRechargeModifierUpgrade(Thing *thing, const SpellRechargeModifierUpgradeModuleData *data);
	CreateModuleInterface *getCreate() override { return this; }
	void giveUpgrade();   ///< the implementation unless executed (then executed)
	void takeUpgrade();   ///< the removal (slot 8, RW 0x8B9FE0)
	void onCreate() override {}
	void onBuildComplete() override;   // RW 0x8B9FB6 (create interface slot 1)
	void onCapture(Player *oldOwner, Player *newOwner) override; // RW 0x8BA05E (module slot 9)
	const SpellRechargeModifierUpgradeModuleData *data() const { return m_data; }
	bool friend_flag20() const { return m_flag20; } ///< +0x20 (RW 0x8B9F90 sets it)

protected:
	void upgradeImplementation() override;   // RW 0x8BA139
	void processUpgradeRemoval() override;   // RW 0x8B9FE0

private:
	const SpellRechargeModifierUpgradeModuleData *m_data;
	bool m_flag20 = false; ///< +0x20
};

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
	std::string m_labelForPalantirString = ">UNSPECIFIED<";    // +0x14C, RW 0x8B9E0D–0x8B9E14
	std::vector<std::string> m_applyToTheseUpgrades;           // +0x150
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CommandPointsUpgrade : public UpgradeModule
{
public:
	OBFME_MODULE_XFER(CommandPointsUpgrade) // lane SAVE-4b: UpgradeModule's
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
	OBFME_MODULE_XFER(CostModifierUpgrade) // lane SAVE-4b: UpgradeModule's
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
