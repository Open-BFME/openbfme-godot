// OpenBFME. GPL-3.0.
//
// The hand-off movement of one playing LargeGroupAudio cell sound (Port 100, issue fork issue 260). Kept apart from LargeGroupAudio.h:
// LiveGameAudio.h carries no audio types (see its header note), but its per-cell groups hold this counter.
//
// RW GridCell update 0x9D8018: while the cell's hand-off counter (byte at cell + 0x46, set by RW 0x9D8668) is above zero, the
// sound's position moves by `current + (preferred - current) * (1.0f / steps)` on every axis and the counter is decremented,
// so the glide ends exactly on the last counted frame; at zero the position moves toward the preferred position clamped to
// MaximumAudioSpeed per logic frame (RW 0x9D80CF..). A cell that receives a displaced sound gets the map's
// HandOffModeDuration as its counter (RW 0x9721C7: `mov eax, [pair + 0x3C]; mov al, [eax + 0xE4]`); a freshly started sound
// gets 0 (RW 0x972238..).

#pragma once

#include "Common/INIDataTypes.h"

struct LargeGroupAudioHandOff
{
	unsigned steps = 0; ///< frames of glide left (RW: the byte at cell + 0x46)
	void begin(unsigned frames) { steps = frames; }
	bool active() const { return steps > 0; }
	// one logic frame of RW 0x9D8018's movement; `maxSpeedPerFrame` is the map's MaximumAudioSpeed (world units per logic frame)
	Coord3D advance(const Coord3D &current, const Coord3D &preferred, float maxSpeedPerFrame);
};
