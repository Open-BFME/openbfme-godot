// OpenBFME. GPL-3.0.
// See GameClient/StealthLook.h.

#include "GameClient/StealthLook.h"

#include "Common/Module.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/System/InvisibilityManager.h"

// Presentation-only input to the look setter (RW 0x81AA85 / 0x777AF7); the conversion is not simulation state.
void InvisibilityManager::clientOpacityRange(const Object &obj, float *lo, float *hi, float *cycleSeconds) const
{
	if (!isInvisible(obj))
	{
		if (const StealthUpdate *st = StealthUpdate::of(obj))
		{
			*lo = st->data()->m_friendlyOpacityMin;
			*hi = st->data()->m_friendlyOpacityMax;
			*cycleSeconds = st->data()->m_pulseFrequency * 0.2f;
			return;
		}
	}
	const GameLogicSettings &s = m_logic.settings();
	*lo = s.invisibilityOpacityMin;
	*hi = s.invisibilityOpacityMax;
	*cycleSeconds = (float)s.invisibilityOpacityCycleFrames / 5.0f;
}

bool StealthLook::setLook(State &s, int look, float min, float max, float cycle, float phaseDraw, bool cannotHide)
{
	// RW 0x6760F9 (BFME2 0x275DCE, DrawableRva00275DCE.cpp:51, tier B same-shape: identical but for the __LINE__ of the random draw)
	if (look == s.look)
	{
		return false;
	}
	const bool wasHidden = s.hidden;
	s.outer = s.inner = 1.0f;
	s.cycle = kMinCycle > cycle ? kMinCycle : (cycle > kMaxCycle ? kMaxCycle : cycle); // comiss order: below 0.1 -> 0.1, above 60 -> 60
	s.blendLeft = (int)((double)kClientFramesPerSecond * (double)cycle);              // ftol(fild 30 * the UNclamped argument)
	s.latched = s.opacity;
	s.look = look;
	switch (look)
	{
	case LOOK_NONE:
	case LOOK_DISGUISED_ENEMY:
		s.hidden = false;
		s.secondPass = 0.0f;
		break;
	case LOOK_VISIBLE_FRIENDLY:
	case LOOK_VISIBLE_FRIENDLY_DETECTED:
		s.inner = min;
		s.outer = max;
		s.middle = (min + max) * 0.5f;
		s.halfRange = (max - min) * 0.5f;
		s.phase = phaseDraw; // GetGameClientRandomValueReal(0, pi) (RW 0x6D33AB)
		s.hidden = false;
		s.secondPass = look == LOOK_VISIBLE_FRIENDLY_DETECTED ? 1.0f : 0.0f;
		break;
	case LOOK_VISIBLE_DETECTED:
		s.hidden = false;
		s.secondPass = 1.0f;
		break;
	case LOOK_INVISIBLE:
		s.hidden = !cannotHide; // +0x43E = (+0x3AB == 0)
		s.secondPass = 0.0f;
		break;
	default:
		break; // the flags keep their values (RW 0x67624B path)
	}
	// not ported: the draw module refresh RW 0x67396E(1) / 0x671841 / 0x67396E(0) (the slot-0xEC walk and the hidden-status report)
	return s.hidden != wasHidden;
}

void StealthLook::update(State &s)
{
	// RW 0x673114, the two step branches (BFME2 0x272DEE, Drawable_rva00275598.cpp:302-322, tier A identical)
	if (s.inner != s.outer)
	{
		// the binary's pulse sin is the x87 FSIN thunk RW 0x42F4D0 on the float phase; std::sin(float) can differ in the last ulp (Sol r1)
		const float pulse = s.middle + s.halfRange * (float)std::sin(s.phase);
		const float span = (float)kClientFramesPerSecond * s.cycle;
		const float blend = (span - s.blendLeft) / span;
		if (s.blendLeft > 0)
		{
			--s.blendLeft;
		}
		s.opacity = pulse * blend + s.latched * (1.0f - blend);
		s.phase += kStepSeconds / s.cycle * kPi;
	}
	else if (s.opacity < 1.0f)
	{
		const float span = (float)kClientFramesPerSecond * s.cycle;
		const float blend = (span - s.blendLeft) / span;
		if (s.blendLeft > 0)
		{
			--s.blendLeft;
		}
		s.opacity = s.latched * (1.0f - blend) + blend;
	}
}

float StealthLook::drawOpacity(float baseOpacity, const State &s)
{
	return baseOpacity * s.opacity;
}

bool StealthLook::imitate(State &s, const State &parent)
{
	// the parent branch of RW 0x673114: the member drawable's +0xB4 / +0x43E / +0x164 take the parent's verbatim (not +0x358, not the pulse fields)
	s.opacity = parent.opacity;
	const bool wasHidden = s.hidden;
	s.hidden = parent.hidden;
	s.look = parent.look;
	return s.hidden != wasHidden;
}

bool StealthLook::moduleDataAffectedByStealth(const ::ModuleData *data)
{
	// the draw ModuleData vtable +0x78 query the Drawable ctor reads into +0x3AB (RW 0x679AE8): the model draw family (RW 0x464A02) answers its
	// AffectedByStealth (+0x15E), every other ModuleData answers the base's 1 (RW 0x8BD372)
	if (const W3DModelDrawModuleData *md = dynamic_cast<const W3DModelDrawModuleData *>(data))
	{
		return md->m_affectedByStealth;
	}
	return true;
}
