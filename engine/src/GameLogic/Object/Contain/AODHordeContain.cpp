// OpenBFME. GPL-3.0.
//
// AODHordeContain: the Army of the Dead's horde contain (RW 0x87E647).
//
// TARGET FACTS (RotWK 2.01 game.dat):
//   * Module data constructor RW 0x64B845 -> 0x87D55F, field table RW 0x87D62E -> 0xC5CE50 (20 rows; FrequencyRandomness
//     and FrequencyRandomnessZ appear twice as in the binary; offsets +0x284..+0x2C8).
//   * Runtime constructor RW 0x64B80A -> 0x87E647 (sizeof 0x8E0, primary vtable 0xC5D3F0, contain vtable 0xC5CFA8).
//     onObjectCreated (vtable +0x14) is RW 0x63F3BF (no-op); buildSlots(true) (RW 0x87E8FC) runs lazily on the first
//     addToContain (RW 0x872D98), after the horde object has received its world position and orientation:
//       - HordeContain::buildSlots(fromScratch) (RW 0x877751);
//       - 6 GameLogicRandom::getValueReal draws per slot (RW 0x87E93E..0x87EAA8, file "AODHordeContain.cpp", lines
//         0x20A ampXY, 0x20B phaseXY, 0x20C freqXY, 0x20F ampZ, 0x210 phaseZ, 0x211 freqZ);
//       - distinct slot.x values (up to 20, RW 0x87EAD3..0x87EB4F) form RankSpineNode records positioned along the
//         horde's facing by (slotX - rank0.slotX, 0) (RW 0x87EB55..0x87EBBF), then pushed tail-to-head into the
//         60-entry breadcrumb ring (RW 0x87EBC1..0x87EBEA -> 0x87D9A1).
//   * Member pass pre-step RW 0x87D9F6: moveStep = loco ? maxSpeed : 100/fps, sampleThreshold = loco ? maxAccel : 10/fps;
//     pushes (hordePos, hordeAngle) when dist3D(hordePos, rank[0].pos) > sampleThreshold; updates each RankSpineNode
//     along the breadcrumb polyline by arc-length (rank[0].slotX - rank[r].slotX) with +-10 deg/frame angle clamping
//     (RW 0x87D691); advances m_scatterProgress by moveStep while the horde has OATH_FULLFILLED (RW 0x87DABE..0x87DADD).
//     Zero RNG calls per frame.
//   * Intended member position RW 0x87DEFB: delegates to HordeContain::getMemberFormationPosition when meleeEngaged;
//     otherwise starts from the matching RankSpineNode + lateral slot.y perpendicular to rank.angle, adds XY sine sway
//     along the rank's lateral axis (blending StillAmplitude..1 by speed/maxSpeed; marks dirty when StillAmplitude > 0.01),
//     adds OATH_FULLFILLED radial XY scatter along rotated slot offset using (slotIdx % 5) * 0.25 * ScatterRandomness +
//     ScatterSpeedFactor, samples water height (isUnderwater) else ground height on the horde's layer, adds Z sine sway
//     shifted by +0.95 (marks dirty when StillAmplitudeZ > 0.01), adds large-unit height lift within LargeUnitTimeout
//     (fading over the second half, stadium distance with 17.0 lateral half-width and u*sqrt(u)+(1-u)*u^2 falloff over
//     LargeUnitTailOff * clampedHeight), and adds OATH_FULLFILLED Z scatter using ((slotIdx + 2) % 5).
//   * Member order RW 0x87DB01: clears JUST_BUILT; leaves members with an active AI state (clearing MOVING); delegates
//     to HordeContain when meleeEngaged; otherwise mirrors MOVING and OATH_FULLFILLED from the horde, turns toward the
//     outward slot ray when OATH_FULLFILLED (else takes rank.angle), steps 3D position toward slotPos by at most
//     1.25 * memberMaxSpeed (9999 without locomotor), sets orientation, position, pendingPosition, and unstuns physics.
//   * Large-object collision RW 0x87E894 + 0x87D919: records living collide targets with DeadCollideSize == LARGE (2),
//     storing position, extent 17.0, ObjectGeometry::maxHeightAbovePosition, and the start logic frame.

#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <algorithm>
#include <variant>

namespace
{
constexpr float kPi = 3.14159274f;            // RW 0xBDD388
constexpr float kTwoPi = 6.28318548f;         // RW 0xBDD38C
constexpr float kTenDegRad = 0.17453294f;     // RW 0x3E32B8C3 (10 deg in radians)
constexpr float kStillDirtyEps = 0.0099999998f; // RW 0xBE5600 (0.01f)
constexpr float kZWaveBias = 0.949999988f;    // RW 0xBDAD74 (0.95f)
constexpr float kQuarter = 0.25f;             // RW 0xBD1904
constexpr float kMemberSpeedBoost = 1.25f;    // RW 0xC2E434
constexpr float kFallbackStep = 9999.0f;      // RW 0xC18414
constexpr float kDefaultMovePerSec = 100.0f;  // RW 0xBD88D8
constexpr float kDefaultSamplePerSec = 10.0f; // RW 0xBD83D8
constexpr float kLargeUnitHalfWidth = 17.0f;  // RW 0xBDC4DC
constexpr const char *kRngFile = "AODHordeContain.cpp";

float normalizeAngle(float a)
{
	if (a > kPi)
	{
		do
		{
			a = SimMath::subf32(a, kTwoPi);
		} while (a > kPi);
	}
	if (-kPi < a)
	{
		return a;
	}
	do
	{
		a = SimMath::addf32(a, kTwoPi);
	} while (-kPi >= a);
	return a;
}

float clampZeroOne(float v)
{
	if (v < 0.0f)
	{
		return 0.0f;
	}
	if (v > 1.0f)
	{
		return 1.0f;
	}
	return v;
}

// RW 0x86BFB5: rotate local (lx, ly) by `angle` using wide sin/cos and PC24 multiply/add/sub.
void rotate2D(float angle, float lx, float ly, float &outX, float &outY)
{
	const float c = SimMath::cosf32(angle);
	const float s = SimMath::sinf32(angle);
	outX = SimMath::fstpDword(SimMath::pc24SubW(SimMath::pc24MulW((double)c, (double)lx), SimMath::pc24MulW((double)s, (double)ly)));
	outY = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW((double)lx, (double)s), SimMath::pc24MulW((double)ly, (double)c)));
}

// RW 0x405406 Coord2D::toAngle
float toAngle2D(float x, float y)
{
	const float len = SimMath::length2d(x, y);
	if (len == 0.0f)
	{
		return 0.0f;
	}
	float c = SimMath::sseDiv(x, len);
	if (c < -1.0f)
	{
		c = -1.0f;
	}
	else if (c > 1.0f)
	{
		c = 1.0f;
	}
	const float a = (float)SimMath::acosDet((double)c);
	return 0.0f <= y ? a : SimMath::subf32(0.0f, a);
}

// RW 0x403312 Coord2D::normalize
void normalize2D(float &x, float &y)
{
	const float len = SimMath::length2d(x, y);
	if (len == 0.0f)
	{
		return;
	}
	const float inv = SimMath::sseDiv(1.0f, len);
	x = SimMath::sseMul(x, inv);
	y = SimMath::sseMul(y, inv);
}

// RW 0x403175 Coord3D::normalize
void normalize3D(Coord3D &v)
{
	const float len = (float)SimMath::length3d(v.x, v.y, v.z);
	if (len == 0.0f)
	{
		return;
	}
	const float inv = SimMath::sseDiv(1.0f, len);
	v.x = SimMath::sseMul(v.x, inv);
	v.y = SimMath::sseMul(v.y, inv);
	v.z = SimMath::sseMul(v.z, inv);
}

int deadCollideSizeOf(const Object &obj)
{
	const FieldValue *field = obj.getTemplate() ? obj.getTemplate()->findField("DeadCollideSize") : nullptr;
	if (!field || std::holds_alternative<std::monostate>(*field))
	{
		return 0; // RW 0x73FFAF: SMALL default
	}
	if (const long long *n = std::get_if<long long>(field))
	{
		return *n == 0 ? 0 : (*n == 1 ? 1 : 2);
	}
	return 0;
}
} // namespace

// RW 0x87E647 (AODHordeContain ctor 0xC5D3F0), RW 0x87E7CF (~AODHordeContain)
AODHordeContain::AODHordeContain(Thing *thing, const HordeContainBehaviorData *data)
	: HordeContain(thing, data)
	, m_aodData(dynamic_cast<const AODHordeContainBehaviorData *>(data))
{
}

AODHordeContain::~AODHordeContain() = default;

const AODHordeContainBehaviorData &AODHordeContain::aodData() const
{
	static const AODHordeContainBehaviorData kDefaultData{};
	return m_aodData ? *m_aodData : kDefaultData;
}

void AODHordeContain::onObjectCreated()
{
	// RW 0xC5D3F0 + 0x14 = 0x63F3BF (no-op): slots and rank spine records build lazily on the first addToContain
	// (RW 0x872D98 -> 0x87E8FC), once the horde object has received its world position and orientation.
}

void AODHordeContain::pushPathSample(const Coord3D &pos)
{
	// RW 0x87D9A1: shift [0 .. count-1] right by 1 (clamped to kMaxPathSamples = 60) and write head [0].
	if (m_pathSampleCount < kMaxPathSamples)
	{
		++m_pathSampleCount;
	}
	for (int i = m_pathSampleCount - 1; i > 0; --i)
	{
		m_pathSamples[(size_t)i] = m_pathSamples[(size_t)(i - 1)];
	}
	m_pathSamples[0].pos = pos;
	m_pathSamples[0].angle = getObject()->getOrientation();
}

void AODHordeContain::buildAodSlots()
{
	// RW 0x87E8FC .. 0x87EBEE
	HordeContain::onObjectCreated(); // m_core->buildSlots(true) (RW 0x877751)
	const std::vector<HordeContainCore::Slot> &slots = core().slots();
	const size_t slotCount = slots.size();
	m_swayRecords.clear();
	m_swayRecords.reserve(slotCount);

	Object *horde = getObject();
	GameLogicRandom &rng = horde->logic().random();
	const AODHordeContainBehaviorData &d = aodData();

	for (size_t i = 0; i < slotCount; ++i)
	{
		SwayRecord rec;
		rec.ampFactorXY = rng.getValueReal(
			SimMath::pc24Sub(1.0f, d.amplitudeRandomness),
			SimMath::pc24Add(d.amplitudeRandomness, 1.0f),
			kRngFile, 0x20A);
		rec.phaseXY = rng.getValueReal(
			0.0f,
			SimMath::pc24Mul(d.frequencyRandomness, kTwoPi),
			kRngFile, 0x20B);
		rec.freqFactorXY = rng.getValueReal(
			SimMath::pc24Sub(1.0f, d.frequencyRandomness),
			SimMath::pc24Add(d.frequencyRandomness, 1.0f),
			kRngFile, 0x20C);
		rec.ampFactorZ = rng.getValueReal(
			SimMath::pc24Sub(1.0f, d.amplitudeRandomnessZ),
			SimMath::pc24Add(d.amplitudeRandomnessZ, 1.0f),
			kRngFile, 0x20F);
		rec.phaseZ = rng.getValueReal(
			0.0f,
			SimMath::pc24Mul(d.frequencyRandomnessZ, kTwoPi),
			kRngFile, 0x210);
		rec.freqFactorZ = rng.getValueReal(
			SimMath::pc24Sub(1.0f, d.frequencyRandomnessZ),
			SimMath::pc24Add(d.frequencyRandomnessZ, 1.0f),
			kRngFile, 0x211);
		m_swayRecords.push_back(rec);
	}

	// RW 0x87EAAB .. 0x87EB4F: collect up to 20 distinct slot.x rows in slot order.
	m_rankCount = 0;
	for (size_t i = 0; i < slotCount && m_rankCount < kMaxRanks; ++i)
	{
		const float sx = slots[i].x;
		bool exists = false;
		for (int r = 0; r < m_rankCount; ++r)
		{
			if (m_ranks[(size_t)r].slotX == sx)
			{
				exists = true;
				break;
			}
		}
		if (!exists)
		{
			RankSpineNode &node = m_ranks[(size_t)m_rankCount++];
			node.slotX = sx;
			node.angle = 0.0f;
			node.pos = Coord3D{ sx, 0.0f, 0.0f };
			node.valid = true;
		}
	}

	// RW 0x87EB55 .. 0x87EBBF: place each rank spine node along horde facing relative to rank 0.
	const Coord3D hordePos = *horde->getPosition();
	const float hordeAngle = horde->getOrientation();
	const float rank0X = m_rankCount > 0 ? m_ranks[0].slotX : 0.0f;
	for (int r = 0; r < m_rankCount; ++r)
	{
		RankSpineNode &node = m_ranks[(size_t)r];
		const float localDx = SimMath::sseSub(node.pos.x, rank0X);
		float rotX = 0.0f, rotY = 0.0f;
		rotate2D(hordeAngle, localDx, node.pos.y, rotX, rotY);
		node.pos.x = SimMath::pc24Add(rotX, hordePos.x);
		node.pos.y = SimMath::pc24Add(rotY, hordePos.y);
		node.pos.z = hordePos.z;
		node.angle = hordeAngle;
	}

	// RW 0x87EBC1 .. 0x87EBEA: push tail-to-head so m_pathSamples[0] is rank 0 and [m_rankCount - 1] is the rear rank.
	m_pathSampleCount = 0;
	for (int r = m_rankCount - 1; r >= 0; --r)
	{
		pushPathSample(m_ranks[(size_t)r].pos);
	}
}

bool AODHordeContain::addToContain(Object *obj)
{
	if (!obj)
	{
		return false;
	}
	// RW 0x0087D649 (AODHordeContain::onContaining): fadeIn 5s + ExperienceTracker::gainLevel(true) (RW 0x0087D673 -> 0x79D7AD)
	if (ObjectClientHooks *client = getObject()->logic().clientHooks())
	{
		client->fadeIn(*obj, LOGICFRAMES_PER_SECOND * 5);
	}
	if (ExperienceTracker *xp = obj->getExperienceTracker())
	{
		xp->gainLevel(true);
	}
	const bool hadKeptSlot = core().slotOf(obj->getID()) >= 0;
	if (!core().built())
	{
		buildAodSlots(); // RW 0x872D98 -> 0x87E8FC
	}
	const bool added = HordeContain::addToContain(obj);
	if (added && !hadKeptSlot && core().slotOf(obj->getID()) >= 0)
	{
		// RW 0x872DCB .. 0x872E58: place newly slotted member at AODHordeContain's intended 3D formation position.
		const Coord3D pos = getMemberFormationPosition(obj);
		obj->setPosition(&pos);
		obj->setOrientation(getObject()->getOrientation());
	}
	m_aodDirty = true;
	return added;
}

void AODHordeContain::removeFromContain(Object *obj)
{
	HordeContain::removeFromContain(obj);
	m_aodDirty = true;
}

void AODHordeContain::markDirty()
{
	HordeContain::markDirty();
	m_aodDirty = true;
}

void AODHordeContain::updateFormation()
{
	HordeContain::updateFormation();
	m_aodDirty = true;
}

void AODHordeContain::onLargeObjectCollide(Object *member, Object *other)
{
	// RW 0x87E894 + 0x87D919
	(void)member;
	if (!other || other->isEffectivelyDead())
	{
		return;
	}
	if (deadCollideSizeOf(*other) != 2) // DeadCollideSize == LARGE
	{
		return;
	}
	m_largeUnitID = other->getID();
	m_largeUnitPos = *other->getPosition();
	m_largeUnitExtent = kLargeUnitHalfWidth; // RW 0xBDC4DC (17.0f)
	m_largeUnitHeight = ObjectGeometry::maxHeightAbovePosition(other->geometryShapes()); // RW 0xAD1920
	m_largeUnitStartFrame = getObject()->logic().getFrame();
	m_aodDirty = true;
}

int AODHordeContain::findRankForSlotX(float slotX) const
{
	for (int r = m_rankCount - 1; r >= 1; --r)
	{
		if (m_ranks[(size_t)r].slotX == slotX)
		{
			return r;
		}
	}
	return 0;
}

float AODHordeContain::surfaceHeightAt(float x, float y) const
{
	// RW 0x87E288 .. 0x87E2DC: standing water height when underwater, else ground height on the horde's layer.
	const Object *horde = getObject();
	if (AIUpdateInterface *ai = const_cast<Object *>(horde)->getAIUpdateInterface())
	{
		LocomotorHost &host = ai->locomotorHost();
		float waterZ = 0.0f;
		if (host.isUnderwater(x, y, &waterZ))
		{
			return waterZ;
		}
		return host.groundHeightAt(x, y);
	}
	return horde->logic().getGroundHeight(x, y);
}

void AODHordeContain::updateRankSpineNode(int rankIdx)
{
	// RW 0x87D691 .. 0x87D916
	Object *horde = getObject();
	RankSpineNode &node = m_ranks[(size_t)rankIdx];
	float desiredAngle = horde->getOrientation();

	if (rankIdx == 0)
	{
		node.pos = *horde->getPosition();
	}
	else
	{
		float remaining = SimMath::sseSub(m_ranks[0].slotX, node.slotX);
		Coord3D prev = *horde->getPosition();
		float prevAngle = horde->getOrientation();
		bool placed = false;

		for (int s = 0; s < m_pathSampleCount; ++s)
		{
			const PathSample &sample = m_pathSamples[(size_t)s];
			const float dx = SimMath::sseSub(prev.x, sample.pos.x);
			const float dy = SimMath::sseSub(prev.y, sample.pos.y);
			const float dz = SimMath::sseSub(prev.z, sample.pos.z);
			const float segLen = (float)SimMath::length3d(dx, dy, dz);
			if (segLen >= remaining)
			{
				const float t = segLen == 0.0f ? 0.0f : SimMath::sseSub(1.0f, SimMath::sseDiv(remaining, segLen));
				node.pos.x = SimMath::sseAdd(SimMath::sseMul(dx, t), sample.pos.x);
				node.pos.y = SimMath::sseAdd(SimMath::sseMul(dy, t), sample.pos.y);
				node.pos.z = SimMath::sseAdd(SimMath::sseMul(dz, t), sample.pos.z);
				desiredAngle = sample.angle;
				placed = true;
				break;
			}
			remaining = SimMath::sseSub(remaining, segLen);
			prev = sample.pos;
			prevAngle = sample.angle;
		}

		if (!placed)
		{
			// RW 0x87D80E .. 0x87D8AC: extrapolate behind the last breadcrumb along prevAngle.
			const float negRem = SimMath::sseSub(0.0f, remaining);
			float offX = 0.0f, offY = 0.0f;
			rotate2D(prevAngle, negRem, 0.0f, offX, offY);
			node.pos.x = SimMath::sseAdd(prev.x, offX);
			node.pos.y = SimMath::sseAdd(prev.y, offY);
			node.pos.z = prev.z;
			desiredAngle = prevAngle;
		}
	}

	// RW 0x87D8AC .. 0x87D912: smooth rank yaw at +-10 deg/frame.
	float delta = normalizeAngle(SimMath::pc24Sub(desiredAngle, node.angle));
	if (delta > kTenDegRad)
	{
		delta = kTenDegRad;
	}
	else if (delta < -kTenDegRad)
	{
		delta = -kTenDegRad;
	}
	node.angle = normalizeAngle(SimMath::pc24Add(node.angle, delta));
}

void AODHordeContain::updateRankSpine()
{
	for (int r = 0; r < m_rankCount; ++r)
	{
		updateRankSpineNode(r);
	}
}

void AODHordeContain::stepSpineAndScatter()
{
	// RW 0x87D9F6 .. 0x87DAE3
	Object *horde = getObject();
	const Coord3D hordePos = *horde->getPosition();

	float moveStep = SimMath::pc24DivD((double)kDefaultMovePerSec, (double)LOGICFRAMES_PER_SECOND);
	float sampleThreshold = SimMath::pc24DivD((double)kDefaultSamplePerSec, (double)LOGICFRAMES_PER_SECOND);
	if (AIUpdateInterface *ai = horde->getAIUpdateInterface())
	{
		if (Locomotor *loco = ai->curLocomotor())
		{
			moveStep = loco->getMaxSpeedForCondition(ai->locomotorHost());
			sampleThreshold = loco->getMaxAccelerationForSpeed(moveStep);
		}
	}

	if (m_rankCount > 0)
	{
		const float dx = SimMath::sseSub(hordePos.x, m_ranks[0].pos.x);
		const float dy = SimMath::sseSub(hordePos.y, m_ranks[0].pos.y);
		const float dz = SimMath::sseSub(hordePos.z, m_ranks[0].pos.z);
		const float dist = (float)SimMath::length3d(dx, dy, dz);
		if (dist > sampleThreshold)
		{
			pushPathSample(hordePos);
		}
		updateRankSpine();
	}

	static const int kOathBit = CombatNames::modelCondition("OATH_FULLFILLED");
	if (kOathBit >= 0 && horde->testModelCondition(kOathBit))
	{
		m_scatterProgress = SimMath::sseAdd(m_scatterProgress, moveStep);
	}
}

Coord3D AODHordeContain::getMemberFormationPosition(const Object *member) const
{
	// RW 0x87DEFB .. 0x87E513
	if (!member || meleeEngaged())
	{
		return HordeContain::getMemberFormationPosition(member);
	}
	const std::vector<HordeContainCore::Slot> &slots = core().slots();
	if (!core().built() || slots.empty() || m_rankCount <= 0)
	{
		return HordeContain::getMemberFormationPosition(member);
	}

	const int slotIdx = core().slotIndexOrZero(member->getID());
	if (slotIdx < 0 || (size_t)slotIdx >= slots.size())
	{
		return HordeContain::getMemberFormationPosition(member);
	}

	const float sx = slots[(size_t)slotIdx].x;
	const float sy = slots[(size_t)slotIdx].y;
	const int rankIdx = findRankForSlotX(sx);
	const RankSpineNode &rank = m_ranks[(size_t)rankIdx];
	const float rankAngle = rank.angle;

	// RW 0x87DF8C .. 0x87DFCE: lateral slot.y perpendicular to the rank spine node's facing.
	Coord3D basePos = rank.pos;
	basePos.x = SimMath::fstpDword(SimMath::pc24SubW((double)basePos.x, SimMath::pc24MulW(SimMath::sind(rankAngle), (double)sy)));
	basePos.y = SimMath::fstpDword(SimMath::pc24AddW((double)basePos.y, SimMath::pc24MulW(SimMath::cosd(rankAngle), (double)sy)));

	if ((size_t)slotIdx >= m_swayRecords.size())
	{
		return basePos;
	}

	const Object *horde = getObject();
	const AODHordeContainBehaviorData &d = aodData();
	const SwayRecord &rec = m_swayRecords[(size_t)slotIdx];
	const unsigned frame = horde->logic().getFrame();

	// RW 0x87DFF2 .. 0x87E02F: curSpeed / maxSpeed ratio on the horde locomotor.
	float speedRatio = 0.0f;
	if (AIUpdateInterface *ai = const_cast<Object *>(horde)->getAIUpdateInterface())
	{
		if (Locomotor *loco = ai->curLocomotor())
		{
			const float maxSpeed = loco->getMaxSpeedForCondition(ai->locomotorHost());
			if (maxSpeed > 0.0f)
			{
				speedRatio = SimMath::pc24Div(loco->speed(), maxSpeed);
			}
		}
	}

	// RW 0x87E032 .. 0x87E13B: XY sine sway perpendicular to rankAngle.
	const float stillXY = clampZeroOne(d.stillAmplitude);
	const double argXY = SimMath::pc24AddW(
		SimMath::pc24MulW(SimMath::pc24MulW(SimMath::fildU32(frame), (double)d.frequencyScale), (double)rec.freqFactorXY),
		(double)rec.phaseXY);
	const float waveXY = SimMath::fstpDword(SimMath::pc24MulW(NumericState::sinDD(argXY), (double)rec.ampFactorXY));
	const float blendXY = SimMath::sseAdd(SimMath::sseMul(SimMath::sseSub(1.0f, stillXY), speedRatio), stillXY);
	const float latSway = SimMath::sseMul(SimMath::sseMul(blendXY, d.amplitudeScale), waveXY);
	const float swayLen = SimMath::length2d(0.0f, latSway);
	const float swayWorldAngle = SimMath::pc24Add(toAngle2D(0.0f, latSway), rankAngle);
	float offX = SimMath::pc24Mul(SimMath::cosf32(swayWorldAngle), swayLen);
	float offY = SimMath::pc24Mul(SimMath::sinf32(swayWorldAngle), swayLen);

	// RW 0x87E13E .. 0x87E1D1: Z sine sway biased upward by +0.95f.
	const float stillZ = clampZeroOne(d.stillAmplitudeZ);
	const double argZ = SimMath::pc24AddW(
		SimMath::pc24MulW(SimMath::pc24MulW(SimMath::fildU32(frame), (double)d.frequencyScaleZ), (double)rec.freqFactorZ),
		(double)rec.phaseZ);
	const double waveZ = SimMath::pc24AddW(SimMath::pc24MulW(NumericState::sinDD(argZ), (double)rec.ampFactorZ), (double)kZWaveBias);
	const double blendZ = SimMath::pc24MulW(
		SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24SubW(1.0, (double)stillZ), (double)speedRatio), (double)stillZ),
		(double)d.amplitudeScaleZ);
	const float zSway = SimMath::fstpDword(SimMath::pc24MulW(blendZ, waveZ));

	// RW 0x87E1D3 .. 0x87E264: OATH_FULLFILLED radial XY scatter along rotated slot offset.
	static const int kOathBit = CombatNames::modelCondition("OATH_FULLFILLED");
	const bool memberOath = kOathBit >= 0 && member->testModelCondition(kOathBit);
	if (memberOath)
	{
		float dirX = 0.0f, dirY = 0.0f;
		rotate2D(horde->getOrientation(), sx, sy, dirX, dirY);
		normalize2D(dirX, dirY);
		const float mod5 = SimMath::sseFromInt32(slotIdx % 5);
		const float randTerm = SimMath::sseMul(SimMath::sseMul(SimMath::sseMul(mod5, d.scatterRandomness), m_scatterProgress), kQuarter);
		const float baseTerm = SimMath::sseMul(d.scatterSpeedFactor, m_scatterProgress);
		const float scatterXY = SimMath::sseAdd(randTerm, baseTerm);
		offX = SimMath::sseAdd(SimMath::sseMul(dirX, scatterXY), offX);
		offY = SimMath::sseAdd(SimMath::sseMul(dirY, scatterXY), offY);
	}

	// RW 0x87E264 .. 0x87E2E9: world XY + water/ground Z + Z sway.
	Coord3D result;
	result.x = SimMath::pc24Add(offX, basePos.x);
	result.y = SimMath::pc24Add(offY, basePos.y);
	result.z = SimMath::sseAdd(surfaceHeightAt(result.x, result.y), zSway);

	// RW 0x87E2DE .. 0x87E46E: large-unit height lift.
	bool timeoutJustExpired = false;
	if (m_largeUnitID != INVALID_ID)
	{
		float peakHeight = SimMath::sseMul(d.largeUnitHeightFactor, m_largeUnitHeight);
		if (peakHeight > d.largeUnitMaxHeight)
		{
			peakHeight = d.largeUnitMaxHeight;
		}
		if (d.largeUnitMinHeight > peakHeight)
		{
			peakHeight = d.largeUnitMinHeight;
		}

		const unsigned elapsed = frame - m_largeUnitStartFrame;
		if (elapsed >= d.largeUnitTimeout)
		{
			timeoutJustExpired = true;
		}
		else
		{
			const unsigned halfTimeout = d.largeUnitTimeout >> 1;
			float timeFade = 1.0f;
			if (elapsed > halfTimeout && halfTimeout > 0)
			{
				timeFade = SimMath::fstpDword(
					SimMath::pc24SubW(1.0, SimMath::pc24DivW(SimMath::fildU32(elapsed - halfTimeout), SimMath::fildU32(halfTimeout))));
			}

			const float relX = SimMath::pc24Sub(result.x, m_largeUnitPos.x);
			const float relY = SimMath::pc24Sub(result.y, m_largeUnitPos.y);
			const float dirX = SimMath::cosf32(horde->getOrientation()); // RW 0x70B9E0
			const float dirY = SimMath::sinf32(horde->getOrientation());
			const float along = (float)SimMath::absD(
				SimMath::pc24AddW(SimMath::pc24MulW((double)relX, (double)dirX), SimMath::pc24MulW((double)relY, (double)dirY)));
			const float across = (float)SimMath::absD(
				SimMath::pc24AddW(SimMath::pc24MulW((double)relY, (double)dirX), SimMath::pc24MulW((double)relX, (double)SimMath::sseSub(0.0f, dirY))));
			float acrossExcess = SimMath::sseSub(across, m_largeUnitExtent);
			if (acrossExcess < 0.0f)
			{
				acrossExcess = 0.0f;
			}
			const float effDist = SimMath::sseAdd(acrossExcess, along);
			const float radius = SimMath::sseMul(d.largeUnitTailOff, peakHeight);
			if (radius > effDist)
			{
				const float u = SimMath::pc24Div(effDist, radius);
				const double uSqrt = SimMath::pc24MulW(SimMath::sqrtPC24((double)u), (double)u);
				const double uQuad = SimMath::pc24MulW(SimMath::pc24SubW(1.0, (double)u), (double)SimMath::sseMul(u, u));
				const double falloff = SimMath::pc24AddW(uSqrt, uQuad);
				const float lift = SimMath::fstpDword(
					SimMath::pc24MulW(SimMath::pc24MulW(SimMath::pc24SubW(1.0, falloff), (double)peakHeight), (double)timeFade));
				result.z = SimMath::pc24Add(lift, result.z);
			}
		}
	}

	// RW 0x87E46E .. 0x87E4D0: OATH_FULLFILLED Z scatter (skipped on the exact call where LargeUnitTimeout expires, RW 0x87E352).
	if (!timeoutJustExpired && memberOath)
	{
		const float mod5Z = SimMath::sseFromInt32((slotIdx + 2) % 5);
		const float baseZ = SimMath::sseMul(SimMath::sseMul(d.scatterSpeedFactor, d.oathFulfilledZFactor), m_scatterProgress);
		const float randZ = SimMath::sseMul(
			SimMath::sseMul(SimMath::sseMul(SimMath::sseMul(mod5Z, d.scatterRandomness), d.oathFulfilledZFactor), m_scatterProgress),
			kQuarter);
		result.z = SimMath::sseAdd(SimMath::sseAdd(randZ, baseZ), result.z);
	}

	return result;
}

bool AODHordeContain::orderAodMember(Object &member, const Coord3D &slotPos)
{
	// RW 0x87DB01 .. 0x87DEF9
	static const int kJustBuiltBit = CombatNames::modelCondition("JUST_BUILT");
	static const int kMovingBit = CombatNames::modelCondition("MOVING");
	static const int kOathBit = CombatNames::modelCondition("OATH_FULLFILLED");

	if (kJustBuiltBit >= 0 && member.testModelCondition(kJustBuiltBit))
	{
		member.setModelConditionState(kJustBuiltBit, false);
	}

	AIUpdateInterface *mai = member.getAIUpdateInterface();
	if (mai && mai->isStateActive())
	{
		if (kMovingBit >= 0)
		{
			member.setModelConditionState(kMovingBit, false);
		}
		return false;
	}

	Object *horde = getObject();
	if (kMovingBit >= 0)
	{
		member.setModelConditionState(kMovingBit, horde->testModelCondition(kMovingBit));
	}
	const bool fulfilled = kOathBit >= 0 && horde->testModelCondition(kOathBit);
	if (kOathBit >= 0)
	{
		member.setModelConditionState(kOathBit, fulfilled);
	}

	const std::vector<HordeContainCore::Slot> &slots = core().slots();
	const int slotIdx = core().slotIndexOrZero(member.getID());
	float desiredAngle = horde->getOrientation();
	if (!slots.empty() && m_rankCount > 0 && slotIdx >= 0 && (size_t)slotIdx < slots.size())
	{
		const int rankIdx = findRankForSlotX(slots[(size_t)slotIdx].x);
		desiredAngle = m_ranks[(size_t)rankIdx].angle;
	}

	float maxStep = kFallbackStep;
	float maxTurnRate = kPi;
	if (mai)
	{
		if (Locomotor *loco = mai->curLocomotor())
		{
			maxStep = SimMath::pc24Mul(loco->getMaxSpeedForCondition(mai->locomotorHost()), kMemberSpeedBoost);
			maxTurnRate = loco->getMaxTurnRate(mai->locomotorHost());
		}
	}

	// RW 0x87DCAB .. 0x87DD34: turn toward the outward slot ray while scattering on OATH_FULLFILLED.
	if (fulfilled)
	{
		float sx = 0.0f, sy = 0.0f;
		if (!slots.empty() && slotIdx >= 0 && (size_t)slotIdx < slots.size())
		{
			sx = slots[(size_t)slotIdx].x;
			sy = slots[(size_t)slotIdx].y;
		}
		float outX = 0.0f, outY = 0.0f;
		rotate2D(horde->getOrientation(), sx, sy, outX, outY);
		const float outwardAngle = toAngle2D(outX, outY);
		const float turnCap = maxTurnRate > 0.0f ? maxTurnRate : kTenDegRad;
		const float curAngle = member.getOrientation();
		float delta = normalizeAngle(SimMath::pc24Sub(outwardAngle, curAngle));
		if (delta > turnCap)
		{
			delta = turnCap;
		}
		else if (delta < -turnCap)
		{
			delta = -turnCap;
		}
		desiredAngle = normalizeAngle(SimMath::pc24Add(delta, curAngle));
	}

	// RW 0x87DD39 .. 0x87DDBC: step 3D position toward slotPos by at most maxStep.
	const Coord3D curPos = *member.getPosition();
	Coord3D deltaPos{
		SimMath::sseSub(slotPos.x, curPos.x),
		SimMath::sseSub(slotPos.y, curPos.y),
		SimMath::sseSub(slotPos.z, curPos.z)
	};
	const float dist = (float)SimMath::length3d(deltaPos.x, deltaPos.y, deltaPos.z);
	Coord3D nextPos = slotPos;
	bool walking = false;
	if (dist > maxStep)
	{
		normalize3D(deltaPos);
		nextPos.x = SimMath::sseAdd(SimMath::sseMul(deltaPos.x, maxStep), curPos.x);
		nextPos.y = SimMath::sseAdd(SimMath::sseMul(deltaPos.y, maxStep), curPos.y);
		nextPos.z = SimMath::sseAdd(SimMath::sseMul(deltaPos.z, maxStep), curPos.z);
		walking = true;
	}

	member.setOrientation(desiredAngle);
	member.setPosition(&nextPos);
	if (mai)
	{
		mai->setPendingPosition(nextPos, horde->logic().getFrame());
	}
	if (PhysicsBehavior *phys = PhysicsBehavior::find(member))
	{
		phys->setStunned(false); // RW 0x792A69(0)
	}
	return walking;
}

bool AODHordeContain::runAodMemberPass()
{
	const AODHordeContainBehaviorData &d = aodData();
	const unsigned frame = getObject()->logic().getFrame();
	bool stillWorking = false;

	if (clampZeroOne(d.stillAmplitude) > kStillDirtyEps || clampZeroOne(d.stillAmplitudeZ) > kStillDirtyEps)
	{
		stillWorking = true; // RW 0x87E06A / 0x87E176: continuous idle hover/sway
	}

	const ContainedItemsList *items = getContainedItemsList();
	if (!items)
	{
		if (m_largeUnitID != INVALID_ID && (frame - m_largeUnitStartFrame) >= d.largeUnitTimeout)
		{
			m_largeUnitID = INVALID_ID;
		}
		return stillWorking;
	}

	const std::vector<Object *> members(items->begin(), items->end());
	bool clearedLargeOnFirst = false;
	for (Object *member : members)
	{
		if (!member || member->isDestroyed() || core().slotOf(member->getID()) < 0)
		{
			continue;
		}
		const Coord3D targetPos = getMemberFormationPosition(member);
		if (!clearedLargeOnFirst && m_largeUnitID != INVALID_ID && (frame - m_largeUnitStartFrame) >= d.largeUnitTimeout)
		{
			m_largeUnitID = INVALID_ID; // RW 0x87E34C: cleared on the first member evaluated at expiry
			clearedLargeOnFirst = true;
		}
		if (orderAodMember(*member, targetPos))
		{
			stillWorking = true;
		}
	}

	if (m_largeUnitID != INVALID_ID && (frame - m_largeUnitStartFrame) >= d.largeUnitTimeout)
	{
		m_largeUnitID = INVALID_ID;
	}
	return stillWorking;
}

void AODHordeContain::syncOathToMembers(bool fulfilled)
{
	static const int kOathBit = CombatNames::modelCondition("OATH_FULLFILLED");
	if (kOathBit < 0)
	{
		return;
	}
	const ContainedItemsList *items = getContainedItemsList();
	if (!items)
	{
		return;
	}
	for (Object *member : *items)
	{
		if (member && !member->isDestroyed() && member->testModelCondition(kOathBit) != fulfilled)
		{
			member->setModelConditionState(kOathBit, fulfilled);
		}
	}
}

UpdateSleepTime AODHordeContain::update()
{
	Object *horde = getObject();
	static const int kOathBit = CombatNames::modelCondition("OATH_FULLFILLED");
	const bool fulfilled = kOathBit >= 0 && horde->testModelCondition(kOathBit);

	if (!movementEnabled())
	{
		syncOathToMembers(fulfilled);
		return UPDATE_SLEEP_FOREVER;
	}

	AIUpdateInterface *ai = horde->getAIUpdateInterface();
	if (!ai)
	{
		recordFlankHistory();
		mirrorMovingCondition();
		bannerCheck(false);
		syncOathToMembers(fulfilled);
		return UPDATE_SLEEP_NONE;
	}

	if (meleeEngaged())
	{
		stepSpineAndScatter();
		const UpdateSleepTime res = HordeContain::update();
		syncOathToMembers(fulfilled);
		return res;
	}

	recordFlankHistory();
	mirrorMovingCondition();
	bannerCheck(false);

	if (isGarrisoned())
	{
		return UPDATE_SLEEP_NONE;
	}

	if (!ai->isMoving() && ai->world().mapReady())
	{
		Pathfinder &pf = ai->world().pathfinder();
		const PathfindLayerEnum layer = pf.objectLayerAt(ai->adapter(), *horde->getPosition());
		if (layer != ai->adapter().getLayer())
		{
			ai->setLayer(layer);
			if (const ContainedItemsList *items = getContainedItemsList())
			{
				for (Object *m : *items)
				{
					if (AIUpdateInterface *mai = m ? m->getAIUpdateInterface() : nullptr)
					{
						mai->setLayer(pf.objectLayerAt(mai->adapter(), *m->getPosition()));
					}
				}
			}
			m_aodDirty = true;
		}
	}

	bool oathOutOfSync = false;
	if (kOathBit >= 0)
	{
		if (const ContainedItemsList *items = getContainedItemsList())
		{
			for (const Object *m : *items)
			{
				if (m && !m->isDestroyed() && m->testModelCondition(kOathBit) != fulfilled)
				{
					oathOutOfSync = true;
					break;
				}
			}
		}
	}

	if (ai->isMoving() || fulfilled || oathOutOfSync || m_largeUnitID != INVALID_ID)
	{
		m_aodDirty = true;
	}

	if (m_aodDirty || !backUpRecords().empty() || quarrelFighter(1) != INVALID_ID)
	{
		m_aodDirty = false;
		stepSpineAndScatter();
		if (core().slots().empty())
		{
			syncOathToMembers(fulfilled);
		}
		else if (runAodMemberPass())
		{
			m_aodDirty = true;
		}
		else if (!ai->isMoving())
		{
			AIWorld &w = ai->world();
			if (w.mapReady())
			{
				w.pathfinder().updateGoal(ai->adapter(), horde->getPosition(), ai->adapter().getLayer());
			}
		}
	}

	return UPDATE_SLEEP_NONE;
}

void AODHordeContain::crc(StateHasher &h) const
{
	HordeContain::crc(h);
	h.addBool(m_aodDirty);
	h.addU32((std::uint32_t)m_swayRecords.size());
	for (const SwayRecord &rec : m_swayRecords)
	{
		h.addFloat(rec.phaseXY);
		h.addFloat(rec.ampFactorXY);
		h.addFloat(rec.freqFactorXY);
		h.addFloat(rec.phaseZ);
		h.addFloat(rec.ampFactorZ);
		h.addFloat(rec.freqFactorZ);
	}
	h.addU32(m_largeUnitID);
	h.addU32(m_largeUnitStartFrame);
	h.addFloat(m_largeUnitPos.x);
	h.addFloat(m_largeUnitPos.y);
	h.addFloat(m_largeUnitPos.z);
	h.addFloat(m_largeUnitExtent);
	h.addFloat(m_largeUnitHeight);
	h.addFloat(m_scatterProgress);
	h.addI32(m_pathSampleCount);
	for (int i = 0; i < m_pathSampleCount; ++i)
	{
		h.addFloat(m_pathSamples[(size_t)i].pos.x);
		h.addFloat(m_pathSamples[(size_t)i].pos.y);
		h.addFloat(m_pathSamples[(size_t)i].pos.z);
		h.addFloat(m_pathSamples[(size_t)i].angle);
	}
	h.addI32(m_rankCount);
	for (int r = 0; r < m_rankCount; ++r)
	{
		h.addFloat(m_ranks[(size_t)r].slotX);
		h.addFloat(m_ranks[(size_t)r].angle);
		h.addFloat(m_ranks[(size_t)r].pos.x);
		h.addFloat(m_ranks[(size_t)r].pos.y);
		h.addFloat(m_ranks[(size_t)r].pos.z);
		h.addBool(m_ranks[(size_t)r].valid);
	}
}

// RW 0x0087E567 (AODHordeContain::xfer), RW 0x0087D917 (loadPostProcess thunk -> HordeContain::loadPostProcess 0x0086C8F2),
// RW 0x0087E784 (getModuleName), RW 0x0087E78A (getModuleNameKey), RW 0x0087E8E0 (scalar deleting dtor),
// RW 0x00879A72 (moveHordeToPosition -> AIUpdateInterface::aiMoveToPosition 0x0066C4CA)
void AODHordeContain::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	HordeContain::xfer(x);
	x.xferBool(&m_aodDirty);

	x.xferVectorSize(m_swayRecords, 6 * sizeof(float));
	for (SwayRecord &rec : m_swayRecords)
	{
		x.xferReal(&rec.phaseXY);
		x.xferReal(&rec.ampFactorXY);
		x.xferReal(&rec.freqFactorXY);
		x.xferReal(&rec.phaseZ);
		x.xferReal(&rec.ampFactorZ);
		x.xferReal(&rec.freqFactorZ);
	}

	x.xferObjectID(&m_largeUnitID);
	x.xferUnsignedInt(&m_largeUnitStartFrame);
	SnapshotXfer::coord3D(x, m_largeUnitPos);
	x.xferReal(&m_largeUnitExtent);
	x.xferReal(&m_largeUnitHeight);
	x.xferReal(&m_scatterProgress);

	x.xferInt(&m_pathSampleCount);
	SnapshotXfer::checkRange(m_pathSampleCount, 0, kMaxPathSamples, "an AOD horde's path sample count");
	if (x.isLoading())
	{
		for (int i = m_pathSampleCount; i < kMaxPathSamples; ++i)
		{
			m_pathSamples[(size_t)i] = PathSample{};
		}
	}
	for (int i = 0; i < m_pathSampleCount; ++i)
	{
		SnapshotXfer::coord3D(x, m_pathSamples[(size_t)i].pos);
		x.xferReal(&m_pathSamples[(size_t)i].angle);
	}

	x.xferInt(&m_rankCount);
	SnapshotXfer::checkRange(m_rankCount, 0, kMaxRanks, "an AOD horde's rank count");
	if (x.isLoading())
	{
		for (int r = m_rankCount; r < kMaxRanks; ++r)
		{
			m_ranks[(size_t)r] = RankSpineNode{};
		}
	}
	for (int r = 0; r < m_rankCount; ++r)
	{
		x.xferReal(&m_ranks[(size_t)r].slotX);
		x.xferReal(&m_ranks[(size_t)r].angle);
		SnapshotXfer::coord3D(x, m_ranks[(size_t)r].pos);
		x.xferBool(&m_ranks[(size_t)r].valid);
	}
}
