// OpenBFME. GPL-3.0.
//
// SwayClientUpdate: RW 0x008CD514 / 0x008CD51A / 0x008CD55F / 0x008CD675 / 0x008CD93F / 0x008CD9CD
// (DONOR BFME2 Code/GameEngine/Source/GameClient/Drawable/Update/SwayClientUpdate.cpp).

#include "GameClient/SwayClientUpdate.h"

#include "Common/INI.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Xfer.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameClient/Drawable.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <cmath>
#include <stdexcept>

namespace
{
// this = Rotate_X(theta) * this (3x3 part; RW 0x008CD7A9..0x008CD906, DONOR BFME2 WWMath Matrix3D::In_Place_Pre_Rotate_X)
void preRotateX(Matrix3D &m, float theta)
{
	const float s = std::sin(theta);
	const float c = std::cos(theta);
	for (int col = 0; col < 3; ++col)
	{
		const float r1 = m.Row[1][col];
		const float r2 = m.Row[2][col];
		m.Row[1][col] = c * r1 - s * r2;
		m.Row[2][col] = s * r1 + c * r2;
	}
}

// this = Rotate_Y(theta) * this (3x3 part; RW 0x008CD8A0..0x008CD906, DONOR BFME2 WWMath Matrix3D::In_Place_Pre_Rotate_Y)
void preRotateY(Matrix3D &m, float theta)
{
	const float s = std::sin(theta);
	const float c = std::cos(theta);
	for (int col = 0; col < 3; ++col)
	{
		const float r0 = m.Row[0][col];
		const float r2 = m.Row[2][col];
		m.Row[0][col] = c * r0 + s * r2;
		m.Row[2][col] = -s * r0 + c * r2;
	}
}

// RW 0x00BDD38C: 2 * PI (6.2831854820251465f)
constexpr float kTwoPi = 6.2831854820251465f;
// RW 0x00BD869C: 0.5f
constexpr float kHalf = 0.5f;
// RW 0x00BD1908: 1.0f
constexpr float kOne = 1.0f;

const FieldParse kEmptySwayFields[] = { { nullptr, nullptr, nullptr, 0 } };
} // namespace

void SwayClientUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// Retail SwayClientUpdate has no INI fields (RW 0x00654AD2).
	p.add(kEmptySwayFields);
}

SwayClientUpdate::SwayClientUpdate(Thing *thing, const SwayClientUpdateModuleData *data)
	: DrawableModule(thing, data)
{
}

// RW 0x008CD514: mov eax, 0xC0A664 ("SwayClientUpdate"); ret
const char *SwayClientUpdate::getModuleName()
{
	return "SwayClientUpdate";
}

// RW 0x008CD51A (DONOR BFME2 0x004C90E0): cached NameKeyType for "SwayClientUpdate"
NameKeyType SwayClientUpdate::getModuleNameKey(NameKeyGenerator &gen)
{
	return gen.nameToKey(getModuleName());
}

// RW 0x008CD55F (DONOR BFME2 0x004C9125): SwayClientUpdate::updateSway
void SwayClientUpdate::updateSway(const W3DVegetationSway::Breeze &info, W3DDrawRandom &random, float fps)
{
	// RW 0x008CD56F..0x008CD589: if (info.randomness == 0.0f) m_curValue = 0.0f;
	if (info.randomness == 0.0f)
	{
		m_curValue = 0.0f;
	}

	// RW 0x008CD58E..0x008CD5C5: delta = info.randomness * 0.5f; range [1.0f - delta, 1.0f + delta]
	const float delta = info.randomness * kHalf;
	const float lo = kOne - delta;
	const float hi = kOne + delta;

	// RW 0x008CD5D0..0x008CD5DE: m_curAngleLimit = info.intensity * GameClientRandomValueReal(lo, hi)
	m_curAngleLimit = info.intensity * random.real(lo, hi);

	// RW 0x008CD5F0..0x008CD617: m_curDelta = (2 * PI) / (fps * breezePeriod) * GameClientRandomValueReal(lo, hi)
	const float rndDelta = random.real(lo, hi);
	const float denom = fps * static_cast<float>(info.breezePeriod);
	m_curDelta = (denom != 0.0f) ? ((rndDelta / denom) * kTwoPi) : 0.0f;

	// RW 0x008CD627..0x008CD637: m_leanAngle = info.lean * GameClientRandomValueReal(lo, hi)
	m_leanAngle = info.lean * random.real(lo, hi);

	// RW 0x008CD63A..0x008CD63E: m_curVersion = info.breezeVersion
	m_curVersion = static_cast<std::int16_t>(info.breezeVersion);

	// RW 0x008CD643..0x008CD66D: if (draw) { angle = info.direction - draw->getZRotation(); sin/cos }
	if (Drawable *draw = getDrawable())
	{
		const float angle = info.direction - draw->getZRotation();
		m_directionSin = std::sin(angle);
		m_directionCos = std::cos(angle);
	}
}

// RW 0x008CD675 (DONOR BFME2 0x004C923B): SwayClientUpdate::clientUpdate
void SwayClientUpdate::clientUpdate(const W3DVegetationSway::Breeze &info, W3DDrawRandom &random,
	bool visible, bool burnedOrDead, float fps)
{
	// RW 0x008CD67E..0x008CD682: if (!m_swaying) return;
	if (!m_swaying)
	{
		return;
	}

	Drawable *draw = getDrawable();

	// RW 0x008CD688..0x008CD6B4: if breeze version changed, always updateSway (even if offscreen);
	// otherwise only update visible drawables.
	if (static_cast<std::int16_t>(info.breezeVersion) != m_curVersion)
	{
		updateSway(info, random, fps);
	}
	else
	{
		if (!draw || !visible)
		{
			return;
		}
	}

	if (!draw)
	{
		return;
	}

	// RW 0x008CD6BA..0x008CD6DA: advance m_curValue and wrap > 2*PI
	m_curValue += m_curDelta;
	if (m_curValue > kTwoPi)
	{
		m_curValue -= kTwoPi;
	}

	// RW 0x008CD6DF..0x008CD707: targetAngle = sin(m_curValue) * m_curAngleLimit + m_leanAngle
	const float targetAngle = std::sin(m_curValue) * m_curAngleLimit + m_leanAngle;
	const float deltaAngle = targetAngle - m_curAngle;

	// RW 0x008CD70C..0x008CD908: pre-rotate drawable instance matrix around X and Y
	Matrix3D xfrm = draw->getInstanceMatrix();
	preRotateX(xfrm, -deltaAngle * m_directionSin);
	preRotateY(xfrm, deltaAngle * m_directionCos);
	draw->setInstanceMatrix(xfrm);

	// RW 0x008CD90D..0x008CD912: m_curAngle = targetAngle
	m_curAngle = targetAngle;

	// RW 0x008CD917..0x008CD937: burned (OBJECT_STATUS_BURNED = 11) or effectivelyDead stops sway
	if (burnedOrDead)
	{
		stopSway();
	}
}

// RW 0x008CD93F (DONOR BFME2 0x004C9505): SwayClientUpdate::xfer
void SwayClientUpdate::xfer(Xfer &x)
{
	// RW 0x008CD948..0x008CD956: ObjectModule::xfer; early-out for light CRC
	if (x.getXferMode() == XFER_CRC)
	{
		return;
	}
	// RW 0x008CD958..0x008CD9C5: version 1, 5 floats, int16, bool, 2 floats
	SnapshotXfer::version(x, 1, 1);
	x.xferReal(&m_curValue);
	x.xferReal(&m_curAngle);
	x.xferReal(&m_curDelta);
	x.xferReal(&m_curAngleLimit);
	x.xferReal(&m_leanAngle);
	x.xferShort(&m_curVersion);
	x.xferBool(&m_swaying);
	x.xferReal(&m_directionSin);
	x.xferReal(&m_directionCos);
}

// RW 0x008CD9CD (DONOR BFME2 0x004C9593): SwayClientUpdate::loadPostProcess (jmp 0x008CD55F)
void SwayClientUpdate::loadPostProcess(const W3DVegetationSway::Breeze &info, W3DDrawRandom &random, float fps)
{
	updateSway(info, random, fps);
}

void SwayClientUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<SwayClientUpdateModuleData>("SwayClientUpdate", MODULETYPE_CLIENT_UPDATE);
	modules.bindModuleProc("SwayClientUpdate", MODULETYPE_CLIENT_UPDATE,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const SwayClientUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("SwayClientUpdate: the module data is not typed");
			}
			return std::make_unique<SwayClientUpdate>(thing, typed);
		});
}
