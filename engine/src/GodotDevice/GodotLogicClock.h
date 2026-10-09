// OpenBFME. GPL-3.0.
//
// Device layer: the 5 Hz logic clock for Godot. The client calls advance(delta) every
// rendered frame, runs that many logic frames, and draws with get_alpha().

#pragma once

#include "Common/LogicFrameClock.h"

#include <godot_cpp/classes/ref_counted.hpp>

namespace godot
{

class LogicClock : public RefCounted
{
	GDCLASS(LogicClock, RefCounted)

public:
	LogicClock();

	int advance(double delta);
	double get_alpha() const { return m_clock.getAlpha(); }
	int64_t get_frame() const { return (int64_t)m_clock.getFrame(); }
	int get_logic_frames_per_second() const { return m_clock.getFramesPerSecond(); }
	void reset() { m_clock.reset(); }

protected:
	static void _bind_methods();

private:
	LogicFrameClock m_clock;
};

} // namespace godot
