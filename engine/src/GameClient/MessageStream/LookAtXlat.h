// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// LookAtTranslator (lane CAM-1): raw input to camera movement, ZH LookAtXlat.cpp as RotWK's version changes it. CLIENT state (no GameMessage leaves it: every message is kept for
// the translators after it, ZH priority 60).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the translator object has the vtable RW 0xC53D3C, the global RW 0xDE8CB0, it is attached at priority 0x3C by RW 0x646BC6;
// translateGameMessage RW 0x83AC4A, the per frame tick RW 0x83B471 called from the client update RW 0x64849E, setScrolling RW 0x83A9BD, stopScrolling RW 0x83AA62):
//   * the right button press arms a scroll (pixel -> anchor and current position); it starts only when the pointer has moved more than Mouse's drag tolerance (RW 0x5ED4B9:
//     |dx| or |dy| above Mouse + 0x12E8, Mouse.ini DragTolerance 15) from the anchor, so a plain right click scrolls nothing; the release disarms it and stops a type 1 scroll;
//   * the pointer at the screen edge (3 pixels) starts a type 3 scroll when GlobalData Windowed is off and the player is not selecting; it stops when the pointer leaves
//     the edge;
//   * the arrow keys (DirectInput 0xC8 up, 0xD0 down, 0xCB left, 0xCD right) start a type 2 scroll; numpad 4 / 6 (0x4B / 0x4D) rotate, numpad 8 / 2 (0x48 / 0x50) zoom in / out;
//   * the middle button press starts a rotation (anchor, 0.005 radians per pixel of x movement while held); a release within 5 pixels and 5 client frames of the press is a
//     click: resetCamera; the wheel zooms in (spin > 0) or out, one step per notch;
//   * the tick (RW 0x83B471): type 1: offset = (current - anchor) * (HorizontalScrollSpeedFactor, VerticalScrollSpeedFactor) plus the same unit direction * factor * KeyboardScrollSpeedFactor^2;
//     type 2: KeyboardScrollSpeedFactor * ScrollSpeedFactor * 100 per frame; type 3: KeyboardScrollSpeedFactor * ScreenEdgeScrollSpeedFactor * ScrollSpeedFactor * the ramp percent
//     (elapsed / ScreenEdgeScrollRampTime, 0 .. 100); then View::scrollBy(offset); a type 1 scroll that hits a map edge moves the anchor to the pointer on that axis; the
//     numpad rotate keys add KeyboardCameraRotateSpeed per frame, the zoom keys zoom once per frame.
// Not ported: stop S-454 (TacticalCamera::acceptanceStops()).

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/TacticalCamera.h"

class LookAtTranslator : public MessageTranslator
{
public:
	enum ScrollType
	{
		SCROLL_NONE = 0,
		SCROLL_RMB = 1,
		SCROLL_KEY = 2,
		SCROLL_SCREENEDGE = 3
	};
	// DirectInput codes the translator reads (RW 0x83B32B ..)
	enum
	{
		DIK_KP8 = 0x48,
		DIK_KP4 = 0x4B,
		DIK_KP6 = 0x4D,
		DIK_KP2 = 0x50
	};

	LookAtTranslator(HudContext &context, TacticalCamera &camera);

	MessageDisposition translate(const ClientMessage &message) override;

	// One client frame: the scroll offset of the held scrolls moves the camera (RW 0x83B471). `nowMs` is the clock of the edge scroll ramp (timeGetTime).
	void tick(unsigned nowMs);
	// the clock of the edge scroll ramp at the time of the next input (retail reads timeGetTime inside setScrolling); tick() sets it too
	void setTime(unsigned nowMs) { m_nowMs = nowMs; }
	// the client frame the translator counts (middle click timing, last mouse move)
	unsigned clientFrame() const { return m_frame; }

	// RW GlobalData Windowed (+0x2C): edge scrolling is off while windowed
	void setEdgeScrollEnabled(bool on) { m_edgeScrollEnabled = on; }

	// ---- state, for tests and the report ----
	bool isScrolling() const { return m_scrolling; }
	int scrollType() const { return m_scrollType; }
	bool isRotating() const { return m_rotating; }
	bool isArmed() const { return m_armed; }
	ICoord2D anchor() const { return m_anchor; }
	ICoord2D current() const { return m_cur; }
	// the offset the last tick handed to scrollBy
	float offsetX() const { return m_offsetX; }
	float offsetY() const { return m_offsetY; }

private:
	void setScrolling(int type);
	void stopScrolling();
	bool isDragging(const ICoord2D &a, const ICoord2D &b) const;
	bool interior(const ICoord2D &p, const ICoord2D &size) const;

	HudContext &m_ctx;
	TacticalCamera &m_camera;
	ICoord2D m_anchor{ 0, 0 }, m_cur{ 0, 0 }, m_rotAnchor{ 0, 0 }, m_midOrigin{ 0, 0 }, m_midCur{ 0, 0 };
	bool m_scrolling = false, m_rotating = false, m_anchorMoved = false, m_armed = false;
	int m_scrollType = SCROLL_NONE;
	unsigned m_scrollStartMs = 0, m_nowMs = 0;
	unsigned m_frame = 0, m_middleFrame = 0, m_lastMoveFrame = 0;
	bool m_rotateLeft = false, m_rotateRight = false, m_zoomIn = false, m_zoomOut = false;
	bool m_scrollKey[4] = { false, false, false, false }; // up, down, left, right (RW 0xDE8CB4 ..)
	bool m_edgeScrollEnabled = true;
	float m_offsetX = 0.0f, m_offsetY = 0.0f;
};
