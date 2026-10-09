// OpenBFME. GPL-3.0. See GameLogic/LargeGroupAudioLink.h (lane AUDIO-4).

#include "GameLogic/LargeGroupAudioLink.h"

#include "Common/StateHash.h"

void LargeGroupAudioLink::notify(const LargeGroupAudioEvent &event)
{
	// RW 0x60D5CB / 0x60D5FF / 0x60D633: `cmp byte [this + 0x38], 0; jne ret`, then `TheGameLogic + 0x40 == [this + 0x3C]: ret`
	if (m_disabled || (std::int32_t)event.frame == m_gateFrame)
	{
		++m_dropped;
		return;
	}
	++m_delivered;
	if (m_sink)
	{
		m_sink->onLargeGroupAudioEvent(event);
	}
}

void LargeGroupAudioLink::crc(StateHasher &h) const
{
	h.addBool(m_disabled);  // + 0x38
	h.addI32(m_gateFrame);  // + 0x3C
}
