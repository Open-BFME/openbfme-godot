// OpenBFME. GPL-3.0.

#include "Common/LogicFrameClock.h"

#include <stdexcept>

namespace
{
// Render deltas such as 1/144 s are not exactly representable, so N of them can sum to a
// hair under the frame boundary. A frame is due once the accumulator is within this much of
// a whole step; 1 microsecond is far below any real frame time.
const double FRAME_EPSILON_SECONDS = 1.0e-6;
}

LogicFrameClock::LogicFrameClock(int framesPerSecond)
	: m_framesPerSecond(framesPerSecond),
	  m_secondsPerFrame(0.0),
	  m_accumulator(0.0),
	  m_frame(0)
{
	if (framesPerSecond <= 0)
	{
		throw std::invalid_argument("LogicFrameClock: framesPerSecond must be positive");
	}
	m_secondsPerFrame = 1.0 / (double)framesPerSecond;
}

int LogicFrameClock::advance(double seconds)
{
	if (!(seconds >= 0.0))
	{
		return -1;
	}
	m_accumulator += seconds;
	int due = 0;
	while (m_accumulator + FRAME_EPSILON_SECONDS >= m_secondsPerFrame)
	{
		m_accumulator -= m_secondsPerFrame;
		++due;
		++m_frame;
	}
	if (m_accumulator < 0.0)
	{
		m_accumulator = 0.0; // absorbed epsilon, never real time
	}
	return due;
}

double LogicFrameClock::getAlpha() const
{
	double alpha = m_accumulator / m_secondsPerFrame;
	if (alpha < 0.0)
	{
		return 0.0;
	}
	if (alpha >= 1.0)
	{
		return 0.0; // unreachable: advance() consumes whole frames
	}
	return alpha;
}

void LogicFrameClock::reset()
{
	m_accumulator = 0.0;
	m_frame = 0;
}
