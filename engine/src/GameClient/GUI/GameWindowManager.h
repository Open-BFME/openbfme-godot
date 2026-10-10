// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The window manager of the native GUI (ZH GameClient/GameWindowManager.h, GUI/GameWindowManager.cpp; BFME1 decompile
// GameWindowManager*.cpp) as far as the APT gadgets need it: the window tree and its lists, system / input message delivery, focus and
// mouse capture, the open-combo-box "lone window", the mouse event routing (winProcessMouseEvent: captor, grab window, modal, the
// ABOVE / normal / BELOW passes, enter / leave), key routing, the repaint, the gogoGadget* creators and the draw primitives.
// It is a core class with no device: it does not own a clock (the owner passes the time), a font library (GameFont objects it makes up
// from a request, heights from the FontMetricsSource) or images (MappedImageCollection, names only).
//
// Not ported (stops S-176, S-177): modal windows (winSetModal and the modal head of the mouse router are kept; message boxes are not),
// tab navigation (winNextTab / winPrevTab are accepted and do nothing: the tab list is registered by WND screens, not by APT), the
// window layout transition handler, the IME manager, tooltips of native windows (the APT tooltip path replaces them), the push
// button clock / overlay / three-slice draws, radio buttons, tab controls, progress bars and static text (no APT screen of the
// RotWK skirmish path creates them).

#pragma once

#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/Image.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

struct ListboxData;
struct SliderData;
struct EntryData;
struct ComboBoxData;

// Font geometry is a device fact (the retail fonts are Windows GDI fonts rasterised by the W3D font library).  The renderer provides the
// real metrics; the headless default (DefaultFontMetrics) is a stand-in with fixed ratios, reported as stop S-176.
class FontMetricsSource
{
public:
	virtual ~FontMetricsSource() = default;
	virtual int fontHeight(const GameFont &font) = 0;
	virtual int textWidth(const GameFont &font, const UnicodeString &text) = 0;
	// Height of `text` wrapped to `wrapWidth` (0 = one line); one line is fontHeight.
	virtual int wrappedHeight(const GameFont &font, const UnicodeString &text, int wrapWidth) = 0;
};

class DefaultFontMetrics : public FontMetricsSource
{
public:
	int fontHeight(const GameFont &font) override { return font.pointSize + 4; }
	int textWidth(const GameFont &font, const UnicodeString &text) override { return (int)text.size() * (font.pointSize * 6 / 10 + 1); }
	int wrappedHeight(const GameFont &font, const UnicodeString &text, int wrapWidth) override;
};

enum
{
	WIN_DRAW_LINE_WIDTH = 1,
	WIN_STACK_DEPTH = 10
};

class GameWindowManager
{
	friend class GameWindow;

public:
	GameWindowManager();
	~GameWindowManager();

	GameWindowManager(const GameWindowManager &) = delete;
	GameWindowManager &operator=(const GameWindowManager &) = delete;

	// ---- services the manager asks its owner for ------------------------------------------------------------------------------------
	void setFontMetrics(FontMetricsSource *metrics) { m_metrics = metrics ? metrics : &m_defaultMetrics; }
	FontMetricsSource &fontMetrics() { return *m_metrics; }
	void setImages(MappedImageCollection *images) { m_images = images; }
	const MappedImageCollection *images() const { return m_images; }
	// lane UI-2: images the engine makes at run time (the lobby's map preview, RW 0x70292F adds it to TheMappedImageCollection)
	MappedImageCollection *mutableImages() { return m_images; }
	// The time of the current update in milliseconds (ZH timeGetTime(): double clicks); the owner advances it.
	void setTimeMs(std::uint32_t ms) { m_timeMs = ms; }
	std::uint32_t timeMs() const { return m_timeMs; }
	void setDisplaySize(int width, int height)
	{
		m_displayWidth = width;
		m_displayHeight = height;
	}
	int displayWidth() const { return m_displayWidth; }
	int displayHeight() const { return m_displayHeight; }
	// ZH GameWindowManager::sendMousePosMessages (a static TRUE, GameWindowManager.cpp:78): GWM_MOUSE_POS reaches the windows only when true.
	// Lane FB7-1: true by default. INFERENCE (S-1916): RotWK's window manager code never tests a message against 0x18 (the test is folded away) and its
	// list box input handles 0x18 (RW 0x727081), so the windows get it
	void setSendMousePosMessages(bool on) { m_sendMousePosMessages = on; }

	// ---- fonts -------------------------------------------------------------------------------------------------------------------------
	GameFont *winFindFont(const std::string &name, int pointSize, bool bold);
	int winFontHeight(GameFont *font);
	int winTextWidth(GameFont *font, const UnicodeString &text);
	int winWrappedHeight(GameFont *font, const UnicodeString &text, int wrapWidth);

	// ---- tree ----------------------------------------------------------------------------------------------------------------------------
	GameWindow *winCreate(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, GameWinSystemFunc system, WinInstanceData *instData = nullptr);
	int winDestroy(GameWindow *window);
	int winDestroyAll();
	void processDestroyList();
	GameWindow *winGetWindowList() { return m_windowList; }
	GameWindow *winGetWindowFromId(GameWindow *window, int id);
	int windowCount() const { return (int)m_owned.size(); }
	// lane WINCRASH-1 (tests): is `window` a window of this manager that is not destroyed or waiting for destruction?
	bool winIsAlive(const GameWindow *window) const;
	void linkWindow(GameWindow *window);
	void unlinkWindow(GameWindow *window);
	void unlinkChildWindow(GameWindow *window);
	void addWindowToParent(GameWindow *window, GameWindow *parent);
	void addWindowToParentAtEnd(GameWindow *window, GameWindow *parent);
	void insertWindowAheadOf(GameWindow *window, GameWindow *aheadOf);
	bool isEnabled(GameWindow *win);
	bool isHidden(GameWindow *win);
	void windowHiding(GameWindow *window);
	void hideWindowsInRange(GameWindow *baseWindow, int first, int last, bool hideFlag);
	void enableWindowsInRange(GameWindow *baseWindow, int first, int last, bool enableFlag);

	// ---- messages ---------------------------------------------------------------------------------------------------------------------
	WindowMsgHandledType winSendSystemMsg(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
	WindowMsgHandledType winSendInputMsg(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);

	// ---- focus, capture, lone window ------------------------------------------------------------------------------------------------------
	int winCapture(GameWindow *window);
	int winRelease(GameWindow *window);
	GameWindow *winGetCapture() { return m_mouseCaptor; }
	GameWindow *winGetFocus() { return m_keyboardFocus; }
	int winSetFocus(GameWindow *window);
	int winSetModal(GameWindow *window);
	int winUnsetModal(GameWindow *window);
	GameWindow *winGetGrabWindow() { return m_grabWindow; }
	void winSetGrabWindow(GameWindow *window) { m_grabWindow = window; }
	// The open combo box: a click anywhere outside its parts closes its list (ZH winSetLoneWindow / winProcessMouseEvent tail).
	void winSetLoneWindow(GameWindow *window);
	GameWindow *winGetLoneWindow() { return m_loneWindow; }
	void winNextTab(GameWindow *) {}
	void winPrevTab(GameWindow *) {}

	// ---- input ------------------------------------------------------------------------------------------------------------------------------
	// `mousePos` in stage pixels; `data` is the ICoord2D delta of a drag or the int wheel position (nullptr otherwise).  Returns
	// WIN_INPUT_USED when a window took the event.
	WinInputReturnCode winProcessMouseEvent(GameWindowMessage msg, ICoord2D *mousePos, void *data);
	WinInputReturnCode winProcessKey(std::uint8_t key, std::uint8_t state);
	// The text path that ZH routes through the IME manager as GWM_IME_CHAR to the window with the keyboard focus.
	WinInputReturnCode winProcessChar(char16_t ch);
	GameWindow *getWindowUnderCursor(int x, int y, bool ignoreEnabled);
	GameWindow *winGetCurrentMouseRegion() { return m_currMouseRgn; }

	// ---- drawing -----------------------------------------------------------------------------------------------------------------------------
	// Repaint every window into `out` (ZH winRepaint: BELOW windows, normal windows, ABOVE windows, each from the tail of the list).
	void winRepaint(GadgetDrawList &out);
	int drawWindow(GameWindow *window);
	// Draw primitives for the gadget draw functions (valid while a repaint runs).
	void winDrawImage(const std::string &image, int startX, int startY, int endX, int endY, Color color = 0xFFFFFFFFu);
	void winFillRect(Color color, float width, int startX, int startY, int endX, int endY);
	void winOpenRect(Color color, float width, int startX, int startY, int endX, int endY);
	void winDrawLine(Color color, float width, int startX, int startY, int endX, int endY);
	void winDrawText(const UnicodeString &text, GameFont *font, int x, int y, Color color, Color dropColor, int wrapWidth = 0, bool wrapCentered = false);
	void setClipRegion(const IRegion2D &region);
	void enableClipping(bool on);
	// The size of a mapped image; false (and an unresolved note in the list being drawn) when it is not known.
	bool imageSize(const std::string &image, int &width, int &height);
	// The window whose draw function is running (the `window` of the commands).
	GameWindow *currentDrawWindow() { return m_drawWindow; }

	// ---- gadget creation (ZH GameWindowManager::gogoGadget*) -----------------------------------------------------------------------------------------
	GameWindow *gogoGadgetPushButton(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, GameFont *defaultFont, bool defaultVisual);
	GameWindow *gogoGadgetCheckbox(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, GameFont *defaultFont, bool defaultVisual);
	GameWindow *gogoGadgetListBox(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, ListboxData *listboxDataTemplate, GameFont *defaultFont, bool defaultVisual);
	GameWindow *gogoGadgetSlider(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, SliderData *sliderData, GameFont *defaultFont, bool defaultVisual);
	GameWindow *gogoGadgetComboBox(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, ComboBoxData *comboBoxDataTemplate, GameFont *defaultFont, bool defaultVisual);
	GameWindow *gogoGadgetTextEntry(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, EntryData *entryData, GameFont *defaultFont, bool defaultVisual);
	// ZH assignDefaultGadgetLook with assignVisual FALSE (every script-created gadget): only the font.  [S-177] the visual defaults of
	// assignVisual TRUE are not ported: the sub-gadgets of the list and combo boxes are skinned from the WND draw data right after creation.
	void assignDefaultGadgetLook(GameWindow *gadget, GameFont *defaultFont, bool assignVisual);

	// Text label -> text (ZH winTextLabelToText through TheGameText): the owner's resolver; unresolved labels are returned as the label
	// itself and recorded.
	typedef std::function<bool(const std::string &label, UnicodeString &out)> TextResolver;
	void setTextResolver(TextResolver resolver) { m_textResolver = std::move(resolver); }
	UnicodeString winTextLabelToText(const std::string &label);
	const std::vector<std::string> &unresolvedLabels() const { return m_unresolvedLabels; }

	// Reasons a gadget could not do what retail does (a missing image, the three-slice button ...): never silent.
	const std::vector<std::string> &notes() const { return m_notes; }
	void note(const std::string &note);

	// The windows created and not destroyed (tests).
	std::vector<GameWindow *> allWindows() const;

private:
	WindowMsgHandledType deliverMouse(GameWindow *window, GameWindowMessage msg, std::uint32_t packed);
	GameWindow *findWindowForPass(int pass, int x, int y, GameWindow **toolTipWindow);

	std::vector<std::unique_ptr<GameWindow>> m_owned;
	std::vector<GameWindow *> m_destroyList;
	GameWindow *m_windowList = nullptr;
	GameWindow *m_windowTail = nullptr;
	GameWindow *m_mouseCaptor = nullptr;
	GameWindow *m_keyboardFocus = nullptr;
	GameWindow *m_grabWindow = nullptr;
	GameWindow *m_currMouseRgn = nullptr;
	GameWindow *m_loneWindow = nullptr;
	std::vector<GameWindow *> m_modal; // the head is the last element
	GameWindow *m_drawWindow = nullptr;
	GadgetDrawList *m_drawList = nullptr;
	bool m_sendMousePosMessages = true; // lane FB7-1 (S-1916)
	std::uint32_t m_timeMs = 0;
	int m_displayWidth = 1024;
	int m_displayHeight = 768;

	DefaultFontMetrics m_defaultMetrics;
	FontMetricsSource *m_metrics = &m_defaultMetrics;
	MappedImageCollection *m_images = nullptr;
	std::map<std::tuple<std::string, int, bool>, std::unique_ptr<GameFont>> m_fonts;
	TextResolver m_textResolver;
	std::vector<std::string> m_unresolvedLabels;
	std::vector<std::string> m_notes;
};
