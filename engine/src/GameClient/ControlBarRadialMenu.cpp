// OpenBFME. GPL-3.0.
// See ControlBarRadialMenu.h (lane HUD-6). Client-only: nothing here reaches the simulation (the presses go through ControlBar::pressButton).

#include "GameClient/ControlBarRadialMenu.h"

#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace
{
// RW constants (float32 in .rdata)
const float kButtonWidthScale = 0.046875f;  // RW 0xC8C990
const float kButtonHeightScale = 0.0625f;   // RW 0xBDD39C
const float kHalf = 0.5f;                   // RW 0xBD869C
const float kRadiusPad = 1.175f;            // RW 0xC8C95C
const float kPi = 3.14159274f;              // RW 0xBDD388
const float kTwoPi = 6.28318548f;           // RW 0xBDD38C
const float kMinRadius = 0.6666667f;        // RW 0xBDAD60
const float kRadiusBias = 0.1f;             // RW 0xBD83D4
const float kClockScale = 46.0f / 48.0f;    // W3DPushButton.cpp RADIAL_CLOCK_SCALE
const int kOffView = -999;                  // RW 0x9458FE (0xFFFFFC19)
// INFERENCE (S-2701): the clock colours (the control bar's clock colour and GlobalData + 0x1160 / + 0x1164) are not identified
const std::uint32_t kClockColor = 0x80000000u, kClockRimColor = 0xFFFFFFFFu;
const std::uint32_t kOverlayColor = 0xFFFFFFFFu; // W3DPushButton.cpp overlayColor GameMakeColor(255, 255, 255, 255)
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// RadialMenuLayout
// ---------------------------------------------------------------------------------------------------------------------------------
void RadialMenuLayout::setCount(int count)
{
	// secondarySetCount RW 0x9D6521: + 8 count, + 4 the position flag, the centre -500, the size, radius, clock and step cleared (+ 0x14 .. + 0x2C)
	m_count = count;
	m_cx = m_cy = -500;
	m_w = m_h = 0;
	m_radius = 0;
	m_step = 0.0f;
}

bool RadialMenuLayout::updateCenter(int x, int y)
{
	// RW 0x9D5B77: abs(+ 0x10 - y) + abs(+ 0xC - x) > 2 moves the centre (and sets the position flag)
	if (std::abs(m_cy - y) + std::abs(m_cx - x) > 2)
	{
		m_cx = x;
		m_cy = y;
		return true;
	}
	return false;
}

void RadialMenuLayout::update(int displayW, int displayH)
{
	if (m_count < 1 || m_radius != 0)
	{
		return; // RW 0x9D6335 (no buttons) / RW 0x9D634B (measured since the restart)
	}
	// RW 0x9D6356 .. 0x9D63A0: fild width; fmul 0.046875f; __ftol2 (truncation) - the same for the height with 0.0625f
	m_w = (int)((float)displayW * kButtonWidthScale);
	m_h = (int)((float)displayH * kButtonHeightScale);
	if (m_count > 2)
	{
		// RW 0x9D63B6 .. 0x9D641D: (w * 0.5f * 1.175f stored as float) / sin(pi / n) (the quotient under the 24-bit FPU precision), truncated; at least
		// w * 0.6666667f (SSE), truncated again
		const float a = (float)((double)m_w * (double)kHalf * (double)kRadiusPad);
		const float angle = kPi / (float)m_count;
		const int r = (int)(float)((double)a / std::sin((double)angle));
		const float fr = (float)r;
		const float m = (float)m_w * kMinRadius;
		m_radius = (int)(fr > m ? fr : m);
	}
	else
	{
		m_radius = (int)((float)m_w * kMinRadius); // RW 0x9D6422
	}
	m_step = kTwoPi / (float)m_count; // RW 0x9D642D
}

void RadialMenuLayout::place(std::vector<Rect> &out) const
{
	out.clear();
	if (m_count < 1)
	{
		return;
	}
	// RW 0x9D5CF2: the first direction (0, -1), or (0, 0) for a single button; the spin-in turn is 1 - 1 = 0 radians ([RW 0xDEC0C4] is 0: t = 1)
	float dirX = 0.0f;
	float dirY = (float)((m_count <= 1 ? 1 : 0) - 1);
	const float rr = ((float)m_radius - kRadiusBias) * 1.0f + kRadiusBias; // RW 0x9D5E25 .. 0x9D5E6D (+ 0x28 is 1.0f)
	const int bw = (int)(((float)m_w - 1.0f) * 1.0f + 1.0f);               // RW 0x9D5DF6 .. 0x9D5E0C (min(+ 0x28, 1) = 1)
	const int bh = (int)(((float)m_h - 1.0f) * 1.0f + 1.0f);
	const float c = (float)std::cos((double)m_step), s = (float)std::sin((double)m_step);
	for (int i = 0; i < m_count; ++i)
	{
		const int x = (int)((float)m_cx + dirX * rr);
		const int y = (int)((float)m_cy + dirY * rr);
		Rect r;
		r.x = x - bw / 2; // RW 0x9D5ED8: cdq; sub; sar (division toward zero)
		r.y = y - bh / 2;
		r.w = bw;
		r.h = bh;
		out.push_back(r);
		// RW 0x9D5F35 .. 0x9D5F65: both from the old direction
		const float ny = dirY * c + s * dirX;
		const float nx = c * dirX - dirY * s;
		dirX = nx;
		dirY = ny;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// ControlBarRadialMenu
// ---------------------------------------------------------------------------------------------------------------------------------
void ControlBarRadialMenu::centerOf(GameLogic &logic, DrawableManager *drawables, const TacticalView &view, ObjectID id, int &x, int &y)
{
	x = y = kOffView;
	Object *obj = id != INVALID_ID ? logic.findObjectByID(id) : nullptr;
	if (!obj || !obj->getTemplate() || obj->isKindOfName("DOZER"))
	{
		return; // RW 0x945815 / 0x94581D (template + 0x109 bit 0x40)
	}
	const Drawable *d = drawables ? drawables->findByObject(id) : nullptr;
	Coord3D pos = d ? *d->getPosition() : *obj->getPosition(); // RW 0x94582F .. 0x945845
	Coord3D menu;
	bool haveMenu = false;
	try
	{
		haveMenu = ObjectGeometry::worldspaceBestContactPoint(*obj, Coord3D{ 0.0f, 0.0f, 0.0f }, "Menu", false, menu); // RW 0x945871 (string RW 0xC8049C)
	}
	catch (const std::logic_error &)
	{
		haveMenu = false; // a template whose contact point rows do not parse has no "Menu" point (its parse error is the INI's report)
	}
	if (haveMenu)
	{
		const Coord3D *op = obj->getPosition(); // RW 0x94587C .. 0x9458C6
		pos.x += menu.x - op->x;
		pos.y += menu.y - op->y;
		pos.z += menu.z - op->z;
	}
	else
	{
		float height = 0.0f;
		try
		{
			height = ObjectGeometry::maxHeightAbovePosition(ObjectGeometry::shapesOf(*obj->getTemplate())); // RW 0x9458CD (GeometryInfo at + 0xA8)
		}
		catch (const std::logic_error &)
		{
			height = 0.0f;
		}
		pos.z += height * kHalf;
	}
	ICoord2D screen{ 0, 0 };
	const ICoord2D size = view.size();
	// vslot 0x160 (W3DView::worldToScreenTriReturn): only a point inside the view frustum projects. INFERENCE: the frustum test is the view rectangle here
	if (!view.worldToScreen(pos, screen) || screen.x < 0 || screen.y < 0 || screen.x >= size.x || screen.y >= size.y)
	{
		return;
	}
	x = screen.x;
	y = screen.y;
}

void ControlBarRadialMenu::update(GameLogic &logic, DrawableManager *drawables, const TacticalView &view, int displayW, int displayH, ObjectID context,
                                  const std::vector<ControlBarButton> &radialButtons, bool allowed)
{
	// RW 0x94564C: the object the next update reads (+ 0x58), taken over by the update (RW 0x945985: + 0x54 = + 0x58)
	if (context != m_object)
	{
		m_object = context;
		m_keys.clear();
		m_layout.reset();
	}
	std::vector<int> keys;
	if (allowed && m_object != INVALID_ID)
	{
		for (const ControlBarButton &b : radialButtons)
		{
			keys.push_back(b.slot);
		}
	}
	if (keys != m_keys) // RW 0x9459D1: a changed list restarts the layout
	{
		m_keys = keys;
		m_layout.setCount((int)keys.size());
		m_pushed = -1;
	}
	m_buttons.clear();
	m_ops.clear();
	if (m_layout.count() == 0)
	{
		m_hilite = -1;
		return;
	}
	int cx = 0, cy = 0;
	centerOf(logic, drawables, view, m_object, cx, cy); // vslot 0x14 -> RW 0x9D5B77
	m_layout.updateCenter(cx, cy);
	m_layout.update(displayW, displayH);
	std::vector<RadialMenuLayout::Rect> rects;
	m_layout.place(rects);
	for (size_t i = 0; i < rects.size() && i < radialButtons.size(); ++i)
	{
		Button b;
		b.slot = radialButtons[i].slot;
		b.rect = rects[i];
		b.source = radialButtons[i];
		m_buttons.push_back(b);
	}
	m_hilite = hit(m_mouseX, m_mouseY);
	buildOps();
}

int ControlBarRadialMenu::hit(int x, int y) const
{
	for (const Button &b : m_buttons)
	{
		// RW 0x715A84 (winPointInChild, byte-matched in the decomp's GameWindowCursorSearch.cpp): both edges inclusive, lo <= p <= lo + size
		if (x >= b.rect.x && y >= b.rect.y && x <= b.rect.x + b.rect.w && y <= b.rect.y + b.rect.h)
		{
			return b.slot;
		}
	}
	return -1;
}

void ControlBarRadialMenu::mouseMove(int x, int y)
{
	m_mouseX = x;
	m_mouseY = y;
	m_hilite = hit(x, y);
	if (m_pushed >= 0 && m_hilite != m_pushed)
	{
		m_pushed = -1; // a push button loses its selected state when the pointer leaves it
	}
	buildOps();
}

bool ControlBarRadialMenu::mouseButton(bool left, bool down, int x, int y, ControlBar &bar)
{
	m_mouseX = x;
	m_mouseY = y;
	const int slot = hit(x, y);
	if (!down)
	{
		const bool had = m_pressTaken;
		m_pressTaken = false;
		m_pushed = -1;
		buildOps();
		return had;
	}
	if (slot < 0)
	{
		return false;
	}
	m_pressTaken = true;
	const Button *b = nullptr;
	for (const Button &c : m_buttons)
	{
		if (c.slot == slot)
		{
			b = &c;
		}
	}
	const bool enabled = b && (b->source.state == ButtonState::Enabled || b->source.state == ButtonState::Active);
	if (left)
	{
		if (enabled)
		{
			m_pushed = slot;
			++m_presses;
			bar.pressButton(slot, false); // ON_MOUSE_DOWN: the press is the command (ControlBar::processCommandUI)
		}
	}
	else
	{
		// RW 0x940C59 .. 0x940C70 (processCommandUI, the right press of a UNIT_BUILD / REVIVE button) and RW 0x940FB3 (an upgrade): the cancel is sent
		// only for a CANCELABLE button (CommandButton + 0x1F & 0x80) of the local player's object; the window takes the press either way
		if (b && b->source.button && b->source.button->hasOption(COMMAND_OPTION_CANCELABLE))
		{
			bar.cancelQueued(slot, false);
			++m_cancels;
		}
	}
	buildOps();
	return true;
}

void ControlBarRadialMenu::buildOps()
{
	m_ops.clear();
	for (const Button &b : m_buttons)
	{
		const ControlBarButton &s = b.source;
		const float x = (float)b.rect.x, y = (float)b.rect.y, w = (float)b.rect.w, h = (float)b.rect.h;
		if (b.rect.x + b.rect.w < 0 || b.rect.y + b.rect.h < 0)
		{
			continue; // the ring of a DOZER or of an object off the view sits at (-999, -999): nothing of it is on screen
		}
		const bool enabled = s.state == ButtonState::Enabled || s.state == ButtonState::Active;
		const bool notReady = s.state == ButtonState::NotReady;
		RadialDrawOp icon;
		icon.kind = RadialDrawOp::ICON;
		icon.image = s.image;
		icon.x = x;
		icon.y = y;
		icon.w = w;
		icon.h = h;
		icon.grayscale = !enabled && !notReady; // the disabled window without the not-ready status: DRAW_IMAGE_RADIAL_GRAYSCALE
		icon.slot = b.slot;
		if (!s.image.empty())
		{
			m_ops.push_back(icon);
		}
		if (s.timer >= 0.0f && s.timer < 1.0f)
		{
			// INVERSE_CLOCK with a percent below 100: RadialClockOverlay1 / 2 over 46/48 of the button, centred
			const float rx = w * 0.5f * kClockScale, ry = h * 0.5f * kClockScale;
			const float mx = x + w * 0.5f, my = y + h * 0.5f;
			for (int k = 0; k < 2; ++k)
			{
				RadialDrawOp c;
				c.kind = RadialDrawOp::CLOCK;
				c.image = k == 0 ? "RadialClockOverlay1" : "RadialClockOverlay2";
				c.x = mx - rx;
				c.y = my - ry;
				c.w = rx * 2.0f;
				c.h = ry * 2.0f;
				c.percent = s.timer * 100.0f;
				c.color = k == 0 ? kClockColor : kClockRimColor;
				c.slot = b.slot;
				m_ops.push_back(c);
			}
		}
		std::string overlay = "RadialBorder";
		if (enabled && m_hilite == b.slot)
		{
			overlay = m_pushed == b.slot ? "RadialPush" : "RadialOver";
		}
		// drawRadialOverlay: centred, the half extent scaled by the image's size / 48
		int iw = 48, ih = 48;
		if (!m_imageSize || !m_imageSize(m_imageCtx, overlay, iw, ih))
		{
			if (m_errors.size() < 20)
			{
				m_errors.push_back("radial overlay " + overlay + ": no such mapped image");
			}
			continue;
		}
		const float rx = w * 0.5f * (float)iw / 48.0f, ry = h * 0.5f * (float)ih / 48.0f;
		RadialDrawOp o;
		o.kind = RadialDrawOp::OVERLAY;
		o.image = overlay;
		o.x = x + w * 0.5f - rx;
		o.y = y + h * 0.5f - ry;
		o.w = rx * 2.0f;
		o.h = ry * 2.0f;
		o.color = kOverlayColor;
		o.slot = b.slot;
		m_ops.push_back(o);
	}
}

std::vector<std::string> ControlBarRadialMenu::acceptanceStops()
{
	return {
		"[S-2701] radial command bubbles (HUD-6): ported RW 0x9459D1 / 0x9D6521 / 0x9D6320 / 0x9D5CCE / 0x9457FE (count, size, radius, ring, centre) and the "
		"look of W3DGadgetPushButtonImageDrawOne RW 0x4A5112; not identified: the clock colours (the control bar's clock colour, GlobalData + 0x1160 / + 0x1164), "
		"the grey-overlay status and the 0x808080 multiplier condition (RW 0x327E0E's flag); not ported: the ring's button flash animations (RW 0x9D5BDC / "
		"0x9D6250), the queue count number (drawNumber); the right press cancels only a CANCELABLE button's queue (RW 0x940C70; upgrade cancels not ported: "
		"ControlBar::cancelQueued is UNIT_BUILD only), the hit area is the window rectangle with inclusive edges (RW 0x715A84); INFERENCE: a point outside the view rectangle is off the frustum, the ring is drawn under the Palantir movie"
	};
}
