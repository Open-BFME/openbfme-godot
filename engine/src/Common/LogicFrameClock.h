// OpenBFME. GPL-3.0.
//
// Fixed-step logic clock. Zero Hour runs one logic frame per rendered frame and caps the
// render loop at the logic rate; BFME's 5 Hz logic cannot be rendered at 5 fps, so the
// rebuild decouples the two: the device layer feeds wall time in, the clock says how many
// whole logic frames to run, and the client draws with interpolation alpha between the
// previous and current logic frame.

#pragma once

#include <cstdint>

class LogicFrameClock
{
public:
	explicit LogicFrameClock(int framesPerSecond);

	// Add wall-clock time (seconds, >= 0). Returns how many logic frames are now due; the
	// caller must run exactly that many GameLogic updates. Negative input is a caller bug
	// and is rejected by returning -1 without touching the clock.
	int advance(double seconds);

	// Fraction of the next logic frame already elapsed, in [0, 1). The client draws
	// lerp(previousFrameState, currentFrameState, alpha).
	double getAlpha() const;

	// Number of logic frames completed since construction/reset.
	std::uint32_t getFrame() const { return m_frame; }

	double getSecondsPerFrame() const { return m_secondsPerFrame; }
	int getFramesPerSecond() const { return m_framesPerSecond; }

	void reset();

private:
	int m_framesPerSecond;
	double m_secondsPerFrame;
	double m_accumulator; // seconds not yet consumed by a logic frame
	std::uint32_t m_frame;
};
