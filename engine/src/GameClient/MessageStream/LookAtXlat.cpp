// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/MessageStream/LookAtXlat.h.

#include "GameClient/MessageStream/LookAtXlat.h"

#include <cmath>
#include <cstdlib>

namespace
{
constexpr float kRotatePerPixel = 0.004999999888241291f; // RW 0xC53B04
// DirectInput codes and the key state bit the translator reads (ZH KeyDefs.h; the HUD's MetaEvent.h KeyCode and the shell's Gadget.h name the same values)
constexpr int kKeyUp = 0xC8, kKeyDown = 0xD0, kKeyLeft = 0xCB, kKeyRight = 0xCD, kKeyStateUp = 0x0001;
constexpr int kEdge = 3;                                 // RW 0x83AF8D: the 3 pixel border
constexpr float kKeyScroll = 100.0f;                     // RW 0xBD88D8
} // namespace

LookAtTranslator::LookAtTranslator(HudContext &context, TacticalCamera &camera)
	: m_ctx(context)
	, m_camera(camera)
{
}

bool LookAtTranslator::isDragging(const ICoord2D &a, const ICoord2D &b) const
{
	// RW 0x5ED4B9: |dx| above the drag tolerance or |dy| above it (unsigned compares)
	const unsigned tol = (unsigned)m_ctx.mouse.dragTolerance;
	return (unsigned)std::abs(a.x - b.x) > tol || (unsigned)std::abs(a.y - b.y) > tol;
}

bool LookAtTranslator::interior(const ICoord2D &p, const ICoord2D &size) const
{
	return p.x >= kEdge && p.y >= kEdge && p.y < size.y - kEdge && p.x < size.x - kEdge;
}

void LookAtTranslator::setScrolling(int type)
{
	// RW 0x83A9BD (the view / client blocks at View + 0x2449 and TheGameClient + 0xC0 are off: S-454)
	if (!m_ctx.ui.getInputEnabled())
	{
		return;
	}
	m_scrolling = true;
	m_ctx.ui.setScrolling(true);
	if (type != m_scrollType)
	{
		m_scrollType = type;
		m_scrollStartMs = m_nowMs;
	}
}

void LookAtTranslator::stopScrolling()
{
	// RW 0x83AA62
	m_scrolling = false;
	m_armed = false;
	m_ctx.ui.setScrolling(false);
	m_scrollType = SCROLL_NONE;
}

MessageDisposition LookAtTranslator::translate(const ClientMessage &msg)
{
	switch (msg.type())
	{
		case CMSG_RAW_KEY_DOWN:
		case CMSG_RAW_KEY_UP:
		{
			// RW 0x83B2FC
			const int key = msg.arg(0).integer & 0xFF;
			const bool pressed = !(msg.arg(1).integer & kKeyStateUp);
			switch (key)
			{
				case kKeyUp: m_scrollKey[0] = pressed; break;
				case kKeyDown: m_scrollKey[1] = pressed; break;
				case kKeyLeft: m_scrollKey[2] = pressed; break;
				case kKeyRight: m_scrollKey[3] = pressed; break;
				case DIK_KP4: m_rotateLeft = pressed; break;
				case DIK_KP6: m_rotateRight = pressed; break;
				case DIK_KP8: m_zoomIn = pressed; break;
				case DIK_KP2: m_zoomOut = pressed; break;
				default: break;
			}
			if (m_ctx.ui.isSelecting() || (m_scrolling && m_scrollType != SCROLL_KEY))
			{
				break;
			}
			int down = 0;
			for (bool k : m_scrollKey)
			{
				down += k ? 1 : 0;
			}
			if (down != 0)
			{
				if (!m_scrolling)
				{
					setScrolling(SCROLL_KEY);
				}
			}
			else if (m_scrolling)
			{
				stopScrolling();
			}
			break;
		}
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		{
			// RW 0x83ACCD
			m_lastMoveFrame = m_frame;
			m_cur = msg.arg(0).pixel;
			m_anchor = m_cur;
			if (!m_ctx.ui.isSelecting() && !m_scrolling)
			{
				m_armed = true;
			}
			break;
		}
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			// RW 0x83AC98
			m_lastMoveFrame = m_frame;
			m_armed = false;
			if (m_scrollType == SCROLL_RMB)
			{
				stopScrolling();
			}
			break;
		}
		case CMSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN:
		{
			// RW 0x83AD9B
			m_lastMoveFrame = m_frame;
			m_rotating = true;
			const ICoord2D p = msg.arg(0).pixel;
			m_rotAnchor = p;
			m_midOrigin = p;
			m_midCur = p;
			m_middleFrame = m_frame;
			break;
		}
		case CMSG_RAW_MOUSE_MIDDLE_BUTTON_UP:
		{
			// RW 0x83AD16
			m_lastMoveFrame = m_frame;
			m_rotating = false;
			const unsigned dx = (unsigned)std::abs(m_midCur.x - m_midOrigin.x);
			const unsigned dy = (unsigned)(m_midCur.y - m_midOrigin.y); // RW compares the signed difference unsigned: a move upwards is never a click
			if (dx > 5 || dy > 5)
			{
				break;
			}
			if (m_frame - m_middleFrame >= 5)
			{
				break;
			}
			m_camera.resetCamera();
			break;
		}
		case CMSG_RAW_MOUSE_WHEEL:
		{
			// RW 0x83B1F5
			m_lastMoveFrame = m_frame;
			int spin = msg.arg(1).integer;
			for (; spin > 0; --spin)
			{
				m_camera.zoomIn();
			}
			for (; spin < 0; ++spin)
			{
				m_camera.zoomOut();
			}
			break;
		}
		case CMSG_RAW_MOUSE_POSITION:
		{
			// RW 0x83AE12
			const ICoord2D p = msg.arg(0).pixel;
			if (m_armed && !m_scrolling && isDragging(m_anchor, p))
			{
				setScrolling(SCROLL_RMB);
			}
			if (p.x != m_cur.x || p.y != m_cur.y)
			{
				m_lastMoveFrame = m_frame;
			}
			if (m_rotating)
			{
				m_anchorMoved = true;
				m_midCur = p;
			}
			else if (m_anchorMoved && m_scrollType == SCROLL_RMB)
			{
				// the right button scroll continues after a rotation: the anchor keeps its offset from the pointer
				const int dy = m_anchor.y - m_cur.y, dx = m_anchor.x - m_cur.x;
				m_cur = p;
				m_anchor.x = p.x + dx;
				m_anchor.y = p.y + dy;
				m_anchorMoved = false;
			}
			else
			{
				m_cur = p;
			}
			const ICoord2D size = m_camera.size();
			if (!m_ctx.ui.getInputEnabled())
			{
				if (m_scrolling)
				{
					stopScrolling();
				}
				break;
			}
			const bool selecting = m_ctx.ui.isSelecting();
			if (m_edgeScrollEnabled && !selecting)
			{
				if (m_scrolling)
				{
					if (m_scrollType == SCROLL_SCREENEDGE && interior(m_cur, size))
					{
						stopScrolling();
					}
				}
				else if (!interior(m_cur, size))
				{
					setScrolling(SCROLL_SCREENEDGE);
				}
			}
			if (m_rotating)
			{
				const float angle = (float)(m_midCur.x - m_rotAnchor.x) * kRotatePerPixel;
				m_camera.setAngle(m_camera.getAngle() + angle);
				m_rotAnchor = p;
			}
			break;
		}
		case CMSG_META_OPTIONS:
			// RW 0x83B1E8 (case 0x70): the options menu stops a scroll
			stopScrolling();
			break;
		case CMSG_META_CAMERA_RESET:
			// lane INPUT-1: CAMERA_RESET (numpad 5) is the CommandTranslator's in RotWK (RW 0x8203FA -> RW 0x69BF39: View::resetCamera (vslot 0xC8) at the
			// view's own position, the meta is kept); the camera is this translator's here (both run every message; the order changes nothing)
			m_camera.resetCamera();
			break;
		default:
			break;
	}
	// lane INPUT-1: the camera bookmarks (RW 0x83AC4A cases 0x24 .. 0x2B SAVE_VIEW1..8, 0x2C .. 0x33 VIEW_VIEW1..8; BFME2 decomp
	// BfmeOwnVVDTranslateGameMessage.cpp, tier A): Ctrl+F1..F8 store the view (View::getLocation) and show GUI:BookmarkXSet, F1..F8 restore it
	// (View::setLocation, only a stored one) while the UI takes input; both destroy the message
	const int t = msg.type();
	if (t >= CMSG_META_SAVE_VIEW1 && t <= CMSG_META_SAVE_VIEW8)
	{
		ViewLocation &v = m_viewLocation[t - CMSG_META_SAVE_VIEW1];
		v.valid = true;
		v.pos = m_camera.position();
		v.angle = m_camera.getAngle();
		v.pitch = m_camera.getPitch();
		v.height = m_camera.getHeightAboveGround();
		m_ctx.ui.message("GUI:BookmarkXSet"); // retail formats the slot number (1 .. 8) into the text
		return MessageDisposition::Destroy;
	}
	if (t >= CMSG_META_VIEW_VIEW1 && t <= CMSG_META_VIEW_VIEW8)
	{
		if (!m_ctx.ui.getInputEnabled())
		{
			return MessageDisposition::Keep;
		}
		const ViewLocation &v = m_viewLocation[t - CMSG_META_VIEW_VIEW1];
		if (v.valid)
		{
			// ZH View::setLocation: setPosition, setAngle, setPitch, setZoom (RotWK's ViewLocation keeps seven floats, RW View slots 0xFC / 0x104 /
			// 0x10C / 0x128: INFERENCE: the position, the angle, the pitch and the zoom, here the height above the ground)
			m_camera.lookAt(v.pos);
			m_camera.setAngle(v.angle);
			m_camera.setPitch(v.pitch);
			m_camera.setHeightAboveGround(v.height);
		}
		return MessageDisposition::Destroy;
	}
	return MessageDisposition::Keep;
}

void LookAtTranslator::tick(unsigned nowMs)
{
	// RW 0x83B471
	++m_frame;
	m_nowMs = nowMs;
	const CameraSettings &gd = m_camera.gameData();
	float offX = 0.0f, offY = 0.0f;
	if (m_scrolling && !m_ctx.ui.isScrolling())
	{
		// forced to stop (a script): the amount is cleared and the scroll ends
		stopScrolling();
	}
	if (m_scrolling)
	{
		const ICoord2D size = m_camera.size();
		switch (m_scrollType)
		{
			case SCROLL_RMB:
			{
				const float dx = (float)(m_cur.x - m_anchor.x), dy = (float)(m_cur.y - m_anchor.y);
				offX = dx * gd.horizontalScrollSpeedFactor;
				offY = dy * gd.verticalScrollSpeedFactor;
				// Coord2D::normalize of the offset (RW 0x403312)
				float vx = offX, vy = offY;
				const float len = std::sqrt(vx * vx + vy * vy);
				if (len > 0.0f)
				{
					vx /= len;
					vy /= len;
				}
				offX = gd.horizontalScrollSpeedFactor * gd.keyboardScrollSpeedFactor * gd.keyboardScrollSpeedFactor * vx + offX;
				offY = gd.verticalScrollSpeedFactor * gd.keyboardScrollSpeedFactor * gd.keyboardScrollSpeedFactor * vy + offY;
				break;
			}
			case SCROLL_KEY:
			{
				const float hx = gd.keyboardScrollSpeedFactor * gd.horizontalScrollSpeedFactor * kKeyScroll;
				const float vy = gd.keyboardScrollSpeedFactor * gd.verticalScrollSpeedFactor * kKeyScroll;
				if (m_scrollKey[0])
				{
					offY -= vy;
				}
				if (m_scrollKey[1])
				{
					offY += vy;
				}
				if (m_scrollKey[2])
				{
					offX -= hx;
				}
				if (m_scrollKey[3])
				{
					offX += hx;
				}
				break;
			}
			case SCROLL_SCREENEDGE:
			{
				const int elapsed = (int)(nowMs - m_scrollStartMs);
				const int ramp = gd.screenEdgeScrollRampTimeMs;
				const int percent = (elapsed > ramp || ramp <= 0) ? 100 : elapsed * 100 / ramp;
				const float p = (float)percent;
				const float vy = gd.keyboardScrollSpeedFactor * gd.screenEdgeScrollSpeedFactor * gd.verticalScrollSpeedFactor * p;
				const float hx = gd.keyboardScrollSpeedFactor * gd.screenEdgeScrollSpeedFactor * gd.horizontalScrollSpeedFactor * p;
				if (m_cur.y < kEdge)
				{
					offY -= vy;
				}
				if (m_cur.y >= size.y - kEdge)
				{
					offY += vy;
				}
				if (m_cur.x < kEdge)
				{
					offX -= hx;
				}
				if (m_cur.x >= size.x - kEdge)
				{
					offX += hx;
				}
				break;
			}
			default:
				break;
		}
	}
	m_offsetX = offX;
	m_offsetY = offY;
	const int mask = m_camera.scrollBy(offX, offY);
	if (mask != 0 && m_scrolling && m_scrollType == SCROLL_RMB)
	{
		// RW 0x83B8BE: the view hit a map edge: the anchor follows the pointer on that axis (the axes swap when the camera is turned sideways)
		const float a = std::fabs(m_camera.getAngle());
		const bool sideways = a > 0.7853981852531433f && 2.356194496154785f > a; // RW 0xBE4388, 0xBE439C
		if (mask & 3)
		{
			if (sideways)
			{
				m_anchor.y = m_cur.y;
			}
			else
			{
				m_anchor.x = m_cur.x;
			}
		}
		if (mask & 0xC)
		{
			if (sideways)
			{
				m_anchor.x = m_cur.x;
			}
			else
			{
				m_anchor.y = m_cur.y;
			}
		}
	}
	// RW 0x83B94F: the numpad keys, while the input is enabled
	if (m_ctx.ui.getInputEnabled())
	{
		if (m_rotateLeft)
		{
			m_camera.setAngle(m_camera.getAngle() - gd.keyboardCameraRotateSpeed);
		}
		if (m_rotateRight)
		{
			m_camera.setAngle(m_camera.getAngle() + gd.keyboardCameraRotateSpeed);
		}
		if (m_zoomIn)
		{
			m_camera.zoomIn();
		}
		if (m_zoomOut)
		{
			m_camera.zoomOut();
		}
	}
}
