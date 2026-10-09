// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/TacticalCamera.h (target facts, donors and the stops).

#include "GameClient/TacticalCamera.h"

#include "Common/Dict.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPi = 3.14159274101257324f; // RW 0xBDD388
constexpr double kDegToRad = 0.017453293005625408; // RW 0xBDD408
constexpr double kDegToRadPi = 0.005555555555555556; // RW 0xBE5638 (1 / 180, applied after * pi)
constexpr float kMaxGroundLevel = 700.0f; // RW 0xBDD378 (BFME1 initHeightForMap)
constexpr float kPitchLimit = 0.6283185482025146f; // RW 0xC0C324 / 0xC0C328
constexpr float kLegacyPitch = 37.5f, kLegacyMin = 120.0f, kLegacyMax = 300.0f; // RW 0xBE5634, 0xBDD424, 0xBD9E90
constexpr float kHeightSampleToWorld = 0.0390625f; // RW 0xBDB8EC = MAP_HEIGHT_SCALE

float normAngle(float a)
{
	// ZH normAngle: into (-pi, pi]
	while (a < -kPi)
	{
		a += 2.0f * kPi;
	}
	while (a > kPi)
	{
		a -= 2.0f * kPi;
	}
	return a;
}
float clampf(float v, float lo, float hi)
{
	// RW 0x40524B: if (lo > v) lo else if (v > hi) hi else v
	if (lo > v)
	{
		return lo;
	}
	if (v > hi)
	{
		return hi;
	}
	return v;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// CameraHeightField (RW 0x710107 / 0x70FE22)

void CameraHeightField::build(const WorldHeightMap &map, float smoothness, float groundMin, float groundMax)
{
	m_ready = false;
	m_data.clear();
	m_gridW = m_gridH = m_passes = 0;
	m_border = 0;
	if (smoothness == 0.0f)
	{
		return; // RW 0x71014C .. 0x710157
	}
	if (groundMin > groundMax)
	{
		std::swap(groundMin, groundMax); // RW 0x7101B5 .. 0x7101CE
	}
	const int width = map.getXExtent(), height = map.getYExtent();
	m_gridW = (width + 3) / 4;
	m_gridH = (height + 3) / 4;
	m_data.assign((size_t)m_gridW * (size_t)m_gridH, 0.0f);
	// the highest sample of each 4 x 4 block, clamped to the ground limits, never below 0 (RW 0x71020D .. 0x710308)
	for (int gx = 0; gx < m_gridW; ++gx)
	{
		for (int gy = 0; gy < m_gridH; ++gy)
		{
			float best = 0.0f;
			for (int sx = gx * 4; sx < gx * 4 + 4; ++sx)
			{
				for (int sy = gy * 4; sy < gy * 4 + 4; ++sy)
				{
					if (sx < width && sy < height)
					{
						const float h = (float)map.getHeight(sx, sy) * kHeightSampleToWorld;
						const float c = clampf(h, groundMin, groundMax);
						if (c > best)
						{
							best = c;
						}
					}
				}
			}
			m_data[(size_t)gy * (size_t)m_gridW + (size_t)gx] = best;
		}
	}
	// the slope limited grow (RW 0x710315 .. 0x71040A): at most max(width, height) passes, in place, in this loop order
	int budget = std::max(m_gridW, m_gridH);
	for (;;)
	{
		bool changed = false;
		for (int bx = 0; bx < m_gridW; ++bx)
		{
			for (int by = 0; by < m_gridH; ++by)
			{
				for (int nx = bx - 1; nx < bx + 2; ++nx)
				{
					if (nx < 0 || nx >= m_gridW)
					{
						continue;
					}
					for (int ny = by - 1; ny < by + 2; ++ny)
					{
						if (ny < 0 || ny >= m_gridH)
						{
							continue;
						}
						float falloff = kScale * smoothness;
						falloff = falloff * 0.6f; // RW 0xBDAD70
						if (nx != bx && ny != by)
						{
							falloff = falloff * 1.4f; // RW 0xBDBC98
						}
						const float candidate = m_data[(size_t)ny * (size_t)m_gridW + (size_t)nx] - falloff;
						float &mine = m_data[(size_t)by * (size_t)m_gridW + (size_t)bx];
						if (candidate > mine)
						{
							mine = candidate;
							changed = true;
						}
					}
				}
			}
		}
		++m_passes;
		--budget;
		if (!changed || budget <= 0)
		{
			break;
		}
	}
	m_border = map.getBorderSize();
	m_ready = true;
}

float CameraHeightField::sample(float x, float y) const
{
	if (!m_ready)
	{
		return 0.0f; // RW 0x70FE30
	}
	const float border = (float)m_border * 10.0f;
	const float inv = 1.0f / kScale;
	const float fx = (border + x) * inv, fy = (border + y) * inv;
	int ix = (int)std::floor((double)fx), iy = (int)std::floor((double)fy);
	const float rx = fx - (float)ix, ry = fy - (float)iy;
	if (ix < 0)
	{
		ix = 0;
	}
	if (iy < 0)
	{
		iy = 0;
	}
	ix = std::min(ix, m_gridW - 1);
	iy = std::min(iy, m_gridH - 1);
	if (ix > m_gridW - 2 || iy > m_gridH - 2)
	{
		return m_data[(size_t)iy * (size_t)m_gridW + (size_t)ix]; // RW 0x70FF65
	}
	auto at = [&](int cx, int cy) { return m_data[(size_t)cy * (size_t)m_gridW + (size_t)cx]; };
	const float d00 = at(ix, iy), d11 = at(ix + 1, iy + 1);
	if (ry > rx)
	{
		const float d01 = at(ix, iy + 1);
		return d01 + (1.0f - ry) * (d00 - d01) + rx * (d11 - d01);
	}
	const float d10 = at(ix + 1, iy);
	return d10 + (1.0f - rx) * (d00 - d10) + ry * (d11 - d10);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// TacticalCamera

TacticalCamera::TacticalCamera(const CameraSettings &gameData)
	: m_gd(gameData)
{
	m_map = MapCameraValues::resolve(m_gd, nullptr);
	m_minHeight = m_map.minHeight;
	m_maxHeight = m_map.maxHeight;
	m_scrollScalar = m_map.scrollSpeedScalar;
	m_heightAboveGround = m_maxHeight;
	computeCameraOffset(m_map, m_cameraOffset, m_angle, m_legacyOffset);
	rebuildPinhole();
	buildPose(m_eye, m_target);
	m_prevEye = m_committedEye = m_eye;
	m_prevTarget = m_committedTarget = m_target;
}

void TacticalCamera::commitFrame()
{
	m_prevEye = m_committedEye;
	m_prevTarget = m_committedTarget;
	m_committedEye = m_eye;
	m_committedTarget = m_target;
}

void TacticalCamera::computeCameraOffset(const MapCameraValues &v, Coord3D &offset, float &angle, bool &legacy)
{
	legacy = v.pitchAngle == kLegacyPitch && v.yawAngle == 0.0f && v.minHeight == kLegacyMin && v.maxHeight == kLegacyMax; // RW 0x501162
	if (!legacy)
	{
		// RW 0x5011D5, mode 1: the distance is max height * 1.642665 (RW 0xBE5640), along the pitch measured above the horizon
		const float distance = v.maxHeight * 1.6426650285720825f;
		const double rad = (double)v.pitchAngle * (double)kPi * kDegToRadPi;
		const float s = (float)std::sin(rad), c = (float)std::cos(rad);
		offset.z = s * distance;
		offset.y = 0.0f - c * distance;
		offset.x = 0.0f;
		angle = (float)((double)v.yawAngle * (double)kPi * kDegToRadPi);
	}
	else
	{
		// mode 0 (ZH W3DView::initHeightForMap): z = max height, y = -z / tan(pitch), x = -(tan(yaw) * y)
		offset.z = v.maxHeight;
		const double t = std::tan((double)v.pitchAngle * kDegToRad);
		offset.y = -(float)((double)offset.z / t);
		offset.x = -(float)(std::tan((double)v.yawAngle * kDegToRad) * (double)offset.y);
		angle = 0.0f;
	}
}

void TacticalCamera::refreshCameraOffset()
{
	// RW 0x48BCF2 (the end of update): the settings object recomputes the offset every frame (its angle output is not wanted)
	MapCameraValues v = m_map;
	v.minHeight = m_minHeight;
	v.maxHeight = m_maxHeight;
	v.scrollSpeedScalar = m_scrollScalar;
	float angle = 0.0f;
	computeCameraOffset(v, m_cameraOffset, angle, m_legacyOffset);
}

float TacticalCamera::heightAroundPos(float x, float y) const
{
	const float r = m_gd.terrainSampleRadius;
	const GameLogic &l = *m_logic;
	const float here = l.getGroundHeight(x, y);
	auto mx = [](float a, float b) { return a > b ? a : b; };
	return mx(here, mx(mx(l.getGroundHeight(x + r, y - r), l.getGroundHeight(x - r, y - r)), mx(l.getGroundHeight(x + r, y + r), l.getGroundHeight(x - r, y + r))));
}

float TacticalCamera::sampleTerrainHeight(float x, float y) const
{
	return m_field.ready() ? m_field.sample(x, y) : heightAroundPos(x, y);
}

void TacticalCamera::setViewport(int width, int height)
{
	m_pinhole.setScreen(std::max(1, width), std::max(1, height));
	rebuildPinhole();
}

float TacticalCamera::verticalFov() const
{
	// RW 0x533590 Set_View_Plane(hfov, -1): half width = tan(hfov / 2), half height = half width / aspect
	const ICoord2D s = m_pinhole.size();
	const double aspect = (double)s.x / (double)s.y;
	return (float)(2.0 * std::atan(std::tan((double)m_fov * 0.5) / aspect));
}

void TacticalCamera::rebuildPinhole()
{
	m_pinhole.setFov(verticalFov());
	m_pinhole.set(m_eye, m_target);
}

void TacticalCamera::constraint(float out[4]) const
{
	out[0] = m_constraintLo[0];
	out[1] = m_constraintLo[1];
	out[2] = m_constraintHi[0];
	out[3] = m_constraintHi[1];
}

void TacticalCamera::buildPose(Coord3D &eye, Coord3D &target) const
{
	// RW 0x502858 mode 0 (ZH W3DView::buildCameraTransform)
	Coord3D pos = m_pos;
	if (m_constraintValid)
	{
		pos.x = std::min(m_constraintHi[0], std::max(m_constraintLo[0], pos.x));
		pos.y = std::min(m_constraintHi[1], std::max(m_constraintLo[1], pos.y));
	}
	float sx = m_cameraOffset.x * m_zoom, sy = m_cameraOffset.y * m_zoom, sz = m_cameraOffset.z * m_zoom;
	const float factor = 1.0f - m_groundLevel / sz;
	const float cp = std::cos(m_pitch), sp = std::sin(m_pitch);
	const float ca = std::cos(m_angle), sa = std::sin(m_angle);
	// pitch about the X axis, then the angle about the up axis
	const float py = sy * cp - sz * sp, pz = sy * sp + sz * cp;
	const float ax = sx * ca - py * sa, ay = sx * sa + py * ca;
	eye.x = ax * factor + pos.x;
	eye.y = ay * factor + pos.y;
	eye.z = pz * factor + m_groundLevel;
	target.x = pos.x;
	target.y = pos.y;
	target.z = m_groundLevel;
}

void TacticalCamera::calcCameraConstraints()
{
	// RW 0x489364 (BFME1 W3DViewCalcCameraConstraintsBfme.cpp / ZH W3DView::calcCameraConstraints)
	if (!m_logic || !m_logic->terrain())
	{
		return;
	}
	float maxX = 0.0f, maxY = 0.0f;
	if (!m_logic->terrain()->getExtent(0, maxX, maxY))
	{
		return;
	}
	const ICoord2D s = m_pinhole.size();
	auto groundPoint = [&](float fracY, float &x, float &y) {
		ICoord2D px;
		px.x = (int)(0.5f * (float)s.x);
		px.y = (int)(fracY * (float)s.y);
		Coord3D o, d;
		m_pinhole.screenToRay(px, o, d);
		// Vector3::Find_X_At_Z / Find_Y_At_Z with a ray of any length
		const float t = (m_groundLevel - o.z) / d.z;
		x = o.x + d.x * t;
		y = o.y + d.y * t;
	};
	float cx, cy, bx, by;
	groundPoint(0.5f, cx, cy);
	groundPoint(0.95f, bx, by);
	const float dx = cx - bx, dy = cy - by;
	float offset = std::sqrt(dx * dx + dy * dy);
	if (std::isnan(offset))
	{
		offset = 0.0f;
	}
	if (offset > maxX * 0.25f)
	{
		offset = 0.0f;
	}
	m_constraintLo[0] = 0.0f + offset;
	m_constraintHi[0] = maxX - offset;
	m_constraintLo[1] = 0.0f + offset;
	m_constraintHi[1] = maxY - offset;
	m_constraintValid = true;
}

void TacticalCamera::setCameraTransform()
{
	// RW 0x48B7B1
	if (!m_constraintValid)
	{
		buildPose(m_eye, m_target);
		rebuildPinhole();
		calcCameraConstraints();
	}
	if (m_constraintValid && m_zoomLimited)
	{
		m_pos.x = std::min(m_constraintHi[0], std::max(m_constraintLo[0], m_pos.x));
		m_pos.y = std::min(m_constraintHi[1], std::max(m_constraintLo[1], m_pos.y));
	}
	buildPose(m_eye, m_target);
	rebuildPinhole();
}

void TacticalCamera::startMap(const GameLogic &logic, const WorldHeightMap &heights, const Dict *worldInfo, const Coord3D &start)
{
	m_logic = &logic;
	m_map = MapCameraValues::resolve(m_gd, worldInfo);
	// the settings object (RW 0x501162): min / max height, scroll scalar from the map or GameData
	m_minHeight = m_map.minHeight;
	m_maxHeight = m_map.maxHeight;
	m_scrollScalar = m_map.scrollSpeedScalar;
	m_field.build(heights, m_map.heightSmoothness, m_map.groundMinHeight, m_map.groundMaxHeight);
	m_pos = Coord3D{ 870.0f, 770.0f, 0.0f }; // RW 0x48B6A4: the view's position before the first look
	m_lockObject = INVALID_ID;
	m_followFactor = -1.0f;
	m_snapImmediate = false;
	m_pitch = m_defaultPitch;
	m_constraintValid = false;
	// RW 0x6311ED (the game start, between the load progress 0x5F and 0x61): setAngleAndPitchToDefault (view vslot 0x114), setZoomToDefault (0x13C), initHeightForMap (0x58),
	// setAngleAndPitchToDefault, setZoomToDefault, then resetCamera(waypoint, 0, 0, 0) (vslot 0xC8) with the local player's start waypoint or (50, 50, 0) (RW 0xBD88C4 = 50.0f)
	m_groundLevel = m_logic->getGroundHeight(m_pos.x, m_pos.y);
	computeCameraOffset(m_map, m_cameraOffset, m_angle, m_legacyOffset);
	setAngleAndPitchToDefault();
	setZoomToDefault();
	// initHeightForMap (BFME1 W3DViewInitHeightForMapBfme.cpp): the ground level under the position, never above 700; the settings' offset and angle
	m_groundLevel = std::min(m_logic->getGroundHeight(m_pos.x, m_pos.y), kMaxGroundLevel);
	computeCameraOffset(m_map, m_cameraOffset, m_angle, m_legacyOffset);
	m_constraintValid = false;
	setAngleAndPitchToDefault();
	setZoomToDefault();
	Coord3D where = start;
	where.z = 0.0f;
	resetCamera(&where);
	m_prevEye = m_committedEye = m_eye;
	m_prevTarget = m_committedTarget = m_target;
}

void TacticalCamera::lookAt(const Coord3D &world)
{
	m_pos.x = world.x;
	m_pos.y = world.y;
	m_pos.z = 0.0f;
	setCameraTransform();
}

void TacticalCamera::setAngle(float angle)
{
	m_angle = normAngle(angle);
	setCameraTransform();
}

void TacticalCamera::setPitch(float pitch)
{
	// RW 0x65E8D0
	m_pitch = pitch;
	if (-kPitchLimit > pitch)
	{
		m_pitch = -kPitchLimit;
	}
	else if (pitch > kPitchLimit)
	{
		m_pitch = kPitchLimit;
	}
	setCameraTransform();
}

void TacticalCamera::setAngleAndPitchToDefault()
{
	// RW 0x48CB99: View::setAngleAndPitchToDefault (the defaults), then the settings object's offset and angle
	m_angle = m_defaultAngle;
	m_pitch = m_defaultPitch;
	computeCameraOffset(m_map, m_cameraOffset, m_angle, m_legacyOffset);
	m_fov = 0.8726646f;
	setCameraTransform();
}

void TacticalCamera::setHeightAboveGround(float h)
{
	// RW 0x48CBFE
	m_heightAboveGround = h;
	if (m_zoomLimited)
	{
		if (m_heightAboveGround < m_minHeight)
		{
			m_heightAboveGround = m_minHeight;
		}
		if (m_heightAboveGround > m_maxHeight)
		{
			m_heightAboveGround = m_maxHeight;
		}
	}
	m_constraintValid = false;
	setCameraTransform();
}

void TacticalCamera::zoomIn()
{
	setHeightAboveGround(m_heightAboveGround * 0.96f - 1.0f); // RW 0x65E803 (0xC0C320 = 0.96f)
}

void TacticalCamera::zoomOut()
{
	setHeightAboveGround(m_heightAboveGround * 1.05f + 1.0f); // RW 0x65E82A (0xBF3DCC = 1.05f)
}

void TacticalCamera::setZoomToDefault()
{
	// BFME1 W3DView::setZoomToDefault (retail 0x743060): the zoom that puts the camera at the maximum height over the terrain around the position
	float terrain = heightAroundPos(m_pos.x, m_pos.y);
	if (m_field.ready())
	{
		terrain = m_field.sample(m_pos.x, m_pos.y);
	}
	m_zoom = (m_maxHeight + terrain) / m_cameraOffset.z;
	m_heightAboveGround = m_maxHeight;
	m_constraintValid = false;
	setCameraTransform();
}

void TacticalCamera::resetCamera(const Coord3D *location)
{
	// BFME1 W3DView::resetCamera (retail 0x743640) with no duration (RW's click on the middle button: 0x83AD90, the game start: RW 0x63135B -> view slot 0xC8 = RW 0x48D26B)
	if (location)
	{
		m_pos = *location;
	}
	m_minHeight = m_map.minHeight;
	m_maxHeight = m_map.maxHeight;
	m_scrollScalar = m_map.scrollSpeedScalar;
	const float terrain = sampleTerrainHeight(m_pos.x, m_pos.y);
	if (terrain != m_groundLevel)
	{
		m_groundLevel = terrain;
	}
	m_zoom = (m_maxHeight + m_groundLevel) / m_cameraOffset.z;
	m_heightAboveGround = m_maxHeight;
	computeCameraOffset(m_map, m_cameraOffset, m_angle, m_legacyOffset);
	m_fov = 0.8726646f;
	m_pitch = 0.0f;
	setCameraTransform();
}

int TacticalCamera::scrollBy(float dx, float dy)
{
	// RW 0x48C774
	if (dx == 0.0f && dy == 0.0f)
	{
		return 0;
	}
	m_scrollX = dx;
	m_scrollY = dy;
	const ICoord2D s = m_pinhole.size();
	const int halfW = s.x / 2, halfH = s.y / 2;
	const float aspectInt = (float)(s.x / s.y);
	Coord3D o, d;
	m_pinhole.screenToRay({ halfW, halfH }, o, d);
	float fx = d.x, fy = d.y;
	const float len2 = fx * fx + fy * fy;
	if (len2 != 0.0f)
	{
		const float inv = 1.0f / std::sqrt(len2); // RW 0x441C56 is a fast inverse square root (S-452)
		fx *= inv;
		fy *= inv;
	}
	const float S = m_zoom * 0.25f * m_scrollScalar;
	const float moveX = (dx * fy + (-fx) * dy) * S;
	const float moveY = (dx * (-fx) * aspectInt + (-fy) * dy) * S;
	m_pos.x += moveX;
	m_pos.y += moveY;
	int mask = 0;
	if (m_constraintValid)
	{
		if (m_constraintLo[0] > m_pos.x)
		{
			mask |= 1;
		}
		if (m_pos.x > m_constraintHi[0])
		{
			mask |= 2;
		}
		if (m_constraintLo[1] > m_pos.y)
		{
			mask |= 4;
		}
		if (m_pos.y > m_constraintHi[1])
		{
			mask |= 8;
		}
	}
	setCameraTransform();
	return mask;
}

void TacticalCamera::setCameraLock(ObjectID id)
{
	m_lockObject = id;
	m_followFactor = -1.0f;
	if (id != INVALID_ID)
	{
		m_snapImmediate = false;
	}
}

int TacticalCamera::lockTarget(Coord3D &position) const
{
	if (m_objectSource)
	{
		return m_objectSource(m_lockObject, position);
	}
	const Object *obj = m_logic ? m_logic->findObjectByID(m_lockObject) : nullptr;
	if (!obj)
	{
		return 0;
	}
	position = *obj->getPosition();
	return 1;
}

void TacticalCamera::update(bool uiScrolling, bool gamePaused)
{
	// RW 0x48BCF2
	if (!m_logic)
	{
		return;
	}
	++m_frame;
	bool recalc = false, scripted = false;
	if (m_lockObject == INVALID_ID)
	{
		m_followFactor = -1.0f;
	}
	else
	{
		Coord3D lockPos;
		const int found = lockTarget(lockPos);
		if (found < 0)
		{
			// SMOOTH-1: the object is newer than the presented snapshot (created in a frame not shown yet): keep the lock, no move this frame
		}
		else if (found == 0)
		{
			m_lockObject = INVALID_ID; // RW: slot 0x19C (loses the lock)
			m_followFactor = -1.0f;
		}
		else
		{
			if (m_followFactor < 0.0f)
			{
				m_followFactor = 0.05f;
			}
			else
			{
				m_followFactor += 0.05f;
				if (m_followFactor > 1.0f)
				{
					m_followFactor = 1.0f;
				}
			}
			const Coord3D objPos = lockPos;
			Coord3D cur = m_pos;
			const float snapThresholdSq = m_gd.partitionCellSize * m_gd.partitionCellSize;
			const float distX = cur.x - objPos.x, distY = cur.y - objPos.y;
			const float curDistSq = distX * distX + distY * distY;
			if (m_snapImmediate)
			{
				cur.x = objPos.x;
				cur.y = objPos.y;
			}
			else
			{
				const float ddx = objPos.x - cur.x, ddy = objPos.y - cur.y;
				if (m_lockType == LOCK_TETHER)
				{
					if (curDistSq >= snapThresholdSq)
					{
						const float ratio = (1.0f - snapThresholdSq / curDistSq) * m_gd.easeFactor;
						cur.x += ddx * ratio;
						cur.y += ddy * ratio;
					}
					else
					{
						const float ratio = 0.01f * m_lockDistance;
						cur.x += (objPos.x - cur.x) * ratio;
						cur.y += (objPos.y - cur.y) * ratio;
					}
				}
				else
				{
					cur.x += ddx * m_followFactor;
					cur.y += ddy * m_followFactor;
				}
			}
			m_pos = cur;
			m_snapImmediate = false;
			m_groundLevel = objPos.z;
			scripted = true;
			recalc = true;
		}
	}

	if (m_lockObject == INVALID_ID)
	{
		const float height = sampleTerrainHeight(m_pos.x, m_pos.y);
		float terrain = height;
		if (m_field.ready())
		{
			if (uiScrolling && !scripted)
			{
				terrain = std::min(height, 700.0f);
				if (terrain != m_groundLevel)
				{
					m_groundLevel = terrain;
					m_constraintValid = false;
				}
			}
		}
		m_terrainHeight = terrain;
		m_currentHeightAboveGround = m_cameraOffset.z * m_zoom - m_terrainHeight;
		if (m_okToAdjustHeight && !gamePaused)
		{
			float desiredZoom = (float)(((double)m_heightAboveGround + (double)m_terrainHeight) / (double)m_cameraOffset.z);
			if (scripted)
			{
				m_heightAboveGround = m_currentHeightAboveGround;
				desiredZoom = m_zoom;
			}
			if (uiScrolling)
			{
				const float scrollLen = std::sqrt(m_scrollX * m_scrollX + m_scrollY * m_scrollY);
				if (m_gd.scrollAmountCutoff > scrollLen || m_minHeight > m_currentHeightAboveGround
					|| (m_gd.enforceMaxCameraHeight && m_maxHeight < m_currentHeightAboveGround))
				{
					const float adj = (desiredZoom - m_zoom) * m_gd.cameraAdjustSpeed;
					if (std::fabs((double)adj) >= 0.0001)
					{
						m_zoom += adj;
						recalc = true;
					}
				}
			}
			else
			{
				const float adj = (m_zoom - desiredZoom) * m_gd.cameraAdjustSpeed;
				if (std::fabs((double)adj) >= 0.0001)
				{
					if (scripted)
					{
						m_zoom = desiredZoom;
					}
					else
					{
						m_zoom -= adj;
						recalc = true;
					}
				}
			}
		}
	}
	else
	{
		// the locked branch: the zoom that keeps the camera a lock height above the object (RW 0x485A9A: height / offset.z)
		Coord3D lockPos;
		if (lockTarget(lockPos) > 0)
		{
			const float h = lockPos.z + m_gd.lockHeightDelta;
			m_zoom = m_gd.defaultMaxHeight > 1.0f ? h / m_cameraOffset.z : 1.0f;
			recalc = true;
		}
	}
	refreshCameraOffset();
	if (recalc)
	{
		setCameraTransform();
	}
}

std::vector<std::string> TacticalCamera::acceptanceStops()
{
	return {
		"[S-450] camera pose: the eye / target maths is RW 0x502858's mode 0 read against ZH W3DView::buildCameraTransform (the rotation about X then Z, the ground level factor); the movement "
		"modes 1 .. 4 of the state (scripted moves, rotate / zoom / pitch cameras, the waypoint path), the slave camera, the shake and the FXPitch / real-zoom variants are not ported",
		"[S-451] GameData: the camera keys must all be present in the install's GameData (their constructor defaults at RW 0x642A00 .. 0x6437FF were resolved for the height, pitch and "
		"scroll limits only); a mod that deletes one gets an error here, retail would keep the constructor value",
		"[S-452] scrolling: RW 0x441C56 (the normalisation of the heading of the centre pick ray) is a fast inverse square root (magic 0xBE6EB508 and two Newton steps), the port divides by "
		"std::sqrt: the scroll length differs by about 1e-5; the camera maths is plain float32, not compared with an oracle run of the binary",
		"[S-453] game start: RW 0x6311ED (load progress 0x5F .. 0x61) calls setAngleAndPitchToDefault and setZoomToDefault (view vslots 0x114 / 0x13C), initHeightForMap (0x58), the same two "
		"again, then resetCamera (vslot 0xC8) at the local player's Player_N_Start waypoint (name built by RW 0x6311A0) or (50, 50, 0); ported in that order (differs from BFME1's "
		"lookAt + initHeightForMap); the AltCamera trigger area scale (RW View + 0xA0 / + 0xA8, GameLogic AI + 0xBC / + 0xC0) is not applied",
		"[S-454] LookAt translator (RW 0x83AC4A, 0x83B471): the locked-mouse rotation path (RW 0x83B029: the view flag at View + 0x2449), the view bookmarks (SAVE_VIEW / VIEW_VIEW, View vslot "
		"0x170 / 0x174), the replay camera sample (RW 0x83B9FA), the RMB anchor clamp (InGameUI + 0x8C4 and the Mouse position read) and the other flags it reads of InGameUI (+0x17C, "
		"vslot 0xCC) are treated as off, the War of the Ring strategic view (RW 0xDE4958, its own translator RW 0x838EBA / 0x8392A7) is not ported",
		"[S-455] field of view: the horizontal field of view is 50 degrees (View + 0x6C) and the view plane's height follows the viewport aspect (RW CameraClass +0xE8 is set elsewhere "
		"from the viewport: assumed equal to the window's)",
		"[S-456] timing: the camera and the scroll translator advance once per client frame at 30 frames a second (retail: UseFPSLimit Yes, FramesPerSecondLimit 30; the translator reads "
		"timeGetTime for the edge scroll ramp); the render interpolates between two client frames, retail does not",
	};
}
