// OpenBFME. GPL-3.0.
//
// EntEnragedUpdate (lane MODGAP-1, MODULE_GAPS.md group 9; no retail 2.01 template uses it, Ennorath's ents do): an ent near a hated enemy building or a dead friend, hurt below half,
// or just done burning goes berserk for EnragedTime. Ported from the RotWK binary (caveat S-001); the BFME2 decompile Object/Update/EntEnragedUpdateUpdate.cpp and
// EntEnragedUpdateScan.cpp name the pieces and agree, except that its "kind" 0x4E and 0xFA are model conditions (RW 0x46E918 is the model condition test).
//
// TARGET FACTS (RotWK game.dat):
//   create RW 0x64F8E1, 0x34 bytes, constructor RW 0x8B661B: + 0x20 = GameLogicRandomValue(0, ScanDelayTime) (line 0x4C; read by nothing else in the module but its xfer), flags
//   zero, + 0x28 = 0, + 0x24 = 0x3FFFFFFF. Table RW 0xC6D690: ScanDelayTime + 0x08, EnragedTime + 0x0C, TimeUntilCanRageAgain + 0x10, ScanDistance + 0x14, HatedObjectFilter + 0x1C,
//   FriendlyDeadFilter + 0x18, EnragedTransitionTime + 0x20, EnragedTransitionFX + 0x24, EnragedOnBuffFX + 0x28, EnragedOffBuffFX + 0x2C. Mask 1: update, answers 1 frame.
//   Fields: + 0x24 the frame the rage ends, + 0x28 the frame it may come again, + 0x2C enraged, + 0x2D suppressed, + 0x2E was aflame, + 0x2F forced, + 0x30 force once.
//   * update RW 0x8B6866: an effectively dead object sleeps for ever. Enraged and the rage over (frame >= + 0x24 and not status AFLAME, or suppressed): force once clears forced;
//     not forced: status ENRAGED (0x36) off, weapon set flag 0x16 off (RW 0x691106), the AI's locomotor set 0 (slot 0x238), + 0x24 = FOREVER, + 0x28 = frame + TimeUntilCanRageAgain,
//     flags off, EnragedOffBuffFX on the object. Still enraged: done. Then with frame > + 0x28 or forced: want = FriendlyDeadFilter scan (RW 0x8B66CE) or HatedObjectFilter scan
//     (RW 0x8B6772) or health fraction below 0.5 (body slot 0x14), or model condition AFLAME gone after it was seen (+ 0x2E; seen: it is kept); status AFLAME: no; forced: yes;
//     suppressed: no. Wanted: not already WEAPONSET_ENRAGED (0xFA) and not status AFLAME: INITIAL_ENRAGED (0x13B) for the transition time, disabled type 4 until frame + time,
//     EnragedTransitionFX; then status ENRAGED, weapon set flag 0x16 (RW 0x691059), the AI's locomotor set 8, enraged, + 0x24 = frame + EnragedTime, EnragedOnBuffFX;
//   * RW 0x8B66CE: the closest object within ScanDistance (distance type 1, RW 0x8B674A; the BFME2 note says 2D, RW 0xA39090): FriendlyDeadFilter, effectively dead (RW 0x660E83), ally of the object (RW 0xC11DC0, 4);
//   * RW 0x8B6772: the closest within ScanDistance: visible (RW 0x660D2C(obj, 0)), HatedObjectFilter, alive (RW 0x660E71);
//   * setters: RW 0x8B649F setForced(forced, once) clears suppressed when forced; RW 0x8B64B8 setSuppressed(on) clears forced when on. Callers: quench (RW 0x89003B / 0x890044: both
//     off), FlammableUpdate's RunToWater branch (RW 0x890A04: water found, suppressed; RW 0x890A15: none, forced), a castle crew's release (RW 0x79A2D3: forced, once).
// The snapshot is OpenBFME native (S-4190). S-5605 closed by Port 232 (issue #555): the search is ported; the quench's client parts are S-983's.
// Local 171: RW 0x64F919 (EntEnragedUpdate newData)

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

#include <string>

class ModuleFactory;
class StateHasher;

class EntEnragedUpdateModuleData : public ModuleData
{
public:
	EntEnragedUpdateModuleData();
	unsigned m_scanDelayTime;                 ///< + 0x08 (frames)
	unsigned m_enragedTime = 0;               ///< + 0x0C
	unsigned m_timeUntilCanRageAgain = 0;     ///< + 0x10
	float m_scanDistance = 300.0f;            ///< + 0x14
	ObjectFilter m_friendlyDeadFilter;        ///< + 0x18
	ObjectFilter m_hatedObjectFilter;         ///< + 0x1C
	unsigned m_enragedTransitionTime = 0;     ///< + 0x20
	std::string m_enragedTransitionFX;        ///< + 0x24
	std::string m_enragedOnBuffFX;            ///< + 0x28
	std::string m_enragedOffBuffFX;           ///< + 0x2C
	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW 0x008B6499 (getModuleName), RW 0x008B655D (getModuleNameKey), RW 0x008B6698 (scalar deleting dtor -> RW 0x008B6480)
class EntEnragedUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(EntEnragedUpdate)
	EntEnragedUpdate(Thing *thing, const EntEnragedUpdateModuleData *data); // RW 0x8B661B
	UpdateSleepTime update() override;                                      // RW 0x8B6866
	void crc(StateHasher &hasher) const override;

	void setForced(bool forced, bool once); // RW 0x8B649F
	void setSuppressed(bool on);            // RW 0x8B64B8
	bool enraged() const { return m_enraged; }
	static EntEnragedUpdate *of(const Object &obj);
	static void registerClass(ModuleFactory &modules);

private:
	bool friendDeadNear() const; // RW 0x8B66CE
	bool hatedNear() const;      // RW 0x8B6772

	const EntEnragedUpdateModuleData *m_data;
	UnsignedInt m_scanCounter = 0;                       ///< + 0x20
	UnsignedInt m_rageEndFrame = 0x3FFFFFFFu;            ///< + 0x24
	UnsignedInt m_nextRageFrame = 0;                     ///< + 0x28
	bool m_enraged = false;                              ///< + 0x2C
	bool m_suppressed = false;                           ///< + 0x2D
	bool m_wasAflame = false;                            ///< + 0x2E
	bool m_forced = false;                               ///< + 0x2F
	bool m_forceOnce = false;                            ///< + 0x30
};
