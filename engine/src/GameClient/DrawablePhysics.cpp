// OpenBFME. GPL-3.0.
//
// DrawablePhysics (lane LOCAL-25, #430): the drawable's presentation physics - calcPitchRoll and calcPhysicsXform (wheels, treads, four legs,
// ships and giant birds tilting with the terrain and bouncing on their suspension). CLIENT ONLY: it reads the object snapshot, the locomotor
// template and the client terrain sampler, steps on the client frame and writes only the drawable's presentation pose; it consumes no logic
// random and never reaches the simulation (tools/sim/sim_policy.json excludes it).
// The code moved here unchanged from Drawable.cpp (that file is a simulation-input file under tools/sim/sim_audit.py).

#include "GameClient/Drawable.h"
#include "Common/Audio/AudioRequests.h"
#include "Common/AsciiString.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/RenderInterpolation.h"
#include "GameClient/StealthLook.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"
#include "Common/ModelState.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameClient/DrawableManager.h"

#include <cmath>
#include <cstring>
#include <variant>

// ------------------------------------------------------------------------------------------------
// LOCAL-25: Client presentation physics (pitch, roll, suspension, bobbing)
// Retail RotWK 2.01:
//   - calcPitchRoll: RW 0x670570 (DONOR BFME2 0x00270276, Drawable_calcPitchRoll.cpp)
//   - calcPhysicsXform: RW 0x67BDC0 (DONOR BFME2 0x0027BA95, DrawableCalcPhysicsXform.cpp)
//   - applyPhysicsXform: RW 0x67BE90 (DONOR BFME2 0x0027BB65, DrawableApplyPhysicsXform.cpp)
//   - calcPhysicsXformWheels: RW 0x677026 (DONOR BFME2 0x00276CFB, DrawablePhysicsXformWheels.cpp)
//   - calcPhysicsXformTreads: RW 0x670B11 (DONOR BFME2 0x00270817, DrawablePhysicsXformTreads.cpp)
//   - calcPhysicsXformHugeFourLegs: RW 0x67B7AA (DONOR BFME2 0x0027B47F, DrawablePhysicsXformHugeFourLegs.cpp)
//   - calcPhysicsXformShip: RW 0x6732DC (DONOR BFME2 0x00272FB6, DrawablePhysicsXformShip.cpp)
//   - calcPhysicsXformGiantBird: RW 0x670E09 (DONOR BFME2 0x00270B0F)
// Stop S-7222: the entry points, the dispatcher, the step gate and the wheels field loads are checked in RotWK game.dat; the arithmetic inside
// the five bodies follows the BFME2 donor (INFERENCE that RotWK keeps it)
// ------------------------------------------------------------------------------------------------

bool Drawable::calcPitchRoll(const Coord3D &pos, const Coord3D &dir, const LocomotorTemplate *loco, float &pitch, float &roll)
{
	Coord3D perp;
	perp.x = -dir.y;
	perp.y = dir.x;
	perp.z = 0.0f;

	// RW 0x670570: if LocomotorTemplate flag +0xED (m_useTerrainSmoothing) is set, sample terrain under 3 corners
	if (loco && loco->m_useTerrainSmoothing && m_services.groundHeight)
	{
		float length = 15.0f;
		float width = 15.0f;
		if (m_template)
		{
			const std::vector<ObjectGeometry::Shape> &shapes = ObjectGeometry::cachedShapesOf(*m_template);
			if (!shapes.empty())
			{
				length = shapes[0].majorRadius;
				width = shapes[0].minorRadius;
			}
		}

		Coord3D halfLength;
		halfLength.x = dir.x * 0.5f * length;
		halfLength.y = (length * dir.y) * 0.5f;
		halfLength.z = 0.0f;

		Coord3D vec;
		vec.x = perp.x * 0.5f * width;
		vec.y = perp.y * 0.5f * width;
		vec.z = 0.0f;

		Coord3D corners[3];
		corners[0].x = pos.x - halfLength.x + vec.x;
		corners[0].y = pos.y - halfLength.y + vec.y;
		corners[0].z = pos.z;

		corners[1].x = pos.x - halfLength.x - vec.x;
		corners[1].y = pos.y - halfLength.y - vec.y;
		corners[1].z = pos.z;

		corners[2].x = halfLength.x + pos.x + vec.x;
		corners[2].y = halfLength.y + pos.y + vec.y;
		corners[2].z = pos.z;

		bool tooSteep = false;
		float heights[3];
		for (int i = 0; i < 3; ++i)
		{
			heights[i] = m_services.groundHeight(corners[i].x, corners[i].y, nullptr);
			if (std::abs(heights[i] - pos.z) > (length + width) * 0.5f)
			{
				tooSteep = true;
			}
		}

		if (!tooSteep)
		{
			Coord3D widthEdge;
			widthEdge.x = corners[1].x - corners[0].x;
			widthEdge.y = corners[1].y - corners[0].y;
			widthEdge.z = heights[1] - heights[0];

			Coord3D lengthEdge;
			lengthEdge.x = corners[2].x - corners[0].x;
			lengthEdge.y = corners[2].y - corners[0].y;
			lengthEdge.z = heights[2] - heights[0];

			vec.x = widthEdge.y * lengthEdge.z - widthEdge.z * lengthEdge.y;
			vec.y = widthEdge.z * lengthEdge.x - widthEdge.x * lengthEdge.z;
			vec.z = widthEdge.x * lengthEdge.y - widthEdge.y * lengthEdge.x;

			float len = std::sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);
			if (len > 0.0f)
			{
				vec.x /= len;
				vec.y /= len;
				vec.z /= len;
			}

			float s = vec.x * dir.x + vec.y * dir.y + vec.z * dir.z;
			s = std::max(-1.0f, std::min(1.0f, s));
			pitch = std::asin(s);

			s = vec.x * perp.x + vec.y * perp.y + vec.z * perp.z;
			s = std::max(-1.0f, std::min(1.0f, s));
			roll = std::asin(s);
			return true;
		}
	}

	// Fallback (or when !m_useTerrainSmoothing): terrain normal at position
	Coord3D normal{ 0.0f, 0.0f, 1.0f };
	if (m_services.groundHeight)
	{
		m_services.groundHeight(pos.x, pos.y, &normal);
	}
	float s = normal.x * dir.x + normal.y * dir.y;
	s = std::max(-1.0f, std::min(1.0f, s));
	pitch = std::asin(s);

	s = normal.x * perp.x + normal.y * perp.y;
	s = std::max(-1.0f, std::min(1.0f, s));
	roll = std::asin(s);
	return true;
}

bool Drawable::calcPhysicsXform(const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const LocomotorTemplate *loco = rec.locomotorTemplate;
	if (!loco)
	{
		return false;
	}

	if (!m_locoInfo)
	{
		m_locoInfo = std::make_unique<DrawableLocoInfo>();
	}

	// Dispatcher by LocomotorAppearance (RW 0x67BDC0 / donor 0x0027BA95)
	switch (loco->m_appearance)
	{
	case LOCO_WHEELS_FOUR: // 1
	case LOCO_TREADS:      // 8
		calcPhysicsXformWheels(loco, rec, pose, info);
		return true;

	case LOCO_HOVER: // 2
	case LOCO_WINGS: // 3
		calcPhysicsXformTreads(loco, rec, pose, info);
		return true;

	case LOCO_LEGS_FOUR_HUGE: // 4
		calcPhysicsXformHugeFourLegs(loco, rec, pose, info);
		return true;

	case LOCO_GIANT_BIRD: // 5
		calcPhysicsXformGiantBird(loco, rec, pose, info);
		return true;

	case LOCO_SHIP: // 9
		calcPhysicsXformShip(loco, rec, pose, info);
		return true;

	default:
		return false;
	}
}

void Drawable::calcPhysicsXformWheels(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const float ACCEL_PITCH_LIMIT = loco->m_accelerationPitchLimit;
	const float BOUNCE_ANGLE_KICK = loco->m_bounceAmount;
	const float PITCH_STIFFNESS = loco->m_pitchStiffness;
	const float ROLL_STIFFNESS = loco->m_rollStiffness;
	const float PITCH_DAMPING = loco->m_pitchDamping;
	const float ROLL_DAMPING = loco->m_rollDamping;
	const float UNIFORM_AXIAL_DAMPING = loco->m_uniformAxialDamping;
	const float MAX_SUSPENSION_EXTENSION = loco->m_maximumWheelExtension;
	const bool DO_WHEELS = loco->m_hasSuspension;

	float length = 15.0f;
	float width = 15.0f;
	if (m_template)
	{
		const std::vector<ObjectGeometry::Shape> &shapes = ObjectGeometry::cachedShapesOf(*m_template);
		if (!shapes.empty())
		{
			length = shapes[0].majorRadius;
			width = shapes[0].minorRadius;
		}
	}

	const Coord3D &pos = pose.position;
	Coord3D dir{ pose.basis[0], pose.basis[3], 0.0f };
	float dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
	if (dirLen > 0.0f)
	{
		dir.x /= dirLen;
		dir.y /= dirLen;
	}
	else
	{
		dir = Coord3D{ 1.0f, 0.0f, 0.0f };
	}

	float groundPitch = 0.0f;
	float groundRoll = 0.0f;
	calcPitchRoll(pos, dir, loco, groundPitch, groundRoll);

	float hheight = m_services.groundHeight ? m_services.groundHeight(pos.x, pos.y, nullptr) : 0.0f;
	bool airborne = rec.aboveTerrain;

	if (airborne)
	{
		if (DO_WHEELS)
		{
			m_locoInfo->m_framesAirborne = 0;
			m_locoInfo->m_framesAirborneCounter++;
			if (pos.z - hheight > -MAX_SUSPENSION_EXTENSION)
			{
				m_locoInfo->m_rearLeftHeightOffset += (MAX_SUSPENSION_EXTENSION - m_locoInfo->m_rearLeftHeightOffset) * 0.5f;
				m_locoInfo->m_rearRightHeightOffset += (MAX_SUSPENSION_EXTENSION - m_locoInfo->m_rearRightHeightOffset) * 0.5f;
			}
			else
			{
				m_locoInfo->m_rearLeftHeightOffset += (0.0f - m_locoInfo->m_rearLeftHeightOffset) * 0.5f;
				m_locoInfo->m_rearRightHeightOffset += (0.0f - m_locoInfo->m_rearRightHeightOffset) * 0.5f;
			}
		}

		float pitchHeight = length * std::sin(m_locoInfo->m_accelerationPitch + m_locoInfo->m_pitch - groundPitch);
		float rollHeight = width * std::sin(m_locoInfo->m_accelerationRoll + m_locoInfo->m_roll - groundRoll);
		info.m_totalZ = (std::abs(pitchHeight) + std::abs(rollHeight)) * 0.25f;
		return;
	}

	float curSpeed = rec.moveSpeed;
	float maxSpeed = loco->m_maxSpeed > 0.0f ? loco->m_maxSpeed : 1.0f;
	if (curSpeed > maxSpeed * 0.1f && BOUNCE_ANGLE_KICK > 0.0f)
	{
		float factor = curSpeed / maxSpeed;
		if (std::abs(m_locoInfo->m_pitchRate) < factor * BOUNCE_ANGLE_KICK * 0.25f && std::abs(m_locoInfo->m_rollRate) < factor * BOUNCE_ANGLE_KICK * 0.125f)
		{
			int r = m_services.random.value(0, 3);
			if (r == 0)
			{
				m_locoInfo->m_pitchRate -= BOUNCE_ANGLE_KICK * factor;
				m_locoInfo->m_rollRate -= BOUNCE_ANGLE_KICK * factor * 0.5f;
			}
			else if (r == 1)
			{
				m_locoInfo->m_pitchRate += BOUNCE_ANGLE_KICK * factor;
				m_locoInfo->m_rollRate -= BOUNCE_ANGLE_KICK * factor * 0.5f;
			}
			else if (r == 2)
			{
				m_locoInfo->m_pitchRate -= BOUNCE_ANGLE_KICK * factor;
				m_locoInfo->m_rollRate += BOUNCE_ANGLE_KICK * factor * 0.5f;
			}
			else
			{
				m_locoInfo->m_pitchRate += BOUNCE_ANGLE_KICK * factor;
				m_locoInfo->m_rollRate += BOUNCE_ANGLE_KICK * factor * 0.5f;
			}
		}
	}

	m_locoInfo->m_pitchRate += (-PITCH_STIFFNESS * (m_locoInfo->m_pitch - groundPitch)) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate);
	if (m_locoInfo->m_pitchRate > 0.0f)
	{
		m_locoInfo->m_pitchRate *= 0.5f;
	}
	m_locoInfo->m_rollRate += (-ROLL_STIFFNESS * (m_locoInfo->m_roll - groundRoll)) + (-ROLL_DAMPING * m_locoInfo->m_rollRate);

	m_locoInfo->m_pitch += m_locoInfo->m_pitchRate * UNIFORM_AXIAL_DAMPING;
	m_locoInfo->m_roll += m_locoInfo->m_rollRate * UNIFORM_AXIAL_DAMPING;

	m_locoInfo->m_accelerationPitchRate += (-PITCH_STIFFNESS * m_locoInfo->m_accelerationPitch) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate);
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

	m_locoInfo->m_accelerationRollRate += (-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate);
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	info.m_totalPitch = m_locoInfo->m_accelerationPitch + m_locoInfo->m_pitch;
	info.m_totalRoll = m_locoInfo->m_accelerationRoll + m_locoInfo->m_roll;

	if (m_locoInfo->m_accelerationPitch > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	info.m_totalYaw = 0.0f;
	info.m_totalZ = 0.0f;

	float pitchHeight = length * std::sin(info.m_totalPitch - groundPitch);
	float rollHeight = width * std::sin(info.m_totalRoll - groundRoll);

	if (DO_WHEELS)
	{
		m_locoInfo->m_framesAirborne = m_locoInfo->m_framesAirborneCounter;
		m_locoInfo->m_framesAirborneCounter = 0;
		float newFrontLeft = 0.0f, newFrontRight = 0.0f, newRearLeft = 0.0f, newRearRight = 0.0f;

		const float SPRING_FACTOR = 0.9f;
		if (pitchHeight < 0.0f)
		{
			newFrontLeft = SPRING_FACTOR * (pitchHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newFrontRight = SPRING_FACTOR * (pitchHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newRearLeft = pitchHeight * (-1.0f / 2.0f + 1.0f / 4.0f);
			newRearRight = pitchHeight * (-1.0f / 2.0f + 1.0f / 4.0f);
		}
		else
		{
			newFrontLeft = (pitchHeight * (-1.0f / 4.0f + 1.0f / 2.0f));
			newFrontRight = (pitchHeight * (-1.0f / 4.0f + 1.0f / 2.0f));
			newRearLeft = SPRING_FACTOR * (pitchHeight * (-1.0f / 2.0f - 1.0f / 3.0f));
			newRearRight = SPRING_FACTOR * (pitchHeight * (-1.0f / 2.0f - 1.0f / 3.0f));
		}
		if (rollHeight > 0.0f)
		{
			newFrontRight += -SPRING_FACTOR * (rollHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newRearRight += -SPRING_FACTOR * (rollHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newRearLeft += rollHeight * 0.5f - rollHeight * 0.25f;
			newFrontLeft += rollHeight * 0.5f - rollHeight * 0.25f;
		}
		else
		{
			newFrontRight += -rollHeight * (1.0f / 2.0f - 1.0f / 4.0f);
			newRearRight += -rollHeight * (1.0f / 2.0f - 1.0f / 4.0f);
			newRearLeft += SPRING_FACTOR * (rollHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newFrontLeft += SPRING_FACTOR * (rollHeight * (1.0f / 3.0f + 1.0f / 2.0f));
		}

		if (newFrontLeft < m_locoInfo->m_frontLeftHeightOffset)
			m_locoInfo->m_frontLeftHeightOffset += (newFrontLeft - m_locoInfo->m_frontLeftHeightOffset) * 0.5f;
		else
			m_locoInfo->m_frontLeftHeightOffset = newFrontLeft;

		if (newFrontRight < m_locoInfo->m_frontRightHeightOffset)
			m_locoInfo->m_frontRightHeightOffset += (newFrontRight - m_locoInfo->m_frontRightHeightOffset) * 0.5f;
		else
			m_locoInfo->m_frontRightHeightOffset = newFrontRight;

		if (newRearLeft < m_locoInfo->m_rearLeftHeightOffset)
			m_locoInfo->m_rearLeftHeightOffset += (newRearLeft - m_locoInfo->m_rearLeftHeightOffset) * 0.5f;
		else
			m_locoInfo->m_rearLeftHeightOffset = newRearLeft;

		if (newRearRight < m_locoInfo->m_rearRightHeightOffset)
			m_locoInfo->m_rearRightHeightOffset += (newRearRight - m_locoInfo->m_rearRightHeightOffset) * 0.5f;
		else
			m_locoInfo->m_rearRightHeightOffset = newRearRight;

		if (m_locoInfo->m_frontLeftHeightOffset < MAX_SUSPENSION_EXTENSION)
			m_locoInfo->m_frontLeftHeightOffset = MAX_SUSPENSION_EXTENSION;
		if (m_locoInfo->m_frontRightHeightOffset < MAX_SUSPENSION_EXTENSION)
			m_locoInfo->m_frontRightHeightOffset = MAX_SUSPENSION_EXTENSION;
		if (m_locoInfo->m_rearLeftHeightOffset < MAX_SUSPENSION_EXTENSION)
			m_locoInfo->m_rearLeftHeightOffset = MAX_SUSPENSION_EXTENSION;
		if (m_locoInfo->m_rearRightHeightOffset < MAX_SUSPENSION_EXTENSION)
			m_locoInfo->m_rearRightHeightOffset = MAX_SUSPENSION_EXTENSION;
	}

	float divisor = 4.0f;
	float pitchDiff = std::abs(info.m_totalPitch - groundPitch);
	constexpr float kPi = 3.14159265358979323846f;
	if (pitchDiff > kPi / 8.0f)
	{
		divisor = ((4.0f * kPi / 8.0f) + (1.0f * (pitchDiff - kPi / 8.0f))) / pitchDiff;
	}
	info.m_totalZ += std::abs(pitchHeight) / divisor;
	info.m_totalZ += std::abs(rollHeight) / divisor;
}

void Drawable::calcPhysicsXformTreads(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const float ACCEL_PITCH_LIMIT = loco->m_accelerationPitchLimit;
	const float PITCH_STIFFNESS = loco->m_pitchStiffness;
	const float ROLL_STIFFNESS = loco->m_rollStiffness;
	const float PITCH_DAMPING = loco->m_pitchDamping;
	const float ROLL_DAMPING = loco->m_rollDamping;
	const float UNIFORM_AXIAL_DAMPING = loco->m_uniformAxialDamping;

	m_locoInfo->m_pitchRate += ((-PITCH_STIFFNESS * m_locoInfo->m_pitch) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate));
	m_locoInfo->m_rollRate += ((-ROLL_STIFFNESS * m_locoInfo->m_roll) + (-ROLL_DAMPING * m_locoInfo->m_rollRate));

	if (!rec.aboveTerrain)
	{
		m_locoInfo->m_pitch += m_locoInfo->m_pitchRate * UNIFORM_AXIAL_DAMPING;
		m_locoInfo->m_roll += m_locoInfo->m_rollRate * UNIFORM_AXIAL_DAMPING;

		m_locoInfo->m_accelerationPitchRate += ((-PITCH_STIFFNESS * m_locoInfo->m_accelerationPitch) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate));
		m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

		m_locoInfo->m_accelerationRollRate += ((-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate));
		m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;
	}
	else
	{
		m_locoInfo->m_accelerationPitch *= 0.5f;
		if (std::abs(m_locoInfo->m_accelerationPitch) < 0.0001f)
			m_locoInfo->m_accelerationPitch = 0.0f;
		m_locoInfo->m_accelerationRoll *= 0.5f;
		if (std::abs(m_locoInfo->m_accelerationRoll) < 0.0001f)
			m_locoInfo->m_accelerationRoll = 0.0f;
	}

	info.m_totalPitch = m_locoInfo->m_accelerationPitch + m_locoInfo->m_pitch;
	info.m_totalRoll = m_locoInfo->m_accelerationRoll + m_locoInfo->m_roll;

	if (m_locoInfo->m_accelerationPitch > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	info.m_totalYaw = 0.0f;
	info.m_totalZ = 0.0f;
}

void Drawable::calcPhysicsXformHugeFourLegs(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const float ACCEL_PITCH_LIMIT = loco->m_accelerationPitchLimit;
	const float PITCH_STIFFNESS = loco->m_pitchStiffness;
	const float ROLL_STIFFNESS = loco->m_rollStiffness;
	const float PITCH_DAMPING = loco->m_pitchDamping;
	const float ROLL_DAMPING = loco->m_rollDamping;
	const float UNIFORM_AXIAL_DAMPING = loco->m_uniformAxialDamping;

	float length = 15.0f;
	float width = 15.0f;
	if (m_template)
	{
		const std::vector<ObjectGeometry::Shape> &shapes = ObjectGeometry::cachedShapesOf(*m_template);
		if (!shapes.empty())
		{
			length = shapes[0].majorRadius;
			width = shapes[0].minorRadius;
		}
	}

	const Coord3D &pos = pose.position;
	float direction = pose.angle;
	float s = std::sin(direction);
	float c = std::cos(direction);

	Coord3D points[4];
	points[0] = Coord3D{ -length * c - width * s + pos.x, -width * c + -length * s + pos.y, pos.z };
	points[1] = Coord3D{  length * c - width * s + pos.x, -width * c +  length * s + pos.y, pos.z };
	points[2] = Coord3D{ -length * c + width * s + pos.x,  width * c + -length * s + pos.y, pos.z };
	points[3] = Coord3D{  length * c + width * s + pos.x,  width * c +  length * s + pos.y, pos.z };

	float heights[4];
	for (int i = 0; i < 4; ++i)
	{
		heights[i] = m_services.groundHeight ? m_services.groundHeight(points[i].x, points[i].y, nullptr) : pos.z;
	}

	float front = (heights[0] + heights[1]) * 0.5f;
	float back = (heights[2] + heights[3]) * 0.5f;
	float frontX = (points[0].x + points[1].x) * 0.5f;
	float frontY = (points[0].y + points[1].y) * 0.5f;
	float backX = (points[2].x + points[3].x) * 0.5f;
	float backY = (points[2].y + points[3].y) * 0.5f;
	float dx = frontX - backX;
	float dy = frontY - backY;
	float dist = std::sqrt(dx * dx + dy * dy);
	if (dist < 1.0f) dist = 1.0f;
	float pitch = std::atan2(back - front, dist);

	m_locoInfo->m_pitchRate += (-PITCH_STIFFNESS * (m_locoInfo->m_pitch - pitch)) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate);
	if (m_locoInfo->m_pitchRate > 0.0f)
		m_locoInfo->m_pitchRate *= 0.5f;

	m_locoInfo->m_pitch += UNIFORM_AXIAL_DAMPING * m_locoInfo->m_pitchRate;
	m_locoInfo->m_roll += UNIFORM_AXIAL_DAMPING * m_locoInfo->m_rollRate;

	m_locoInfo->m_accelerationPitchRate += (-PITCH_STIFFNESS * m_locoInfo->m_accelerationPitch) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate);
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

	m_locoInfo->m_accelerationRollRate += (-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate);
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	info.m_totalPitch = m_locoInfo->m_accelerationPitch + m_locoInfo->m_pitch;
	info.m_totalRoll = m_locoInfo->m_accelerationRoll + m_locoInfo->m_roll;

	if (m_locoInfo->m_accelerationPitch > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	float oldZ = m_locoInfo->m_overlapZ;
	float wanted = (front + back) * 0.5f - pos.z;
	m_locoInfo->m_overlapZ += m_locoInfo->m_overlapZVel;

	if ((wanted >= oldZ && wanted <= m_locoInfo->m_overlapZ) || (wanted <= oldZ && wanted >= m_locoInfo->m_overlapZ))
	{
		m_locoInfo->m_overlapZ = wanted;
		m_locoInfo->m_overlapZVel = 0.0f;
	}
	else if ((wanted > m_locoInfo->m_overlapZ && m_locoInfo->m_overlapZVel < 0.0f) || (wanted < m_locoInfo->m_overlapZ && m_locoInfo->m_overlapZVel > 0.0f))
	{
		m_locoInfo->m_overlapZ -= m_locoInfo->m_overlapZVel;
		m_locoInfo->m_overlapZVel = 0.0f;
	}
	else if (wanted > m_locoInfo->m_overlapZ)
	{
		m_locoInfo->m_overlapZVel += 1.5f;
	}
	else
	{
		m_locoInfo->m_overlapZVel -= 0.5f;
	}

	info.m_totalZ = m_locoInfo->m_overlapZ;
	info.m_totalYaw = 0.0f;
}

void Drawable::calcPhysicsXformGiantBird(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	// RW 0x670E09 / donor 0x00270B0F: roll ease towards banked angle, clamped to +-0.8
	m_locoInfo->m_giantBirdRoll *= 0.8f;
	info.m_totalRoll = m_locoInfo->m_giantBirdRoll;
	info.m_totalPitch = 0.0f;
	info.m_totalYaw = 0.0f;
	info.m_totalZ = 0.0f;
}

void Drawable::calcPhysicsXformShip(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const float ACCEL_PITCH_LIMIT = loco->m_accelerationPitchLimit;
	const float PITCH_STIFFNESS = loco->m_pitchStiffness;
	const float ROLL_STIFFNESS = loco->m_rollStiffness;
	const float PITCH_DAMPING = loco->m_pitchDamping;
	const float ROLL_DAMPING = loco->m_rollDamping;
	const float FORWARD_VEL_COEF = loco->m_forwardVelocityPitchFactor;
	const float FORWARD_ACCEL_COEF = loco->m_forwardAccelerationPitchFactor;
	const float LATERAL_ACCEL_COEF = loco->m_lateralAccelerationRollFactor;
	const float UNIFORM_AXIAL_DAMPING = loco->m_uniformAxialDamping;
	const float PITCH_KICK = loco->m_bounceAmount;

	float length = 20.0f;
	float width = 10.0f;
	float height = 15.0f;
	if (m_template)
	{
		const std::vector<ObjectGeometry::Shape> &shapes = ObjectGeometry::cachedShapesOf(*m_template);
		if (!shapes.empty())
		{
			length = shapes[0].majorRadius;
			width = shapes[0].minorRadius;
			height = shapes[0].height;
		}
	}

	Coord3D dir{ pose.basis[0], pose.basis[3], 0.0f };
	float dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
	if (dirLen > 0.0f)
	{
		dir.x /= dirLen;
		dir.y /= dirLen;
	}
	else
	{
		dir = Coord3D{ 1.0f, 0.0f, 0.0f };
	}

	Coord3D vel;
	vel.x = pose.position.x - rec.previousPos.x;
	vel.y = pose.position.y - rec.previousPos.y;
	vel.z = 0.0f;

	m_locoInfo->springStep(m_locoInfo->m_pitchRate, m_locoInfo->m_pitch, PITCH_STIFFNESS, PITCH_DAMPING);
	m_locoInfo->springStep(m_locoInfo->m_rollRate, m_locoInfo->m_roll, ROLL_STIFFNESS, ROLL_DAMPING);
	m_locoInfo->m_pitch += UNIFORM_AXIAL_DAMPING * m_locoInfo->m_pitchRate;
	m_locoInfo->m_roll += UNIFORM_AXIAL_DAMPING * m_locoInfo->m_rollRate;

	m_locoInfo->springStep(m_locoInfo->m_accelerationPitchRate, m_locoInfo->m_accelerationPitch, PITCH_STIFFNESS, PITCH_DAMPING);
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;
	m_locoInfo->springStep(m_locoInfo->m_accelerationRollRate, m_locoInfo->m_accelerationRoll, ROLL_STIFFNESS, ROLL_DAMPING);
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	info.m_totalPitch = m_locoInfo->m_pitch + m_locoInfo->m_accelerationPitch;
	info.m_totalRoll = m_locoInfo->m_roll + m_locoInfo->m_accelerationRoll;

	if (!rec.aboveTerrain)
	{
		float forwardVel = vel.x * dir.x + vel.y * dir.y;
		m_locoInfo->m_pitchRate += -(FORWARD_VEL_COEF * forwardVel);
		m_locoInfo->m_accelerationPitchRate += -(FORWARD_ACCEL_COEF * forwardVel * 0.1f);
	}

	if (m_locoInfo->m_accelerationPitch > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	// Bobbing modulators (donor 0x00272FB6: pitch and roll sinusoidal bobbing)
	m_locoInfo->m_yawModulator += 0.05f;
	info.m_totalRoll += std::sin(m_locoInfo->m_yawModulator) * 0.02f;
	m_locoInfo->m_pitchModulator += 0.03f;
	info.m_totalPitch += std::cos(m_locoInfo->m_pitchModulator) * 0.02f;

	float pitchHeight = std::sin(info.m_totalPitch) * length;
	float rollHeight = std::sin(info.m_totalRoll) * width;
	const float Z_SCALE = 0.7f;
	info.m_totalZ = (std::abs(pitchHeight) + std::abs(rollHeight)) * Z_SCALE - 0.5f * height;
	if (info.m_totalZ < 0.0f)
		info.m_totalZ = 0.0f;
	info.m_totalYaw = 0.0f;
}

void Drawable::applyPhysicsXform(const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose)
{
	if (!rec.locomotorTemplate)
	{
		m_hasPhysicsXform = false;
		m_physicsStepped = false;
		return;
	}
	// RW 0x67BDF6 .. 0x67BE17: the step is made once per client frame (`call [TheGameClient + 0x7C]`, compared with the stamp at the locomotor info
	// + 0xAC, RW 0x67BE04 `cmp` / `je`; stamped at RW 0x67BE17). Retail draws one picture per client frame, so it has nothing between two steps;
	// INFERENCE: the port, drawing more often, keeps the last step's angles and height on the pictures between two client frames
	const bool step = !m_services.clientFrame || !m_physicsStepped || *m_services.clientFrame != m_physicsClientFrame;
	if (step)
	{
		PhysicsXformInfo info;
		m_hasPhysicsXform = calcPhysicsXform(rec, pose, info);
		if (m_hasPhysicsXform)
		{
			m_lastPhysicsInfo = info;
		}
		m_physicsStepped = true;
		m_physicsClientFrame = m_services.clientFrame ? *m_services.clientFrame : 0;
	}
	if (m_hasPhysicsXform)
	{
		const PhysicsXformInfo &info = m_lastPhysicsInfo;

		Matrix3D mtx;
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				mtx.Row[r][c] = pose.basis[r * 3 + c];
			}
		}
		mtx.Row[0][3] = pose.position.x;
		mtx.Row[1][3] = pose.position.y;
		mtx.Row[2][3] = pose.position.z;

		// Retail RW 0x67BE90 (donor BFME2 0x0027BB65):
		// Translate Z, Rotate Y (pitch), Rotate X (-roll), Rotate Z (yaw)
		mtx.Translate(Vector3(0.0f, 0.0f, info.m_totalZ));
		mtx.Rotate_Y(info.m_totalPitch);
		mtx.Rotate_X(-info.m_totalRoll);
		mtx.Rotate_Z(info.m_totalYaw);

		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				m_presentationBasis[r * 3 + c] = mtx.Row[r][c];
			}
		}
		m_presentationPosition.x = mtx.Row[0][3];
		m_presentationPosition.y = mtx.Row[1][3];
		m_presentationPosition.z = mtx.Row[2][3];
	}
}

void Drawable::getPresentationPose(float outBasis[9], Coord3D &outPos) const
{
	if (m_hasPhysicsXform)
	{
		std::memcpy(outBasis, m_presentationBasis, sizeof(m_presentationBasis));
		outPos = m_presentationPosition;
	}
	else
	{
		std::memcpy(outBasis, getBasis(), sizeof(float) * 9);
		outPos = *getPosition();
	}
}

Coord3D Drawable::getPresentationPosition() const
{
	if (m_hasPhysicsXform)
	{
		return m_presentationPosition;
	}
	return *getPosition();
}

// LOCAL-25: moved unchanged from Drawable.h
void DrawableLocoInfo::springStep(float &rate, float position, float stiffness, float damping)
{
	rate += (-stiffness * (position * (1.0f - damping))) + (-damping * rate);
}

// LOCAL-25: moved unchanged from DrawableManager::advance
void DrawableManager::advanceClientFrame(double elapsedMs)
{
	// LOCAL-25: RotWK's shared 30 Hz client frame counter (RW 0x6487D6 via GameClient::update RW 0x64849E); the presentation physics steps once per frame
	m_clientClockMs += elapsedMs;
	int clientTicks = 0;
	static constexpr double kClientTickMs = 1000.0 / 30.0;
	while (m_clientClockMs >= kClientTickMs - 1e-6 && clientTicks < 8)
	{
		m_clientClockMs -= kClientTickMs;
		++clientTicks;
		++m_clientFrame;
	}
	if (clientTicks >= 8 || m_clientClockMs < 0.0)
	{
		m_clientClockMs = 0.0; // clamp a stall
	}
}
