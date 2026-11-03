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
			startMove(m_move.active ? m_target : cameraTarget, r.position, realParam(r, 1), realParam(r, 3), realParam(r, 4));
		}
		return;
	}
	if (a == "RESET_CAMERA")
	{
		if (r.hasPosition)
		{
			// (position, seconds, ease in, ease out): the move, the default angle (0) and the default pitch / zoom
			startMove(m_move.active ? m_target : cameraTarget, r.position, realParam(r, 1), realParam(r, 2), realParam(r, 3));
			// lane CAMPUI-1: a move or turn starts from where the camera is now (the player may have moved it since the last one), the director's own value only
			// while one of its moves is still running
			const float from = m_rotate.active ? m_angle : cameraAngle;
			m_rotate.active = true;
			m_rotate.from = from;
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
		const float from = m_rotate.active ? m_angle : cameraAngle;
		m_rotate.active = true;
		m_rotate.from = from;
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
		m_captionDurationMs = m_captionMs;
		++m_captionSerial;
		return;
	}
	if (a == "CAMERA_FADE_ADD" || a == "CAMERA_FADE_SUBTRACT" || a == "CAMERA_FADE_SATURATE" || a == "CAMERA_FADE_MULTIPLY")
	{
		// ScriptEngine::setFade (RW 0x6030C1 ~ BFME2 0x203777, Open-BFME-2 ScriptEngine.cpp:660-711): (min, max, frames to increase,
		// frames to hold, frames to decrease), the counts in LOGIC frames; an increase of 0 runs the first updateFades step here
		Fade &f = m_fadeState;
		if (a == "CAMERA_FADE_ADD")
		{
			f.type = FADE_ADD;
		}
		else if (a == "CAMERA_FADE_SUBTRACT")
		{
			f.type = FADE_SUBTRACT;
		}
		else if (a == "CAMERA_FADE_SATURATE")
		{
			f.type = FADE_SATURATE;
		}
		else if (a == "CAMERA_FADE_MULTIPLY")
		{
			f.type = FADE_MULTIPLY;
		}
		else
		{
			f.type = FADE_NONE; // retail's switch default: unreachable, the action dispatch only sends these four
			return;
		}
		f.curFrame = 0;
		f.minV = realParam(r, 0);
		f.maxV = realParam(r, 1);
		f.increase = intParam(r, 2);
		f.hold = intParam(r, 3);
		f.decrease = intParam(r, 4);
		f.curValue = f.minV;
		f.startFrame = r.frame;
		if (f.increase == 0)
		{
			updateFades();
		}
		f.active = true;
		return;
	}
	if (a == "CAMERA_FOLLOW_NAMED") // (unit, snap, ?): RW 0x7BB4B0 -> the view's follow (slot 0x184) of the object
	{
		m_followId = r.objectId;
		return;
	}
	if (a == "MOVE_CAMERA_BY_ANIMATION")
	{
		return; // lane CAMANIM-1: the tactical view plays it (GameClient/CameraAnimation.h: CameraAnimationPlayback), not the script camera
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
			const Coord3D from = m_move.active ? m_target : cameraTarget;
			float to = std::atan2(r.position.x - from.x, r.position.y - from.y); // the viewer's angle: 0 looks north (+y), the view direction (sin, cos)
			if (intParam(r, 4) != 0)
			{
				to += kPi;
			}
			const float seconds = realParam(r, 1);
			const float fromAngle = m_rotate.active ? m_angle : cameraAngle;
			m_rotate.active = true;
			m_rotate.from = fromAngle;
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
	if (a == "SET_CAMERA_CLIP_DEPTH_MULTIPLIER")
	{
		return; // lane MODCOMPAT-AI: the HUD camera's far plane (scripts/campaign_cine.gd -> InGameHud camera_set_clip_depth_multiplier)
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

// ScriptEngine::updateFades (RW 0x603016 ~ BFME2 0x2036CC, Open-BFME-2 ScriptEngine.cpp:688-712): ++frame, then the increase ramp,
// the hold at the maximum, the decrease ramp (its divisor is decrease + 1), and past the last frame FADE_NONE — the fade is gone
// and draws nothing, not its last value (lane CAMP-1's rule: the Amon Sul intro's (1, 0, 0, 0, 0) ends the same frame it starts)
void ScriptCameraDirector::updateFades()
{
	Fade &f = m_fadeState;
	++f.curFrame;
	const int increase = f.increase; // retail reads the member through a volatile view after the bump; the value is the same
	int frame = f.curFrame;
	f.active = false;
	if (frame <= increase)
	{
		f.curValue = f.minV + (float)frame / increase * (f.maxV - f.minV);
		return;
	}
	frame -= increase;
	if (frame <= f.hold)
	{
		f.curValue = f.maxV;
		return;
	}
	frame -= f.hold;
	if (frame <= f.decrease)
	{
		int divisor = f.decrease + 1;
		if (divisor == 0)
		{
			divisor = 1;
		}
		f.curValue = f.maxV + (float)frame / divisor * (f.minV - f.maxV);
		return;
	}
	f.type = FADE_NONE;
}

void ScriptCameraDirector::update(double ms, std::uint32_t presentedFrame, double alpha)
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
	{
		Fade &f = m_fadeState;
		// ScriptEngine::update (RW 0x60CC67 ~ BFME2 0x20D065) runs updateFades once per logic frame, before the frame's actions — so
		// a fade set on frame F steps first at F + 1, plus the step run inside setFade when increase is 0. A FADE_NONE steps no more.
		const int due = (int)(presentedFrame >= f.startFrame ? presentedFrame - f.startFrame : 0) + (f.increase == 0 ? 1 : 0);
		while (f.type != FADE_NONE && f.curFrame < due)
		{
			updateFades();
		}
		// W3DStatusCircle::Render's tail (RW 0x48E0EE ~ BFME2 0x8E7A4): latched once per logic frame, previous := current, current :=
		// the step value (0 once the type is gone); a fade set since (retail's m_fadeActive) snaps previous to current. Then the draw
		// lerps previous -> current by the frame fraction; FADE_NONE draws nothing (fadeType() gates the quad)
		if (presentedFrame != m_fadePresent || f.active)
		{
			m_fadePrev = m_fadeCur;
			m_fadeCur = f.type == FADE_NONE ? 0.0f : f.curValue;
			if (f.active)
			{
				m_fadePrev = m_fadeCur;
			}
			m_fadePresent = presentedFrame;
		}
		m_fade = m_fadePrev < 0.0f ? m_fadeCur : m_fadePrev + (m_fadeCur - m_fadePrev) * (float)alpha;
	}
}
