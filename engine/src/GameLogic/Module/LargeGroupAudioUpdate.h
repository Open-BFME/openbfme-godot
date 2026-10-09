// OpenBFME. GPL-3.0.
//
// LargeGroupAudioUpdate (lane AUDIO-4; RotWK ModuleFactory name "LargeGroupAudioUpdate", string RW 0xC0B350, registered at RW 0x659888 with the create
// procs RW 0x64F013 / data RW 0x64F04E, module vtable RW 0xC6B4D4, update interface RW 0xC6B408, listener interface (module + 0x20) RW 0xC6B400, member
// interface (module + 0x24) RW 0xC6B3D0; source file "LargeGroupAudioUpdate.cpp" RW 0xC6B2D0). The logic half of the battle sounds of large groups: the
// module tells TheLargeGroupAudio (GameLogic/LargeGroupAudioLink.h) where its object is and what state it is in; the maps there count the members.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; DONOR Open-BFME-2 LargeGroupAudioUpdate*.cpp (BFME2 0x004AB7C8 .. 0x004ABB4B) has the same structure):
//   * data (size 0x20, ctor RW 0x8AF542, field table RW 0xC6B380): TimeBetweenUpdatesMin (parseDurationUnsignedInt, + 0x14, default ceil(0.005f * 500.0f)
//     = 3 frames computed by the ctor on the x87: RW 0x8AF565 .. 0x8AF587), TimeBetweenUpdatesVariation (parseDurationUnsignedInt, + 0x18, 1),
//     UnitWeight (RW 0x42EC11: an int in 0 .. 0xFFFF, + 0x1C, 1), Key (RW 0x7EEC77: every token of the line is added, RW 0x7EEB0C, to the data's key
//     set + 8: the keys are interned in a std::map keyed by the name with StringBase's compare, RW 0x7EE97A -> 0x42C33B -> 0x4065D4: exact bytes, read
//     like RW 0x406585). The ctor registers the data with RW 0x8AF518 (a registry at RW 0xDE9720 whose key union RW 0x8AF446 raises no error).
//   * module (ctor RW 0x8AEF0C): + 0x28 / + 0x2C the stored position (0.0), + 0x30 the stored model condition flags (0x4C bytes), + 0x7C the stored
//     status bits (0x10 bytes), + 0x8C the stored "stealthed" byte, + 0x8D "registered" (false), + 0x90 the stored frame (-1).
//   * the sleep time (RW 0x8AEEC3, BFME2 0x004AB7C8): GameLogicRandomValue(0, TimeBetweenUpdatesVariation, "LargeGroupAudioUpdate.cpp", 0xA7)
//     (RW 0x6D328E) + TimeBetweenUpdatesMin + 1.
//   * join (RW 0x8AF005; onObjectCreated, slot 5 RW 0x8AF232, calls it when not registered; the listener interface's slot 1 too): not registered and the
//     object not INAUDIBLE (status 52, Object + 0x98 bit 20): registered; TheLargeGroupAudio add (RW 0x60D5FF); setWakeFrame(object, the sleep time)
//     (RW 0x850C32); stored position = the object's x, y; stored flags / status = the object's (+ 0x10C / + 0x94); stored stealthed = RW 0x694C0D(no
//     viewer); stored frame = TheGameLogic's frame.
//   * leave (RW 0x8AF09D; onDelete, slot 8 RW 0x8AF241; the listener interface's slot 0): registered: TheLargeGroupAudio remove (RW 0x60D633),
//     setWakeFrame(object, UPDATE_SLEEP_FOREVER), not registered.
//   * update (RW 0x8AF246): not registered, or no object: return the sleep time. Else stealthed = RW 0x694C0D(no viewer); unless the object's x and y
//     equal the stored ones (ucomiss: an unordered compare notifies), its flags and status equal the stored ones (RW 0x4B37BC / 0x66329B), stealthed
//     equals the stored byte and the stored frame is after TheLargeGroupAudio + 0x3C (signed `jg`): TheLargeGroupAudio update (RW 0x60D5CB) with the
//     stored values still in place, then the stored position, flags, status, stealthed and frame take the object's. Return the sleep time.
//   * the listener interface (RW 0x690728 sets INAUDIBLE and calls slot 0 of every module's interface 36 (module + 0x20), RW 0x690772 clears it and
//     calls slot 1; RW 0x692313, the object leaving the world, is the caller of RW 0x690728): objectBecameInaudible / objectBecameAudible below. The port
//     never sets INAUDIBLE (RW 0x692313 / 0x690772 are not ported, stop S-1463): they have no caller yet.
//   * xfer (RW 0x8AF0E5, version 2): + 0x28 / + 0x2C, + 0x30, + 0x7C, + 0x8C, + 0x90 (version 2), + 0x8D.
// The module draws logic random numbers at every wake: it is simulation, hashed (crc).

#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/UpdateModule.h"

#include <cstdint>
#include <string>
#include <vector>

class ModuleFactory;
struct LargeGroupAudioEvent;

class LargeGroupAudioUpdateModuleData : public ModuleData
{
public:
	LargeGroupAudioUpdateModuleData();
	std::vector<std::string> m_keys;             // + 8 (Key)
	std::uint32_t m_timeBetweenUpdatesMin = 0;   // + 0x14 frames (the constructor's x87 default)
	std::uint32_t m_timeBetweenUpdatesVariation = 1; // + 0x18 frames
	std::uint16_t m_unitWeight = 1;              // + 0x1C

	static void buildFieldParse(MultiIniFieldParse &p);
};

class LargeGroupAudioUpdate : public UpdateModule
{
public:
	LargeGroupAudioUpdate(Thing *thing, const LargeGroupAudioUpdateModuleData *data);

	void onObjectCreated() override; // slot 5, RW 0x8AF232
	void onDelete() override;        // slot 8, RW 0x8AF241
	UpdateSleepTime update() override; // RW 0x8AF246

	// the listener interface (module + 0x20, RW 0xC6B400): slot 0 (RW 0x8AF16C -> 0x8AF09D) and slot 1 (RW 0x8AF174 -> 0x8AF005)
	void objectBecameInaudible() { leave(); }
	void objectBecameAudible() { join(); }

	const LargeGroupAudioUpdateModuleData *data() const { return m_data; }
	bool registered() const { return m_registered; }
	std::int32_t storedFrame() const { return m_storedFrame; }
	void crc(StateHasher &hasher) const override;

	static void registerClass(ModuleFactory &modules);

private:
	UpdateSleepTime sleepTime() const; // RW 0x8AEEC3
	void join();                       // RW 0x8AF005
	void leave();                      // RW 0x8AF09D
	void store(bool stealthed);
	void fillEvent(LargeGroupAudioEvent &e, bool stealthed) const;

	const LargeGroupAudioUpdateModuleData *m_data;
	float m_storedX = 0.0f, m_storedY = 0.0f;    // + 0x28 / + 0x2C
	ModelConditionMask m_storedConditions{};     // + 0x30
	ObjectStatusMaskType m_storedStatus{};       // + 0x7C
	bool m_storedStealthed = false;              // + 0x8C
	bool m_registered = false;                   // + 0x8D
	std::int32_t m_storedFrame = -1;             // + 0x90
};
