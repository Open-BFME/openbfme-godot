// OpenBFME. GPL-3.0.
//
// ControlBarRadialMenu (lane HUD-6): RotWK's command "bubbles" over a selected structure, the round gold-rimmed buttons arranged in a ring above the building
// (a fortress's units, upgrades and powers; a hero-recruit building's portraits). Retail draws them as the control bar's own command windows (GameWindows of
// ControlBar.wnd with the RADIAL status), placed by a radial layout object the control bar keeps at + 0x2A8; this class is that object and the windows' look.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp is the donor: the RotWK code is tier A identical to BFME2 1.06 at every address below):
//   * which buttons: ControlBar populate RW 0x943D6F sets a command window's RADIAL status 0x4000000 (RW 0x7155AA) when its CommandButton is Radial
//     (+ 0x101) and clears it otherwise (RW 0x7155B9); at the end of the populate, when the radial object exists (RW 0x9443C2): if the context object is not
//     locally controlled (RW 0x68B749) while the local player is active (RW 0x6AAC52: + 0x35A and + 0x754 both 0) the ring is cleared (RW 0x945938);
//     otherwise the 33 windows go to RW 0x9459D1, which keeps the visible ones with the RADIAL status in window order (the others are parked at (-100, -100),
//     size 1 x 1) and, when that list changed, restarts the layout with the new count (secondarySetCount RW 0x9D6521: centre (-500, -500), radius 0);
//   * the object: ControlBar::switchToContext (RW 0x71D8BE) hands the context drawable's object to the ring (RW 0x94564C: its id, + 0x58 -> + 0x54);
//   * the centre (vslot 0x10, RW 0x9457FE): no object, or a template of KindOf DOZER (+ 0x109 bit 0x40), gives (-999, -999) (the builder's buttons are on the
//     side bar instead, AptPalantirSideBar.cpp); else the drawable's position (the object's without a drawable) moved by the object's worldspace best contact
//     point "Menu" (RW 0x690BD2 with the origin, not preferred; the point minus the object's position), or without that point raised by half the geometry's
//     max height (RW 0xAD1920 * 0.5f); projected by the view (vslot 0x160); a point off the view gives (-999, -999). The ring follows the centre only when it
//     moved by more than 2 pixels (|dx| + |dy| > 2, RW 0x9D5B77);
//   * the update (RW 0x9D6320, the first after a restart): the button size is ftol(display width * 0.046875) x ftol(display height * 0.0625) (constants
//     RW 0xC8C990 / 0xBDD39C: 48 x 48 at 1024 x 768); the radius for n > 2 buttons is ftol(max(ftol(w * 0.5 * 1.175 / sin(pi / n)), w * 0.6666667)), for n <= 2
//     ftol(w * 0.6666667) (RW 0xBD869C 0.5, 0xC8C95C 1.175, 0xBDD388 pi, 0xBDAD60 0.6666667); the step angle is 2 pi / n (RW 0xBDD38C); the clock runs on
//     timeGetTime while the game is not paused (RW 0xDE7890 + 0x5C);
//   * the placement (RW 0x9D5CCE): one button sits on the centre; from two on the first is straight above the centre (direction (0, -1)) and each next one
//     turns by the step angle (sin / cos of the float angle, the turn in the binary's order: y' = y cos + s x on the FPU, x' = c x - y s in SSE), clockwise on
//     the screen; button i is centred on trunc(centre + direction * radius) (radius' = (radius - 0.1) * 1 + 0.1, RW 0xBD83D4). The spin-in of the directions
//     (1 - elapsed / [RW 0xDEC0C4] radians) never runs: that float is never written (zero in .bss), so the turn is 0 and the input of every button is enabled;
//   * the look (W3DGadgetPushButtonImageDrawOne RW 0x4A5112, byte-matched in the decomp, W3DPushButton.cpp): the button image clipped to the ellipse of the
//     window (the display's ellipse stencil, the decomp's rva000A47DE), grey (the radial grayscale mode) when the window is disabled and not "not ready"; then over it, by state: RadialPush when the
//     enabled window is hilited and selected (pressed), RadialOver when it is hilited, else RadialBorder (white, or (144, 144, 144) with the grey-overlay status);
//     an overlay is centred on the button and scaled by its image size / 48 (drawRadialOverlay); a recharge or build clock draws RadialClockOverlay1 /
//     RadialClockOverlay2 wiped by the percent, 46/48 of the button (RADIAL_CLOCK_SCALE); no cameo flash and no border colour for a radial button;
//   * the input: the command windows are ON_MOUSE_DOWN push buttons (ControlBar.wnd ButtonCommand): the left press is the button's command
//     (ControlBar::processCommandUI).
// INFERENCE (stop S-2701, InGameHud reports it): the clock's colours (GlobalData + 0x1160 / + 0x1164 and the control bar's clock colour) are not identified (a
// translucent black wipe here); the button flash animations of the ring (RW 0x9D5BDC / 0x9D6250 for windows whose CommandButton + 0xF8 > 0) are not drawn;
// the right press cancels only a CANCELABLE button's queue (RW 0x940C70); the hit area is inclusive (RW 0x715A84); the grey-overlay status and the 0x808080 multiplier condition
// (RW 0x327E0E's flag) are not set; the ring is drawn under the Palantir movie.

#pragma once

#include "GameClient/ControlBar.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <string>
#include <vector>

class DrawableManager;
class GameLogic;
class TacticalView;

// The radial layout (RW 0x9D62D1 .. 0x9D6521; the decomp's Rva005C6D4D): count, centre, button size, radius, step angle and the placement.
class RadialMenuLayout
{
public:
	struct Rect
	{
		int x = 0, y = 0, w = 0, h = 0;
	};
	// secondarySetCount RW 0x9D6521: a new count restarts the layout (centre (-500, -500), size, radius and clock 0)
	void setCount(int count);
	// RW 0x9D620D: no object (count 0, everything cleared)
	void reset() { setCount(0); m_count = 0; }
	// RW 0x9D5B77: the centre follows `p` only when it moved by more than 2 pixels; true when it did
	bool updateCenter(int x, int y);
	// RW 0x9D6320's first update: the button size and the radius from the display size (window pixels)
	void update(int displayW, int displayH);
	// RW 0x9D5CCE: the window rectangle of each button (window pixels)
	void place(std::vector<Rect> &out) const;

	int count() const { return m_count; }
	int centerX() const { return m_cx; }
	int centerY() const { return m_cy; }
	int buttonW() const { return m_w; }
	int buttonH() const { return m_h; }
	int radius() const { return m_radius; }

private:
	int m_count = 0;
	int m_cx = -500, m_cy = -500; ///< + 0xC / + 0x10
	int m_w = 0, m_h = 0;          ///< + 0x14 / + 0x18
	int m_radius = 0;              ///< + 0x1C (0: the next update measures)
	float m_step = 0.0f;           ///< + 0x2C
};

// One thing to draw for the ring, in order (window pixels).
struct RadialDrawOp
{
	enum Kind
	{
		ICON,    ///< the button image in the ellipse of (x, y, w, h); `grayscale` for a disabled window
		OVERLAY, ///< a mapped image over (x, y, w, h), tinted by `color` (ARGB)
		CLOCK    ///< a mapped image wiped clockwise from the top by `percent` (0 .. 100) over (x, y, w, h), tinted by `color`
	};
	Kind kind = ICON;
	std::string image;
	float x = 0, y = 0, w = 0, h = 0;
	bool grayscale = false;
	std::uint32_t color = 0xFFFFFFFFu;
	float percent = 0.0f;
	int slot = -1; ///< the command set slot of the button
};

class ControlBarRadialMenu
{
public:
	// the size of a mapped image in its texture's pixels (width, height); false when the collection has no such image (the overlays need RadialBorder, ...)
	typedef bool (*ImageSizeFn)(void *ctx, const std::string &name, int &w, int &h);

	// Once per HUD update: the context object (RW 0x94564C), the buttons the control bar shows on the side bar (the visible Radial ones in window order:
	// RW 0x9459D1 keeps exactly those), whether the ring may show them (RW 0x9443CF: the object is the local player's, or the local player is not active),
	// and the view to project with. Builds the button rectangles and the draw ops.
	void update(GameLogic &logic, DrawableManager *drawables, const TacticalView &view, int displayW, int displayH, ObjectID context,
	            const std::vector<ControlBarButton> &radialButtons, bool allowed);
	void setImageSize(ImageSizeFn fn, void *ctx) { m_imageSize = fn; m_imageCtx = ctx; }

	// ---- input (window pixels) ----
	// the slot of the button under the pixel (the button's ellipse: retail's window rectangle is the hit area of a push button; INFERENCE: the rectangle),
	// -1 none
	int hit(int x, int y) const;
	void mouseMove(int x, int y);
	// a press / release: true when the ring took it (the world gets nothing); `press` is called for the left press on an enabled button, `cancel` for the right
	bool mouseButton(bool left, bool down, int x, int y, ControlBar &bar);

	const std::vector<RadialDrawOp> &ops() const { return m_ops; }
	struct Button
	{
		int slot = -1;
		RadialMenuLayout::Rect rect;
		ControlBarButton source;
	};
	const std::vector<Button> &buttons() const { return m_buttons; }
	const RadialMenuLayout &layout() const { return m_layout; }
	ObjectID object() const { return m_object; }
	int hilitedSlot() const { return m_hilite; }
	unsigned presses() const { return m_presses; }
	unsigned cancels() const { return m_cancels; } ///< right presses that sent a cancel (CANCELABLE buttons)
	const std::vector<std::string> &errors() const { return m_errors; }
	static std::vector<std::string> acceptanceStops();

	// RW 0x9457FE: the screen point the ring is centred on ((-999, -999) when none)
	static void centerOf(GameLogic &logic, DrawableManager *drawables, const TacticalView &view, ObjectID id, int &x, int &y);

private:
	void buildOps();
	RadialMenuLayout m_layout;
	ObjectID m_object = INVALID_ID;
	std::vector<int> m_keys; ///< the windows (slots) of the last list (RW 0x9459D1 compares the lists)
	std::vector<Button> m_buttons;
	std::vector<RadialDrawOp> m_ops;
	ImageSizeFn m_imageSize = nullptr;
	void *m_imageCtx = nullptr;
	int m_mouseX = -10000, m_mouseY = -10000;
	int m_hilite = -1;
	int m_pushed = -1; ///< WIN_STATE_SELECTED: the button the left press went down on, while it is held
	bool m_pressTaken = false; ///< the ring took the last press: its release is the ring's too
	unsigned m_presses = 0, m_cancels = 0;
	std::vector<std::string> m_errors;
};
