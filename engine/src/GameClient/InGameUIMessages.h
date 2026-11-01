// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The in-game UI message lines (ZH InGameUI::message / messageColor / addMessageText / removeMessageAtIndex, the expiry in InGameUI::update and
// drawUIMessages; lane CHAT-1): the six lines at the top right where chat lines and the "GUI:..." notices appear. Client state only.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the InGameUI rows (table at file offset 0x812420): MessageColor1 / MessageColor2 (parseColorInt RW 0x42F13E, + 0x814 / + 0x818), MessagePosition /
//     MessagePositionLW (RW 0x42F2FA, + 0x81C / + 0x824), MessageFont (parseAsciiString, + 0x82C), MessagePointSize (parseInt, + 0x830), MessageBold
//     (parseBool, + 0x834), MessageDelayMS (parseInt, + 0x838). Constructor defaults (RW 0x6A6DE0 .. 0x6A6E62): colours 0xFFFFFFFF / 0xFFB4B4B4, both
//     positions (10, 10), font "Arial" (RW 0xBE9BEC) 10, not bold, 5000 ms. InGameUI's init (RW 0x69EDA7 .. 0x69EDD7) replaces the font name, size and
//     bold with GlobalLanguage's MessageFont (Language + 0x38) when that names a font.
//   * addMessageText (RW 0x69C4B3, ZH's code): a colour makes both alternates rgb | 0xFF000000; the oldest of the 6 lines (+ 0x5D0, stride 0x10: text,
//     display string, timestamp, colour) is dropped, the others move down, the new one is line 0 with the logic frame (GameLogic + 0x40) as its timestamp
//     and colour 1, unless line 1 exists with a colour other than colour 2 (then colour 2); the display string's font is MessageFont at
//     adjustFontSize(MessagePointSize) (RW 0x5E9BEF).
//   * the expiry (InGameUI::update RW 0x6A1F4D, RW 0x6A1FA6 .. 0x6A205E, ZH's code): timeout = MessageDelayMS / 5 / 1000 frames (integer division, as
//     ZH's: 75000 gives 15); a line older than that loses ftol((frame - timestamp) * 0.01f) of its alpha on every client update and goes at alpha 0.
//   * the draw (RW 0x6A0BC0, when InGameUI + 0x811 "messages on"): MessagePositionLW in the War of the Ring map (RW 0xDE4958 + 0x18), else
//     MessagePosition; y = floor(height * Y / 768 + 0.5), x = floor(width * X / 1024 + 0.5); lines 5 .. 0 that exist, oldest first, each at
//     (x, y) (a negative x: width + x - the line's width), y advancing by the line's font height.
// INFERENCE (S-5150): the client update rate of the fade is a fixed 30 per second here (retail: one per drawn frame); the line's drop shadow (black, the
// line's alpha) is ZH's drawUIMessages.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

class ArchiveFileSystem;

struct InGameUIMessageSettings
{
	std::uint32_t color1 = 0xFFFFFFFFu, color2 = 0xFFB4B4B4u;
	int positionX = 10, positionY = 10;     ///< MessagePosition (1024 x 768 units)
	int positionLWX = 10, positionLWY = 10; ///< MessagePositionLW (the War of the Ring map)
	std::string font = "Arial";
	int pointSize = 10;
	bool bold = false;
	int delayMS = 5000;
	bool loaded = false;
	// data\ini\ingameui.ini's InGameUI rows over the constructor defaults, then language.ini's MessageFont; a missing file or a parse error is an error
	static bool load(ArchiveFileSystem &fs, InGameUIMessageSettings &out, std::string *error);
	static bool scan(const std::string &inGameUi, const std::string &language, InGameUIMessageSettings &out, std::string *error);
};

class InGameUIMessages
{
public:
	static constexpr int kMaxLines = 6; // ZH MAX_UI_MESSAGES
	struct Line
	{
		std::u16string text;
		std::uint32_t timestamp = 0;
		std::uint32_t argb = 0;
		bool used = false;
	};

	void setSettings(const InGameUIMessageSettings &s) { m_settings = s; }
	const InGameUIMessageSettings &settings() const { return m_settings; }

	// RW 0x69C4B3: `rgb` null = the alternating colours (InGameUI::message), else the colour (messageColor; 0x00RRGGBB, alpha forced to 0xFF)
	void addMessageText(const std::u16string &text, const std::uint32_t *rgb, std::uint32_t logicFrame);
	// RW 0x6A1FA6: one client update's expiry and fade at the current logic frame
	void update(std::uint32_t logicFrame);
	void clear() { m_lines = {}; }
	const std::array<Line, kMaxLines> &lines() const { return m_lines; }
	int lineCount() const;

	// RW 0x6A0BC0: the first line's top-left for a screen of width x height (before the right-edge rule, which needs the line's width)
	static int screenX(int width, int units) { return (int)std::floor((float)width * (float)units * 0.0009765625f + 0.5f); }
	static int screenY(int height, int units) { return (int)std::floor((float)height * (float)units * 0.0013020834f + 0.5f); }

private:
	InGameUIMessageSettings m_settings;
	std::array<Line, kMaxLines> m_lines{};
};

