// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotLogicClock.h"

#include "Common/GameCommon.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot
{

LogicClock::LogicClock() : m_clock(LOGICFRAMES_PER_SECOND)
{
}

int LogicClock::advance(double delta)
{
	int due = m_clock.advance(delta);
	if (due < 0)
	{
		UtilityFunctions::push_error("LogicClock.advance: negative delta ", delta);
	}
	return due;
}

void LogicClock::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("advance", "delta"), &LogicClock::advance);
	ClassDB::bind_method(D_METHOD("get_alpha"), &LogicClock::get_alpha);
	ClassDB::bind_method(D_METHOD("get_frame"), &LogicClock::get_frame);
	ClassDB::bind_method(D_METHOD("get_logic_frames_per_second"), &LogicClock::get_logic_frames_per_second);
	ClassDB::bind_method(D_METHOD("reset"), &LogicClock::reset);
}

} // namespace godot
