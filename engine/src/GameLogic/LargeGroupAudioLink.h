// OpenBFME. GPL-3.0.
//
// LargeGroupAudioLink (lane AUDIO-4): the face TheLargeGroupAudio (RW 0xDE3BE8) shows the logic. In RotWK the LargeGroupAudioUpdate modules (logic update
// modules) call TheLargeGroupAudio synchronously from inside the logic frame: add (RW 0x60D5FF), update (RW 0x60D5CB) and remove (RW 0x60D633) of the
// module's member interface (module + 0x24, vtable RW 0xC6B3D0). TheLargeGroupAudio itself (its maps, cells and sounds) is audio: the port keeps the call
// points and the moment and hands each call, with the member state the maps read during it, to a LargeGroupAudioSink the client installs (LiveGameAudio),
// as FXEvents does for the effects.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * TheLargeGroupAudio + 0x38 is a "disabled" byte (1 from the constructor RW 0x60DA95, again after the INI load RW 0x60D753), + 0x3C a logic frame
//     (-1 from the constructor RW 0x60DA8B). RW 0x60D4D3 (called by the new game RW 0x62F91A at 0x62FD90, between load progress 0x13 and 0x14, before
//     the map's objects are made) resets every map (RW 0x7EECFA), clears + 0x38 and + 0x3A and sets + 0x3C to TheGameLogic's frame (+ 0x40; 0 without a
//     game logic).
//   * add / update / remove (RW 0x60D5FF / 0x60D5CB / 0x60D633) do nothing while + 0x38 is set or while TheGameLogic's frame equals + 0x3C; otherwise
//     every map (+ 0x10 .. + 0x14) gets the call (RW 0x7EF032 / 0x7EEF0D / 0x7EF0A1).
//   * the modules compare their last notified frame (module + 0x90) with + 0x3C (RW 0x8AF2DB): a member last notified at or before it notifies again.
// The gate is logic state the modules read (hashed); it changes only at the new game. The events are not hashed and never read back.
#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <string>
#include <vector>

// The state of one member as the maps read it during a call (the member interface RW 0xC6B3D0): slot 0 the position the module stored at its last
// notification (module + 0x28 / + 0x2C), slot 1 the object's position (+ 0x38 / + 0x3C), slot 2 / 3 the stored / current model condition flags
// (module + 0x30 / object + 0x10C), slot 4 / 5 the stored / current status bits (module + 0x7C / object + 0x94), slot 6 UnitWeight, slot 7 the keys,
// slot 8 / 9 stealthed now (RW 0x694C0D with no viewer) / stored (module + 0x8C), slot 10 the stored frame (module + 0x90).
class StateHasher;

struct LargeGroupAudioEvent
{
	enum Kind : std::uint8_t
	{
		ADD,    ///< RW 0x60D5FF (the module joined: RW 0x8AF005)
		UPDATE, ///< RW 0x60D5CB (the module's update saw a change, or its last notification is not after the gate: RW 0x8AF2EC)
		REMOVE  ///< RW 0x60D633 (the module left: RW 0x8AF09D)
	};
	Kind kind = ADD;
	UnsignedInt frame = 0; ///< the logic frame of the call
	ObjectID object = INVALID_ID;
	const std::vector<std::string> *keys = nullptr; ///< the module data's Key list (template data, outlives the game)
	std::uint16_t weight = 1;
	float storedX = 0.0f, storedY = 0.0f, x = 0.0f, y = 0.0f;
	ModelConditionMask storedConditions{}, conditions{};
	ObjectStatusMaskType storedStatus{}, status{};
	bool storedStealthed = false, stealthed = false;
	UnsignedInt storedFrame = 0;
};

class LargeGroupAudioSink
{
public:
	virtual ~LargeGroupAudioSink() = default;
	// called at the moment of the retail call, inside the logic frame; must not change logic state or draw logic random numbers
	virtual void onLargeGroupAudioEvent(const LargeGroupAudioEvent &event) = 0;
};

class LargeGroupAudioLink
{
public:
	// RW 0x60D4D3 at the new game: enabled, the gate frame is `frame`
	void enable(UnsignedInt frame)
	{
		m_disabled = false;
		m_gateFrame = (std::int32_t)frame;
	}
	bool disabled() const { return m_disabled; }
	std::int32_t gateFrame() const { return m_gateFrame; } ///< + 0x3C, compared signed by the modules (RW 0x8AF2EA `jg`); -1 before the first enable
	// RW 0x60D5FF / 0x60D5CB / 0x60D633: delivered to the sink unless disabled or in the gate frame (then only counted)
	void notify(const LargeGroupAudioEvent &event);

	// the gate is logic state (the modules' notification test reads it): GameLogic::hashState hashes it (review r1)
	void crc(StateHasher &h) const;

	void setSink(LargeGroupAudioSink *sink) { m_sink = sink; }
	LargeGroupAudioSink *sink() const { return m_sink; }
	unsigned long long delivered() const { return m_delivered; }
	unsigned long long dropped() const { return m_dropped; }

private:
	bool m_disabled = true;                 ///< + 0x38 (RW 0x60DA95)
	std::int32_t m_gateFrame = -1;          ///< + 0x3C (RW 0x60DA8B: -1)
	LargeGroupAudioSink *m_sink = nullptr;
	unsigned long long m_delivered = 0, m_dropped = 0;
};
