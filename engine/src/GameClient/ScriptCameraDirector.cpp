// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/ScriptCameraDirector.h (client presentation: plain float arithmetic on the render clock, never logic state).

#include "GameClient/ScriptCameraDirector.h"

#include <cmath>

#include "GameLogic/ScriptEngine/ScriptEngine.h"

namespace
{
float clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float realParam(const ScriptClientRequest &r, size_t i)
{
	return i < r.params.size() ? r.params[i].realValue : 0.0f;
}

int intParam(const ScriptClientRequest &r, size_t i)
{
	return i < r.params.size() ? r.params[i].intValue : 0;
}

constexpr float kPi = 3.14159265358979f;
constexpr double kFrameMs = 1000.0 / 30.0; ///< the CAMERA_FADE frame counts (ZH TheW3DFrameLengthInMsec, the 30 Hz client frame)
} // namespace

// ZH ParabolicEase.cpp:52 / :75
void ParabolicEase::setEaseTimes(float easeIn, float easeOut)
{
	m_in = clamp01(easeIn);
	m_out = clamp01(1.0f - easeOut);
	if (m_in > m_out)
	{
		m_in = m_out;
	}
}

float ParabolicEase::operator()(float param) const
{
	param = clamp01(param);
	const float v0 = 1.0f + m_out - m_in;
	if (param < m_in)
	{
		return param * param / (v0 * m_in);
	}
	if (param <= m_out)
	{
		return (float)((m_in + 2.0 * (param - m_in)) / v0);
	}
	return (float)((m_in + 2.0 * (m_out - m_in) + (2.0 * (param - m_out) + m_out * m_out - param * param) / (1.0f - m_out)) / v0);
}

void ScriptCameraDirector::startMove(const Coord3D &from, const Coord3D &to, float seconds, float easeIn, float easeOut)
{
	m_move.active = true;
	m_move.from = from;
	m_move.to = to;
	m_move.ms = seconds * 1000.0 < 1.0 ? 1.0 : seconds * 1000.0; // ZH: milliseconds < 1 -> 1 (an instant move)
	m_move.elapsed = 0.0;
	m_move.ease.setEaseTimes((float)(easeIn * 1000.0 / m_move.ms), (float)(easeOut * 1000.0 / m_move.ms));
	m_hasTarget = true;
	m_target = from;
	if (m_move.ms <= 1.0)
	{
		update(0.0);
	}
}

void ScriptCameraDirector::apply(const ScriptClientRequest &r, const Coord3D &cameraTarget, float cameraAngle)
{
	const std::string &a = r.action;
	if (a == "MOVE_CAMERA_TO")
	{
		if (r.hasPosition)
		{
			// (position, seconds, shutter, ease in, ease out)
			startMove(m_hasTarget ? m_target : cameraTarget, r.position, realParam(r, 1), realParam(r, 3), realParam(r, 4));
		}
		return;
	}
	if (a == "RESET_CAMERA")
	{
		if (r.hasPosition)
		{
			// (position, seconds, ease in, ease out): the move, the default angle (0) and the default pitch / zoom
			startMove(m_hasTarget ? m_target : cameraTarget, r.position, realParam(r, 1), realParam(r, 2), realParam(r, 3));
			m_rotate.active = true;
			m_rotate.from = m_hasAngle ? m_angle : cameraAngle;
			m_rotate.to = 0.0f;
			m_rotate.ms = m_move.ms;
			m_rotate.elapsed = 0.0;
			m_rotate.ease = m_move.ease;
			m_hasAngle = true;
			m_angle = m_rotate.from;
			m_resetView = true;
		}
		return;
	}
	if (a == "ROTATE_CAMERA")
	{
		// (rotations, seconds, ease in, ease out)
		const float seconds = realParam(r, 1);
		m_rotate.active = true;
		m_rotate.from = m_hasAngle ? m_angle : cameraAngle;
		m_rotate.to = m_rotate.from + 2.0f * kPi * realParam(r, 0);
		m_rotate.ms = seconds * 1000.0 < 1.0 ? 1.0 : seconds * 1000.0;
		m_rotate.elapsed = 0.0;
		m_rotate.ease.setEaseTimes((float)(realParam(r, 2) * 1000.0 / m_rotate.ms), (float)(realParam(r, 3) * 1000.0 / m_rotate.ms));
		m_hasAngle = true;
		m_angle = m_rotate.from;
		return;
	}
	if (a == "CAMERA_LETTERBOX_BEGIN")
	{
		m_letterbox = true;
		return;
	}
	if (a == "CAMERA_LETTERBOX_END")
	{
		m_letterbox = false;
		return;
	}
	if (a == "DISABLE_INPUT")
	{
		m_inputDisabled = true;
		return;
	}
	if (a == "ENABLE_INPUT")
	{
		m_inputDisabled = false;
		return;
	}
	if (a == "SHOW_MILITARY_CAPTION")
	{
		m_captionLabel = r.params.empty() ? std::string() : r.params[0].stringValue;
		m_captionMs = r.params.size() > 1 && r.params[1].type == 1 ? realParam(r, 1) * 1000.0 : (double)intParam(r, 1);
		return;
	}
	if (a == "CAMERA_FADE_ADD" || a == "CAMERA_FADE_SUBTRACT" || a == "CAMERA_FADE_SATURATE" || a == "CAMERA_FADE_MULTIPLY")
	{
		// (min, max, frames to increase, frames to hold, frames to decrease)
		m_fadeState.active = true;
		m_fadeState.minV = realParam(r, 0);
		m_fadeState.maxV = realParam(r, 1);
		m_fadeState.inMs = intParam(r, 2) * kFrameMs;
		m_fadeState.holdMs = intParam(r, 3) * kFrameMs;
		m_fadeState.outMs = intParam(r, 4) * kFrameMs;
		m_fadeState.elapsed = 0.0;
		return;
	}
	if (a == "CAMERA_FOLLOW_NAMED") // (unit, snap, ?): RW 0x7BB4B0 -> the view's follow (slot 0x184) of the object
	{
		m_followId = r.objectId;
		return;
	}
	if (a == "CAMERA_STOP_FOLLOW") // RW 0x7BCCE1
	{
		m_followId = 0;
		return;
	}
	if (a == "ZOOM_CAMERA" || a == "PITCH_CAMERA") // (factor, seconds, ease in, ease out)
	{
		(a == "ZOOM_CAMERA" ? m_zoom : m_pitch).start(realParam(r, 0), realParam(r, 1), realParam(r, 2), realParam(r, 3));
		return;
	}
	if (a == "CAMERA_LOOK_TOWARD_WAYPOINT")
	{
		// (waypoint, seconds, ease in, ease out, reverse): the camera turns to look from where it looks now toward the waypoint (ZH W3DView::lookAt donor)
		if (r.hasPosition)
		{
			const Coord3D from = m_hasTarget ? m_target : cameraTarget;
			float to = std::atan2(r.position.x - from.x, r.position.y - from.y); // the viewer's angle: 0 looks north (+y), the view direction (sin, cos)
			if (intParam(r, 4) != 0)
			{
				to += kPi;
			}
			const float seconds = realParam(r, 1);
			m_rotate.active = true;
			m_rotate.from = m_hasAngle ? m_angle : cameraAngle;
			m_rotate.to = to;
			m_rotate.ms = seconds * 1000.0 < 1.0 ? 1.0 : seconds * 1000.0;
			m_rotate.elapsed = 0.0;
			m_rotate.ease.setEaseTimes((float)(realParam(r, 2) * 1000.0 / m_rotate.ms), (float)(realParam(r, 3) * 1000.0 / m_rotate.ms));
			m_hasAngle = true;
			m_angle = m_rotate.from;
		}
		return;
	}
	if (a == "DISPLAY_NOTIFICATION_BOX" || a == "DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE")
	{
		// (type, label, seconds)
		m_notificationLabel = r.params.size() > 1 ? r.params[1].stringValue : std::string();
		m_notificationMs = intParam(r, 2) * 1000.0;
		return;
	}
	if (a == "SHOW_MISSION_OBJECTIVE" || a == "MARK_MISSION_OBJECTIVE_COMPLETED" || a == "HIDE_MISSION_OBJECTIVE")
	{
		const int index = intParam(r, 0);
		auto it = m_objectives.begin();
		while (it != m_objectives.end() && it->first != index)
		{
			++it;
		}
		if (a == "HIDE_MISSION_OBJECTIVE")
		{
			if (it != m_objectives.end())
			{
				m_objectives.erase(it);
			}
		}
		else if (it == m_objectives.end())
		{
			m_objectives.emplace_back(index, a == "MARK_MISSION_OBJECTIVE_COMPLETED");
		}
		else if (a == "MARK_MISSION_OBJECTIVE_COMPLETED")
		{
			it->second = true;
		}
		return;
	}
	m_unhandled.push_back(a);
}

void ScriptCameraDirector::Scalar::start(float target, float seconds, float easeIn, float easeOut)
{
	active = true;
	from = value;
	to = target;
	ms = seconds * 1000.0 < 1.0 ? 1.0 : seconds * 1000.0;
	elapsed = 0.0;
	ease.setEaseTimes((float)(easeIn * 1000.0 / ms), (float)(easeOut * 1000.0 / ms));
	if (ms <= 1.0)
	{
		update(0.0);
	}
}

void ScriptCameraDirector::Scalar::update(double dt)
{
	if (!active)
	{
		return;
	}
	elapsed += dt;
	const float p = (float)(elapsed / ms);
	const float e = ease(p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p));
	value = from + (to - from) * e;
	if (p >= 1.0f)
	{
		value = to;
		active = false;
	}
}

void ScriptCameraDirector::update(double ms)
{
	if (m_move.active)
	{
		m_move.elapsed += ms;
		const float p = (float)(m_move.elapsed / m_move.ms);
		const float e = m_move.ease(clamp01(p));
		m_target.x = m_move.from.x + (m_move.to.x - m_move.from.x) * e;
		m_target.y = m_move.from.y + (m_move.to.y - m_move.from.y) * e;
		m_target.z = m_move.from.z + (m_move.to.z - m_move.from.z) * e;
		if (p >= 1.0f)
		{
			m_target = m_move.to;
			m_move.active = false;
		}
	}
	if (m_rotate.active)
	{
		m_rotate.elapsed += ms;
		const float p = (float)(m_rotate.elapsed / m_rotate.ms);
		m_angle = m_rotate.from + (m_rotate.to - m_rotate.from) * m_rotate.ease(clamp01(p));
		if (p >= 1.0f)
		{
			m_angle = m_rotate.to;
			m_rotate.active = false;
		}
	}
	m_zoom.update(ms);
	m_pitch.update(ms);
	if (m_notificationMs > 0.0)
	{
		m_notificationMs -= ms;
		if (m_notificationMs <= 0.0)
		{
			m_notificationMs = 0.0;
			m_notificationLabel.clear();
		}
	}
	if (m_captionMs > 0.0)
	{
		m_captionMs -= ms;
		if (m_captionMs <= 0.0)
		{
			m_captionMs = 0.0;
			m_captionLabel.clear();
		}
	}
	if (m_fadeState.active)
	{
		Fade &f = m_fadeState;
		f.elapsed += ms;
		float v;
		if (f.elapsed < f.inMs)
		{
			v = f.minV + (f.maxV - f.minV) * (float)(f.elapsed / f.inMs);
		}
		else if (f.elapsed < f.inMs + f.holdMs)
		{
			v = f.maxV;
		}
		else if (f.elapsed < f.inMs + f.holdMs + f.outMs)
		{
			v = f.maxV - (f.maxV - f.minV) * (float)((f.elapsed - f.inMs - f.holdMs) / f.outMs);
		}
		else
		{
			v = f.minV;
			f.active = false;
		}
		m_fade = clamp01(v);
	}
}
