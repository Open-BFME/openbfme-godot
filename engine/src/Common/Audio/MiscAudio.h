// OpenBFME. GPL-3.0.
//
// MiscAudio: the MiscAudio block of MiscAudio.ini (the sounds and music the engine itself triggers: UI clicks, shell and score screen
// music, rally points, ...).
//
// Port of ZH GameEngine/Include/Common/MiscAudio.h changed to RotWK: the table is RW 0xBF5450 (61 rows, RW 0x5ECE27 parses it
// into TheAudio's MiscAudio object) and every row is the name of an audio event: "NoSound" (stricmp) clears the slot, otherwise the event
// must exist ("Invalid Sound '%s'", RW 0x73B217 -> 0x73AA94). MiscAudio.ini is loaded LAST (RW 0x453A1C) so the names resolve.

#pragma once

#include "Common/Audio/AudioEventInfo.h"

#include <string>
#include <vector>

class MiscAudio
{
public:
	// The row names in the binary's order; the index is the slot (the binary stores them at 4 * index in some order of its own).
	static const std::vector<std::string> &fieldNames();

	// The event name of a slot, "" for NoSound / not given.
	const std::string &get(const std::string &fieldName) const;
	bool has(const std::string &fieldName) const;
	const std::vector<std::string> &slots() const { return m_names; }

	// RW 0x5ECE27. `store` resolves the names.
	void parse(INI *ini, const AudioEventInfoStore &store);

private:
	std::vector<std::string> m_names = std::vector<std::string>(fieldNames().size());
};
