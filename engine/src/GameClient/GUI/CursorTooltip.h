// OpenBFME. GPL-3.0.
//
// CursorTooltip (lane HOVER-1): the menus' tool tips. RotWK shows the game text "TOOLTIP:<name>" next to the pointer while it rests on a menu button or
// hover area (Options' "tooltip2DShadows", Skirmish's columns, the main menu's buttons through AptButtonTooltipMap.ini), drawn by GuiFX.apt's ToolTip clip.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp is the donor at the addresses marked tier A / B):
//   * the hover (AptPlayer::HandleOverButtons RW 0x624C68, BFME2 decomp AptPlayer.cpp, tier A): the hovered button's target path below its
//     level ("SoloPlayNav/Skirmish/bttn"; an unnamed button gives an empty last name, so the path ends with '/') goes first to the level's over-button
//     handlers, then "<movie without its extension>/<path>" to the global ones (the hero bar, the spell book: InGameHud); a name that does not end with '/'
//     goes to the tooltip function (RW 0x6235E2, decomp attempt 0x002238e6.cpp): the name is replaced by its ButtonMap entry when it has one (the map at
//     + 0xC, filled from data\ini\AptButtonTooltipMap.ini's "ButtonMap = <path> <name>" rows), the text "TOOLTIP:" + name is fetched, and ONLY when the
//     string file has it Mouse::setCursorTooltip(text, -1, null, 1.0) runs (RW 0x5EE1A6, BFME2 decomp MouseRva001EEA6D.cpp, tier A: the request text,
//     the delay override -1, the default colour);
//   * Mouse::drawTooltip (RW 0x5EE7A8, BFME2 decomp MouseDrawTooltip.cpp, tier B same-shape), once a frame: a request that differs from the last one restarts
//     the delay clock (timeGetTime); the delay is Mouse.ini's TooltipDelayTime (Mouse + 0x127C) unless the request carried one (>= 0), 0 with GlobalData
//     + 0x9C1 (a command line switch, false by default, RW 0x642E19); once the delay has passed the tip may show: with GlobalData + 0x9BF (true, RW 0x642E0C),
//     a display and a non-empty request: a request that differs from the one sent calls ShowToolTip with the font (Language.ini's TooltipFontName when it
//     names one, RW 0x778C93 at GlobalLanguage + 0xBC, else Mouse.ini's TooltipFontName / TooltipFontSize / TooltipFontIsBold) and TooltipTextColor, then
//     MoveToolTip at the pointer; the hide count (+ 0x1304) is set to 4. Otherwise a sent tip counts down and, at 0, HideToolTip runs and nothing is sent;
//   * ShowToolTip (RW 0x782DD9, BFME2 decomp AptGuiFXCallbacks.cpp Rva003808F0Show, tier B edited): an earlier string is hidden first; the font at
//     size * min(the window's x / y scale); the string's pixel size (w, h) and pad = (font height + 1) / 3; GuiFX's ShowToolTip(%g of (w + pad) / x scale
//     * 0.5, %g of (h + pad) / y scale) (the movie's ToolTip.Show(width, height));
//   * MoveToolTip (RW 0x782CDC, tier A): GuiFX's MoveToolTip(%g of x * stage-per-pixel x, %g of y * stage-per-pixel y) (ToolTip.Move clamps it to the stage);
//     HideToolTip (RW 0x782CA0, tier A): GuiFX's HideToolTip and the string goes;
//   * the "ToolTipText" render (RW 0x782D52) draws the string centred in the clip: x = trunc(left + width * 0.5 - w * 0.5 + 0.5), the same for y.
// INFERENCE (stop S-4610): the pointer's resting test is the window manager's hover path (the pointer's button / clip), ScriptEngine + 0x1A238 (no tip
// while it is set, an in-game state) is not read, the text's pixel size is the font metrics' stage measure of its widest line (and its line count times the
// font height) times min(scale), the size the device draws it at, and the ToolTipText text is placed in stage units.

#pragma once

#include "GameClient/GUI/GameWindow.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class ArchiveFileSystem;
class FontMetricsSource;
class GadgetDrawList;
class GameTextSource;
class WindowManager;

// Mouse.ini's Tooltip* fields (Mouse rows RW 0xBF6158 .. 0xBF61B8) with Language.ini's TooltipFontName (RW 0xBF4D70)
struct CursorTooltipSettings
{
	bool loaded = false;
	GameFont font;                 ///< the font the tip is drawn with (point size unscaled)
	int delayMs = 0;               ///< TooltipDelayTime (parseInt RW 0x42EC5E)
	std::uint32_t textColor = 0;   ///< TooltipTextColor (parseRGBAColorInt RW 0x42F03F) as ARGB
	// a missing file, block or field is an error (no default)
	static bool load(ArchiveFileSystem &fs, CursorTooltipSettings &out, std::string *error);
	static bool scan(const std::string &mouseIni, const std::string &languageIni, CursorTooltipSettings &out, std::string *error);
};

// AptButtonTooltipMap.ini (the block's subsystem, S-002): a hover path -> the name its tooltip label is made of
struct AptButtonTooltipMap
{
	std::map<std::string, std::string> buttonMap;
	static bool load(ArchiveFileSystem &fs, AptButtonTooltipMap &out, std::string *error);
	static bool scan(const std::string &text, AptButtonTooltipMap &out, std::string *error);
};

class CursorTooltip
{
public:
	struct Call
	{
		std::string function; ///< GuiFX's ShowToolTip / MoveToolTip / HideToolTip
		std::vector<std::string> args;
	};
	// RW 0x5EE1A6: the request (an empty text asks for none); `delayMs` < 0 takes the settings' delay
	void setTooltip(const UnicodeString &text, int delayMs = -1);
	// RW 0x5EE7A8 at `nowMs` with the pointer at (stageX, stageY) (retail: its window position times the stage per pixel); `scaleX` / `scaleY` are window
	// pixels per stage unit; `metrics` measures a font at the stage's own size (the pixel size is that times min(scale))
	void update(std::uint32_t nowMs, float stageX, float stageY, const CursorTooltipSettings &settings, float scaleX, float scaleY, FontMetricsSource &metrics,
	            std::vector<Call> &out);
	// RW 0x782D52: the "ToolTipText" render of the clip at (x, y, w, h) stage units; nothing without a string
	void render(float x, float y, float w, float h, float scaleX, float scaleY, GadgetDrawList &out) const;
	const UnicodeString &request() const { return m_request; }
	const UnicodeString &shown() const { return m_sent; } ///< the text GuiFX shows ("" none)
	bool hasString() const { return m_haveString; }

	static std::vector<std::string> acceptanceStops();

private:
	UnicodeString m_request;   ///< Mouse + 0x12FC
	UnicodeString m_mirror;    ///< + 0x1300
	UnicodeString m_sent;      ///< + 0x12F8
	int m_hideCount = 0;       ///< + 0x1304
	bool m_delayPassed = false; ///< + 0x1308
	int m_delayOverride = -1;  ///< + 0x4FCC
	std::uint32_t m_changedMs = 0; ///< + 0x4FD8
	// TheTooltipString (RW 0xDE7D2C) and its size (RW 0xDE7D34 / 0xDE7D38)
	bool m_haveString = false;
	GameFont m_font;
	std::uint32_t m_color = 0;
	std::vector<UnicodeString> m_lines;
	int m_textW = 0, m_textH = 0;
};

// The shell's driver: each update takes the window manager's tooltip label, keeps it only when the string file has it (RW 0x6235E2), runs CursorTooltip and
// calls GuiFX.apt's functions on its level; it registers the "ToolTipText" render. Nothing shows while GuiFX.apt is not loaded (a named error).
class ShellCursorTooltip
{
public:
	ShellCursorTooltip(WindowManager &windows, const GameTextSource *text, const CursorTooltipSettings &settings);
	~ShellCursorTooltip();
	ShellCursorTooltip(const ShellCursorTooltip &) = delete;
	ShellCursorTooltip &operator=(const ShellCursorTooltip &) = delete;
	// for a window of (windowW, windowH), the pointer being the Apt player's; `metrics` measures at the stage's own size (the shell's gadget metrics)
	void update(std::uint32_t nowMs, float windowW, float windowH, FontMetricsSource &metrics);
	const CursorTooltip &tooltip() const { return m_tip; }
	const std::vector<std::string> &calls() const { return m_calls; } ///< "ShowToolTip 0.17 0.04" etc., in order
	const std::vector<std::string> &errors() const { return m_errors; }
	const std::string &label() const { return m_label; } ///< the label of the last request ("" none)

private:
	WindowManager &m_windows;
	const GameTextSource *m_text;
	CursorTooltipSettings m_settings;
	CursorTooltip m_tip;
	std::string m_label;
	float m_scaleX = 1.0f, m_scaleY = 1.0f;
	std::vector<std::string> m_calls, m_errors;
};
