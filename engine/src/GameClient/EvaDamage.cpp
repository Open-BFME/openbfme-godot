// OpenBFME. GPL-3.0. See EvaDamage.h (lane EVADAMAGE-1).

#include "GameClient/EvaDamage.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameLogic/Object/EvaDamageNotice.h"

namespace
{
// RW 0x671DFD (Drawable_extendEvaDamageFarFrame): with a template, + 0x388 = max(+ 0x388, frame + EvaEventSecondDamageFarFromFirstTimeoutMS)
void raiseFarFrame(Drawable *d, UnsignedInt frame, EvaDamage::Stats &stats)
{
	if (d && d->getTemplate())
	{
		d->raiseEvaDamageFarFrame(frame + EvaDamage::timeoutFrames(*d->getTemplate()));
		++stats.farFramesRaised;
	}
}
} // namespace

void EvaDamage::apply(const ClientEvent &ev, DrawableManager &drawables, Stats &stats)
{
	++stats.notices;
	Drawable *self = drawables.findByObject(ev.object);
	const ThingTemplate *tt = self ? self->getTemplate() : nullptr; // RW 0x67B580: the drawable's template (+ 4); none: no Eva
	if (!tt)
	{
		return;
	}
	const EvaDamageNotice &n = ev.eva;
	const std::string ambushed = templateEvent(*tt, "EvaEventAmbushed");                         // + 0x5A8
	const std::string byFire = templateEvent(*tt, "EvaEventDamagedByFireOwner");                 // + 0x598
	const std::string shrouded = templateEvent(*tt, "EvaEventDamagedFromShroudedSourceOwner");   // + 0x594
	const std::string damaged = templateEvent(*tt, "EvaEventDamagedOwner");                      // + 0x590
	const std::string far = templateEvent(*tt, "EvaEventSecondDamageFarFromFirstOwner");          // + 0x59C
	// RW 0x67B596 .. 0x67B635: the event and its second
	std::string event, second;
	std::uint64_t *row = &stats.damaged;
	if (!ambushed.empty() && n.ambushable)
	{
		event = ambushed;
		second = far;
		row = &stats.ambushed;
	}
	else if (n.byFire && !byFire.empty())
	{
		event = byFire; // no second (RW 0x67B5F9)
		row = &stats.byFire;
	}
	else
	{
		const Drawable *source = !n.byFire && n.attacker != INVALID_ID ? drawables.findByObject(n.attacker) : nullptr; // RW 0x70E013
		if (source && source->shroudLook().fullyObscured && !shrouded.empty())                                          // + 0x440
		{
			event = shrouded;
			row = &stats.shrouded;
		}
		else
		{
			event = damaged;
		}
		second = far;
	}
	if (event.empty())
	{
		return;
	}
	++stats.picked;
	// RW 0x67B644 .. 0x67B6A4: the far frame against TheGameLogic's frame (the hit's), the Eva's state on its thread
	const bool farFrameDue = self->evaDamageFarFrame() <= ev.frame;
	const std::string chosen = AudioApi::reportEvaDamage(event, second, farFrameDue, ev.position);
	if (!chosen.empty())
	{
		++stats.reported;
		++*(chosen == event ? row : &stats.secondFar);
		stats.lastEvent = chosen;
	}
	// RW 0x67B6AB .. 0x67B776: the victim alone (no scan range or no DamagedOwner), else every scanned object's drawable (RW 0x70E013)
	if (!n.scanned)
	{
		raiseFarFrame(self, ev.frame, stats);
		return;
	}
	for (const ObjectID id : n.scan)
	{
		raiseFarFrame(drawables.findByObject(id), ev.frame, stats);
	}
}

std::vector<std::string> EvaDamage::acceptanceStops()
{
	return {
		"[S-7090] Eva damage notice (EVADAMAGE-1, RW 0x67B4B7): INFERENCE: the SecondDamageFarFromFirst scan is centred on the object's position (retail: the "
		"drawable's interpolated one, RW 0x676711) and reads the object's template (retail: the drawable's; they differ for a shown disguise); RW 0x694CCC's "
		"stealth-garrison branch takes the container as the apparent controlling player (contain slot 0x4C not ported) and reads the HORDE branch's slot 0x78 "
		"as the horde interface; TheEva's blocked-by-timeout / about-to-play (RW 0x5DCC88 / 0x5DCCEB) are answered from the port's Eva.ini rules (S-247); NOT PORTED: the drawable's "
		"far frame (+0x388) is not saved (RotWK's drawable xfer RW 0x67A544 transfers it from version 9, RW 0x67A78D), so a loaded game starts every far frame at 0",
	};
}
