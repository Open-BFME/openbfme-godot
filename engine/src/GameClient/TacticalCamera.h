// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// TacticalCamera (lane CAM-1): the tactical view of RotWK, W3DView / View of ZH (W3DView.cpp, View.cpp) with the RotWK differences read in game.dat. It is CLIENT
// state: the logic never sees it (only ground points and object ids a translator derives from it), a float result is never compared between peers, and it is
// not in the state hash. It has no Godot dependency; the device layer reads eye() / target() and sets its camera from them (GodotInGameHud.cpp).
//
// TARGET FACTS (RotWK game.dat, caveat S-001, addresses are virtual addresses of that image; the W3DView vtable is RW 0xBDD490, its CameraSettings object has the
// vtable RW 0xBE5648):
//   update                  RW 0x48BCF2 (W3DView + 0xB4 interface; the lock branch, the terrain height under the camera, the zoom follow with CameraAdjustSpeed,
//                           ScrollAmountCutoff and EnforceMaxCameraHeight, the camera offset recomputed every frame, setCameraTransform when something moved)
//   setCameraTransform      RW 0x48B7B1 (near 10.0, far = GlobalData + 0x950 (1.0) * 1800.0, horizontal field of view View + 0x6C = 50 degrees (RW 0xBDD410, set
//                           by resetCamera RW 0x48CB99), constraint clamp when zoom limited, buildCameraTransform RW 0x489F96 -> state RW 0x502858)
//   buildCameraTransform    RW 0x502858 mode 0 (ZH W3DView::buildCameraTransform): eye = pos + angle(Z) * pitch(X) * (offset * zoom) * (1 - groundLevel / (offset.z * zoom)),
//                           target = pos, both lifted by the ground level; looking at the target with up = Z
//   calcCameraConstraints   RW 0x489364 (the ZH body plus an isnan and a quarter-map-width guard; picks at 0.5 and 0.95 of the viewport height)
//   scrollBy                RW 0x48C774 (RW's own: the ground move is S = zoom * 0.25 * scroll speed scalar times the screen delta turned by the horizontal heading
//                           of the centre pick ray; the x part of the y component is multiplied by the INTEGER width / height)
//   setHeightAboveGround    RW 0x48CBFE (clamped to the map's min / max height while zoom limited), zoomIn RW 0x65E803 (height * 0.96 - 1.0), zoomOut RW 0x65E82A
//                           (height * 1.05 + 1.0), setPitch RW 0x65E8D0 (clamped to +- 0.6283185 rad), setAngle RW 0x48CA7F (normalised to +- pi)
//   CameraSettings object   RW 0x50136C .. 0x501454 (min / max height, pitch, yaw, scroll scalar: the map dict's reals else GameData's), RW 0x5011D5 (the camera
//                           offset: pitch 37.5, yaw 0, min 120, max 300 -> ZH's offset (z = max height, y = -z / tan(pitch)); any other value -> the offset along
//                           pitch measured from straight down), RW 0x501162 (the choice flag)
//   height field            RW 0x710107 (build) / 0x70FE22 (sample), see CameraHeightField
//   init / initHeightForMap RW 0x48B6A4.. (position (870, 770)), BFME1 initHeightForMap (max ground level 700)
//
// What is not ported (stops S-450 .. S-459, TacticalCamera::acceptanceStops()): see there.

#pragma once

#include <functional>

#include "Common/INIDataTypes.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <string>
#include <vector>

class Dict;
class GameLogic;
class WorldHeightMap;

// RW 0x710107 (build) and 0x70FE22 (sample): the height the camera follows. One cell per 4 x 4 height samples (40 world units), the highest sample of the block
// (clamped to the map's ground limits, never below 0), then a slope limited grow: a cell is at least a neighbour minus smoothness * 40 * 0.6 (diagonals * 1.4),
// at most max(width, height) passes.
class CameraHeightField
{
public:
	// `smoothness` 0 builds nothing (RW 0x710157: the field stays not ready and the camera samples the terrain around the point instead)
	void build(const WorldHeightMap &map, float smoothness, float groundMin, float groundMax);
	bool ready() const { return m_ready; }
	float sample(float x, float y) const;
	int gridWidth() const { return m_gridW; }
	int gridHeight() const { return m_gridH; }
	float cell(int x, int y) const { return m_data[(size_t)y * (size_t)m_gridW + (size_t)x]; }
	int passes() const { return m_passes; }

private:
	std::vector<float> m_data;
	int m_gridW = 0, m_gridH = 0, m_border = 0, m_passes = 0;
	bool m_ready = false;
	static constexpr float kScale = 40.0f;
};

class TacticalCamera : public TacticalView
{
public:
	explicit TacticalCamera(const CameraSettings &gameData);

	// ---- the map (RW initHeightForMap / View::reset) ----
	// Loads the map's camera values (`worldInfo` = the map's WorldInfo dict, may be null), builds the height field, sets the default view and looks at `start` at the
	// default zoom. The logic must have its terrain set.
	void startMap(const GameLogic &logic, const WorldHeightMap &heights, const Dict *worldInfo, const Coord3D &start);
	bool hasMap() const { return m_logic != nullptr; }
	// lane SMOOTH-1 (S-810): where the camera's lock / follow target is. Set: the camera asks it (the device layer answers from the presented snapshot
	// of the last completed logic frame: the logic may be running on its worker); unset: the live logic object (tests without a worker)
	// of the last completed logic frame: the logic may be running on its worker); unset: the live logic object (tests without a worker).
	// The source answers 1 (found, `position` set), 0 (gone: the lock is lost) or -1 (not published yet: a new object; the lock stays, no move)
	void setObjectSource(std::function<int(ObjectID id, Coord3D &position)> source) { m_objectSource = std::move(source); }
	const MapCameraValues &mapValues() const { return m_map; }
	const CameraHeightField &heightField() const { return m_field; }

	// ---- the viewport (window pixels) ----
	void setViewport(int width, int height);

	// ---- TacticalView ----
	ICoord2D size() const override { return m_pinhole.size(); }
	bool worldToScreen(const Coord3D &world, ICoord2D &screen) const override { return m_pinhole.worldToScreen(world, screen); }
	bool screenToRay(const ICoord2D &pixel, Coord3D &origin, Coord3D &direction) const override { return m_pinhole.screenToRay(pixel, origin, direction); }
	Coord3D position() const override { return m_pos; }
	// RW View::lookAt / ZH W3DView::lookAt: the ground point goes to the centre of the view (z is not used)
	void lookAt(const Coord3D &world) override;

	// ---- View state ----
	float getAngle() const { return m_angle; }
	float getPitch() const { return m_pitch; }
	float getZoom() const { return m_zoom; }
	float getHeightAboveGround() const { return m_heightAboveGround; }
	float getGroundLevel() const { return m_groundLevel; }
	float terrainHeightUnderCamera() const { return m_terrainHeight; }
	const Coord3D &cameraOffset() const { return m_cameraOffset; }
	float minHeight() const { return m_minHeight; }
	float maxHeight() const { return m_maxHeight; }
	bool constraintValid() const { return m_constraintValid; }
	// lo.x, lo.y, hi.x, hi.y
	void constraint(float out[4]) const;
	bool zoomLimited() const { return m_zoomLimited; }
	void setZoomLimited(bool on) { m_zoomLimited = on; }

	void setAngle(float angle);
	void setPitch(float pitch);
	void setAngleAndPitchToDefault();
	void setHeightAboveGround(float h);
	void zoomIn();
	void zoomOut();
	void setZoomToDefault();
	// RW 0x48D26B (milliseconds 0): the camera back to the map's default view at `location` (null: the current position); the game start and the middle click call it
	void resetCamera(const Coord3D *location = nullptr);
	// moves by a screen delta (RW 0x48C774); returns the bit mask of the constraint edges the new position is outside of (1 lo.x, 2 hi.x, 4 lo.y, 8 hi.y)
	int scrollBy(float dx, float dy);
	float lastScrollX() const { return m_scrollX; }
	float lastScrollY() const { return m_scrollY; }

	// ---- the follow lock (RW update, the lock branch) ----
	enum LockType
	{
		LOCK_FOLLOW = 0,
		LOCK_TETHER = 1
	};
	void setCameraLock(ObjectID id);
	ObjectID getCameraLock() const { return m_lockObject; }
	void setSnapMode(LockType type, float distance)
	{
		m_lockType = type;
		m_lockDistance = distance;
	}
	void snapToLockTarget() { m_snapImmediate = true; }

	// One client frame (RW updates the camera once per displayed frame; retail runs at FramesPerSecondLimit 30). `uiScrolling` is InGameUI::isScrolling.
	void update(bool uiScrolling, bool gamePaused = false);

	// the camera pose: the eye and the point it looks at (SAGE axes: x east, y north, z up)
	const Coord3D &eye() const { return m_eye; }
	const Coord3D &target() const { return m_target; }
	// The pose committed at the end of the previous client frame (render interpolation: previous -> eye() / target(), the live pose). commitFrame() closes a client frame: it makes
	// the live pose the committed one, so the movement of the next frame (the translator's scroll, then update) is interpolated from it.
	const Coord3D &previousEye() const { return m_prevEye; }
	const Coord3D &previousTarget() const { return m_prevTarget; }
	void commitFrame();
	float nearPlane() const { return 10.0f; }
	float farPlane() const { return 1800.0f; }
	// the horizontal field of view of the 3D camera (radians; View + 0x6C, RW 0xBDD410 = 50 degrees)
	float horizontalFov() const { return m_fov; }
	// the vertical field of view for the current viewport (the W3D view plane: height = width / aspect)
	float verticalFov() const;
	unsigned long long frame() const { return m_frame; }

	const CameraSettings &gameData() const { return m_gd; }

	// Stops of the camera: one "[S-45x] ..." line each (docs/STOPS.md)
	static std::vector<std::string> acceptanceStops();

	// the camera offset of the settings object (RW 0x5011D5), exposed for the tests: min / max / pitch / yaw of the map, `legacy` = pitch 37.5, yaw 0, min 120, max 300
	static void computeCameraOffset(const MapCameraValues &v, Coord3D &offset, float &angle, bool &legacy);
	// RW getHeightAroundPos (BFME1 W3DViewGetHeightAroundPosBfme.cpp, 5 samples at +- CameraTerrainSampleRadiusForHeight)
	float heightAroundPos(float x, float y) const;

private:
	void buildPose(Coord3D &eye, Coord3D &target) const;
	void setCameraTransform();
	void calcCameraConstraints();
	void refreshCameraOffset();
	float sampleTerrainHeight(float x, float y) const;
	void rebuildPinhole();
	void zoomFromHeightField();

	CameraSettings m_gd;
	MapCameraValues m_map;
	CameraHeightField m_field;
	const GameLogic *m_logic = nullptr;
	PinholeView m_pinhole;

	// View + 0xC, + 0x28, + 0x2C, + 0x3C, + 0x40, + 0x44
	Coord3D m_pos{ 870.0f, 770.0f, 0.0f }; // RW 0x48B6A4 (87.0f * 10, 77.0f * 10)
	float m_angle = 0.0f, m_pitch = 0.0f, m_zoom = 1.0f, m_heightAboveGround = 0.0f;
	bool m_zoomLimited = true;
	float m_defaultAngle = 0.0f, m_defaultPitch = 0.0f;
	float m_fov = 0.8726646f; // RW 0xBDD410: 50 degrees
	float m_terrainHeight = 0.0f, m_currentHeightAboveGround = 0.0f;
	float m_groundLevel = 0.0f;
	// the settings object (RW 0x501xxx)
	float m_minHeight = 0.0f, m_maxHeight = 0.0f, m_scrollScalar = 1.0f;
	bool m_legacyOffset = true;
	Coord3D m_cameraOffset{ 0.0f, 0.0f, 1.0f };
	float m_constraintLo[2] = { 0, 0 }, m_constraintHi[2] = { 0, 0 };
	bool m_constraintValid = false;
	float m_scrollX = 0.0f, m_scrollY = 0.0f;
	// the lock
	ObjectID m_lockObject = 0;
	std::function<int(ObjectID, Coord3D &)> m_objectSource;
	int lockTarget(Coord3D &position) const; ///< the lock object's position (m_objectSource, else the live object): 1 found, 0 gone, -1 not published yet
	LockType m_lockType = LOCK_FOLLOW;
	float m_lockDistance = 0.0f;
	bool m_snapImmediate = false;
	float m_followFactor = -1.0f;
	bool m_okToAdjustHeight = true;
	Coord3D m_eye{ 0, -400, 300 }, m_target{ 0, 0, 0 }, m_prevEye{ 0, -400, 300 }, m_prevTarget{ 0, 0, 0 }, m_committedEye{ 0, -400, 300 }, m_committedTarget{ 0, 0, 0 };
	unsigned long long m_frame = 0;
};
