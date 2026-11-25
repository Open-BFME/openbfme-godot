// OpenBFME. GPL-3.0.
//
// PoisonedBehavior (LOCAL-97, fork issue 762): periodic poison damage, poisoned tint, and cure on healing.
//
// TARGET FACTS (RotWK 2.01 game.dat, caveat S-001; both editions identical):
//   * data (PoisonedBehaviorModuleData):
//     - ctor RW 0x008861D8 / DONOR BFME2 0x00482F6B (m_poisonDamageInterval = 0, m_poisonDuration = 0)
//     - buildFieldParse RW 0x008861E9, table RW 0xC60134:
//       PoisonDamageInterval (+0x08, INI::parseDurationUnsignedInt RW 0x73A429),
//       PoisonDuration (+0x0C, INI::parseDurationUnsignedInt RW 0x73A429).
//   * module (PoisonedBehavior : public UpdateModule, public DamageModuleInterface):
//     - registration RW 0x00658636, getModuleName RW 0x00886165 ("PoisonedBehavior", 0xC0BA4C),
//       getModuleNameKey RW 0x00886193 (NAMEKEY("PoisonedBehavior")).
//     - ctor RW 0x008861FA (m_poisonDamageFrame = 0, m_poisonOverallStopFrame = 0,
//       m_poisonDamageAmount = 0.0f, m_deathType = DEATH_POISONED (5), setWakeFrame(UPDATE_SLEEP_FOREVER)).
//     - dtor RW 0x00886145 / scalar deleting dtor RW 0x00886272.
//     - calcSleepTime RW 0x0088616B -> UpdateModule::frameToSleepTime RW 0x00850BE9.
//     - onDamage RW 0x00886378: if damageType == DAMAGE_POISON (0x1A), calls startPoisonedEffects (RW 0x0088628E).
//     - onHealing RW 0x0088638E: calls stopPoisonedEffects (RW 0x00886300) and setWakeFrame(UPDATE_SLEEP_FOREVER).
//     - startPoisonedEffects RW 0x0088628E: stores out.m_actualDamageDealt (+0x70), sets overall stop = now + duration,
//       sets next damage frame = (existing != 0) ? min(existing, now + interval) : (now + interval), stores m_deathType,
//       sets Drawable TINT_STATUS_POISONED (0x04), and wakes via calcSleepTime().
//     - stopPoisonedEffects RW 0x00886300: zeroes timers and amount, calls Drawable::clearTintStatus(0x04) (RW 0x00670913).
//     - update RW 0x008863B0: if m_poisonOverallStopFrame == 0 returns UPDATE_SLEEP_FOREVER;
//       if m_poisonDamageFrame != 0 && now >= m_poisonDamageFrame deals DAMAGE_UNRESISTABLE (8) with
//       m_damageFXOverride = 27 (0x1B, "POISON"), m_deathType, m_amount = m_poisonDamageAmount, source = INVALID_ID,
//       and advances m_poisonDamageFrame = now + interval;
//       if m_poisonOverallStopFrame != 0 && now >= m_poisonOverallStopFrame && !isEffectivelyDead() (+0x458 bit 0)
//       calls stopPoisonedEffects(); returns calcSleepTime().
//     - xfer RW 0x00886326 (DONOR BFME2 0x004830A9): UpdateModule::xfer, version (1, 1),
//       m_poisonDamageFrame, m_poisonOverallStopFrame, m_poisonDamageAmount, m_deathType.
// Local 171: RW 0x64C05B (PoisonedBehavior newInstance), RW 0x64C093 (PoisonedBehavior newData)

#pragma once

#include "GameLogic/Damage.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/UpdateModule.h"

class ModuleFactory;

class PoisonedBehaviorModuleData : public ModuleData
{
public:
	UnsignedInt m_poisonDamageInterval = 0; ///< +0x08 PoisonDamageInterval (frames, RW 0x008861D8)
	UnsignedInt m_poisonDuration = 0;       ///< +0x0C PoisonDuration (frames, RW 0x008861D8)

	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x008861E9
};

class PoisonedBehavior : public UpdateModule, public DamageModuleInterface
{
public:
	OBFME_MODULE_XFER(PoisonedBehavior) // RW 0x00886326
	PoisonedBehavior(Thing *thing, const PoisonedBehaviorModuleData *data); // RW 0x008861FA
	~PoisonedBehavior() override;                                           // RW 0x00886145 / RW 0x00886272

	DamageModuleInterface *getDamage() override { return this; }

	void onDamage(const DamageInfo &info) override;  // RW 0x00886378
	void onHealing(const DamageInfo &info) override; // RW 0x0088638E
	void onBodyDamageStateChange(const DamageInfo *, BodyDamageType, BodyDamageType) override {}

	UpdateSleepTime update() override; // RW 0x008863B0
	void crc(StateHasher &hasher) const override;

	UnsignedInt poisonDamageFrame() const { return m_poisonDamageFrame; }
	UnsignedInt poisonOverallStopFrame() const { return m_poisonOverallStopFrame; }
	float poisonDamageAmount() const { return m_poisonDamageAmount; }
	int poisonDeathType() const { return m_deathType; }
	const PoisonedBehaviorModuleData *moduleData() const { return m_data; }

	// RW 0x00658636, RW 0x00886165 ("PoisonedBehavior"), RW 0x00886193 (NAMEKEY)
	static void registerClass(ModuleFactory &modules);

private:
	UpdateSleepTime calcSleepTime() const;                     // RW 0x0088616B
	void startPoisonedEffects(const DamageInfo &damageInfo);   // RW 0x0088628E
	void stopPoisonedEffects();                                // RW 0x00886300

	const PoisonedBehaviorModuleData *m_data = nullptr;
	UnsignedInt m_poisonDamageFrame = 0;      ///< +0x24
	UnsignedInt m_poisonOverallStopFrame = 0; ///< +0x28
	float m_poisonDamageAmount = 0.0f;        ///< +0x2C
	int m_deathType = DEATH_POISONED;         ///< +0x30 (RW 0x00886255: 5 = DEATH_POISONED)
};
