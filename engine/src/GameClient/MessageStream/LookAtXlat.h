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
//   * lane INPUT-1: SAVE_VIEW1..8 / VIEW_VIEW1..8 (the cases 0x24 .. 0x33) store and restore the camera bookmarks; OPTIONS (0x70) stops a scroll.
//   * lane BACKLOG3 (BFME2 decomp BfmeOwnVVDTranslateGameMessage.cpp / BfmeOwnVVDScrollUpdate.cpp, both tier A for RW 0x83AC4A / 0x83B471; the facts below
//     are read in RotWK's code):
//       - the tick ends a scroll when InGameUI's scrolling flag is off (vslot 0xA8) OR its vslot 0x17C answers (RW 0x48E916: InGameUI + 0x810, the flag
//         whose setter RW 0x69B6A8 clears the input modes when it goes on: the input is DISABLED; RW 0x83B493 .. 0x83B4E6);
//       - the anchor clamp of the right button scroll (RW 0x83B75E: the anchor kept within half the display of the pointer) runs only with InGameUI + 0x8C4
//         (the decomp's m_moveRMBScrollAnchor), which RotWK 2.01 only ever clears (the constructor RW 0x6A7234; no other write in the code): never in retail;
//       - a right button scroll also ends while InGameUI's vslot 0xCC answers (RW 0x48E8E6: + 0x98C, set by vslot 0xC4 RW 0x69B507 with a world position
//         and by RW 0x6A5A20); the engine has no such mode (S-454);
//       - the end of the tick (RW 0x83BAF8 ..): while scrolling, an arrow key TheKeyboard has not down (RW 0x63F0E2) is dropped, and a key scroll with no
//         key left stops; the numpad keys the same.
// Not ported: stop S-454 (TacticalCamera::acceptanceStops()).

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/TacticalCamera.h"

#include <functional>

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

	// lane BACKLOG3 (S-454): TheKeyboard's state of a DirectInput key (RW 0x63F0E2; BFME2 decomp KeyboardKeyAccessors.cpp, tier A). The end of the tick
	// (RW 0x83BAF8 ..) drops a held scroll / rotate / zoom key the keyboard no longer has down, so a release the stream never carried (the keyboard's reset
	// when the window loses the focus) cannot leave the camera moving. Unset: every key the translator holds counts as down
	void setKeyboard(std::function<bool(int)> isKeyDown) { m_isKeyDown = std::move(isKeyDown); }
	// lane OPTS-6: every held scroll, rotate and zoom ends (a modal menu opened over the game). Retail's own way to end a held scroll is the tick's
	// RW 0x83B4A5 .. 0x83B4B5 (InGameUI + 0x810, the input flag, cleared: stopScrolling); the keys it held are let go with it
	void cancelHeld();
	// RW GlobalData Windowed (+0x2C): edge scrolling is off while windowed
	void setEdgeScrollEnabled(bool on) { m_edgeScrollEnabled = on; }
	// ---- lane OPTS-2: the OpenBFME camera options (not retail; client only, the logic never sees the camera). Defaults are retail's (off / 1) ----
	// the right button drag scrolls the other way (the offset of the tick's type 1 scroll is negated)
	void setInvertDragScroll(bool on) { m_invertDrag = on; }
	bool invertDragScroll() const { return m_invertDrag; }
	// the rotation speed: the numpad keys' KeyboardCameraRotateSpeed and the middle button's radians per pixel times `scale`
	void setRotateSpeedScale(float scale) { m_rotateScale = scale; }
	float rotateSpeedScale() const { return m_rotateScale; }

	// ---- state, for tests and the report ----
	// lane OWNERFIX-1: RW 0x83AB5D (BFME2 decomp BfmeOwnVVD, tier A: the byte writes +0x38 isScrolling, +0x39 isRotating, +0x3B, +0x3C, +0x154 .. +0x157 the held
	// rotate / zoom keys), called by DISABLE_INPUT (RW 0x7BC586): a scroll in progress ends (retail leaves InGameUI's own scrolling flag and the Scroll
	// cursor to the next mouse move, which stops a scroll while the input is off: RW 0x42EBF9; the port ends it here), the rotation and the four held keys
	// are off. +0x3B / +0x3C (a pitch flag and one more) are not mapped (S-4964)
	void clearFlags();
	void cancelPointer() { stopScrolling(); m_rotating = false; }
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
	std::function<bool(int)> m_isKeyDown; // lane BACKLOG3
	bool m_invertDrag = false;    // lane OPTS-2
	float m_rotateScale = 1.0f;   // lane OPTS-2
	// lane INPUT-1: the eight camera bookmarks (BFME2 decomp BfmeOwnVVD + 0x48, ViewLocation m_viewLocation[8])
	struct ViewLocation
	{
		bool valid = false;
		Coord3D pos;
		float angle = 0.0f, pitch = 0.0f, height = 0.0f;
	};
	ViewLocation m_viewLocation[8];
	float m_offsetX = 0.0f, m_offsetY = 0.0f;
};
