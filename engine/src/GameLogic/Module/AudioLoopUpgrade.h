// OpenBFME. GPL-3.0.
// Ported from RotWK 2.01 (game.dat) with the BFME2 decomp as the shape (decomp-progress bfme2 Code/GameEngine/Source/GameLogic/Object/Upgrade/
// AudioLoopUpgrade{CtorModuleFactory,ModuleDataCtor,UpgradeImplementation,Update,Rva004B7E3C,Xfer,_getDisabledTypesToProcess}.cpp; BFME1 decomp
// AudioLoopUpgradeUpgradeImplementation.cpp names slot 10). ZH has no such class.
//
// AudioLoopUpgrade (lane MODCOMPAT-MOD1): an update / upgrade / die module that plays a looping sound on its object while its upgrade is executed (the
// castles' and walls' "build loop" while an upgrade's build-up animation plays: SoundToPlay = BuildingConstructionLoop, KillAfterMS = 6166).
//
// SIDE: the upgrade mux (executed flag) and the update's wake frames are SIMULATION (hashed, saved like every mux); the sound is the audio side's: the
// module posts it fire-and-forget (AudioApi::postHeldSound / stopHeldSound, slot HELD_UPGRADE_LOOP) and no logic reads it back (the logic hash does not
// depend on the sound).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden: create RW 0x6504FA, data RW 0x650532, tables RW 0xC76AD8 (extra 20), RW 0xC76BD8 (extra
// 324), RW 0xC6F9F8 (extra 0), mask 131 = update + upgrade + die):
//   * data: create RW 0x650532 (0x174 bytes), constructor RW 0x8BBD1E (vtable RW 0xC6F958): SoundToPlay +0x08 (empty), KillAfterMS +0x0C = 0,
//     KillOnDeath +0x10 = true, the upgrade data at +0x14 (RW 0x653173), the DieMux data at +0x144 (RW 0x8D2970). Table RW 0xC6F9F8: SoundToPlay
//     (+0x08, RW 0x73B217: an audio event, "NoSound" none, an unknown one an INI error), KillAfterMS (+0x0C, parseDurationUnsignedInt RW 0x73A429:
//     ms -> frames), KillOnDeath (+0x10, parseBool RW 0x42E558).
//   * module: create RW 0x6504FA (0x30 bytes), constructor RW 0x8BBDDA over UpdateModule RW 0x653114; the mux at + 0x20 (RW 0x8D26E6, vtable
//     RW 0xC6F8D0), the die interface at + 0x28 (vtable RW 0xC6F8C8), the sound handle at + 0x2C = 1 (none); primary vtable RW 0xC6F924 (xfer slot 3
//     RW 0x8BBF93), update interface + 0x10 RW 0xC6F918. The update sleeps forever from creation (RW 0x850C32(obj, 0x3FFFFFFF)).
//   * mux (vtable RW 0xC6F8D0): slots 4 / 7 RW 0x9188EB false (isPermanent: the data's Permanent is not consulted), slot 6 RW 0x63F3BF no-op, slot 8 the
//     removal RW 0x8BBEFE (TheAudio (RW 0xDE42FC) slot 0x6C removes the handle, handle = 1; then the update sleeps forever), slot 10 the implementation
//     RW 0x8BBE71 (TheAudio present: a held handle (!= 1) is removed, an AudioEventRTS of SoundToPlay for the object's id (RW 0x6DB5B3, Object + 0x74)
//     is added (slot 0x64) and its handle kept; then KillAfterMS > 0 (unsigned) wakes the update after KillAfterMS frames), slots 11 .. 15 the upgrade
//     data's (+0x14).
//   * update RW 0x8BBCB6: TheAudio removes the handle, handle = 1; sleeps forever. getDisabledTypesToProcess RW 0x8BBFF7: ~(DISABLED_EMP | DISABLED_UNMANNED)
//     (RW 0x850F76(mask, 2, 5), then `not`).
//   * onDie RW 0x8BBF5A (die vtable slot 0): KillOnDeath clear: nothing. Else TheAudio removes the handle, handle = 1, and the update sleeps forever. The
//     DieMux data (+0x144) is parsed but this onDie does not test it (no isDieApplicable call).
//   * destructor RW 0x8BBC4F (primary vtable slot 0 RW 0x8BBE55): TheAudio (when present) removes the handle (slot 0x6C).
//   * xfer RW 0x8BBF93: version (1, 1), TheAudio's xferAudioHandle(+0x2C) (slot 0x160), UpdateModule (RW 0x850C60), the mux (RW 0x8D27E8).
//   * BFME2 1.06 cross-check (decomp above): the same fields 0x0C lower for the upgrade data (+0x14 -> DieMux at +0x124), the same steps.
// WHAT IS INFERENCE (OpenBFME):
//   * the handle lives on the audio side (AudioApi held sounds), keyed by a per-module key; the module keeps only whether retail's handle would be held
//     (`m_soundHeld`, retail's + 0x2C != 1): saved, not hashed. A load re-posts a held loop (RW's xferAudioHandle on load was not read);
//   * TheAudio's presence: the requests always go out; without an audio manager they are counted and dropped (AudioEntryPoints, Route::Unbound).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <cstdint>
#include <string>

class ModuleFactory;

class AudioLoopUpgradeModuleData : public UpgradeModuleData
{
public:
	StoreReference m_soundToPlay; ///< +0x08 (OpenContainModuleData::parseAudioEvent's store: the name, empty for NoSound)
	unsigned m_killAfter = 0;   ///< +0x0C (KillAfterMS, frames)
	bool m_killOnDeath = true;  ///< +0x10
	DieMuxData m_dieMux;        ///< +0x144 (parsed, not consulted by RW 0x8BBF5A)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AudioLoopUpgrade : public UpdateModule, public UpgradeMux, public DieModuleInterface
{
public:
	OBFME_MODULE_XFER(AudioLoopUpgrade)
	AudioLoopUpgrade(Thing *thing, const AudioLoopUpgradeModuleData *data); // RW 0x8BBDDA
	~AudioLoopUpgrade() override;

	UpgradeMux *getUpgrade() override { return this; }
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;  // RW 0x8BBF5A
	UpdateSleepTime update() override;                             // RW 0x8BBCB6
	DisabledMaskType getDisabledTypesToProcess() const override     // RW 0x8BBFF7
	{
		return (DisabledMaskType) ~((1u << DISABLED_EMP) | (1u << DISABLED_UNMANNED));
	}
	bool isPermanent() const override { return false; } // mux slot 7: RW 0x9188EB
	void crc(StateHasher &hasher) const override;

	// tests: whether retail's handle (+ 0x2C) is held, the audio side's key of this module's loop, the requests posted (OpenBFME, not hashed)
	bool soundHeld() const { return m_soundHeld; }
	std::uint32_t soundKey() const { return m_soundKey; }
	unsigned long long soundStarts() const { return m_soundStarts; }
	unsigned long long soundStops() const { return m_soundStops; }

	static void registerModule(ModuleFactory &modules);

protected:
	void upgradeImplementation() override;  // mux slot 10: RW 0x8BBE71
	void processUpgradeRemoval() override;  // mux slot 8: RW 0x8BBEFE

private:
	void startSound();
	void stopSound(); // TheAudio slot 0x6C with the handle, handle = 1
	const AudioLoopUpgradeModuleData *m_data;
	std::uint32_t m_soundKey;     ///< the audio side's holder key (not logic state)
	bool m_soundHeld = false;     ///< retail's + 0x2C != 1 (saved, not hashed)
	unsigned long long m_soundStarts = 0;
	unsigned long long m_soundStops = 0;
};
