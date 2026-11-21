// OpenBFME. GPL-3.0. See FadeAndDieOrnamentUpdate.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/FadeAndDieOrnamentUpdate.h"

#include "Common/AsciiString.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>

namespace
{
// RW 0x0073A74B: parses inline key=value pairs of an OrnamentEnvelopeData until "End" or end-of-line.
void parseEnvelope(INI *ini, void *, void *store, const void *)
{
	auto *env = static_cast<OrnamentEnvelopeData *>(store);
	env->elapsedFrames = 0;
	env->state = OrnamentEnvelopeData::STATE_INITIAL_DELAY;

	for (const char *tok = ini->getNextTokenOrNull(); tok != nullptr; tok = ini->getNextTokenOrNull())
	{
		if (AsciiStringUtil::compareNoCase(tok, "End") == 0)
		{
			break;
		}
		if (AsciiStringUtil::compareNoCase(tok, "InitialOpacity") == 0)
		{
			INI::parseReal(ini, nullptr, &env->initialOpacity, nullptr);
		}
		else if (AsciiStringUtil::compareNoCase(tok, "PeakOpacity") == 0)
		{
			INI::parseReal(ini, nullptr, &env->peakOpacity, nullptr);
		}
		else if (AsciiStringUtil::compareNoCase(tok, "SustainOpacity") == 0)
		{
			INI::parseReal(ini, nullptr, &env->sustainOpacity, nullptr);
		}
		else if (AsciiStringUtil::compareNoCase(tok, "InitialDelay") == 0)
		{
			INI::parseDurationUnsignedInt(ini, nullptr, &env->initialDelay, nullptr);
		}
		else if (AsciiStringUtil::compareNoCase(tok, "AttackTime") == 0)
		{
			INI::parseDurationUnsignedInt(ini, nullptr, &env->attackTime, nullptr);
		}
		else if (AsciiStringUtil::compareNoCase(tok, "DecayTime") == 0)
		{
			INI::parseDurationUnsignedInt(ini, nullptr, &env->decayTime, nullptr);
		}
		else if (AsciiStringUtil::compareNoCase(tok, "SustainTime") == 0)
		{
			INI::parseDurationUnsignedInt(ini, nullptr, &env->sustainTime, nullptr);
		}
		else if (AsciiStringUtil::compareNoCase(tok, "ReleaseTime") == 0)
		{
			INI::parseDurationUnsignedInt(ini, nullptr, &env->releaseTime, nullptr);
		}
	}
}

// RW table 0x00C08668 (DONOR BFME2 0x00BF2358).
// Note: in retail 0x00C08678, FollowTarget's table entry mistakenly points to parseEnvelope (0x73A74B),
// while the field at +0x34 is a bool defaulting to true (ctor RW 0x0065535A, store RW 0x0065539E) and read at RW 0x007A7808.
// No retail or mod INI sets FollowTarget; parsing it as bool allows synthetic INI tests to exercise both branches.
const FieldParse kFadeAndDieOrnamentFieldParse[] = {
	{ "AttachToTargetBone", INI::parseAsciiString, nullptr, (int)offsetof(FadeAndDieOrnamentUpdateModuleData, m_attachToTargetBone) },
	{ "Envelope", parseEnvelope, nullptr, (int)offsetof(FadeAndDieOrnamentUpdateModuleData, m_envelope) },
	{ "FollowTarget", INI::parseBool, nullptr, (int)offsetof(FadeAndDieOrnamentUpdateModuleData, m_followTarget) },
	{ nullptr, nullptr, nullptr, 0 }
};

float clampMinOne(UnsignedInt frames)
{
	const float f = static_cast<float>(frames);
	return f < 1.0f ? 1.0f : f;
}
} // namespace

// RW 0x00740659: 5-stage ADSR envelope step (exact SSE arithmetic and state transitions).
bool OrnamentEnvelopeData::update(float *out)
{
	switch (state)
	{
	case STATE_INITIAL_DELAY: // RW 0x007407BF
	{
		const UnsignedInt prev = elapsedFrames;
		elapsedFrames = prev + 1;
		if (prev >= initialDelay)
		{
			elapsedFrames = 0;
			state = STATE_ATTACK;
			*out = initialOpacity;
		}
		// RW 0x007407DE .. 0x007407E7: retail falls through to zero *out and return true.
		*out = 0.0f;
		return true;
	}
	case STATE_ATTACK: // RW 0x00740764
	{
		const float denom = clampMinOne(attackTime);
		const float step = SimMath::divf32(SimMath::subf32(peakOpacity, initialOpacity), denom);
		*out = SimMath::addf32(*out, step);
		if (!(*out < peakOpacity))
		{
			*out = peakOpacity;
			state = STATE_DECAY;
		}
		return true;
	}
	case STATE_DECAY: // RW 0x00740704
	{
		const float denom = clampMinOne(decayTime);
		const float step = SimMath::divf32(SimMath::subf32(peakOpacity, sustainOpacity), denom);
		*out = SimMath::subf32(*out, step);
		if (!(sustainOpacity < *out))
		{
			*out = sustainOpacity;
			state = STATE_SUSTAIN;
			elapsedFrames = 0;
		}
		return true;
	}
	case STATE_SUSTAIN: // RW 0x007406E5
	{
		const UnsignedInt prev = elapsedFrames;
		elapsedFrames = prev + 1;
		if (prev >= sustainTime)
		{
			state = STATE_RELEASE;
		}
		*out = sustainOpacity;
		return true;
	}
	case STATE_RELEASE: // RW 0x00740687
	{
		const float denom = clampMinOne(releaseTime);
		const float step = SimMath::divf32(sustainOpacity, denom);
		*out = SimMath::subf32(*out, step);
		if (!(0.0f < *out))
		{
			*out = 0.0f;
			state = STATE_DONE;
			elapsedFrames = 0;
		}
		return true;
	}
	default: // RW 0x00740680 (STATE_DONE)
		*out = 0.0f;
		return false;
	}
}

void FadeAndDieOrnamentUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFadeAndDieOrnamentFieldParse);
}

// RW 0x007A772B (DONOR BFME2 0x003A439D)
FadeAndDieOrnamentUpdate::FadeAndDieOrnamentUpdate(Thing *thing, const FadeAndDieOrnamentUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
	, m_parentId(INVALID_ID)
	, m_opacity(0.0f)
	, m_envelope(data ? data->m_envelope : OrnamentEnvelopeData{})
{
}

// RW 0x007A76CF
void FadeAndDieOrnamentUpdate::setParent(const Object *parent)
{
	m_parentId = parent ? parent->getID() : INVALID_ID;
}

// RW 0x007A77EA
Object *FadeAndDieOrnamentUpdate::getParentObject() const
{
	if (m_parentId == INVALID_ID || !getObject())
	{
		return nullptr;
	}
	return getObject()->logic().findObjectByID(m_parentId);
}

// RW 0x008A7734
void FadeAndDieOrnamentUpdate::loadPostProcess()
{
	setParent(getParentObject());
}

// RW 0x007A7801
UpdateSleepTime FadeAndDieOrnamentUpdate::update()
{
	Object *obj = getObject();
	if (!obj || !m_data)
	{
		return UPDATE_SLEEP_NONE;
	}
	if (m_data->m_followTarget) // RW 0x007A7808
	{
		Object *parent = getParentObject(); // RW 0x007A7815 -> RW 0x007A77EA
		if (parent && !parent->isEffectivelyDead()) // RW 0x007A781A .. 0x007A7825
		{
			obj->setPosition(parent->getPosition()); // RW 0x007A782D -> RW 0x0070C201
		}
		else
		{
			obj->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // RW 0x007A783A -> RW 0x00698EC3(8, 0)
		}
	}
	// RW 0x007A783F .. 0x007A785B: update envelope and push opacity to Drawable + 0xB0.
	m_envelope.update(&m_opacity); // RW 0x007A7854 -> RW 0x00740659
	if (ObjectClientHooks *client = obj->clientHooks())
	{
		client->setDrawableOpacity(*obj, m_opacity);
	}
	return UPDATE_SLEEP_NONE;
}

void FadeAndDieOrnamentUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(m_parentId);
	hasher.addFloat(m_opacity);
	hasher.addU32(m_envelope.elapsedFrames);
	hasher.addI32(m_envelope.state);
}

// RW 0x007A7796: retail version (1, 2) transfers UpdateModule, m_parentId (+0x20), and m_opacity (+0x24, v>=2).
// OpenBFME native snapshot (S-4190) also preserves envelope runtime state (elapsedFrames, state).
void FadeAndDieOrnamentUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferObjectID(&m_parentId);
	x.xferReal(&m_opacity);
	x.xferUnsignedInt(&m_envelope.elapsedFrames);
	x.xferInt(&m_envelope.state);
	SnapshotXfer::checkRange(m_envelope.state, (int)OrnamentEnvelopeData::STATE_INITIAL_DELAY, (int)OrnamentEnvelopeData::STATE_DONE, "FadeAndDieOrnamentUpdate envelope state");
	if (x.isLoading())
	{
		loadPostProcess(); // RW 0x008A7734
	}
}

// RW 0x00659D52, getModuleName RW 0x007A76C9, getModuleNameKey RW 0x007A76E6
void FadeAndDieOrnamentUpdate::registerClass(ModuleFactory &modules)
{
	static const char *const kName = "FadeAndDieOrnamentUpdate";
	modules.bindTypedData<FadeAndDieOrnamentUpdateModuleData>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const FadeAndDieOrnamentUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("FadeAndDieOrnamentUpdate: the module data is not typed");
		}
		return std::make_unique<FadeAndDieOrnamentUpdate>(thing, typed);
	});
}
