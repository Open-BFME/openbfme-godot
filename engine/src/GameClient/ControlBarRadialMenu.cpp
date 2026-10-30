// OpenBFME. GPL-3.0.
// See ControlBarRadialMenu.h (lane HUD-6). Client-only: nothing here reaches the simulation (the presses go through ControlBar::pressButton).

#include "GameClient/ControlBarRadialMenu.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioLog.h"

#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
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
const std::uint32_t kGrayOverlayColor = 0xFF909090u; // lane HUD-7: W3DPushButton.cpp grayOverlayColor GameMakeColor(144, 144, 144, 255)
const std::uint32_t kAlwaysColorMultiplier = 0xFF909090u; // lane HUD-7: the colour multiplier of a disabled WIN_STATUS_ALWAYS_COLOR window's image
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

bool ControlBarRadialMenu::mouseButton(bool left, bool down, int x, int y, ControlBar &bar, bool shift)
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
	// RW 0x72A027 (BFME2 Rva003285D4Input, tier A): left/right down, not release; a disabled cancelable right press uses the enabled cue.
	const bool cancelable = b && b->source.button && b->source.button->hasOption(COMMAND_OPTION_CANCELABLE);
	{
		AudioLog::Scope sound("HUD radial press");
		AudioApi::playUiSound(enabled || (!left && cancelable) ? "Gui_PalantirCommandButtonClick" : "Gui_PalantirCommandButtonDisabledClick");
	}
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
			bar.cancelQueued(slot, false, shift);
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
		// lane HUD-7, TARGET FACTS: the window statuses updateContextCommand sets per availability (RW 0x943C1F .. 0x943C7A, caveat S-001): 0 (Restricted)
		// WIN_STATUS_GRAY_OVERLAY 0x80000000, 5 (NotReady) WIN_STATUS_NOT_READY 0x400000, 6 / 7 (CantAfford) WIN_STATUS_ALWAYS_COLOR 0x01000000, 8 (Purchased)
		// both GRAY_OVERLAY and ALWAYS_COLOR; W3DGadgetPushButtonImageDrawOne (RW 0x4A5112, the BFME2 decomp's byte-matched W3DPushButton.cpp) draws a
		// disabled window's image in grayscale only without NOT_READY and without ALWAYS_COLOR, an ALWAYS_COLOR one in colour times 0xFF909090, and tints
		// the RadialBorder (144, 144, 144) with GRAY_OVERLAY: an upgrade being researched stays in dimmed colour with a grey rim
		const bool alwaysColor = s.state == ButtonState::CantAfford || s.state == ButtonState::Purchased;
		const bool grayOverlay = s.state == ButtonState::Restricted || s.state == ButtonState::Purchased;
		RadialDrawOp icon;
		icon.kind = RadialDrawOp::ICON;
		icon.image = s.image;
		icon.x = x;
		icon.y = y;
		icon.w = w;
		icon.h = h;
		icon.grayscale = !enabled && !notReady && !alwaysColor; // DRAW_IMAGE_RADIAL_GRAYSCALE
		icon.color = !enabled && !notReady && alwaysColor ? kAlwaysColorMultiplier : 0xFFFFFFFFu;
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
		// lane OWNERFIX-3: the count (RW 0x4A4CD3), after the clock and before the radial overlays (RW 0x4A5112's order)
		if (s.productionCount >= 1)
		{
			countOp(b, enabled);
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
		o.color = grayOverlay ? kGrayOverlayColor : kOverlayColor;
		o.slot = b.slot;
		m_ops.push_back(o);
	}
}

// lane OWNERFIX-3: Rva000A52AEDrawNumber (RW 0x4A4CD3; BFME2 decomp W3DPushButton.cpp, tier A) for the window of `b`
void ControlBarRadialMenu::countOp(const Button &b, bool enabled)
{
	if (!m_countLook.loaded || !m_metrics)
	{
		if (m_errors.size() < 20)
		{
			m_errors.push_back(std::string("radial count: ") + (m_countLook.loaded ? "no font metrics" : "ControlBar.wnd:ButtonCommand's font and text colours are not loaded"));
		}
		return;
	}
	RadialDrawOp t;
	t.kind = RadialDrawOp::TEXT;
	t.text = std::to_string(b.source.productionCount); // L"%d" (RW 0xBDF1B0)
	t.font = m_countLook.font;
	// getButtonTextColors (RW 0x4A43A2; decomp W3DPushButton.cpp): WIN_STATUS_ENABLED clear -> the disabled pair, WIN_STATE_HILITED -> the hilite pair
	if (!enabled)
	{
		t.color = m_countLook.disabled;
		t.dropColor = m_countLook.disabledBorder;
	}
	else if (m_hilite == b.slot)
	{
		t.color = m_countLook.hilite;
		t.dropColor = m_countLook.hiliteBorder;
	}
	else
	{
		t.color = m_countLook.enabled;
		t.dropColor = m_countLook.enabledBorder;
	}
	UnicodeString wide;
	for (char c : t.text)
	{
		wide.push_back((char16_t)c);
	}
	const int width = m_metrics->textWidth(t.font, wide), height = m_metrics->fontHeight(t.font);
	// Int textX = size.x * 0.5f - width * 0.5f + origin.x (a float sum truncated); Int textY = origin.y + size.y - height
	t.x = (float)(int)((float)b.rect.w * 0.5f - (float)width * 0.5f + (float)b.rect.x);
	t.y = (float)(b.rect.y + b.rect.h - height);
	t.w = (float)width;
	t.h = (float)height;
	t.slot = b.slot;
	m_ops.push_back(t);
}

namespace
{
// ZH File::scanString-style tokens of a .wnd value (the separators parseFont / parseTextColor give strtok: " ,\n\r\t" and ':' for the labels)
std::vector<std::string> wndTokens(const std::string &v)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : v)
	{
		if (std::strchr(" ,:\n\r\t\"", c))
		{
			if (!cur.empty())
			{
				out.push_back(cur);
				cur.clear();
			}
			continue;
		}
		cur.push_back(c);
	}
	if (!cur.empty())
	{
		out.push_back(cur);
	}
	return out;
}
bool wndInt(const std::string &t, int &v)
{
	char *end = nullptr;
	const long r = std::strtol(t.c_str(), &end, 10);
	if (t.empty() || !end || *end != 0)
	{
		return false;
	}
	v = (int)r;
	return true;
}
} // namespace

bool CommandWindowLook::scan(const std::string &text, CommandWindowLook &out, std::string *error)
{
	out = CommandWindowLook();
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = "ControlBar.wnd:ButtonCommand: " + why;
		}
		return false;
	};
	const size_t name = text.find("\"ControlBar.wnd:ButtonCommand\"");
	if (name == std::string::npos)
	{
		return fail("no such window");
	}
	// the window's own fields: up to its first CHILD / END / next WINDOW
	size_t end = text.size();
	for (const char *stop : { "CHILD", "END", "WINDOW" })
	{
		const size_t at = text.find(stop, name);
		end = at != std::string::npos && at < end ? at : end;
	}
	const std::string block = text.substr(name, end - name);
	auto field = [&](const char *key, std::string &value) {
		const size_t k = block.find(key);
		if (k == std::string::npos)
		{
			return false;
		}
		const size_t eq = block.find('=', k), semi = block.find(';', k);
		if (eq == std::string::npos || semi == std::string::npos || semi < eq)
		{
			return false;
		}
		value = block.substr(eq + 1, semi - eq - 1);
		return true;
	};
	std::string font, colors;
	if (!field("FONT", font))
	{
		return fail("no FONT");
	}
	if (!field("TEXTCOLOR", colors))
	{
		return fail("no TEXTCOLOR");
	}
	// FONT = NAME: "<name>", SIZE: <n>, BOLD: <n>;
	{
		const size_t q1 = font.find('"'), q2 = q1 == std::string::npos ? q1 : font.find('"', q1 + 1);
		if (q2 == std::string::npos)
		{
			return fail("FONT has no quoted NAME");
		}
		out.font.name = font.substr(q1 + 1, q2 - q1 - 1);
		const std::vector<std::string> t = wndTokens(font.substr(q2 + 1));
		int size = 0, bold = 0;
		if (t.size() != 4 || t[0] != "SIZE" || t[2] != "BOLD" || !wndInt(t[1], size) || !wndInt(t[3], bold))
		{
			return fail("FONT is not NAME: \"..\", SIZE: n, BOLD: n");
		}
		out.font.pointSize = size;
		out.font.bold = bold != 0;
	}
	// TEXTCOLOR = ENABLED: r g b a, ENABLEDBORDER: r g b a, DISABLED: ..., DISABLEDBORDER: ..., HILITE: ..., HILITEBORDER: ...;
	{
		const std::vector<std::string> t = wndTokens(colors);
		static const char *const labels[] = { "ENABLED", "ENABLEDBORDER", "DISABLED", "DISABLEDBORDER", "HILITE", "HILITEBORDER" };
		std::uint32_t *slots[] = { &out.enabled, &out.enabledBorder, &out.disabled, &out.disabledBorder, &out.hilite, &out.hiliteBorder };
		if (t.size() != 30)
		{
			return fail("TEXTCOLOR has " + std::to_string(t.size()) + " tokens, not six labelled r g b a colours");
		}
		for (int i = 0; i < 6; ++i)
		{
			int c[4];
			if (t[(size_t)i * 5] != labels[i])
			{
				return fail(std::string("TEXTCOLOR's colour ") + std::to_string(i + 1) + " is not " + labels[i]);
			}
			for (int k = 0; k < 4; ++k)
			{
				if (!wndInt(t[(size_t)i * 5 + 1 + (size_t)k], c[k]) || c[k] < 0 || c[k] > 255)
				{
					return fail(std::string("TEXTCOLOR ") + labels[i] + " has a bad component");
				}
			}
			*slots[i] = ((std::uint32_t)c[3] << 24) | ((std::uint32_t)c[0] << 16) | ((std::uint32_t)c[1] << 8) | (std::uint32_t)c[2]; // GameMakeColor
		}
	}
	out.loaded = true;
	return true;
}

std::vector<std::string> ControlBarRadialMenu::acceptanceStops()
{
	return {
		"[S-2701] radial command bubbles (HUD-6): ported RW 0x9459D1 / 0x9D6521 / 0x9D6320 / 0x9D5CCE / 0x9457FE (count, size, radius, ring, centre) and the "
		"look of W3DGadgetPushButtonImageDrawOne RW 0x4A5112; not identified: the clock colours (the control bar's clock colour, GlobalData + 0x1160 / + 0x1164), "
		"the 0x808080 multiplier condition (RW 0x327E0E's flag; lane HUD-7: the grey-overlay and always-colour statuses are RotWK's availability 0 / 6 / 7 / 8); not ported: the ring's button flash animations (RW 0x9D5BDC / "
		"0x9D6250); the right press cancels only a CANCELABLE button's queue (RW 0x940C70; an upgrade's RW 0x940E77 / 0x940FB3, lane FBSIM-1), the hit area is the window rectangle with inclusive edges (RW 0x715A84); INFERENCE: a point outside the view rectangle is off the frustum, the ring is drawn under the Palantir movie; radial icon fills ignore texture alpha inside the ellipse to retain their RGB backgrounds (target blend state unverified)"
	};
}
