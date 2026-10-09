// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (W3DView::moveCameraTo / resetCamera / rotateCamera,
// ParabolicEase, InGameUI military captions, letterbox).
//
// ScriptCameraDirector (lane SCRIPT-1): the CLIENT side of the script engine's presentation requests (ScriptClientRequest): the scripted camera
// moves, the letterbox, the military caption, the screen fade and the input lock of a cinematic. Pure client state on the render clock; the logic
// never reads it.
//
// DONOR FACTS (ZH, stop S-1182: the RotWK camera code of these actions was not read):
//   * MOVE_CAMERA_TO(position, seconds, shutter, ease in, ease out) -> W3DView::moveCameraTo(pos, seconds * 1000 ms, shutter, false,
//     easeIn * 1000, easeOut * 1000): the look-at point moves from the current one to the target over the time; the progress is
//     ParabolicEase(easeIn / ms, easeOut / ms) of the elapsed fraction.
//   * RESET_CAMERA(position, seconds, ease in, ease out): moveCameraTo, then the angle goes to the default (0) and the pitch / zoom to the defaults
//     over the same time.
//   * ROTATE_CAMERA(rotations, seconds, ease in, ease out): the angle turns by 2 pi * rotations over the time with the same ease.
//   * ParabolicEase(in, out): v0 = 1 + out' - in with out' = 1 - out; p < in: p^2 / (v0 in); p <= out': (in + 2 (p - in)) / v0; else
//     (in + 2 (out' - in) + (2 (p - out') + out'^2 - p^2) / (1 - out')) / v0.
//   * SHOW_MILITARY_CAPTION(label, seconds): the caption text (the label's game text) shown for the duration; CAMERA_LETTERBOX_BEGIN / END;
//     DISABLE_INPUT / ENABLE_INPUT; CAMERA_FADE_*: (min, max, increase frames, hold frames, decrease frames).

#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct ScriptClientRequest;

class ParabolicEase
{
public:
	void setEaseTimes(float easeIn, float easeOut);
	float operator()(float param) const;

private:
	float m_in = 0.0f, m_out = 1.0f;
};

class ScriptCameraDirector
{
public:
	// `cameraTarget` / `cameraAngle`: where the camera looks now (the start of a move)
	void apply(const ScriptClientRequest &r, const Coord3D &cameraTarget, float cameraAngle);
	// advances the render clock
	void update(double ms);

	bool moving() const { return m_move.active || m_rotate.active; }
	bool hasTarget() const { return m_hasTarget; }
	Coord3D target() const { return m_target; }
	bool hasAngle() const { return m_hasAngle; }
	float angle() const { return m_angle; }
	bool resetView() const { return m_resetView; } ///< a RESET_CAMERA asked for the default pitch / zoom
	bool letterbox() const { return m_letterbox; }
	bool inputDisabled() const { return m_inputDisabled; }
	const std::string &captionLabel() const { return m_captionLabel; }
	double captionMsLeft() const { return m_captionMs; }
	float fade() const { return m_fade; } ///< 0 none .. 1 full (the CAMERA_FADE_* amount now)
	// lane SCRIPT-2: CAMERA_FOLLOW_NAMED / CAMERA_STOP_FOLLOW (the object the camera follows, 0 none), ZOOM_CAMERA / PITCH_CAMERA (factors of the default
	// distance / pitch over time, 1 = the default), the notification box (DISPLAY_NOTIFICATION_BOX: its label for its seconds), the objectives shown
	std::uint32_t followId() const { return m_followId; }
	float zoom() const { return m_zoom.value; }
	float pitch() const { return m_pitch.value; }
	const std::string &notificationLabel() const { return m_notificationLabel; }
	const std::vector<std::pair<int, bool>> &objectives() const { return m_objectives; } ///< (index, completed) in the order shown
	const std::vector<std::string> &unhandled() const { return m_unhandled; }

private:
	struct Move
	{
		bool active = false;
		Coord3D from, to;
		double ms = 1.0, elapsed = 0.0;
		ParabolicEase ease;
	};
	struct Rotate
	{
		bool active = false;
		float from = 0.0f, to = 0.0f;
		double ms = 1.0, elapsed = 0.0;
		ParabolicEase ease;
	};
	struct Fade
	{
		bool active = false;
		float minV = 0.0f, maxV = 1.0f;
		double inMs = 0.0, holdMs = 0.0, outMs = 0.0, elapsed = 0.0;
	};
	void startMove(const Coord3D &from, const Coord3D &to, float seconds, float easeIn, float easeOut);
	struct Scalar
	{
		bool active = false;
		float value = 1.0f, from = 1.0f, to = 1.0f;
		double ms = 1.0, elapsed = 0.0;
		ParabolicEase ease;
		void start(float target, float seconds, float easeIn, float easeOut);
		void update(double ms);
	};
	Scalar m_zoom, m_pitch;
	std::uint32_t m_followId = 0;
	std::string m_notificationLabel;
	double m_notificationMs = 0.0;
	std::vector<std::pair<int, bool>> m_objectives;

	Move m_move;
	Rotate m_rotate;
	Fade m_fadeState;
	bool m_hasTarget = false, m_hasAngle = false, m_resetView = false;
	Coord3D m_target;
	float m_angle = 0.0f;
	bool m_letterbox = false, m_inputDisabled = false;
	std::string m_captionLabel;
	double m_captionMs = 0.0;
	float m_fade = 0.0f;
	std::vector<std::string> m_unhandled;
};
