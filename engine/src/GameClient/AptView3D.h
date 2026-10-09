// OpenBFME. GPL-3.0.
//
// The animation of an Apt View3D render object (lane UI-2, owner feedback F2: the victory / defeat screen's rings and Evenstar / Eye circle, GuiFX.apt's
// GoodW3D / EvilW3D clips; also the shell's SFE_MenuFrame). Client presentation only; the Godot viewer is GodotDevice/GodotAptView3D.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * a clip tagged `_type = "View3D"` with a `_RenderObj` gets a viewer (RW 0x8145DD: `_KeepAspectRatio` true unless the value contains 'f',
//     `_AnimMode`), drawn by the render component RW 0x814DDC registered as "View3D" (RW 0x8151AC);
//   * the viewer (vtable RW 0xD0A160): a scene with ambient (0.5, 0.5, 0.5) (RW 0xB5442B), a camera with clip planes 1 / 5000, horizontal field of view
//     0.8726646 rad, vertical from the aspect; setRenderObject (RW 0xB54D33) loads the model, takes its bone named CAMERA (case-insensitive) as the
//     camera, makes a point light of every bone whose name contains LIGHT with a number after '_' (none in the corpus' View3D models), puts the model at
//     the identity and starts the animation "<name>.<name>" (RW 0xB54C1A) with Set_Animation(anim, 1.0, mode) (RW 0xB5430E; skipped when frame and
//     mode equal the last call's);
//   * the clip's "_frame=<n>" command (RW 0x813514, when the command string changes) calls setFrame(n, `_AnimMode`) (RW 0xB54BFC) =
//     Set_Animation(anim, n, mode); each draw (RW 0xB5470F) sets the viewport to the clip over the display, the aspect to the clip's when
//     `_KeepAspectRatio`, the camera to the CAMERA bone's transform, and renders;
//   * the mode names (RW 0xB54BC6 over the table RW 0xDC3BC0, case-insensitive; an unknown name is index 0): MANUAL, LOOP, ONCE, LOOP_PINGPONG,
//     PLAY_TO_FRAME, LOOP_BACKWARDS, ONCE_BACKWARDS.
// DONOR (Open-BFME-1 Animatable3DObj_Set_Animation_MH.cpp, byte-matched; animobj.cpp Compute_Current_Frame, not byte-matched): Set_Animation stores
// the clamped request as PrevFrame, the frame itself unless the mode is PLAY_TO_FRAME (which keeps the frame reached and turns towards the target),
// the direction +1 below ONCE_BACKWARDS; the frame then moves by frame rate * elapsed ms / 1000 * direction: ONCE stops at the last frame, LOOP wraps
// by (frames - 1), PLAY_TO_FRAME stops at the target, LOOP_PINGPONG reflects, the backward modes mirror.
// INFERENCE [S-1483]: the "_frame=" command is the clip's `_Frame` member (the string RW 0x813514 parses is assembled by code not read), the
// elapsed time is the device's real time, and the model is lit by the game's light set (the device's W3D lighting is global, not per scene).

#pragma once

#include <climits>
#include <string>

enum AptView3DAnimMode
{
	APT_VIEW3D_MANUAL = 0,
	APT_VIEW3D_LOOP = 1,
	APT_VIEW3D_ONCE = 2,
	APT_VIEW3D_LOOP_PINGPONG = 3,
	APT_VIEW3D_PLAY_TO_FRAME = 4,
	APT_VIEW3D_LOOP_BACKWARDS = 5,
	APT_VIEW3D_ONCE_BACKWARDS = 6
};

// RW 0xB54BC6
int AptView3DAnimModeIndex(const std::string &name);
// RW 0x8145DD: `_KeepAspectRatio` keeps the aspect unless its text contains 'f'
bool AptView3DKeepAspect(const std::string &value);
// RW 0x813514: the frame of a "_frame=<n>" command (atoi)
int AptView3DFrameOf(const std::string &value);

class AptView3DAnimation
{
public:
	// RW 0xB5430E -> Set_Animation(anim, (float)frame, mode); `nowMs` is the sync time. False when the call was skipped (same frame and mode).
	bool setAnimation(int numFrames, int frame, int mode, double nowMs);
	// Single_Anim_Progress: the frame at `nowMs` becomes the current one
	void progress(double nowMs, float frameRate);
	float frame() const { return m_frame; }
	float target() const { return m_prevFrame; }
	int mode() const { return m_mode; }
	bool started() const { return m_started; }

private:
	float compute(double nowMs, float frameRate, float *direction) const;

	bool m_started = false;
	int m_numFrames = 0;
	int m_lastRequestFrame = INT_MIN, m_lastRequestMode = -1; // RW 0xB5430E's +0x0C / +0x08
	float m_frame = 0.0f;      // constructor: 0
	float m_prevFrame = 0.0f;
	int m_mode = APT_VIEW3D_MANUAL;
	double m_lastSyncMs = 0.0;
	float m_direction = 1.0f;
};
