// OpenBFME. GPL-3.0.
//
// The money-moving modules that act on deaths and hits (lane ECON-1): RefundDie (RW create proc 0x64C502, data 0x6536A3, die interface mask 2), PillageModule (RW create
// 0x64C492, data 0x6535ED, interface mask 0x2000: "damage dealt"), and the data of SalvageCrateCollide (RW create 0x651052, data 0x655980, collide interface mask 0x10).
// What calls them (the death pipeline, the damage pipeline, the collision system) belongs to other lanes (stop S-257): RefundDie answers Object::friend_onDie,
// PillageModule::onDamageDealt is the function the damage pipeline calls for each hit its object lands, SalvageCrateCollide is data only (stop S-260).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * DieMuxData (the start of every die module's table, RW 0xC76BD8 extra 8; defaults RW 0x8D2970): DeathTypes (RW 0x73A68A, default ALL), ExemptStatus (+4) and
//     RequiredStatus (+0x14) object status masks (default empty), DamageAmountRequired (+0x24, default -1.0), MinKillerAngle (+0x28, default 1.0) and MaxKillerAngle (+0x2C,
//     default -1.0), both in radians (RW 0x42EE15). isDieApplicable (RW 0x8D29A9, args the object and the damage info): false unless bit (deathType - 1) of DeathTypes is set (the
//     shift count is the x86's, modulo 32); false unless the object's status has every RequiredStatus bit and none of ExemptStatus; with DamageAmountRequired >= 0 false when
//     it is larger than the damage amount; when MaxKillerAngle > MinKillerAngle the killer's bearing must fall inside the window (not ported: reported, S-257).
//   * RefundDie (module size 0x1C, data 0x44): table RW 0xC06998: UpgradeRequired (+0x38, none), RefundPercent (+0x3C, parsePercentToReal, 0.0), BuildingRequired (+0x40,
//     ObjectFilter, unset). onDie (RW 0x888519): the die mux test; the object is neither under construction (status 2) nor sold (status 19); its owner exists, has completed
//     UpgradeRequired (when set) and owns an object satisfying BuildingRequired (when valid, completedOnly = false); refund = ceil(object + 0x33C * RefundPercent) (x87 at
//     24 bits, the MSVCR71 ceil, stored as float, fistp); a non-zero refund is deposited to the owner (keeper, sound) and shown as floating text.
//   * PillageModule (module size 0x1C): table RW 0xC068B8: PillageAmount (+8, unsigned), NumDamageEventsPerPillage (+0xC, unsigned), PillageFilter (+0x10, ObjectFilter).
//     The damage-dealt handler (RW 0x88826E, argument the victim): when the victim satisfies PillageFilter (asked with no second player) the counter (module + 0x18) grows; when it
//     reaches NumDamageEventsPerPillage it is cleared and min(PillageAmount, the victim owner's cash) is withdrawn from the victim's owner (keeper) and deposited to this
//     object's owner (keeper); a withdrawal of 0 stops there.

#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DieModule.h"

#include <memory>
#include <string>

struct ObjectFilter;
class Object;

struct DieMuxData
{
	std::uint32_t m_deathTypes = 0xFFFFFFFFu;
	ObjectStatusMaskType m_exemptStatus{};
	ObjectStatusMaskType m_requiredStatus{};
	float m_damageAmountRequired = -1.0f;
	float m_minKillerAngle = 1.0f;
	float m_maxKillerAngle = -1.0f;
	// RW 0x8D29A9 (see above); false + `*angleNotPorted` set when the angle window would have been consulted
	bool isDieApplicable(const Object &obj, const DieModuleInterface::Event &event, bool *angleNotPorted = nullptr) const;
};

class RefundDieModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;                                       // RW 0xC76BD8
	std::string m_upgradeRequired;                             // +0x38
	float m_refundPercent = 0.0f;                              // +0x3C
	std::shared_ptr<const ObjectFilter> m_buildingRequired;    // +0x40
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RefundDie : public BehaviorModule, public DieModuleInterface
{
public:
	RefundDie(Thing *thing, const RefundDieModuleData *data);
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;
	// the refund this die would pay now (0 when a condition fails): RW 0x8885BF .. 0x8885E4, exposed for tests
	int refundAmount(const DieModuleInterface::Event &event) const;
	const RefundDieModuleData *data() const { return m_data; }

private:
	const RefundDieModuleData *m_data;
};

class PillageModuleData : public ModuleData
{
public:
	std::uint32_t m_pillageAmount = 0;                         // +8
	std::uint32_t m_numDamageEventsPerPillage = 0;             // +0xC
	std::shared_ptr<const ObjectFilter> m_pillageFilter;       // +0x10 (null = unset)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class PillageModule : public BehaviorModule
{
public:
	PillageModule(Thing *thing, const PillageModuleData *data);
	// RW 0x88826E: the damage pipeline calls this for every hit this object's weapons land on `victim`
	void onDamageDealt(Object &victim);
	unsigned counter() const { return m_counter; }
	const PillageModuleData *data() const { return m_data; }
	void crc(StateHasher &hasher) const override;

private:
	const PillageModuleData *m_data;
	unsigned m_counter = 0; // RW module + 0x18
};

// the data of the crate collide (no runtime: stop S-260)
class SalvageCrateCollideModuleData : public ModuleData
{
public:
	KindOfMaskType m_requiredKindOf{};   // +8
	KindOfMaskType m_forbiddenKindOf{};  // +0x24
	bool m_forbidOwnerPlayer = false;    // +0x40
	bool m_buildingPickup = false;       // +0x41
	bool m_humanOnly = false;            // +0x42
	std::string m_pickupScience;         // +0x44
	std::string m_executeFX;             // +0x48
	std::string m_executeAnimation;      // +0x4C
	float m_executeAnimationTime = 0.0f; // +0x50
	float m_executeAnimationZRise = 0.0f; // +0x54
	bool m_executeAnimationFades = false; // +0x58
	float m_porterChance = 0.0f;         // +0x5C
	float m_bannerChance = 0.0f;         // +0x60
	float m_levelUpChance = 0.0f;        // +0x64
	float m_levelUpRadius = 0.0f;        // +0x68
	float m_resourceChance = 0.0f;       // +0x6C
	int m_minResource = 0;               // +0x70
	int m_maxResource = 0;               // +0x74
	std::string m_upgrade;               // +0x78
	bool m_allowAIPickup = false;        // +0x7C
	static void buildFieldParse(MultiIniFieldParse &p);
};
