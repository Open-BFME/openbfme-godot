// OpenBFME. GPL-3.0.
//
// TerrainResourceBehavior (RotWK ModuleFactory name "TerrainResourceBehavior", create proc RW 0x64BF55, data create proc RW 0x64BF8D, module class vtable RW
// 0xC5FC94, interface mask 0xB = UPDATE | DIE | CREATE; BFME-new, ZH has nothing like it), lane ECON-1: the module of every resource building (farm, mill,
// mine, ...) and of the "dead spot" every other building carries (MaxIncome 0, IncomeInterval 999999, HighPriority Yes). It claims ground from the
// TerrainResourceManager (GameLogic/System/TerrainResourceManager.h) and, every IncomeInterval, pays its owner MaxIncome scaled by the share of its
// ground it holds.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; B2 Open-BFME-2 TerrainResourceBehavior*.cpp agree on the layout and the data defaults):
//   * data (size 0x24, ctor RW 0x88525D, field table RW 0xC5FD78): Radius (parseReal, +8, default 0), MaxIncome (parseInt, +0xC, 0), IncomeInterval
//     (parseDurationUnsignedInt, +0x10, default 0xFFFFFFFF), HighPriority (+0x14, false), Visible (+0x15, true), UpgradeMustBePresent (ObjectFilter, +0x18, unset),
//     Upgrade (parseUpgradeTemplate, +0x1C, none), UpgradeBonusPercent (parsePercentToReal, +0x20, default 1.0 = RW 0xBD1908).
//   * module (size 0x30, ctor RW 0x8852C9): the UpdateModule base, a Die interface at +0x20 (vtable RW 0xC5FBC8), a Create interface at +0x24 (RW 0xC5FBB8);
//     +0x28 claimed (false), +0x29 needToRunOnBuildComplete (true), +0x2C the claimed share of its ground (float, 0.0); the constructor wakes it
//     (setWakeFrame(object, UPDATE_SLEEP_NONE), RW 0x850C32).
//   * onCreate does nothing. onBuildComplete (RW 0x8853A0): setWakeFrame(NONE); needToRunOnBuildComplete = false; when not claimed yet: claim and set
//     claimed; then TerrainResourceManager::onBuildComplete(object) (the finished building turns its shared ground into owned ground).
//   * onDie (RW 0x885380 / 0x885362): TerrainResourceManager::unclaim(object, data.Radius).
//   * update (RW 0x8854D3, always called with the object's disabled mask handled by the scheduler):
//       if not claimed: claim(object, Radius, Visible, HighPriority), claimed = true, and when needToRunOnBuildComplete (still under construction) return
//         UPDATE_SLEEP_FOREVER (RW returns 0x3FFFFFFF: the building sleeps until onBuildComplete wakes it);
//       owner = the object's controlling player; none, or a player without a PlayerTemplate: return IncomeInterval;
//       bonus = 1.0; when Upgrade is set and the owner has completed it and the owner has an object that satisfies UpgradeMustBePresent (Player::hasObjectMatching
//         with completedOnly = true): bonus = UpgradeBonusPercent;
//       modifier = 1.0 * the product of the object's active PRODUCTION attribute modifiers (type 0xD; RW 0x68C82D: out = 1.0 first, then the product);
//       resourceFactor = 1.0, or when the owner's PlayerTemplate has a ResourceModifierObjectFilter (valid) that allows this object: n = the number of the owner's
//         live objects that the filter allows (RW 0x6ABABD with the callback RW 0x885230: an object under construction (status bit 2) is skipped; the filter asked
//         with no second player); resourceFactor = values[n] * 0.01f when n < values.size(), else values.back() * 0.01f - (n - values.size()) * 0.02f clamped at 0
//         (all SSE: cvtsi2ss, mulss, subss);
//       amount = ceil(MaxIncome * share * modifier * bonus) with the x87 chain fild; fmul share; fmul modifier; fmul bonus (each at 24 bits), the MSVCR71 ceil of the
//         double, stored as float, fistp;   when amount > 0: amount = ceil(amount * resourceFactor) the same way (x87 fild, fmul, ceil, fstp, fistp), at least 1;
//         owner.money.deposit(amount, owner's score keeper, sound) (RW 0x7B18B8) and the floating "+amount" text over the building is a client event (EconomyEvents);
//         whether or not anything was paid, the object's experience tracker (when it has one and it is trainable) gets (float)amount experience points (RW 0x79D833:
//         a hook, stop S-256);
//       returns IncomeInterval (the next payment in that many frames).
//   * the share is written by the TerrainResourceManager (RW 0x5F2D53: module + 0x2C). A module that was never given one pays 0.
// Differences: the first matching module of the class is the one the manager finds (Object::findModule), exactly like RW's findModule by class name key: an
// object with two TerrainResourceBehavior modules gives the second one no share (it pays nothing).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <memory>
#include <string>

struct ObjectFilter;

class TerrainResourceBehaviorModuleData : public ModuleData
{
public:
	float m_radius = 0.0f;                                            // +8
	int m_maxIncome = 0;                                              // +0xC
	std::uint32_t m_incomeInterval = 0xFFFFFFFFu;                      // +0x10 frames (RW default -1)
	bool m_highPriority = false;                                      // +0x14
	bool m_visible = true;                                            // +0x15
	std::shared_ptr<const ObjectFilter> m_upgradeMustBePresent;       // +0x18 (null = the handle -1)
	std::string m_upgrade;                                            // +0x1C ("" = none)
	float m_upgradeBonusPercent = 1.0f;                               // +0x20

	static void buildFieldParse(MultiIniFieldParse &p);
};

class TerrainResourceBehavior : public UpdateModule, public CreateModuleInterface, public DieModuleInterface
{
public:
	TerrainResourceBehavior(Thing *thing, const TerrainResourceBehaviorModuleData *data);

	CreateModuleInterface *getCreate() override { return this; }
	DieModuleInterface *getDie() override { return this; }
	UpdateSleepTime update() override;
	// CreateModuleInterface
	void onCreate() override {}
	void onBuildComplete() override;
	// DieModuleInterface
	void onDie(const DieModuleInterface::Event &event) override;

	// RW 0x5F2D53: the manager reports the share of the ground this module holds
	void setClaimShare(float share) { m_share = share; }
	float claimShare() const { return m_share; }
	bool claimed() const { return m_claimed; }
	bool needsBuildComplete() const { return m_needToRunOnBuildComplete; }
	const TerrainResourceBehaviorModuleData *data() const { return m_data; }

	void crc(StateHasher &hasher) const override;

private:
	const TerrainResourceBehaviorModuleData *m_data;
	bool m_claimed = false;                 // +0x28
	bool m_needToRunOnBuildComplete = true; // +0x29
	float m_share = 0.0f;                   // +0x2C
};
