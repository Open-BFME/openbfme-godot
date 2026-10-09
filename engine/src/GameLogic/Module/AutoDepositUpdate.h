// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (ZH's AutoDepositUpdate is the model; RotWK changed it).
//
// AutoDepositUpdate (RotWK ModuleFactory name "AutoDepositUpdate", create proc RW 0x64E1D5, data create proc RW 0x653F1F, module vtable RW 0xC66C58, update vtable
// RW 0xC66C4C, interface mask 1 = UPDATE), lane ECON-1: a building that pays its owner a fixed amount at a fixed interval (every castle keep: GENERIC_KEEP_MONEY).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; B2 Open-BFME-2 AutoDepositUpdateModuleData* agree on the field names):
//   * data (size 0x24, ctor RW 0x653EBA, field table RW 0xC07FD8): DepositTiming (parseDurationUnsignedInt, +8, 0), DepositAmount (parseInt, +0xC, 0), InitialCaptureBonus
//     (parseInt, +0x10, 0), Upgrade (+0x14, none), UpgradeBonusPercent (parsePercentToReal, +0x18, 1.0), UpgradeMustBePresent (ObjectFilter, +0x1C, unset), GiveNoXP (+0x20,
//     false), OnlyWhenGarrisoned (+0x21, false).
//   * module (size 0x28, ctor RW 0x89D9BE): +0x20 the frame of the next deposit = now + DepositTiming (set by the constructor and by every capture), +0x24 `awardInitialCaptureBonus`
//     (false), +0x25 `started` (false).
//   * update (RW 0x89DB5D), returns UPDATE_SLEEP_NONE every time (the module is called every frame and compares the frame itself):
//       now < nextDeposit: nothing;  the first time through: awardInitialCaptureBonus = started = true;  nextDeposit = now + DepositTiming;
//       OnlyWhenGarrisoned and the object is not GARRISONED (model condition 10): nothing;  the owner is the neutral player: nothing;  DepositAmount <= 0: nothing;
//       the object is not complete (RW + 0x288 != -1.0, the construction percent): nothing;  RUBBLE (5), POST_RUBBLE (59) or POST_COLLAPSE (60): nothing;
//       bonus = the PRODUCTION attribute modifier product (1.0 times); with Upgrade set, owned by a player that completed it and that has an object satisfying
//         UpgradeMustBePresent (hasObjectMatching with completedOnly = false): bonus = UpgradeBonusPercent * bonus (SSE mulss);
//       amountF = (float)DepositAmount * bonus (SSE); in a LAN / skirmish / internet game amountF = MultiPlayMoneyMult[n - 1] * amountF (x87 at 24 bits); amount = cvttss2si;
//       amount = the owner's handicap (RW 0x6AA858); owner.money.deposit(amount, keeper, sound);  unless GiveNoXP, the experience tracker gets DepositAmount * bonus
//         (SSE) points (a hook, S-256); the floating text is a client event.
//   * awardInitialCaptureBonus (RW 0x89DA1E, called by the owner change, RW 0x6AE068, with the NEW owner when it is not the neutral player): nextDeposit = now + DepositTiming
//     always; when the player exists, the flag is set and InitialCaptureBonus > 0: amount = InitialCaptureBonus (x87: times the multiplayer money multiplier, ftol), the
//     handicap, deposit, and the flag is cleared (so the bonus is paid once, to the first player that captures the building after it started).
// Retail data: every InitialCaptureBonus is 0.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <memory>
#include <string>

struct ObjectFilter;
class Player;

class AutoDepositUpdateModuleData : public ModuleData
{
public:
	std::uint32_t m_depositTiming = 0;                             // +8 frames
	int m_depositAmount = 0;                                       // +0xC
	int m_initialCaptureBonus = 0;                                 // +0x10
	std::string m_upgrade;                                         // +0x14
	float m_upgradeBonusPercent = 1.0f;                            // +0x18
	std::shared_ptr<const ObjectFilter> m_upgradeMustBePresent;    // +0x1C
	bool m_giveNoXP = false;                                       // +0x20
	bool m_onlyWhenGarrisoned = false;                             // +0x21

	static void buildFieldParse(MultiIniFieldParse &p);
};

class AutoDepositUpdate : public UpdateModule
{
public:
	AutoDepositUpdate(Thing *thing, const AutoDepositUpdateModuleData *data);
	UpdateSleepTime update() override;
	// RW 0x89DA1E (see the file comment)
	void awardInitialCaptureBonus(Player *newOwner);

	const AutoDepositUpdateModuleData *data() const { return m_data; }
	UnsignedInt depositOnFrame() const { return m_depositOnFrame; }
	bool initialBonusPending() const { return m_awardInitialCaptureBonus; }
	void crc(StateHasher &hasher) const override;

private:
	const AutoDepositUpdateModuleData *m_data;
	UnsignedInt m_depositOnFrame = 0;          // +0x20
	bool m_awardInitialCaptureBonus = false;   // +0x24
	bool m_started = false;                    // +0x25
};
