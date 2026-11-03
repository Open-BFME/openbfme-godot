// OpenBFME. GPL-3.0.
//
// FloatingText: InGameUI's floating texts, the "+N" money text a resource building, a fortress or a captured structure shows when it pays its owner.
//
// DONOR FACTS (Open-BFME-2, tier A per file; ZH InGameUI.cpp is the shape):
//   * the payers (AutoDepositUpdate.cpp, TerrainResourceBehaviorUpdate.cpp): the text is GUI:AddCash formatted with the amount paid, at the object's
//     position raised by 10 (AutoDepositUpdate, its deposit and its initial capture bonus) or by the geometry's getMaxHeightAbovePosition
//     (TerrainResourceBehavior), in the owner's colour with alpha 0xE6. Economy::noteIncome logs those (Economy::IncomeEvent).
//   * addFloatingText (InGameUIFloatingText.cpp): only while TheGameLogic draws the icon UI; the record keeps the text, colour, position, a frame count of 0
//     and the time out: the client frame + FloatingTextTimeOut * 0.03 (BfmeClientFramesPerMillisecond), or + 30 / 3 frames when the time out is not positive.
//   * updateFloatingText (InGameUI_updateFloatingText.cpp): once per client frame every record counts a frame; past its time out its alpha drops by
//     (int)(FloatingTextVanishRate / 30 * (frame - time out)) (an alpha below 0 is 0), and a record at alpha 0 is deleted.
//   * drawFloatingText (InGameUI_drawFloatingText.cpp): a record whose position projects into the view and whose cell is CLEAR for the local player is drawn
//     with its top-left at (x - width / 2, y - frameCount * FloatingTextMoveUpSpeed / 30), in its colour with a black drop shadow of the same alpha.
// INFERENCE (stop S-283): the 30 of both divisions is the client frame rate (GameEngine + 0x38 / its frame field were not read); FloatingTextTimeOut is kept
// in milliseconds and the two speeds as the INI writes them (BFME2's parseDurationUnsignedInt / parseVelocityReal conversions were not read). The text is
// drawn in the HUD's caption font (as the construction text, S-1951).
#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <vector>

class ArchiveFileSystem;
struct IconUIOp;

struct FloatingTextSettings
{
	unsigned timeOutMs = 0;    ///< InGameUI FloatingTextTimeOut
	float moveUpSpeed = 0.0f;  ///< FloatingTextMoveUpSpeed
	float vanishRate = 0.0f;   ///< FloatingTextVanishRate
	// the three rows of Data\INI\InGameUI.ini; false (and `error`) when one is missing or does not parse
	static bool load(ArchiveFileSystem &fs, FloatingTextSettings &out, std::string *error);
};

class FloatingTextList
{
public:
	static constexpr float kClientFramesPerSecond = 30.0f;
	// INFERENCE (S-283): the text's size in 800 x 600 stage pixels (retail's display string font was not read); the device scales it with the window as the
	// HUD movies are, so it reads the same at 5K as at 800 x 600
	static constexpr float kStageTextSize = 15.0f;
	// a jump longer than this (a long stall) steps this many frames: every record has faded long before (the hold is 30, the fade under 100)
	static constexpr unsigned kMaxStepFrames = 1800;

	struct Record
	{
		std::string text;          ///< UTF-8
		std::uint32_t color = 0;   ///< 0xAARRGGBB
		Coord3D pos{};
		unsigned frameTimeOut = 0;
		unsigned frameCount = 0;
		unsigned addedFrame = 0; ///< the client frame of addFloatingText (the record steps on the frames after it)
	};

	void setSettings(const FloatingTextSettings &s) { m_settings = s; m_enabled = true; }
	// A failed HUD settings load must not retain labels with a zero fade rate.
	void disable() { m_enabled = false; clear(); }
	// addFloatingText at client frame `frame`
	void add(const std::string &text, const Coord3D &pos, std::uint32_t color, unsigned frame);
	// updateFloatingText for every client frame since the last call (several when a render frame took more than one client frame): each record
	// steps once per client frame after the one it was added on, so its rise and fade follow the client clock, not the render rate
	void update(unsigned frame);
	// drawFloatingText: TEXT ops for the records that project (`project`: world -> window pixel) and pass `visible` (the cell CLEAR for the local player)
	void ops(const std::function<bool(const Coord3D &, float &x, float &y)> &project, const std::function<bool(const Coord3D &)> &visible,
		std::vector<IconUIOp> &out) const;
	void clear() { m_records.clear(); }
	// the runtime stop of the port's inferences (docs/STOPS.md S-283)
	static std::vector<std::string> acceptanceStops();
	const std::list<Record> &records() const { return m_records; }
	unsigned long long added() const { return m_added; }

private:
	FloatingTextSettings m_settings;
	std::list<Record> m_records; ///< newest first (push_front)
	unsigned m_lastFrame = 0;
	bool m_haveLastFrame = false;
	bool m_enabled = false;
	unsigned long long m_added = 0;
};
