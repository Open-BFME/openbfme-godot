// OpenBFME. GPL-3.0. See GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDebrisDraw.h.
// TARGET RW 0x004B15BC getModuleNameKey, RW 0x004B1601 GetSnapshotName, RW 0x004B17F4 scalar deleting destructor, RW 0x00499E84 shadow release.
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDebrisDraw.h"

#include "Common/AsciiString.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/SnapshotXfer.h"

#include <memory>
#include <stdexcept>

namespace
{
const FieldParse kEmptyFields[] = { { nullptr, nullptr, nullptr, 0 } };
} // namespace

void W3DDebrisDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kEmptyFields); // RW table 0x0: no fields
}

W3DDebrisDraw::W3DDebrisDraw(Thing *thing, const W3DDebrisDrawModuleData *data)
	: DrawableModule(thing, data)
	, m_data(data)
{
}

void W3DDebrisDraw::setShadowsEnabled(bool enable)
{
	// RW 0x004B12FE (DONOR BFME2 0x000B19D9, W3DDebrisDrawLegacy.cpp): sets m_shadow->enableShadowRender(enable)
	m_shadowsEnabled = enable;
}

void W3DDebrisDraw::setFullyObscuredByShroud(bool fullyObscured)
{
	// RW 0x004B130F (DONOR BFME2 0x000B19EA, W3DDebrisDrawLegacy.cpp): sets m_shadow->enableShadowInvisible(fullyObscured)
	m_fullyObscuredByShroud = fullyObscured;
}

void W3DDebrisDraw::reactToTransformChange(const Coord3D * /*oldPos*/, float /*oldAngle*/)
{
	// RW 0x004B1339 (DONOR BFME2 0x000B1A14, W3DDebrisDrawReactToTransformChange.cpp): forwards drawable transform matrix to m_renderObject
	if (!m_modelName.empty())
	{
		++m_transformChanges;
	}
}

void W3DDebrisDraw::setModelName(const std::string &name, std::int32_t color, int shadowType)
{
	// RW 0x004B1810 (DONOR BFME2 0x000B1EEB, W3DDebrisDrawSetModelName.cpp): the model is loaded and added to the scene only when no model is set
	// yet and the name is not empty.
	if (!m_modelName.empty() || name.empty())
	{
		return;
	}
	m_modelName = name;
	m_modelColor = color;
	m_shadowType = shadowType;
}

void W3DDebrisDraw::setAnimNames(const std::string &initial, const std::string &flying, const std::string &final)
{
	// RW 0x004B19A4 (DONOR BFME2 0x000B207F): "STOP" means the final animation is the flying one frozen on its last frame.
	std::string finalName = final;
	if (AsciiStringUtil::compareNoCase(final, "STOP") == 0)
	{
		m_finalStop = true;
		finalName = flying;
	}
	else
	{
		m_finalStop = false;
	}
	m_animInitial = initial;
	m_animFlying = flying;
	m_animFinal = finalName;
	m_state = INITIAL;
	m_frames = 0;
}

W3DDebrisDrawStep W3DDebrisDraw::advance(bool isAboveTerrain, bool animationComplete)
{
	// RW 0x004B135C doDrawModule (DONOR BFME2 0x000B1A37). No model -> nothing is drawn and the machine does not advance.
	W3DDebrisDrawStep step;
	if (m_modelName.empty())
	{
		return step;
	}
	step.hasModel = true;

	const int oldState = m_state;
	const bool firstFrame = m_frames == 0;
	if (m_state != FINAL && !isAboveTerrain && m_frames > MIN_FINAL_FRAMES)
	{
		m_state = FINAL; // the piece landed: go straight to the final animation
	}
	else if (m_state < FINAL && animationComplete)
	{
		++m_state;
	}

	step.anim = m_state;
	step.animChanged = firstFrame || oldState != m_state;
	step.mode = (m_state == FLYING) ? W3DDebrisDrawStep::MODE_LOOP : W3DDebrisDrawStep::MODE_ONCE; // RW TheAnimModes
	if (m_state == FINAL)
	{
		step.finalFX = oldState != FINAL; // the final FX fires once, when FINAL is entered
		if (m_finalStop)
		{
			step.mode = W3DDebrisDrawStep::MODE_MANUAL;
		}
	}

	++m_frames;
	return step;
}

void W3DDebrisDraw::xfer(Xfer &x)
{
	// RW 0x004B16E8 (DONOR BFME2 0x000B1DC3, W3DDebrisDrawXfer.cpp): calls DrawModule::xfer, then version(1, 1),
	// xfers m_modelName, m_modelColor (calls setModelName on load), xfers m_animInitial, m_animFlying, m_animFinal
	// (calls setAnimNames on load), then xfers m_state, m_frames, m_finalStop.
	SnapshotXfer::version(x, 1, 1);
	std::string loadedModel = m_modelName;
	x.xferAsciiString(&loadedModel);
	x.xferInt(&m_modelColor);
	if (x.isLoading())
	{
		m_modelName.clear();
		setModelName(loadedModel, m_modelColor, 0);
	}
	std::string loadedInitial = m_animInitial;
	std::string loadedFlying = m_animFlying;
	std::string loadedFinal = m_animFinal;
	x.xferAsciiString(&loadedInitial);
	x.xferAsciiString(&loadedFlying);
	x.xferAsciiString(&loadedFinal);
	if (x.isLoading())
	{
		setAnimNames(loadedInitial, loadedFlying, loadedFinal);
	}
	SnapshotXfer::enumIn(x, m_state, INITIAL, STATECOUNT - 1, "a debris draw's state"); // INITIAL, FLYING, FINAL (enumIn transfers it)
	x.xferInt(&m_frames);
	x.xferBool(&m_finalStop);
}

void W3DDebrisDraw::crc(StateHasher &hasher) const
{
	hasher.addString(m_modelName);
	hasher.addU32((std::uint32_t)m_modelColor);
	hasher.addString(m_animInitial);
	hasher.addString(m_animFlying);
	hasher.addString(m_animFinal);
	hasher.addI32(m_state);
	hasher.addI32(m_frames);
	hasher.addBool(m_finalStop);
}

void W3DDebrisDraw::registerClass(ModuleFactory &modules)
{
	// RW 0x004B15BC getModuleNameKey / RW 0x004B1601 GetSnapshotName
	modules.bindTypedData<W3DDebrisDrawModuleData>("W3DDebrisDraw", MODULETYPE_DRAW);
	modules.bindModuleProc("W3DDebrisDraw", MODULETYPE_DRAW,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const W3DDebrisDrawModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("W3DDebrisDraw: the module data is not typed");
			}
			return std::make_unique<W3DDebrisDraw>(thing, typed);
		});
}
