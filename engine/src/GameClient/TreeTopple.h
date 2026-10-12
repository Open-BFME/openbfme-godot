// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// TreeTopple (lane SPELL-3): the client side of a toppling map tree, the part of the tree buffer that makes a felled tree fall, bounce and lie (or sink).
// The logic's TerrainTrees (GameLogic/Map/TerrainTrees.h) says which tree falls and in which direction; this animates its static instance. Client only:
// nothing here reaches the simulation.
//
// DONOR (ZH GeneralsMD W3DTreeBuffer.cpp; RotWK's own tree buffer, TheGameClient vslots 0x70 / 0x7C, is not read: stop S-930):
//   * applyTopplingForce: only an upright tree; the speed is raised to MinimumToppleSpeed; the direction normalised; angular velocity = speed *
//     InitialVelocityPercent, acceleration = speed * InitialAccelPercent; ToppleFX at the tree's position;
//   * updateTopplingTree, once per update: the step is the velocity, cut so the total stays within ANGULAR_LIMIT = PI/2 - PI/64; the tree turns about
//     X by -step * dir.y and about Y by step * dir.x; at the limit with a positive velocity it bounces (velocity *= -BounceVelocityPercent): below 0.01
//     it stops (down; KillWhenFinishedToppling starts the sink), at 0.03 or more BounceFX plays three tree radii up the trunk; otherwise the velocity
//     grows by the acceleration;
//   * a down tree with KillWhenFinishedToppling sinks SinkDistance over SinkTime frames, then is deleted.
// INFERENCE (S-930): the updates run per drawn frame at RotWK's 30 frames per second cap (ZH's draw-time update; one per 1/30 s), SinkTime is the INI's
// time (RotWK's parser stores it in logic frames of 200 ms: the sink lasts SinkTime seconds) and the drawn angle is interpolated between two updates; the turn is one rotation by the accumulated angle about (-dir.y, dir.x, 0) (ZH's two small turns per update, applied
// to the identity each time, compose to it); a tree whose lumber is gone (TheGameClient vslot 0x7C) falls, then sinks as a killed tree does; the fog
// pause of ZH (TOPPLE_FOGGED) is not ported.

#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <string>
#include <vector>

class W3DTreeDrawModuleData;

class TreeTopple
{
public:
	enum State
	{
		UPRIGHT,
		FALLING,
		DOWN,
		GONE
	};
	struct Tree
	{
		std::uint32_t clientId = 0;
		const W3DTreeDrawModuleData *data = nullptr;
		Coord3D position{};
		State state = UPRIGHT;
		Coord3D direction{};          ///< normalised, z = 0
		float angularVelocity = 0.0f;
		float angularAcceleration = 0.0f;
		float angle = 0.0f;           ///< ZH m_angularAccumulation
		float previousAngle = 0.0f;   ///< the angle before the last update (drawn with the interpolation)
		float sink = 0.0f;            ///< how far it sank
		float previousSink = 0.0f;
		unsigned sinkFramesLeft = 0;  ///< the sink's updates left
		bool sinking = false;
		bool removed = false;         ///< the logic took its lumber
	};
	// an FX the animation asks for (ToppleFX, BounceFX)
	struct FX
	{
		std::string name;
		Coord3D position{};
	};

	static constexpr float kAngularLimit = 1.5217089f; ///< ZH ANGULAR_LIMIT = PI/2 - PI/64
	static constexpr float kUpdateSeconds = 0.0333333351f; ///< 1 / 30: one update per drawn frame at 30 fps (INFERENCE, S-930)
	static constexpr float kSinkFrameSeconds = 0.2f;      ///< a SinkTime frame is a logic frame (parseDurationUnsignedInt)

	// a tree the map placed; its index is the handle
	size_t add(std::uint32_t clientId, const Coord3D &position, const W3DTreeDrawModuleData *data);
	// the logic's topple event (applyTopplingForce); false when the id is unknown or the tree is not upright
	bool topple(std::uint32_t clientId, const Coord3D &direction, float speed);
	// the logic's removal event (the lumber is gone)
	void remove(std::uint32_t clientId);
	// advances by `seconds` (whole updates of kUpdateSeconds; the rest is kept for the interpolation)
	void advance(double seconds);
	// the indices of the trees that moved since the last call (their transform must be redrawn)
	std::vector<size_t> takeChanged();
	// the tree's rotation (row major 3x3) and its position offset at the current interpolation point
	void pose(size_t index, float rotation[9], Coord3D &offset) const;
	// the drawn placement: the pose applied to the tree's own placement (row major 3x3 basis, columns X, Y, Z) and position
	void placement(size_t index, const float baseBasis[9], const Coord3D &basePosition, float outBasis[9], Coord3D &outPosition) const;
	// a harness filter (GameWorld.get_logic_trees): the point lies within `radius` of (x, y) in 2D
	static bool within2D(const Coord3D &p, double x, double y, double radius);
	std::vector<FX> takeFX();
	const std::vector<Tree> &trees() const { return m_trees; }
	size_t animating() const;

private:
	void update(Tree &t); // ZH updateTopplingTree plus the sink
	std::vector<Tree> m_trees;
	std::vector<size_t> m_changed;
	std::vector<FX> m_fx;
	double m_accumulator = 0.0;
};
