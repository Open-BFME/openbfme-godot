// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The part of the tactical view the input translators read (ZH Include/GameClient/View.h, W3DView.cpp: worldToScreen, screenToTerrain, getPosition,
// lookAt), lane HUD-1. The retail camera (limits, zoom, scrolling, rotation) is lane CAM-1's TacticalCamera, a TacticalView; a PinholeView is the
// stand-in a test or a viewer without it fills in (stop S-282). Everything here is CLIENT state: the logic never
// sees it, only the object ids and ground locations a translator derives from it, so a float result is never compared across peers.

#pragma once

#include "Common/INIDataTypes.h"

class GameLogic;

class TacticalView
{
public:
	virtual ~TacticalView() = default;
	// the viewport in window pixels (its origin is (0, 0))
	virtual ICoord2D size() const = 0;
	// false when the point is behind the camera
	virtual bool worldToScreen(const Coord3D &world, ICoord2D &screen) const = 0;
	// the ray under a pixel: origin and a unit direction in SAGE world axes (x east, y north, z up)
	virtual bool screenToRay(const ICoord2D &pixel, Coord3D &origin, Coord3D &direction) const = 0;
	// ZH View::getPosition: the camera's look-at position on the ground (the translators compare it between a button down and up)
	virtual Coord3D position() const = 0;
	virtual void lookAt(const Coord3D &world) = 0;

	// ZH View::screenToTerrain: where the ray under `pixel` meets the ground (the logic's height field); false when it never does within `maxDistance`
	bool screenToTerrain(const ICoord2D &pixel, const GameLogic &logic, Coord3D &out, float maxDistance = 100000.0f) const;
};

// A perspective camera: eye, forward and a world up. lookAt moves the eye by the same offset as the target.
class PinholeView : public TacticalView
{
public:
	PinholeView() = default;
	PinholeView(const Coord3D &eye, const Coord3D &target, int width, int height, float verticalFovRadians);

	void set(const Coord3D &eye, const Coord3D &target);
	void setScreen(int width, int height) { m_width = width; m_height = height; }
	void setFov(float verticalFovRadians) { m_fov = verticalFovRadians; }

	ICoord2D size() const override { return { m_width, m_height }; }
	bool worldToScreen(const Coord3D &world, ICoord2D &screen) const override;
	bool screenToRay(const ICoord2D &pixel, Coord3D &origin, Coord3D &direction) const override;
	Coord3D position() const override { return m_target; }
	void lookAt(const Coord3D &world) override;

	const Coord3D &eye() const { return m_eye; }

private:
	void rebuild();
	Coord3D m_eye, m_target;
	Coord3D m_forward, m_right, m_up;
	int m_width = 1024, m_height = 768;
	float m_fov = 0.7f;
};
