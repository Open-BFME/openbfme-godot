// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Thing: the base of Object and Drawable (ZH Include/Common/Thing.h, Source/Common/Thing/Thing.cpp): a template and a transform.
//
// ZH keeps a Matrix3D and derives the angle on demand. This port keeps the three pieces the live layer reads: the position, the
// angle about +Z (radians, counter-clockwise from +X, as the map files and the INI parsers use it) and the rotation basis (a row-major
// 3x3 whose columns are the thing's X, Y and Z axes in world space; the same convention MapObjectDrawable::basis uses). setOrientation
// builds the basis from the angle with the simulation maths facade (GameLogic/SimMath.h); setTransform takes a full basis (an object
// aligned to the terrain) and derives the angle from its X axis (`atan2(x.y, x.x)`, ZH Thing::getOrientation).
//
// Determinism: only SimMath and plain float arithmetic touch the numbers; nothing is read from a global.

#pragma once

#include "Common/INIDataTypes.h"

class ThingTemplate;
class Object;
class Drawable;

class Thing
{
public:
	explicit Thing(const ThingTemplate *tt)
		: m_template(tt)
	{
	}
	virtual ~Thing() = default;

	const ThingTemplate *getTemplate() const { return m_template; }

	const Coord3D *getPosition() const { return &m_position; }
	float getOrientation() const { return m_angle; }
	// row-major 3x3, columns X, Y, Z
	const float *getBasis() const { return m_basis; }

	void setPosition(const Coord3D *pos);
	// rotation about +Z (the basis is rebuilt from the angle)
	void setOrientation(float angle);
	// lane PERF-3: setOrientation with the cosine and sine of `angle` computed beforehand (SimMath::cosf32 / sinf32 of it: the values setOrientation
	// computes), so a caller can compute them on another thread and apply them in its own order
	void setOrientationTrig(float angle, float c, float s);
	// position and a full basis (an object aligned to the terrain); the angle is atan2 of the X axis
	void setTransform(const Coord3D *pos, const float basis[9]);

	// ZH Thing.h Object / Drawable down-casts (the modules take a Thing* and need the kind)
	virtual Object *asObject() { return nullptr; }
	virtual Drawable *asDrawable() { return nullptr; }

protected:
	// ZH Thing::reactToTransformChange: called after the position or the orientation changed
	virtual void reactToTransformChange(const Coord3D * /*oldPos*/, float /*oldAngle*/) {}

	const ThingTemplate *m_template;

private:
	Coord3D m_position;
	float m_angle = 0.0f;
	float m_basis[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
};
