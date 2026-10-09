// OpenBFME. GPL-3.0.
//
// ShroudManager (lane VIS-1): what each player can see. RotWK keeps the shroud OUTSIDE the partition manager (unlike ZH): TheShroudManager (RW global
// 0xDE4358, made by RW 0x62CE75 right after ThePartitionManager, vtable RW 0xD09DB0) forwards every call to an implementation object (RW + 0x10, ctor RW
// 0xB524B0, 0x70 bytes). This file ports that implementation.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read in this lane):
//   * The grid (RW 0xB51D90): origin = TheTerrainLogic::getExtent's lo corner (RW 0x62FC78 -> vslot 0x20, the new game at load progress 0x11), cell size =
//     GameData PartitionCellSize (GlobalData + 0xD4, set at init RW 0x62CFBB), inverse = 1 / size (x87), counts = max(1, ceil(extent * inverse)), an extent
//     narrower than 1 is widened to 1. A grid change copies the old cells where they overlap and re-runs every object's look.
//   * A cell (0xA8 bytes) keeps per player (20 players, RW 0x14) an int16 looker count at + 4 + 8 * player and three uint16 "channels" after it. Status
//     (RW 0xB4FB20 / 0xB4FAB0): -1 -> SHROUDED (2), 0 -> FOGGED (1), > 0 -> CLEAR (0). The constructor's value is -1 (never seen).
//   * addLooker RW 0xB52E10: ++count, and a count that became 0 (it was -1) becomes 1; removeLooker RW 0xB52EC0: --count. A status edge invalidates the cached
//     status of every object covering the cell and, for the local player (RW + 100), calls the client callback (RW + 0x6C: x, y, status).
//   * revealMapForPlayer RW 0xB4F570 (add + remove on every cell: fog), revealMapForPlayerPermanently RW 0xB4F5D0 (add), undoRevealMapForPlayerPermanently
//     RW 0xB519B0 (flush the unlook queue, remove), shroudMapForPlayer RW 0xB51A00 (flush, a count of 0 goes back to -1: RW 0xB52F60).
//   * An object (RW Object + 0x4E4: its record, 0x11C bytes, ctor RW 0xB4E010) registers when it enters the world (RW 0x68E355 -> 0xB515E0) and leaves in its
//     destructor (RW 0x69A8BC -> 0xB4F510). markDirty (RW 0xB4E2A0: forced) / (RW 0xB4E290: not forced) put the record at the head of the dirty list.
//   * update (RW 0xB51970, the FIRST of the four subsystem updates before the destroy list, RW 0x62EB85): ++counter (+ 0x38); every dirty record from the head
//     (RW 0xB4EF50) re-covers its geometry cells and, when its cell changed or it was forced (or its facing turned while its look is not round), queues the
//     unlook of its last look (RW 0xB526A0: due at counter + UnlookPersistDuration), takes the three channels off, asks the object's look (vtable RW 0xC123B0
//     slot 0xC = RW 0x694A45) and looks; then the unlook queue runs every entry whose due frame is below the counter (RW 0xB51690).
//   * The look (RW 0xB50C90 / unlook RW 0xB50E60): the radii in cells are ceil(range / cellSize) (-1 for a negative range); with the three radii equal it is
//     the integer midpoint circle RW 0xB50100 of hlines; else a 16-vertex polygon (RW 0xB501A0, 8.8 fixed-point scan conversion): vertex i at angle
//     (i - 4) * 0.392625 uses radius forward (i < 8) or rear (i >= 8) along the facing and side across it, rotated by 1.5705 - facing (x87 fsin / fcos),
//     rounded by +0.5 and _ftol2 (truncation).
//   * The object's look (RW 0x694A45): no controlling player, a non-positive ShroudClearingRange or a contained object (status 38 with a container whose
//     RW 0x66352C value is below 400) -> radii -1; mask = every player for REVEAL_TO_ALL, else the allies of the owner and the owner (RW 0x6A8695(index,
//     3, 0)) | the owner's spied mask (player + 0x3C8); object + 0x30 bit 0 or + 0x3F4 bit 0 -> radii 0.1; else forward = side = rear =
//     getShroudClearingRange (RW 0x68E4E2), side *= VisionSide, rear *= VisionRear (a HordeContain's VisionSideOverride / VisionRearOverride when > 0).
//   * getShroudClearingRange RW 0x68E4E2: ShroudClearingRange (object + 0x1B4) * (1 + the vision attribute modifier 0x14), the geometry's bounding radius
//     (+ 0xB8) while UNDER_CONSTRUCTION, then times 1 + clamp((z - groundRef) * VisionBonusPercentPerFoot, MinVisionBonusPercent, MaxVisionBonusPercent)
//     when VisionBonusPercentPerFoot > 0; groundRef (object + 0x1C4, RW 0x69264D) is the mean ground height of VisionBonusTestSegments points on a circle of
//     VisionBonusTestRadius around the object, recomputed when its integer position changes.
//   * The status of an object for a player (RW 0xB4E890, through Object RW 0x68D8F7: 0 = always visible when the object has no record or is ALWAYS_VISIBLE):
//     over the cells its geometry covers: none or all shrouded -> SHROUDED (4); some clear and some not -> PARTIAL_CLEAR (2); all clear -> CLEAR (1); else
//     FOGGED (3), and FOGGED becomes SHROUDED when the object hides when fogged (RW 0x68EDD0: not DONT_HIDE_IF_FOGGED and a drawable, and either mobile or
//     HIDE_IF_FOGGED, an immobile object of another team being hidden only until the player saw it once).
//   * The new game (RW 0x62F91A after load progress 0x1E): the ReplayObserver player gets revealMapForPlayerPermanently; for each occupied slot an observer gets the
//     permanent reveal and every other player revealMapForPlayer unless MultiplayerSettings UseShroud (RW 0xDE7D3C + 0x1C, table RW 0xC2F778) is set. The retail
//     multiplayer.ini says `UseShroud = No`: a skirmish starts with the whole map explored (fogged), not black.
//
// NOT PORTED / INFERENCE (stops S-560 .. S-566, ShroudManager::stopLines): the three channels (RW 0xB51030: value / threat / campness maps the AI reads, RW
// 0x68EC3B) are computed and stored but no consumer reads them; ghost objects (RW 0xB4E190 keeps a destroyed immobile object's record for a player that saw it
// fogged) are not kept; the status cache is evaluated eagerly in update() for every invalidated record (retail evaluates it when asked, which only moves when
// the "seen" byte changes); the vision attribute modifiers, the spied mask and the contained-object rule read no state yet; the x87 extended precision of the
// polygon vertices is computed in binary64.

#pragma once

#include "GameLogic/ObjectTypes.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

class GameLogic;
class Object;
class StateHasher;
class ThingTemplate;

enum CellShroudStatus
{
	CELLSHROUD_CLEAR = 0,
	CELLSHROUD_FOGGED = 1,
	CELLSHROUD_SHROUDED = 2
};

// RW 0xB4E890's values (ZH ObjectShroudStatus numbering)
enum ObjectShroudStatus
{
	OBJECTSHROUD_INVALID = 0, ///< not computed (or, from Object's query, an object that is always visible)
	OBJECTSHROUD_CLEAR = 1,
	OBJECTSHROUD_PARTIAL_CLEAR = 2,
	OBJECTSHROUD_FOGGED = 3,
	OBJECTSHROUD_SHROUDED = 4
};

class ShroudManager
{
public:
	static const int kMaxPlayers = 20; ///< RW 0x14
	static const int kChannels = 3;

	// the template values the object side reads (RW ThingTemplate + 0x4C0 ShroudClearingRange, + 0x4C4 / + 0x4C8 VisionSide / VisionRear, + 0x4CC .. + 0x4D8 and
	// + 0x554 the VisionBonus fields; the geometry's last shape and GeometryIsSmall), read from the template at every use like retail's direct reads (no cache:
	// a template changed in place by a later INI block is seen at once)
	struct TemplateVision
	{
		float shroudClearingRange = 0.0f, visionSide = 1.0f, visionRear = 1.0f;
		float bonusPerFoot = 0.0f, bonusTestRadius = 0.0f, maxBonus = 0.0f, minBonus = 0.0f;
		int bonusTestSegments = 0;
		bool small = false;
		ObjectGeometry::Shape shape; ///< the default shape when the template has no geometry row
	};
	// throws std::logic_error naming the template when a geometry row does not parse (PLAN rule 10: no default coverage)
	// the inputs of one look, as the object answers them (RW 0x694A45); a negative radius means "does not look"
	struct LookInputs
	{
		std::uint32_t mask = 0;
		float forward = -1.0f, side = -1.0f, rear = -1.0f;
	};
	// the object side (RW vtable 0xC123B0): tests replace it with synthetic objects
	struct ObjectSource
	{
		std::function<void(const Object &, const TemplateVision &, LookInputs &)> look; ///< slot 0xC (the template's values read once for this look)
		std::function<bool(const Object &, int player)> hidesWhenFogged;    ///< slot 0x14 (RW 0x68EDD0)
		std::function<float(const Object &)> coverageRadius;                ///< the geometry's radius for the covered cells
	};

	static TemplateVision templateVision(const ThingTemplate &finalOverride);
	// RW 0x69264D: object + 0x1C4, the mean ground height of the VisionBonus test circle, recomputed only when the object's truncated integer position changed
	float groundReferenceFor(const Object &obj, const TemplateVision &tv);

	explicit ShroudManager(GameLogic &logic);
	~ShroudManager();
	ShroudManager(const ShroudManager &) = delete;
	ShroudManager &operator=(const ShroudManager &) = delete;

	// the logic's seams: the world hooks (enter / leave), the phase work row "subsystemsBeforeDestroy" (RW 0x62EB85) and the state hash
	void attach();
	void detach();

	// ---- settings (GameData PartitionCellSize and UnlookPersistDuration, RW 0x62CFBB / 0x62CFD1) ----
	void init(float cellSize, unsigned unlookPersistFrames);
	// RW 0xB520F0 -> 0xB51D90: lo / hi corners of TheTerrainLogic::getExtent; cellSize <= 0 keeps the current size
	void setExtent(float loX, float loY, float hiX, float hiY, float cellSize = 0.0f);
	// RW 0xB4F440: the player whose cell edges reach the client callback (x, y, status)
	void setLocalPlayer(int playerIndex, std::function<void(int, int, CellShroudStatus)> callback);
	// RW 0xB4F9C0: the callback for every cell of the local player
	void refreshLocalPlayer();
	// every live record's cached statuses cleared and recomputed for every live player, in registration order (the peer-independent part of the refresh)
	void reevaluateAll();
	// the object side (default: the port of RW 0x694A45 / 0x68EDD0 over the live Object)
	void setObjectSource(const ObjectSource &source) { m_source = source; }
	static ObjectSource retailObjectSource();

	// ---- objects ----
	void registerObject(Object &obj);   ///< RW 0xB515E0
	void unregisterObject(Object &obj); ///< RW 0xB4F510
	void markDirty(const Object &obj, bool force); ///< RW 0xB4E2A0 (force) / 0xB4E290
	bool isRegistered(const Object &obj) const;

	// ---- the frame ----
	void update(); ///< RW 0xB51970
	std::uint32_t counter() const { return m_counter; }

	// ---- map-wide operations ----
	void revealMapForPlayer(int player);               ///< RW 0xB4F570
	void revealMapForPlayerPermanently(int player);    ///< RW 0xB4F5D0
	void undoRevealMapForPlayerPermanently(int player);///< RW 0xB519B0
	void shroudMapForPlayer(int player);               ///< RW 0xB51A00
	// RW 0x62F91A's tail: the reveals of a new game (observers permanently; everyone else unless MultiplayerSettings UseShroud)
	void applyNewGameShroud(bool useShroud, const std::vector<int> &slotPlayers, const std::vector<int> &observerPlayers);

	// ---- queries ----
	CellShroudStatus getCellStatus(int player, int cellX, int cellY) const; ///< out of the grid or of the player range: SHROUDED
	CellShroudStatus getStatusAt(int player, float x, float y) const;       ///< RW 0xB4FB20
	int lookerCount(int player, int cellX, int cellY) const;               ///< the raw int16 (-1 never seen)
	// RW 0x68D8F7: OBJECTSHROUD_INVALID (0) for an object without a record; computes and caches like RW 0xB4E890
	ObjectShroudStatus getObjectStatus(const Object &obj, int player);
	// SMOOTH-1: the same status without getObjectStatus's lazy cache write (Record::status is hashed): what a client reader asks (the render snapshot,
	// the radar), so that reading for the local player never changes a peer's state hash
	ObjectShroudStatus clientObjectStatus(const Object &obj, int player) const;
	ObjectShroudStatus peekObjectStatus(const Object &obj, int player) const; ///< the cached value (0 when invalid)
	bool hasSeen(const Object &obj, int player) const;                       ///< RW 0xB4E270 (record + 0xC4)
	bool worldToCell(float x, float y, int &cx, int &cy) const;             ///< floor((p - lo) * inverse), false outside the grid
	// lane MODULES-2: RW 0xB4D9E0 -> 0xB4FF50: the sum of the cell's `channel` values (0 .. 2) over the players of `playerMask` (bits 0 .. 19); 0 outside the
	// grid, for a channel out of range or an empty mask (the emotion threat test reads channel 1)
	int channelSum(float x, float y, int channel, std::uint32_t playerMask) const;
	// lane MODULES-2: RW 0x68E4E2 getShroudClearingRange of a live object (the port of the look's range, the attribute modifier S-560 aside)
	float visionRange(const Object &obj);
	// RW 0x82C167 (stop S-565): the source's controlling player is human and the target is FOGGED or worse for it (the caller exempts scripts)
	static bool isShroudedForAction(const Object &source, const Object &target);

	int cellCountX() const { return m_countX; }
	int cellCountY() const { return m_countY; }
	float cellSize() const { return m_cellSize; }
	float originX() const { return m_loX; }
	float originY() const { return m_loY; }
	unsigned unlookPersistFrames() const { return m_unlookPersist; }
	size_t pendingUnlooks() const { return m_pending.size(); }
	size_t recordCount() const;

	// ---- low level (the hline drawers of the look, also used by tests) ----
	struct Radii
	{
		int forward = -1, side = -1, rear = -1;
	};
	void lookAt(int cellX, int cellY, const Radii &r, float facing, std::uint32_t mask);   ///< RW 0xB50C90
	void unlookAt(int cellX, int cellY, const Radii &r, float facing, std::uint32_t mask); ///< RW 0xB50E60 (immediate)

	// the client's level of a cell status: GameData ClearAlpha / FogAlpha / ShroudAlpha (client data, not hashed; set by the game from VisionSettings)
	void setDisplayLevels(unsigned clear, unsigned fog, unsigned shroud)
	{
		m_levels[0] = clear;
		m_levels[1] = fog;
		m_levels[2] = shroud;
	}
	unsigned displayLevel(CellShroudStatus s) const { return m_levels[(int)s]; }
	// whether the client draws the shroud (a game started from a game setup; a bare map load is drawn without it); client data, not hashed
	void setDisplayed(bool on) { m_displayed = on; }
	bool displayed() const { return m_displayed; }

	void hash(StateHasher &h) const;
	static std::vector<std::string> stopLines();
	// the per-frame counters of the report
	struct Stats
	{
		unsigned long long looks = 0, unlooks = 0, polygonLooks = 0, edges = 0;
	};
	const Stats &stats() const { return m_stats; }

private:
	struct Cell
	{
		std::array<std::int16_t, kMaxPlayers> lookers;
		std::array<std::array<std::uint16_t, kChannels>, kMaxPlayers> channels;
		std::vector<std::uint32_t> coveredBy; ///< record slots covering the cell (the RW + 0 list), for the invalidation
		Cell();
	};
	struct Record
	{
		ObjectID id = INVALID_ID;
		bool inUse = false;
		bool dirty = false, force = false;
		int lookX = (int)0xDEADBEEF, lookY = (int)0x0BADF00D; ///< RW + 0xD8 / + 0xDC (the constructor's markers)
		Radii radii;                                            ///< RW + 0xE0 .. + 0xE8
		std::uint32_t mask = 0;                                 ///< RW + 0xEC
		float facing = 0.0f;                                    ///< RW + 0xF0
		std::array<int, kChannels> channelRadius{ { -1, -1, -1 } };
		std::array<std::uint32_t, kChannels> channelMask{ { 0, 0, 0 } };
		std::array<int, kChannels> channelAmount{ { 0, 0, 0 } };
		std::vector<int> covered;                               ///< cell indices (RW + 0x1C / + 0x20)
		std::array<std::uint8_t, kMaxPlayers> status{};         ///< RW + 0x24 (0 = invalid)
		std::array<std::uint8_t, kMaxPlayers> previous{};       ///< RW + 0x74
		std::array<std::uint8_t, kMaxPlayers> seen{};           ///< RW + 0xC4
		bool groundValid = false;                               ///< RW object + 0x418 / + 0x41C (the integer position) and + 0x1C4
		int groundX = 0, groundY = 0;
		float groundRef = 0.0f;
	};

	Cell *cellAt(int x, int y);
	const Cell *cellAt(int x, int y) const;
	void addLooker(Cell &c, int index, int player);
	void removeLooker(Cell &c, int index, int player);
	void resetLooker(Cell &c, int index, int player);
	void edge(Cell &c, int index, int player, CellShroudStatus s);
	bool hline(int x1, int x2, int y, std::uint32_t mask, bool add);
	void circle(int cx, int cy, int r, std::uint32_t mask, bool add);
	void polygon(const int *xs, const int *ys, int n, std::uint32_t mask, bool add);
	void shape(int cx, int cy, const Radii &r, float facing, std::uint32_t mask, bool add);
	void queueUnlook(const Record &r);
	void processPending(bool considerTime); ///< RW 0xB51690
	void processRecord(Record &r);          ///< RW 0xB4EF50
	void clearCoverage(Record &r);          ///< RW 0xB4E620
	void cover(Record &r, const Object &obj, const TemplateVision &tv);
	void removeLookOf(Record &r);           ///< RW 0xB4E100
	void evaluate(Record &r, int player, const Object *obj);
	ObjectShroudStatus computeStatus(const Record &r, int player, const Object *obj, int &seen) const;
	Record *recordOf(const Object &obj);
	const Record *recordOf(const Object &obj) const;
	int playerCountLimit() const;

	GameLogic &m_logic;
	ObjectSource m_source;
	bool m_attached = false;
	int m_hashToken = 0, m_hookToken = 0;

	float m_cellSize = 1.0f, m_cellSizeInv = 1.0f;          ///< RW + 0x1C / + 0x20
	float m_loX = 0.0f, m_loY = 0.0f, m_hiX = 0.0f, m_hiY = 0.0f;
	int m_countX = 0, m_countY = 0;                         ///< RW + 0x24 / + 0x28
	std::vector<Cell> m_cells;                              ///< RW + 0x2C
	std::uint32_t m_counter = 0;                            ///< RW + 0x38
	unsigned m_unlookPersist = 2;                           ///< RW + 0 (constructor 2, GameData sets it)
	int m_localPlayer = -1;                                 ///< RW + 100
	std::function<void(int, int, CellShroudStatus)> m_callback; ///< RW + 0x6C

	struct Pending
	{
		std::uint32_t due;
		int x, y;
		Radii radii;
		float facing;
		std::uint32_t mask;
	};
	std::deque<Pending> m_pending;                          ///< RW + 0x3C .. + 0x54 (a deque of 0x20-byte entries)
	std::vector<Record> m_records;                          ///< slots; free slots reused
	std::vector<std::uint32_t> m_slotById;                  ///< id -> slot + 1 (0 = none)
	std::vector<std::uint32_t> m_dirty;                     ///< the dirty list, head = back (RW prepends)
	std::vector<std::uint32_t> m_registration;              ///< registration order (RW + 0x30 list, head = back)
	std::vector<std::uint32_t> m_freeSlots;                 ///< released record slots (reused last released first)
	Stats m_stats;
	unsigned m_levels[3] = { 0, 0, 0 };
	bool m_displayed = false;
};
