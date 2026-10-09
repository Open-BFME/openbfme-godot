// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Radar (ZH Include/Common/Radar.h, Source/Common/System/Radar.cpp, W3DRadar.cpp; B1 AptPalantirRenderRadar.cpp), lane HUD-1: the minimap model. The map picture is
// built from the terrain heights (a shaded height field, once), the object blips are the live objects reduced to a point and a colour (the owner's colour; structures larger),
// the view box is the camera's ground footprint, and a click on the radar is a world point. The Palantir movie's clip tagged `AptPalantir::RenderRadar` gives the rectangle
// the picture is drawn into (the callback receives its two corner points, B1 aptPalantirRenderRadar); `ClipRadar` and `RenderRadarViewBox` tag the clip rectangle and the view box.
//
// Lane HUD-2, TARGET FACTS (RotWK game.dat, caveat S-001), W3DRadar (vtable RW 0xBDADD4, 0x1504 bytes, created at RW 0x441797):
// - the radar picture of a map is its art image when the map has one: buildTerrainTexture (RW 0x44F3AB) takes the map's file name, cuts it at its last '.',
//   appends "_art.tga" and, when TheFileSystem has that file (RW 0xA14AEB), loads it as the radar texture (+0x1474) and sets +0x14D5; only a map without one
//   gets the texture built from the terrain (+0x1470, the loop from RW 0x44F4AD). The display image (+0x146C) takes the art texture when +0x14D5 is set
//   (RW 0x44DC96), with the UV rectangle (0, 1) - (1, 0): row 0 of the D3D texture is at the bottom. The loader puts a TGA's storage rows into the texture in
//   file order for a bottom-left file (ZH textureloader.cpp: "DX8 uses image upside down compared to TGA", the origin flag is toggled), so the picture shows
//   the way the TGA looks (north up). Every one of the 109 retail art files is a 32-bit uncompressed TGA with an alpha channel: the drawing lies over the
//   movie's own background (the Palantir's RadarBackground clip, a parchment);
// - draw (vtable +0x1C, RW 0x44FDF3): the picture goes into the clip rectangle reduced to the map's aspect (RW 0x6D89F2, drawRect below); a map without art
//   gets the two letterbox bands filled 0xFF000000 / 0xFF323232 first (RW 0x44FE8C); then the object overlay (redrawn every 6 frames), the shroud image over
//   the WHOLE clip rectangle, the events, the icons and the view box;
// - the shroud (+0x148C) is the mapped image "ScrollShroud" (RW 0x44EE5D, 128 x 128) whose alpha setShroudLevel (vtable +0x24, RW 0x44E3B6) writes per texel
//   (A8R8G8B8 only, RW 0x516734): SHROUDED 255, FOGGED 127, CLEAR 0, over the texels of the cell's world rectangle (worldToRadar RW 0x6D881F: (int)(p / sample),
//   clamped to 0..127, sample = extent / 128) squeezed to the map's aspect around the texture's centre (RW 0x6D8960, see shroudAlpha).
// NOT PORTED (stop S-291, narrowed): the terrain texture of a map without art (the loop from RW 0x44F4AD: here a shaded height field), the image at +0x1494 /
// texture +0x1498, radar events (pings: the movie's CreateRadarPing), the radar-forced / hidden state and the radar upgrade, the object-type icons (the hero and
// building markers), the blink of attacked objects and the overlay's 6-frame cadence. Everything here is client state.

#pragma once

#include "GameClient/LogicSnapshot.h"

#include "GameClient/HudContext.h"

class ArchiveFileSystem;

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Radar
{
public:
	explicit Radar(HudContext &ctx) : m_ctx(ctx) {}

	// the world rectangle the radar shows (the playable map); false when the logic has no terrain
	bool setupFromTerrain();
	bool ready() const { return m_ready; }

	// pixel (0, 0) is the top left of the picture, which is the map's north west corner
	void worldToRadar(float wx, float wy, int size, float &rx, float &ry) const;
	void radarToWorld(float rx, float ry, int size, float &wx, float &wy) const;

	// the terrain picture, `size` x `size` RGBA8, row 0 on top (a height shaded field); cached per size
	const std::vector<std::uint8_t> &terrainImage(int size);

	// lane HUD-2: the map's art image (RW 0x44F3AB). `mapFile` is the map's file name ("maps/x/x.map"): its "<stem>_art.tga" is read when the file system
	// has it; false + *error when it exists and cannot be read or decoded (no fallback); true without art when there is none.
	bool loadArt(ArchiveFileSystem &fs, const std::string &mapFile, std::string *error);
	static std::string artFileFor(const std::string &mapFile);
	// the art TGA as the radar shows it (RGBA8, row 0 on top, the vertical and horizontal origin bits honoured); false + *error when it cannot be decoded
	static bool decodeArt(const std::vector<std::uint8_t> &bytes, int &w, int &h, std::vector<std::uint8_t> &rgba, std::string *error);
	bool hasArt() const { return m_artW > 0; }
	int artWidth() const { return m_artW; }
	int artHeight() const { return m_artH; }
	const std::string &artFile() const { return m_artFile; }
	// RGBA8, row 0 on top (north), as the radar shows it
	const std::vector<std::uint8_t> &artImage() const { return m_art; }
	// RW 0x6D89F2: the rectangle (upper left, lower right; integers) the picture fills inside the clip rectangle (x, y, w, h)
	void drawRect(int x, int y, int w, int h, int ul[2], int lr[2]) const;
	// RW 0x44E3B6 over every shroud cell of the local player: the alpha of the ScrollShroud image per texel, kShroudTexture x kShroudTexture, row 0 on top;
	// -1 where no cell wrote it (the image's own alpha stays). Empty without a shown shroud.
	static constexpr int kShroudTexture = 128;
	std::vector<std::int16_t> shroudAlpha() const;
	// SMOOTH-1 (merge with HUD-2): from the snapshot's view of the local player's shroud (the Palantir draw callback runs beside the logic worker)
	std::vector<std::int16_t> shroudAlpha(const ShroudView &view) const;
	static unsigned long long shroudVersion(const ShroudView *view);

	struct Blip
	{
		float x = 0, y = 0;     ///< radar pixels of a `size` picture
		std::uint32_t color = 0; ///< 0xAARRGGBB of the owner
		int radius = 1;         ///< pixels
		bool mine = false;
		ObjectID object = 0;
	};
	std::vector<Blip> blips(int size) const;
	// lane SMOOTH-1 (S-810): the same blips from a completed frame's snapshot (the device layer's draw callback: the logic may be running on its worker)
	std::vector<Blip> blips(const LogicSnapshot &snapshot, int size, int localPlayerIndex) const;

	// VIS-1: the terrain picture with the local player's shroud (each pixel times its shroud cell's GameData alpha / 255: ClearAlpha, FogAlpha, ShroudAlpha;
	// RW 0xB52E10 sends the local player's cell edges to TheRadar as to the display) and the shroud's change counter (a new picture is needed when it moved).
	// Blips of objects the local player cannot see (status SHROUDED, RW 0xB4E890) are left out. INFERENCE (stop S-567): W3DRadar's shroud texture is not read.
	std::vector<std::uint8_t> shroudedImage(int size);
	unsigned long long shroudVersion() const;

	// the camera's ground footprint: the four screen corners on the ground, in radar pixels (false when a corner misses the ground)
	bool viewBox(int size, float out[8]) const;

	// a click at radar pixel (rx, ry): the world point; false outside the picture
	bool click(float rx, float ry, int size, float &wx, float &wy) const;

	// ---- lane HUD-4: the radar events Space visits (RW 0x5DCB28) --------------------------------------------------------------------------------------------
	// TARGET FACTS (RotWK game.dat, caveat S-001): TheRadar keeps its events in a list (+ 0x68, the event's position at + 0xC), a cursor (+ 0x6C) and the time of
	// the last jump (+ 0x70). The jump (RW 0x5DCB28): nothing without events, nor in the game modes 8 / 9 (RW 0x5DC46C); now = the client clock (global 0xDE4388
	// vslot 0x7C); while the cursor is on an event and now <= ftol(window (+ 0x90, unsigned) * 0.03f) + the last time, the cursor moves to the next event; at the
	// end of the list, or when the window passed, it goes back to the first; the view looks at the cursor's event and the time is stored.
	// NOT PORTED (stop S-1674): the events themselves (who creates them: attacks, EVA, scripts, the radar pings of the movie) and the window's source (+ 0x90:
	// no writer found in the binary but a copy): the window is 0 here, so a press within the same client frame goes on, later presses start from the first event.
	struct Event
	{
		Coord3D position{ 0.0f, 0.0f, 0.0f };
	};
	void addEvent(const Coord3D &position) { m_events.push_back(Event{ position }); }
	const std::vector<Event> &events() const { return m_events; }
	// RW 0x5DCB28; `now` in client frames (30 a second)
	void tryJumpToNextEvent(TacticalView &view);
	void setClientFrame(unsigned frame) { m_clientFrame = frame; }
	int eventCursor() const { return m_eventCursor; }

	static std::vector<std::string> acceptanceStops();

private:
	std::vector<Event> m_events;
	int m_eventCursor = -1;      ///< + 0x6C: -1 is the list's head (no event)
	unsigned m_lastJump = 0;     ///< + 0x70
	unsigned m_eventWindow = 0;  ///< + 0x90 (S-1674)
	unsigned m_clientFrame = 0;
	std::vector<std::int16_t> shroudAlphaOf(int countX, int countY, float cell, const std::function<CellShroudStatus(int, int)> &statusOf) const;
	HudContext &m_ctx;
	bool m_ready = false;
	float m_minX = 0, m_minY = 0, m_maxX = 1, m_maxY = 1;
	int m_imageSize = 0;
	std::vector<std::uint8_t> m_image;
	std::string m_artFile;
	int m_artW = 0, m_artH = 0;
	std::vector<std::uint8_t> m_art;
};
