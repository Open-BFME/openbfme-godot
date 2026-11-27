// OpenBFME. GPL-3.0.
// See GameClient/LaserUpdate.h (editions: both RotWK 2.01 and BFME2 1.06).

#include "GameClient/LaserUpdate.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/SimMath.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLaserDraw.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

namespace
{
#define LU_OFF(m) (int)offsetof(LaserUpdateModuleData, m)
// RW 0xC2D890 / BFME2 0xBDD288
const FieldParse kLaserUpdateModuleDataFieldParse[] = {
	{ "MuzzleParticleSystem", INI::parseAsciiString, nullptr, LU_OFF(m_muzzleParticleSystem) },
	{ "ParentFireBoneName", INI::parseAsciiString, nullptr, LU_OFF(m_parentFireBoneName) },
	{ "ParentFireBoneOnTurret", INI::parseBool, nullptr, LU_OFF(m_parentFireBoneOnTurret) },
	{ "TargetParticleSystem", INI::parseAsciiString, nullptr, LU_OFF(m_targetParticleSystem) },
	{ "LaserLifetime", INI::parseReal, nullptr, LU_OFF(m_laserLifetime) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef LU_OFF

// RW 0xD9F624: 0.03f client frames per millisecond (30 frames/sec client clock)
constexpr float kClientFramesPerMsec = 0.03f;
// RW 0xC2D8F0: 0.83f target geometry height scalar
constexpr float kTargetGeometryHeightScalar = 0.83f;
// RW 0xBD869C: 0.5f parent geometry height scalar
constexpr float kParentGeometryHeightScalar = 0.5f;
} // namespace

// RW 0x764DBB
void LaserUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kLaserUpdateModuleDataFieldParse);
}

// RW 0x764B43 / BFME2 0x362EEE
LaserUpdate::LaserUpdate(Thing *thing, const LaserUpdateModuleData *data)
	: DrawableModule(thing, data)
	, m_data(data)
{
	// RW 0x764B43: m_currentWidthScalar initialized to 1.0f (RW 0xBD1908), others zeroed.
}

// RW 0x764BB9 / BFME2 0x362F64
LaserUpdate::~LaserUpdate()
{
	// In retail, destroys particle systems via TheParticleSystemManager->destroyParticleSystemByID.
	// Particle systems are not ported yet in this engine layer.
}

// RW 0x7651B1 / BFME2 0x36355C
void LaserUpdate::initLaser(const Object *parent, const Coord3D *startPos, const Coord3D *endPos, int sizeDeltaFrames, unsigned currentFrame)
{
	if (sizeDeltaFrames > 0)
	{
		m_widening = true;
		m_decaying = false;
		m_widenStartFrame = currentFrame;
		m_widenFinishFrame = currentFrame + (unsigned)sizeDeltaFrames;
		m_currentWidthScalar = 0.0f;
	}
	else if (sizeDeltaFrames < 0)
	{
		m_decaying = true;
		m_widening = false;
		m_decayStartFrame = currentFrame;
		m_decayFinishFrame = currentFrame + (unsigned)(-sizeDeltaFrames);
		m_currentWidthScalar = 1.0f;
	}

	if (m_data && m_data->m_laserLifetime > 0.0f)
	{
		// RW 0xD9F624: ceil(0.03 * lifetime)
		const unsigned lifetimeFrames = static_cast<unsigned>(std::ceil(kClientFramesPerMsec * m_data->m_laserLifetime));
		m_expiryFrame = currentFrame + lifetimeFrames;
	}
	else
	{
		m_expiryFrame = 0;
	}

	if (parent && m_data && !m_data->m_parentFireBoneName.empty())
	{
		ProjectileLaunchOffsets *provider = parent->logic().combat().launchOffsets();
		float b[12];
		if (provider && provider->singleLogicalBone(*parent, m_data->m_parentFireBoneName, b))
		{
			m_startPos = ConvertBonePosToWorldPos(*parent, Coord3D{ b[3], b[7], b[11] });
		}
		else if (startPos)
		{
			m_startPos = *startPos;
		}
		else if (parent->getPosition())
		{
			m_startPos = *parent->getPosition();
			m_startPos.z += ObjectGeometry::maxHeightAbovePosition(parent->geometryShapes()) * kParentGeometryHeightScalar;
		}
	}
	else if (startPos)
	{
		m_startPos = *startPos;
	}
	else if (parent && parent->getPosition())
	{
		m_startPos = *parent->getPosition();
		m_startPos.z += ObjectGeometry::maxHeightAbovePosition(parent->geometryShapes()) * kParentGeometryHeightScalar;
	}

	if (endPos)
	{
		m_endPos = *endPos;
	}

	// Trigger particle systems (stubs in port, matching retail fields)
	if (m_data && !m_data->m_muzzleParticleSystem.empty())
	{
		m_particleSystemID = 1;
	}
	if (m_data && !m_data->m_targetParticleSystem.empty())
	{
		m_targetParticleSystemID = 2;
	}

	// RW 0x765140..0x76518A: midpoint position
	const Coord3D midpoint{
		(m_startPos.x + m_endPos.x) * 0.5f,
		(m_startPos.y + m_endPos.y) * 0.5f,
		(m_startPos.z + m_endPos.z) * 0.5f
	};
	if (getDrawable())
	{
		getDrawable()->setPosition(&midpoint);
	}
	m_dirty = true;
}

// RW 0x7654A8 / BFME2 0x604460
void LaserUpdate::initFromDrawables(Drawable *parent, Drawable *target, int sizeDeltaFrames, unsigned currentFrame)
{
	if (!parent || !target)
	{
		return;
	}
	m_parentID = parent->getID();
	m_targetID = target->getID();

	Coord3D sPos = parent->getPosition() ? *parent->getPosition() : Coord3D{ 0.0f, 0.0f, 0.0f };
	if (m_data && !m_data->m_parentFireBoneName.empty())
	{
		Coord3D bonePos;
		if (parent->boneWorldPosition(m_data->m_parentFireBoneName, bonePos))
		{
			sPos = bonePos;
		}
		else
		{
			sPos.z += parent->geometryHeight() * kParentGeometryHeightScalar; // RW 0x765062
		}
	}
	else
	{
		sPos.z += parent->geometryHeight() * kParentGeometryHeightScalar; // RW 0x765062
	}

	Coord3D tPos = target->getPosition() ? *target->getPosition() : Coord3D{ 0.0f, 0.0f, 0.0f };
	tPos.z += target->geometryHeight() * kTargetGeometryHeightScalar; // RW 0x76509D / RW 0xC2D8F0

	initLaser(nullptr, &sPos, &tPos, sizeDeltaFrames, currentFrame);
}

// RW 0x764E88 / BFME2 0x363233
bool LaserUpdate::clientUpdate(unsigned currentFrame, const std::function<Drawable *(DrawableID)> &findDrawable)
{
	// RW 0x764EA7: expiry check
	if (m_expiryFrame != 0 && currentFrame > m_expiryFrame)
	{
		return false;
	}

	// RW 0x764EAC: decaying
	if (m_decaying)
	{
		if (m_decayFinishFrame > m_decayStartFrame)
		{
			const float progress = static_cast<float>(currentFrame - m_decayStartFrame) /
			                       static_cast<float>(m_decayFinishFrame - m_decayStartFrame);
			m_currentWidthScalar = 1.0f - progress;
		}
		else
		{
			m_currentWidthScalar = 0.0f;
		}
		m_dirty = true;
		if (m_currentWidthScalar <= 0.0f)
		{
			m_currentWidthScalar = 0.0f;
			return false; // decayed completely -> expired
		}
	}
	else if (m_widening) // RW 0x764F12: widening
	{
		if (m_widenFinishFrame > m_widenStartFrame)
		{
			const float progress = static_cast<float>(currentFrame - m_widenStartFrame) /
			                       static_cast<float>(m_widenFinishFrame - m_widenStartFrame);
			m_currentWidthScalar = progress;
		}
		else
		{
			m_currentWidthScalar = 1.0f;
		}
		m_dirty = true;
		if (m_currentWidthScalar >= 1.0f)
		{
			m_currentWidthScalar = 1.0f;
			m_widening = false;
		}
	}

	m_dirty = true; // RW 0x764F6D

	// RW 0x764F68..0x76518A: dynamic tracking if parent and target IDs are set
	if (m_parentID != 0 && m_targetID != 0 && findDrawable)
	{
		Drawable *parent = findDrawable(m_parentID);
		Drawable *target = findDrawable(m_targetID);
		if (parent && target)
		{
			// RW 0x764FB1..0x765046: query parent bone
			if (m_data && !m_data->m_parentFireBoneName.empty())
			{
				Coord3D bonePos;
				if (parent->boneWorldPosition(m_data->m_parentFireBoneName, bonePos))
				{
					m_startPos = bonePos;
				}
				else if (parent->getPosition())
				{
					m_startPos = *parent->getPosition();
					m_startPos.z += parent->geometryHeight() * kParentGeometryHeightScalar; // RW 0x765062
				}
			}
			else if (parent->getPosition())
			{
				m_startPos = *parent->getPosition();
				m_startPos.z += parent->geometryHeight() * kParentGeometryHeightScalar; // RW 0x765062
			}

			// RW 0x76507F..0x7650C2: target position + 0.83 * geometry height
			if (target->getPosition())
			{
				m_endPos = *target->getPosition();
				m_endPos.z += target->geometryHeight() * kTargetGeometryHeightScalar; // RW 0x76509D / RW 0xC2D8F0
			}

			// RW 0x765140..0x76518A: midpoint
			const Coord3D midpoint{
				(m_startPos.x + m_endPos.x) * 0.5f,
				(m_startPos.y + m_endPos.y) * 0.5f,
				(m_startPos.z + m_endPos.z) * 0.5f
			};
			if (getDrawable())
			{
				getDrawable()->setPosition(&midpoint);
			}
		}
	}

	return true;
}

// RW 0x764C18 / BFME2 0x362FC3
void LaserUpdate::decay(unsigned frames, unsigned currentFrame)
{
	if (frames == 0)
	{
		return;
	}
	m_decaying = true;
	m_widening = false;
	m_decayStartFrame = currentFrame;
	m_decayFinishFrame = currentFrame + frames;
	m_currentWidthScalar = 1.0f;
	m_dirty = true;
}

// RW 0x764CC2 / BFME2 0x363065
float LaserUpdate::getCurrentLaserRadius() const
{
	const Drawable *draw = getDrawable();
	if (!draw)
	{
		return 0.0f;
	}
	for (const DrawEntry &e : draw->entries())
	{
		if (const auto *laserData = dynamic_cast<const W3DLaserDrawModuleData *>(e.data))
		{
			return laserData->getLaserTemplateWidth() * m_currentWidthScalar;
		}
	}
	return 0.0f;
}

// RW 0x764C4D / BFME2 0x362FF8
void LaserUpdate::xfer(Xfer &x)
{
	XferVersion base{ 1, 1 }; // RW 0x7082E2 ObjectModule::xferObjectModule
	x.xferVersion(base);
	xferModule(x);

	XferVersion version{ 1, 2 }; // RW 0x764C74
	x.xferVersion(version);

	x.xferReal(&m_startPos.x);
	x.xferReal(&m_startPos.y);
	x.xferReal(&m_startPos.z);
	x.xferReal(&m_endPos.x);
	x.xferReal(&m_endPos.y);
	x.xferReal(&m_endPos.z);
	x.xferBool(&m_dirty);
	x.xferUnsignedInt(&m_particleSystemID);
	x.xferUnsignedInt(&m_targetParticleSystemID);
	x.xferBool(&m_widening);
	x.xferBool(&m_decaying);
	x.xferUnsignedInt(&m_widenStartFrame);
	x.xferUnsignedInt(&m_widenFinishFrame);
	x.xferReal(&m_currentWidthScalar);
	x.xferUnsignedInt(&m_decayStartFrame);
	x.xferUnsignedInt(&m_decayFinishFrame);
	x.xferObjectID(&m_parentID);
	x.xferObjectID(&m_targetID);
	if (version.current >= 2)
	{
		x.xferUnsignedInt(&m_expiryFrame);
	}
	else if (x.isLoading())
	{
		m_expiryFrame = 0;
	}
}

void LaserUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<LaserUpdateModuleData>("LaserUpdate", MODULETYPE_CLIENT_UPDATE);
	modules.bindModuleProc("LaserUpdate", MODULETYPE_CLIENT_UPDATE,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const LaserUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("LaserUpdate: module data is not typed");
			}
			return std::make_unique<LaserUpdate>(thing, typed);
		});
}
