// OpenBFME. GPL-3.0.
//
// DrawableFade (lane PROJ-2): the drawable's fade - what makes a launched siege stone appear a moment after it leaves the arm, and a projectile flying out
// of or into the fog fade. CLIENT ONLY: it reads a completed frame's snapshot (GameClient/LogicSnapshot) and the projectile template's module data, and
// writes only the drawable's opacity and hidden flag; it consumes no random and never reaches the simulation (tools/sim/sim_policy.json excludes it).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   RW 0x85EE00 .. 0x85EF31 (BezierProjectileBehavior::projectileFireAtObjectOrPosition, at every launch and bounce), when the projectile has a drawable and
//     is not KindOf HERO (template +0x113 bit 2) or MONSTER (+0x109 bit 2):
//       launcher visible = the producer (object +0x78) exists and its shroud status for the local player (RW 0x68D8F7) is 1 (OBJECTSHROUD_CLEAR), unless the
//         stealth branch RW 0x68FC06 / +0x31C / 0x7A3DAD / 0x6AAC52 says otherwise;
//       target visible = RW 0xB4D9A0(local player, flight end +0x5C) == 0 (the cell is clear);
//       both seen: if InvisibleFrames (+0x40) or FadeInTime (+0x44): RW 0x67309D(ftol(InvisibleFrames * 0.03f), ftol(FadeInTime * 0.03f)) - the module data are
//         milliseconds, DAT 0xD9F624 = 0.03 turns them into 30 Hz client frames;
//       only the launcher seen: RW 0x670A50 fade out over ftol((global +0x38) * segments) client frames;
//       only the target seen: RW 0x670AA2 fade in over the same count;
//       neither: RW 0x6718FB(1) hidden.
//   RW 0x67309D: hidden; mode 5, counter 0, target = the invisible frames, +0x134 = the fade-in frames, the start = the client frame (TheGameClient vslot 0x7C).
//   RW 0x670AA2 fadeIn(n): n == 0: opacity 1, mode 0; else opacity 0, mode 1; counter 0, target n, the start now. RW 0x670A50 fadeOut(n): n == 0: opacity 0,
//     mode 0; else opacity 1, mode 2; counter 0, target n, the start now.
//   RW 0x675AB7 .. 0x675BBF (Drawable::updateDrawable, every client frame) while mode != 0: counter = min(counter + client frames since the last step, target);
//     mode 1 / 2: opacity = (mode 1 ? counter : target - counter) / target (target 0: 1 for mode 1, 0 for mode 2), mode 0 when counter >= target; then a hidden
//     drawable is shown, a shown one whose opacity * 255 < 1 is hidden. Mode 3 / 4 / 5 when counter == target: hidden = (mode == 4); mode 5 then fadeIn(+0x134).
// THIS PORT: the client frames are counted from render time (elapsedMs * 0.03, fractional: the presentation may be smoother than retail's 30 Hz steps); the
// targets are ftol'd from the module data exactly as retail.
// TARGET FACT: the global at +0x38 of RW 0xDE4324 that scales the fog fades is engine +0x38 = 30 / 5 = 6 client frames per logic frame (RW 0x63CF0F .. 0x63CF27: idiv of the constants 30 at 0xD9F60C and 5 at 0xD9F608).
// Not ported (stop S-1001): the stealth branch of the launcher's visibility; the drawable's particle systems (its ParticleSysBone trails) are not hidden with it.

#include "GameClient/Drawable.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/System/ShroudManager.h"

namespace
{
const float kMsToClientFrames = 0.03f;      // DAT 0xD9F624
const double kClientFramesPerLogicFrame = 6.0; // RW 0x63CF0F .. 0x63CF27: 30 / 5 (constants 0xD9F60C / 0xD9F608)
const double kClientFramesPerMs = 0.03;     // retail's client runs 30 frames per second

const BezierProjectileBehaviorModuleData *bezierData(const ThingTemplate &tt)
{
	for (const ThingTemplate::Nugget &n : tt.behaviorModules().nuggets())
	{
		if (const BezierProjectileBehaviorModuleData *d = dynamic_cast<const BezierProjectileBehaviorModuleData *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}

// ftol of a float product (RW 0x85EED9 / 0x85EEF5: fild, fmul dword 0.03, ftol: truncation)
double framesOfMs(unsigned ms)
{
	return (double)(unsigned)((float)ms * kMsToClientFrames);
}
} // namespace

void Drawable::setFadeHidden(bool hidden)
{
	if (hidden != m_fade.hidden)
	{
		m_fade.hidden = hidden;
		++m_changeCount;
	}
}

// RW 0x670AA2
void Drawable::fadeIn(double frames)
{
	if (frames == 0.0)
	{
		m_fade.opacity = 1.0f;
		m_fade.mode = 0;
	}
	else
	{
		m_fade.opacity = 0.0f;
		m_fade.mode = 1;
	}
	m_fade.counter = 0.0;
	m_fade.target = frames;
}

// RW 0x670A50
void Drawable::fadeOut(double frames)
{
	if (frames == 0.0)
	{
		m_fade.opacity = 0.0f;
		m_fade.mode = 0;
	}
	else
	{
		m_fade.opacity = 1.0f;
		m_fade.mode = 2;
	}
	m_fade.counter = 0.0;
	m_fade.target = frames;
}

// RW 0x85EE00 .. 0x85EF31
void Drawable::startProjectileFade(const ObjectSnapshot &rec, const LogicSnapshot &snapshot)
{
	if (!rec.projectile || rec.projectileFireFrame == m_fade.seenFire)
	{
		return;
	}
	m_fade.seenFire = rec.projectileFireFrame;
	if (isKindOfName("HERO") || isKindOfName("MONSTER"))
	{
		return; // RW 0x85EE8F .. 0x85EEA3: template +0x113 bit 2 / +0x109 bit 2
	}
	const ShroudView *shroud = snapshot.shroud.get();
	const bool shroudShown = shroud && shroud->displayed && shroud->localPlayer >= 0;
	const ObjectSnapshot *launcher = rec.projectileLauncher != INVALID_ID ? snapshot.find(rec.projectileLauncher) : nullptr;
	// RW 0x68D8F7 == 1: OBJECTSHROUD_CLEAR (without a displayed shroud every object is seen)
	const bool launcherSeen = launcher && (!shroudShown || launcher->objectShroud == (int)OBJECTSHROUD_CLEAR);
	// RW 0xB4D9A0 == 0: the cell of the flight's end is clear
	const bool targetSeen = !shroudShown || shroud->statusAt(rec.projectileEnd.x, rec.projectileEnd.y) == CELLSHROUD_CLEAR;
	const double flightFrames = kClientFramesPerLogicFrame * (double)rec.projectileSegments;
	if (launcherSeen)
	{
		if (targetSeen)
		{
			const BezierProjectileBehaviorModuleData *d = getTemplate() ? bezierData(*getTemplate()) : nullptr;
			if (d && (d->m_invisibleFrames != 0 || d->m_fadeInTime != 0))
			{
				// RW 0x67309D
				setFadeHidden(true);
				m_fade.counter = 0.0;
				m_fade.target = framesOfMs(d->m_invisibleFrames);
				m_fade.mode = 5;
				m_fade.fadeIn = framesOfMs(d->m_fadeInTime);
			}
		}
		else
		{
			fadeOut(flightFrames);
		}
	}
	else if (targetSeen)
	{
		fadeIn(flightFrames);
	}
	else
	{
		setFadeHidden(true);
	}
}

// RW 0x675AB7 .. 0x675BBF
void Drawable::updateFade(double elapsedMs)
{
	if (m_fade.mode == 0)
	{
		return;
	}
	const double step = elapsedMs > 0.0 ? elapsedMs * kClientFramesPerMs : 0.0;
	double next = m_fade.counter + step;
	if (m_fade.target - next < 1e-6)
	{
		next = m_fade.target; // a whole client frame of render time is a whole frame (1000 / 30 ms * 0.03 rounds below 1)
	}
	m_fade.counter = next;
	if (m_fade.mode < 3)
	{
		float opacity;
		if (m_fade.target == 0.0)
		{
			opacity = m_fade.mode == 1 ? 1.0f : 0.0f;
		}
		else
		{
			const double v = m_fade.mode == 1 ? m_fade.counter : m_fade.target - m_fade.counter;
			opacity = (float)(v / m_fade.target);
		}
		m_fade.opacity = opacity;
		if (m_fade.counter >= m_fade.target)
		{
			m_fade.mode = 0;
		}
		if (m_fade.hidden)
		{
			setFadeHidden(false);
		}
		else if (opacity * 255.0f < 1.0f)
		{
			setFadeHidden(true);
		}
		++m_changeCount; // the device layer re-reads the opacity
		return;
	}
	if (m_fade.counter == m_fade.target)
	{
		setFadeHidden(m_fade.mode == 4);
		if (m_fade.mode == 5)
		{
			fadeIn(m_fade.fadeIn);
			++m_changeCount;
		}
	}
}
