// OpenBFME. GPL-3.0.
// See GameClient/AptView3D.h.

#include "GameClient/AptView3D.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace
{
bool equalNoCase(const std::string &a, const char *b)
{
	std::size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return i == a.size() && b[i] == 0;
}
} // namespace

int AptView3DAnimModeIndex(const std::string &name)
{
	// the table at RW 0xDC3BC0
	static const char *const kNames[] = { "MANUAL", "LOOP", "ONCE", "LOOP_PINGPONG", "PLAY_TO_FRAME", "LOOP_BACKWARDS", "ONCE_BACKWARDS" };
	for (int i = 0; i < 7; ++i)
	{
		if (equalNoCase(name, kNames[i]))
		{
			return i;
		}
	}
	return 0;
}

bool AptView3DKeepAspect(const std::string &value)
{
	return value.find('f') == std::string::npos;
}

int AptView3DFrameOf(const std::string &value)
{
	return std::atoi(value.c_str());
}

bool AptView3DAnimation::setAnimation(int numFrames, int frame, int mode, double nowMs)
{
	if (m_started && frame == m_lastRequestFrame && mode == m_lastRequestMode)
	{
		return false; // RW 0xB54322 .. 0xB5432F
	}
	m_lastRequestFrame = frame;
	m_lastRequestMode = mode;
	m_started = true;
	m_numFrames = numFrames;
	const float requested = (float)frame;
	m_prevFrame = std::max(std::min((float)(numFrames - 1), requested), 0.0f);
	if (mode == APT_VIEW3D_PLAY_TO_FRAME)
	{
		m_direction = m_prevFrame >= m_frame ? 1.0f : -1.0f;
	}
	else
	{
		m_frame = requested;
		m_direction = mode < APT_VIEW3D_ONCE_BACKWARDS ? 1.0f : -1.0f;
	}
	m_lastSyncMs = nowMs;
	m_mode = mode;
	return true;
}

float AptView3DAnimation::compute(double nowMs, float frameRate, float *direction) const
{
	float frame = m_frame;
	*direction = m_direction;
	if (!m_started || m_mode == APT_VIEW3D_MANUAL)
	{
		return frame;
	}
	const float last = (float)(m_numFrames - 1);
	frame += frameRate * (float)(nowMs - m_lastSyncMs) * m_direction * 0.001f;
	switch (m_mode)
	{
		case APT_VIEW3D_ONCE:
			if (frame >= last)
			{
				frame = last;
			}
			break;
		case APT_VIEW3D_LOOP:
			if (frame >= last)
			{
				frame -= last;
			}
			if (frame >= last)
			{
				frame = 0.0f;
			}
			break;
		case APT_VIEW3D_ONCE_BACKWARDS:
			if (frame < 0.0f)
			{
				frame = 0.0f;
			}
			break;
		case APT_VIEW3D_LOOP_BACKWARDS:
			if (frame < 0.0f)
			{
				frame += last;
			}
			if (frame < 0.0f)
			{
				frame = last;
			}
			break;
		case APT_VIEW3D_LOOP_PINGPONG:
			if (m_direction >= 1.0f)
			{
				if (frame >= last)
				{
					frame = last * 2.0f - frame;
					if (frame >= last)
					{
						frame = last;
					}
					*direction = -m_direction;
				}
			}
			else if (frame < 0.0f)
			{
				frame = -frame;
				if (frame >= last)
				{
					frame = 0.0f;
				}
				*direction = -m_direction;
			}
			break;
		case APT_VIEW3D_PLAY_TO_FRAME:
			if (m_direction > 0.0f ? frame > m_prevFrame : frame < m_prevFrame)
			{
				frame = m_prevFrame;
			}
			break;
		default:
			break;
	}
	return frame;
}

void AptView3DAnimation::progress(double nowMs, float frameRate)
{
	if (!m_started)
	{
		return;
	}
	float direction = m_direction;
	m_frame = compute(nowMs, frameRate, &direction);
	m_direction = direction;
	m_lastSyncMs = nowMs;
}
