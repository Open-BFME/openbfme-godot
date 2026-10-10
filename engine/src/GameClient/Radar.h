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
// Lane RADAR-1, the evaluation (RotWK 2.01 against what HUD-1 / HUD-2 drew; BFME2 1.06 decomp = Open-BFME-2, tier A = identical code in RotWK):
// | element              | before RADAR-1 (ZH-style)                      | RotWK (TARGET FACTS, caveat S-001)                                            | now          |
// |----------------------|------------------------------------------------|-------------------------------------------------------------------------------|--------------|
// | clip shape / mask    | the picture in the RenderRadar clip's square   | a RECTANGLE: draw sets TheDisplay's clip region to ClipRadar's rectangle      | as before    |
// |                      |                                                | (+0x144C when +0x145C, RW 0x44FE23; set by RenderRadar RW 0x6D4176 -> 0x6D8C0D)|              |
// |                      |                                                | the round Palantir look is the movie's art (RadarBackground, the frame) and   |              |
// |                      |                                                | the mask clip RadarPings.instance1 over the pings and the view box            |              |
// | picture              | art TGA / shaded height field                  | art TGA (RW 0x44F3AB) / the terrain texture (loop RW 0x44F4AD)                 | S-291 (open) |
// | blips: which objects | selectable coloured objects; a horde = 1 blip  | TheRadar's lists (RW 0x6D9042 addObject): RadarPriority (RW 0x68EBE9: INI     | ported       |
// |                      |                                                | value; INVALID -> STRUCTURE when garrisonable or CAPTURABLE) not INVALID /     |              |
// |                      |                                                | NOT_ON_RADAR: every horde MEMBER (UNIT), no horde object (no RadarPriority);  |              |
// |                      |                                                | STRUCTUREs: COMMANDCENTER / GARRISON / LINKED_TO_FLAG, else not neutral-owned |              |
// |                      |                                                | and not NEUTRAL to the local player; WALK_ON_TOP_OF_WALL / DEFENSIVE_WALL out;|              |
// |                      |                                                | per draw (RW 0x44F9DA): hidden drawables, FOGGED / SHROUDED, LOCAL_UNIT_ONLY  |              |
// | blips: shape / size  | squares r 2 / 3 at screen size, white ring own | 128 x 128 texel overlay scaled to the picture: unit / structure 2 x 2 texels; |              |
// |                      |                                                | HERO 6 x 6 halo + 4 x 4 lighter core (masks RW 0xBDAE24 / 0xBDAE20);          |              |
// |                      |                                                | COMMANDCENTER a circle outline of the bounding radius (DiscreteCircle);       |              |
// |                      |                                                | WALL_SEGMENT its footprint (geometry vs spheres 30 / 5, alpha 0x80 / 0xFF)     | ported       |
// | blips: colour        | the owner's colour                             | Object::getIndicatorColor saturated x1.6 in HSV (RW 0x6D8C53), times the draw | ported       |
// |                      |                                                | tint (-1); others' list first, the local list on top; priority order          |              |
// | hero / building icons| none                                           | the HERO blip above; no RadarIcon INI; RadarMarkerClientUpdate (crates:       | markers:     |
// |                      |                                                | PingOneRing / PingBeacon) moves a movie ping (RW 0x44EDAE)                    | S-2451 (open)|
// | attacked blink       | none                                           | none in W3DRadar: an attack is a radar EVENT (PingAttack); the only blink is  | stealth blink|
// |                      |                                                | the stealth one (RW 0x44DFBA: alpha 64..255 over 60 client frames)            | ported       |
// | events / pings       | the list Space visits, nothing creates them    | 64 events (+0x2C, 0x50 bytes), drawn as Palantir pings PingBeacon / Attack /  | ported for   |
// |                      |                                                | Generic / Information / Upgrade (RW 0x44DE58, table RW 0xBDAD30), faded half a| attacks;     |
// |                      |                                                | second before their 4 s end; tryEvent 10 s / 600 units (RW 0x6D9B4D); the     | S-2452 /     |
// |                      |                                                | local player's attacked objects (RW 0x67B4B7 -> 0x6D9BF3), scripts, stealth   | S-2457       |
// | view box             | 1.5 px white lines, screen corners on terrain  | the corners at the terrain average height (RW 0x44D25B, ZH order TL TR BR BL),| ported       |
// |                      |                                                | pixels RW 0x44D41B ((127 - y) rows), drawn by RenderRadarViewBox (RW 0x50434F)|              |
// |                      |                                                | as a mitred band textured RadarViewBoxEdge, width image width x display       |              |
// |                      |                                                | width / 1024 capped by the shortest edge (RW 0x503C33), inside the mask       |              |
// | shroud / fog         | ScrollShroud alpha (HUD-2)                     | the same (RW 0x44E3B6), over the whole clip rectangle                         | as before    |
// | upgrade / hidden /   | always drawn                                   | drawn only when Player::hasRadar (RadarUpgrade modules) or TheRadar + 0x11,   | ported       |
// | forced               |                                                | which every radar update sets (RW 0x6D8E41): on from the first client frame   | (S-2453)     |
// | 6-frame cadence      | blips every draw                               | the overlay is rebuilt when the client frame % 6 == 0 (or forced, +0x1464)    | ported       |
// | clicks               | move the camera / order the selection          | LeftHUDInput (RW 0x803CE4): the same (radar gate as above)                    | as before    |
// NOT PORTED (stop S-291, narrowed): the terrain texture of a map without art (the loop from RW 0x44F4AD: here a shaded height field), the image at +0x1494 /
// texture +0x1498, the other event creators and the event sounds (S-2452), markers (S-2451), the hidden state and the radar upgrade modules (S-2453). Everything here is client
// state (Radar.cpp is outside the simulation sources; LogicSnapshot reads the logic, never writes it).

#pragma once

#include "GameClient/LogicSnapshot.h"

#include "GameClient/HudContext.h"
#include "GameLogic/Object/ObjectGeometry.h"

class ArchiveFileSystem;
class GameLogic;
class Object;
class Player;

#include <cstdint>
#include <functional>
#include <map>
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
	// lane RADAR-1: the client clock (TheGameClient's frame, RW vslot 0x7C of 0xDE4388) runs 30 frames a second whatever the render rate: `seconds` of
	// render time advance it by whole frames, each one running update() (RW 0x6D8E2B); returns the frames run
	static const double kClientFrameSeconds; ///< 1 / 30 s (Radar.cpp)
	unsigned advanceClock(double seconds);
	// RW 0x6D8E2B (TheRadar's update, once a client frame): the radar is forced on (RW 0x6D8E41: the secondary base's + 0xD is Radar + 0x11), then the
	// events whose die frame passed end (their pings released)
	void update();
	// RW 0x44FE0B .. 0x44FE1D: W3DRadar::draw (and LeftHUDInput RW 0x803CF2) work only when the local player has radar or TheRadar + 0x11 is set
	bool drawn(bool localHasRadar) const { return localHasRadar || m_forced; }
	bool forced() const { return m_forced; }
	// RW 0x45011C: the overlay is rebuilt in a client frame divisible by 6; a draw that skipped such frames (a slow render) rebuilds when one passed since
	// the last rebuild (`last` ~0u: never built)
	static bool overlayDue(unsigned clientFrame, unsigned last)
	{
		return last == ~0u || clientFrame / kOverlayRefreshRate != last / kOverlayRefreshRate;
	}
	int eventCursor() const { return m_eventCursor; }
	unsigned clientFrame() const { return m_clientFrame; } ///< lane RADAR-1: the overlay cadence and the stealth blink

	// ---- lane RADAR-1: the object overlay of W3DRadar::draw (RW 0x44FDF3 -> renderObjectList RW 0x44F9DA) -----------------------------------------------
	enum Shape : std::uint8_t
	{
		SHAPE_DOT = 0,            ///< 2 x 2 texels
		SHAPE_HERO = 1,           ///< template KindOf HERO (+0x113 & 4)
		SHAPE_WALL = 2,           ///< KindOf WALL_SEGMENT (+0x11C bit 29)
		SHAPE_COMMAND_CENTER = 3  ///< KindOf COMMANDCENTER (+0x108 bit 17)
	};
	enum RadarPriority : std::int8_t
	{
		RADAR_PRIORITY_INVALID = 0,
		RADAR_PRIORITY_NOT_ON_RADAR = 1,
		RADAR_PRIORITY_STRUCTURE = 2,
		RADAR_PRIORITY_UNIT = 3,
		RADAR_PRIORITY_LOCAL_UNIT_ONLY = 4
	};
	static constexpr int kCells = 128;                 ///< RADAR_CELL_WIDTH / HEIGHT, the overlay texture (+0x149C / +0x14A0)
	static constexpr unsigned kOverlayRefreshRate = 6; ///< RW 0x45011C: the overlay is rebuilt when the client frame % 6 == 0
	// what TheRadar keeps of a live object (run on the logic's thread by LogicSnapshot::build; `local` is the local player)
	static void captureObject(GameLogic &logic, const Object &o, const Player *local, ObjectSnapshot::RadarEntry &out);
	// RW 0x6D8C53: the colour's HSV saturation times 1.6 (at most 1)
	static std::uint32_t saturateColor(std::uint32_t argb);
	// RW 0x44D013: the channels multiplied (a * b / 255)
	static std::uint32_t modulateColor(std::uint32_t a, std::uint32_t b);
	// RW 0x44D717: `src` blended over `dst` with the weight src alpha * factor / 255
	static std::uint32_t blendColor(std::uint32_t dst, std::uint32_t src, std::uint8_t factor);
	// RW 0x44DFBA: false when the object is not drawn; a stealthed object the local player sees blinks (its alpha replaced). `stealthLook` is
	// InvisibilityManager::clientLook (INFERENCE for the mapping, stop S-2454)
	static bool stealthBlink(int stealthLook, unsigned clientFrame, std::uint32_t &color);
	// the overlay: kCells x kCells 0xAARRGGBB, index ty * kCells + tx, ty the radar row (texture row 0 is shown at the bottom); `tint` is draw's
	// fifth argument (RenderRadar passes -1)
	void renderOverlay(const LogicSnapshot &snapshot, unsigned clientFrame, std::uint32_t tint, std::vector<std::uint32_t> &texels) const;
	// the same for a map of (mapWidth, mapHeight) world units (the extent's size, Radar + 0x1434)
	// `wallShapes` (optional) caches the wall templates' geometry between calls
	static void renderObjects(const LogicSnapshot &snapshot, float mapWidth, float mapHeight, unsigned clientFrame, std::uint32_t tint, std::vector<std::uint32_t> &texels,
	                          std::map<const ThingTemplate *, std::vector<ObjectGeometry::Shape>> *wallShapes = nullptr);
	// RW 0x6D8EBE: the average of the truncated ground heights of the dry radar cells, all 128 x 128 (Radar + 0x1C)
	float terrainAverageZ() const { return m_terrainAverageZ; }
	// the view box's corners in window pixels (RW 0x44D25B + 0x44D47A) for a picture at (ulx, uly) of (width, height): TL, TR, BR, BL of the view
	// at the terrain average height; false when a corner ray does not meet that plane
	bool viewBoxCorners(float ulx, float uly, int width, int height, float out[8]) const;
	// RW 0x503C33: the band along the box (outer / inner corner of each corner) for a band `thickness` pixels wide
	static void viewBoxBand(const float corners[8], float thickness, float outer[8], float inner[8]);
	// RW 0x503C9C .. 0x503D0C: the band's width, the image width x display width / 1024, at most the shortest edge
	static float viewBoxThickness(int imageWidth, unsigned displayWidth, const float corners[8]);

	// ---- lane RADAR-1: the radar events (64 records at Radar + 0x2C, 0x50 bytes each) and the Palantir pings that show them ----------------------------
	enum EventType
	{
		RADAR_EVENT_UNDER_ATTACK = 3,  ///< RW 0x6D9C05 (tryUnderAttackEvent), the ping PingAttack
		RADAR_EVENT_INFILTRATION = 4,  ///< RW 0x6D9AA6
		RADAR_EVENT_BEACON = 6,        ///< no sound (RW 0x44DE92), the ping PingBeacon
		RADAR_EVENT_FAKE = 10,         ///< never drawn (RW 0x44DE83)
		RADAR_EVENT_INVALID = 11       ///< the end of the colour table and the empty record (RW 0x6D8DC4)
	};
	static constexpr int kMaxEvents = 64;
	static constexpr unsigned kClientFramesPerSecond = 30; ///< the global RW 0xD9F60C the events' times scale by
	struct RadarEventRecord
	{
		int type = RADAR_EVENT_INVALID;  ///< + 0x0
		bool active = false;             ///< + 0x4
		unsigned createFrame = 0;        ///< + 0x8 (client frames)
		unsigned dieFrame = 0;           ///< + 0xC
		unsigned fadeFrame = 0;          ///< + 0x10
		Coord3D worldLoc{ 0, 0, 0 };     ///< + 0x34
		int radarX = 0, radarY = 0;      ///< + 0x40 (RW 0x6D881F worldToRadar)
		bool soundPlayed = false;        ///< + 0x48
		int ping = -1;                   ///< + 0x4C the ping (an index into pings()), -1 none
	};
	// RW 0x6D98FA createEvent -> RW 0x6D93EB internalCreateEvent: the next record (round robin), active, created now, dies `seconds` later, fades half a second
	// before; the colours of RW 0xDBCD08 are not kept (no BFME draw reads them)
	void createEvent(const Coord3D &world, int type, float seconds = 4.0f);
	// RW 0x6D9B4D: no new event of the type within 600 units of one created less than 10 seconds (300 client frames) ago
	bool tryEvent(int type, const Coord3D &world);
	// RW 0x6D9BF3: an UNDER_ATTACK event at the object (its message and EVA sound are not ported, stop S-2452)
	bool tryUnderAttackEvent(const Coord3D &position);
	// RW 0x67B4B7's radar half for every listed object of the local player whose attack frame (Object::radarAttackFrame) changed since the last call; the
	// logic must be idle
	void noteAttacks(GameLogic &logic);
	const RadarEventRecord &radarEvent(int i) const { return m_radarEvents[(size_t)i]; }
	// table RW 0xBDAD30: the ping's movie symbol for an event type (nullptr: none)
	static const char *pingName(int type);
	// Palantir::Impl::RadarPing (RW 0x6D4CE3 ctor, Update RW 0x6D5A9D, DoMove RW 0x6D5B23, the fade RW 0x6D5BD9): the movie calls it makes
	struct Ping
	{
		int id = 0;                   ///< + 0x18 (AptPalantir + 0x144, a counter below 0xFFFF)
		std::string name;             ///< + 0x10
		bool created = false;         ///< + 0x14
		bool faded = false;           ///< + 0x15
		float x = 0.0f, y = 0.0f;     ///< + 0x1C / + 0x20, window pixels
		bool released = false;        ///< the event let it go
	};
	struct PingCall
	{
		enum Kind
		{
			Create,
			Move,
			FadeOut
		} kind = Create;
		int id = 0;
		std::string name; ///< Create
		float x = 0.0f, y = 0.0f; ///< Move: window pixels (the movie's stage scale is the caller's, RW 0x6D5AD9)
	};
	// RW 0x44DE58 (drawEvents) for a picture at (ulx, uly) of (width, height), then the pings' update (RW 0x6D5A9D): the movie calls in order
	void drawEvents(int ulx, int uly, int width, int height, std::vector<PingCall> &calls);
	const std::vector<Ping> &pings() const { return m_pings; }
	const unsigned *attackStats() const { return m_attackStats; } ///< the report's counters (noteAttacks)
	// the picture rectangle the device drew last (the events' frame; RenderRadar's ul / size)
	void setPicture(int ulx, int uly, int width, int height) { m_picture[0] = ulx; m_picture[1] = uly; m_picture[2] = width; m_picture[3] = height; }
	bool picture(int out[4]) const;

	static std::vector<std::string> acceptanceStops();

private:
	std::vector<Event> m_events;
	RadarEventRecord m_radarEvents[kMaxEvents];
	int m_nextEvent = 0;               ///< + 0x142C
	int m_nextPingId = 0;              ///< AptPalantir + 0x144
	std::vector<Ping> m_pings;
	unsigned m_attackStats[3] = { 0, 0, 0 }; ///< noteAttacks: calls, objects with an attack frame, of the local player
	std::map<ObjectID, UnsignedInt> m_attackSeen;
	mutable std::map<const ThingTemplate *, std::vector<ObjectGeometry::Shape>> m_wallShapes; ///< renderOverlay's cache (templates outlive the HUD's game) ///< the attack frame each object last reported (looked up only)
	int m_picture[4] = { 0, 0, 0, 0 };
	int m_eventCursor = -1;      ///< + 0x6C: -1 is the list's head (no event)
	unsigned m_lastJump = 0;     ///< + 0x70
	unsigned m_eventWindow = 0;  ///< + 0x90 (S-1674)
	unsigned m_clientFrame = 0;
	double m_clockSeconds = 0.0;       ///< lane RADAR-1: render time not yet a whole client frame
	bool m_forced = false;             ///< Radar + 0x11 (cleared by the reset RW 0x6D8E25, set by every update RW 0x6D8E41)
	std::vector<std::int16_t> shroudAlphaOf(int countX, int countY, float cell, const std::function<CellShroudStatus(int, int)> &statusOf) const;
	HudContext &m_ctx;
	bool m_ready = false;
	float m_minX = 0, m_minY = 0, m_maxX = 1, m_maxY = 1;
	float m_terrainAverageZ = 0.0f; ///< lane RADAR-1 (Radar + 0x1C)
	int m_imageSize = 0;
	std::vector<std::uint8_t> m_image;
	std::string m_artFile;
	int m_artW = 0, m_artH = 0;
	std::vector<std::uint8_t> m_art;
};
