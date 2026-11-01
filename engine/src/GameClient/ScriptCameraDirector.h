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
//
// TARGET FACTS (RotWK 2.01; the BFME2 1.06 pair is byte-matched, tier B same-shape, so one rule serves both editions):
//   * CAMERA_FADE_* -> ScriptEngine::setFade (RW 0x6030C1 ~ BFME2 0x203777, Open-BFME-2 ScriptEngine.cpp:660-711): the fade type by the
//     action (anything else FADE_NONE), the counts in LOGIC frames (LOGICFRAMES_PER_SECOND = 5, [0xD9F608]).
//   * ScriptEngine::updateFades (RW 0x603016 ~ BFME2 0x2036CC): ++frame; frame <= increase: min + frame / increase * (max - min); then
//     hold -> max; then decrease: max + frame / (decrease + 1) * (min - max); past that FADE_NONE (draws nothing). Retail's only
//     per-frame caller is ScriptEngine::update (RW 0x60CC67 ~ BFME2 0x20D065): it runs before the frame's actions, and setFade itself
//     runs it once when increase is 0.
//   * The draw (W3DStatusCircle::Render's tail, RW 0x48E0EE ~ BFME2 0x8E7A4): once per logic frame (the client-frame gate RW 0x63252F
//     ~ BFME2 0x225D38) it latches current -> previous and reads the step value; a fade set since (m_fadeActive, +0x1A13C) snaps
//     previous to current instead of lerping over the seam; then it draws previous -> current lerped by the frame fraction
//     (GameEngine + 0x3C), a white quad of that intensity blended by the type.

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
	// retail ScriptEngine::TFade (+0x1A138): FADE_NONE draws nothing (lane CAMP-1's rule kept)
	enum FadeType
	{
		FADE_NONE = 0,
		FADE_SUBTRACT = 1,
		FADE_ADD = 2,
		FADE_SATURATE = 3,
		FADE_MULTIPLY = 4
	};
	// `cameraTarget` / `cameraAngle`: where the camera looks now (the start of a move)
	void apply(const ScriptClientRequest &r, const Coord3D &cameraTarget, float cameraAngle);
	// advances the render clock; `presentedFrame` is the latest published logic frame and `alpha` the fraction through it
	// (the fade steps per logic frame and lerps the last two steps by alpha: the retail draw's frameFraction, GameEngine + 0x3C)
	void update(double ms, std::uint32_t presentedFrame = 0, double alpha = 1.0);

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
	// lane CAMPUI-1: SHOW_MILITARY_CAPTION's count (each one restarts the caption, RW 0x69FF85) and its duration in ms (the caption's own lifetime: RW 0x69CE5F
	// keeps it until max(duration, typed + 2 s), then fades it; the client draws it from these)
	unsigned captionSerial() const { return m_captionSerial; }
	double captionDurationMs() const { return m_captionDurationMs; }
	float fade() const { return m_fade; }      ///< the presented intensity: the last two steps lerped by the frame fraction (RW 0x48E0EE)
	int fadeType() const { return m_fadeState.type; }     ///< retail TFade (ScriptEngine + 0x1A138); FADE_NONE draws nothing
	float fadeValue() const { return m_fadeState.curValue; } ///< retail m_curFadeValue (+ 0x1A148): the current logic-frame step
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
	// the ScriptEngine fade block (+0x1A138 ff): the type, the per-logic-frame stepper of updateFades (RW 0x603016) and the
	// action's parameters as setFade (RW 0x6030C1) stores them
	struct Fade
	{
		int type = FADE_NONE;
		bool active = false;      ///< retail m_fadeActive (+0x1A13C): set by setFade, cleared at the top of the next updateFades
		int curFrame = 0;         ///< retail m_curFadeFrame (+0x1A14C)
		int increase = 0, hold = 0, decrease = 0;
		float minV = 0.0f, maxV = 0.0f, curValue = 0.0f;
		std::uint32_t startFrame = 0; ///< the logic frame the action ran on (the request's frame): updateFades first steps it at +1
	};
	void startMove(const Coord3D &from, const Coord3D &to, float seconds, float easeIn, float easeOut);
	void updateFades(); // ScriptEngine::updateFades (RW 0x603016 ~ BFME2 0x2036CC): one step per logic frame
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
	// the draw-side latch of W3DStatusCircle::Render's tail (RW 0x48E0EE): +0xCC previousIntensity (starts < 0: snap), +0xD0
	// currentIntensity, and the presented frame they were latched on (retail latches once per logic frame, RW 0x63252F)
	float m_fadePrev = -1.0f, m_fadeCur = 0.0f;
	std::uint32_t m_fadePresent = 0xFFFFFFFFu;
	bool m_hasTarget = false, m_hasAngle = false, m_resetView = false;
	Coord3D m_target;
	float m_angle = 0.0f;
	bool m_letterbox = false, m_inputDisabled = false;
	std::string m_captionLabel;
	double m_captionMs = 0.0;
	unsigned m_captionSerial = 0;
	double m_captionDurationMs = 0.0;
	float m_fade = 0.0f;
	std::vector<std::string> m_unhandled;
};
