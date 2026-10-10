// OpenBFME. GPL-3.0.
// See GameClient/DrawableManager.h.

#include "GameClient/DrawableManager.h"
#include "GameClient/RenderInterpolation.h"
#include "GameClient/ClientEvents.h"

#include "Common/AsciiString.h"
#include "Common/JobSystem.h"
#include "Common/Prefetch.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>

DrawableManager::DrawableManager(WW3DAssetManager &assets, GameLogic &logic)
	: m_services(assets, logic.modules(), hostOptions(m_scriptFrame))
{
	m_slots.resize(1); // slot 0 unused
	m_services.animationSounds = &m_animationSounds; // lane AUDIO-4
}

DrawableManager::~DrawableManager() = default;

// GetFrame of the drawable state is the logic frame (RW 0x73461B). SMOOTH-1: the frame of the event being applied (the frame whose call made it), or
// of the snapshot being drawn: never the live logic, which may be running on its own thread
W3DLuaDrawScriptHost::Options DrawableManager::hostOptions(const std::uint32_t &frame)
{
	W3DLuaDrawScriptHost::Options o;
	const std::uint32_t *f = &frame;
	o.frame = [f]() { return *f; };
	return o;
}

std::string DrawableManager::fadeStopLine()
{
	return "[S-1001] projectile fade (lane PROJ-2): a launched projectile's drawable fades as RW 0x85EE00 / 0x67309D / 0x675AB7 do (hidden for InvisibleFrames ms * 0.03 client "
		"frames, then faded in over FadeInTime ms * 0.03; a fade over the flight when only the launcher or only the end is seen, hidden when neither), counted in render "
		"time at 30 client frames per second, the fog fades last 6 client frames per logic segment (RW 0x63CF0F .. 0x63CF27: engine +0x38 = 30 / 5, constants 0xD9F60C / 0xD9F608); not ported: the launcher's stealth branch (RW 0x68FC06 / 0x7A3DAD / 0x6AAC52), the hiding of the drawable's particle systems; the look-ahead point of the update "
		"(RW 0x85F6E7) is the Catmull-Rom's P3";
}

std::string DrawableManager::flushStopLine()
{
	return "[S-1583] model condition flush (lane ANIM-1): a drawable's changed flags reach its draw modules at the end of the logic frame (RW 0x6759C4, the first "
		"client frame of a logic frame) and at the pre-fire flush (RW 0x69213E -> 0x67449C); the other callers of RW 0x67449C (object creation paths RW 0x4B3609, "
		"0x5F0EE6, 0x67651E, 0x695A06, 0x78142F, 0x793372, 0x79350E, 0x7987EE, 0x799F0F: their flags reach the draw at the end of the frame instead) and the "
		"flushes before a bone query (RW 0x674673, 0x6756A1, 0x674B1F: the launch bones are computed from the object's flags, DrawableLaunchBones) are not "
		"reproduced; the draw scripts' target record is the one at the flush event, not read live";
}

std::string DrawableManager::wallFadeStopLine()
{
	return "[S-1520] wall span look (lane BUILD-4): each tile of a span fades in over 138 client frames (RW 0x7954A8 -> Drawable::fadeIn RW 0x670AA2, a FADE_IN client "
		"event) and a draw script's CurDrawableShowSubObject / HideSubObject with a draw module's tag shows / hides that module (RW 0x734EF4 -> Drawable::showModule "
		"RW 0x6789B4: a wall segment's floor while it is built); not read: whether the fade's opacity (Drawable + 0xB0) reaches the drawable's particle systems (the "
		"construction dust of a waiting tile is drawn at full strength from the span's frame on) - the port fades the models only; no retail recording of a wall "
		"going up was compared";
}

std::string DrawableManager::interpolationStopLine()
{
	return "[S-151] render interpolation and logic pacing: the drawn pose is retail's (RW 0x674B1F gather, RW 0x6765B9 Catmull-Rom / slerp, SMOOTH-1) from the snapshot of the last completed frame, one logic frame behind the logic; a logic frame runs as one atomic step (retail spreads its six phases over six client ticks; LiveGame's six_tick_pacing does too)";
}

void DrawableManager::Sink::message(const std::string &object, const std::string &className, const std::string &text)
{
	const DrawMessageClass c = classifyDrawMessage(text);
	switch (c.kind)
	{
	case DrawMessageClass::HIDE_MISS: ++hideMisses[c.subObject]; break;
	case DrawMessageClass::DATA_DEFECT: ++dataDefects[className.empty() ? text : className + ": " + text]; break;
	default: errors.push_back(object + (className.empty() ? std::string() : " " + className) + ": " + text); break;
	}
}

void DrawableManager::collect(const Drawable &d, Sink &sink)
{
	const std::string &object = d.getTemplate()->getName();
	for (const DrawEntry &e : d.entries())
	{
		if (!e.draw)
		{
			continue;
		}
		for (const std::string &err : e.draw->errors())
		{
			sink.message(object, e.className, err);
		}
		for (const W3DStopHit &s : e.draw->stops())
		{
			if (sink.stopKeys.insert(s.Id).second)
			{
				sink.stops.push_back(s.Message);
			}
		}
	}
}

Drawable *DrawableManager::findByObject(ObjectID id) const
{
	auto it = m_byObject.find(id);
	return it == m_byObject.end() ? nullptr : find(it->second);
}

void DrawableManager::created(const ClientEvent &ev)
{
	const DrawableID id = (DrawableID)m_slots.size();
	std::vector<std::string> problems;
	std::unique_ptr<Drawable> d(new Drawable(m_services, ev.tmpl, id, ev.object, ev.clientModules, problems));
	for (const std::string &p : problems)
	{
		m_sink.message(ev.tmpl->getName(), std::string(), p);
	}
	// ZH Object::friend_bindToDrawable: new drawables follow the map's time of day and weather (the flags the recorder took from the settings)
	if (ev.flags.any())
	{
		d->setModelConditionFlags(ev.flags);
	}
	if (ev.hasHouseColor)
	{
		d->setHouseColor(ev.houseColor);
	}
	d->setOrientation(ev.angle);
	d->setPosition(&ev.position);
	d->setKindOf(ev.kindOf);
	m_slots.push_back(std::move(d));
	m_byObject[ev.object] = id;
	++m_live;
	++m_created;
	m_events.push_back({ true, id });
	m_slots[id]->friend_boundToObject();
}

void DrawableManager::destroyed(const ClientEvent &ev)
{
	auto it = m_byObject.find(ev.object);
	if (it == m_byObject.end())
	{
		return;
	}
	const DrawableID id = it->second;
	m_byObject.erase(it);
	if (Drawable *d = find(id))
	{
		collect(*d, m_sink);
	}
	m_slots[id].reset();
	--m_live;
	++m_destroyed;
	m_events.push_back({ false, id });
}

void DrawableManager::applyEvents(const std::vector<ClientEvent> &events)
{
	for (const ClientEvent &ev : events)
	{
		if (ev.seq != m_nextSeq)
		{
			// exactly once, in order: a gap or a repeat is a defect of the publication, never skipped silently
			m_sink.errors.push_back("client event " + std::to_string(ev.seq) + " applied out of order (expected " + std::to_string(m_nextSeq) + ")");
		}
		m_nextSeq = ev.seq + 1;
		m_scriptFrame = ev.frame;
		++m_eventsApplied;
		switch (ev.kind)
		{
		case ClientEvent::CREATED: created(ev); break;
		case ClientEvent::DESTROYED: destroyed(ev); break;
		case ClientEvent::REPLACED: // lane STEALTH-2 (RW 0x776F03): the old drawable goes, a new one of the event's template comes
			destroyed(ev);
			created(ev);
			break;
		case ClientEvent::MODEL_CONDITION:
			if (Drawable *d = findByObject(ev.object))
			{
				d->storeModelCondition(ev.bit, ev.on); // lane ANIM-1: stored, applied by the next MODEL_FLUSH (RW 0x679512)
			}
			break;
		case ClientEvent::MODEL_FLUSH:
			if (Drawable *d = findByObject(ev.object))
			{
				d->setScriptTarget(ev.scriptTarget); // lane FX-3: before the flush runs the state scripts
				d->flushModelConditions(ev.hasWeaponTiming, ev.weaponTimingFrames);
			}
			break;
		case ClientEvent::CUSTOM_COLORS: // lane CAH-2 (RW 0x80AF0B -> 0x6727B0)
			if (Drawable *d = findByObject(ev.object))
			{
				d->setCustomColors(ev.customKind, ev.customColors);
			}
			break;
		case ClientEvent::FADE_IN: // lane BUILD-4 (RW 0x670AA2)
			if (Drawable *d = findByObject(ev.object))
			{
				d->fadeIn((double)ev.fadeFrames);
			}
			break;
		case ClientEvent::SHOW_MODULE:
			if (Drawable *d = findByObject(ev.object))
			{
				d->showModule(ev.name, ev.visible, ev.permanent);
			}
			break;
		case ClientEvent::SHOW_SUB_OBJECT:
			if (Drawable *d = findByObject(ev.object))
			{
				d->showSubObject(ev.name, ev.visible, ev.permanent);
			}
			break;
		case ClientEvent::PLACEMENT:
			if (Drawable *d = findByObject(ev.object))
			{
				if (ev.setFlags)
				{
					d->setModelConditionFlags(ev.flags);
				}
				if (ev.setScale)
				{
					d->setInstanceScale(ev.scale);
				}
				d->setDrawsInMirror(ev.mirror);
			}
			break;
		}
	}
}

std::vector<DrawableManager::Event> DrawableManager::takeEvents()
{
	std::vector<Event> out;
	out.swap(m_events);
	return out;
}

void DrawableManager::prefetchAhead(size_t i) const
{
	const size_t n = m_slots.size();
	if (i + 12 < n && m_slots[i + 12])
	{
		OPENBFME_PREFETCH(&m_slots[i + 12]->entries());
	}
	if (i + 8 < n && m_slots[i + 8])
	{
		const Drawable &b = *m_slots[i + 8];
		if (!b.entries().empty())
		{
			OPENBFME_PREFETCH(&b.entries()[0].draw);
		}
		if (!b.clientModules().empty())
		{
			OPENBFME_PREFETCH(&b.clientModules()[0]);
		}
	}
	if (i + 4 < n && m_slots[i + 4])
	{
		const Drawable &c = *m_slots[i + 4];
		if (!c.entries().empty() && c.entries()[0].draw)
		{
			OPENBFME_PREFETCH(c.entries()[0].draw.get());
		}
		if (!c.clientModules().empty())
		{
			OPENBFME_PREFETCH(c.clientModules()[0].get());
		}
	}
}

void DrawableManager::advance(double elapsedMs)
{
	for (size_t i = 1; i < m_slots.size(); ++i)
	{
		prefetchAhead(i); // lane PERF-3
		if (Drawable *d = m_slots[i].get())
		{
			d->advanceAnimation(elapsedMs);
			d->updateFade(elapsedMs); // lane PROJ-2: RW 0x675AB7 (DrawableFade.cpp)
		}
	}
}

void DrawableManager::syncTransforms(const LogicSnapshot &snapshot, double alpha, bool interpolate)
{
	m_scriptFrame = snapshot.frame;
	// lane COMBAT-4: RW 0x4BF2D8 (W3DModelDraw::replaceModelConditionState): a model draw with DependencySharedModelFlags hands the flags of that set to its dependent
	// drawables (+ 0xA8, by drawable id; RW 0x67651E clearAndSet on each): Grond's MOVING / TURN_* / BACKING_UP reach its troll crew, whose pushing AnimationStates
	// need them. INFERENCE (stop S-1791): the dependents are the drawables of the objects the container holds (its riders and crew); the writer of the list is not read
	for (size_t i = 1; i < m_slots.size(); ++i)
	{
		Drawable *d = m_slots[i].get();
		const ObjectSnapshot *rec = d ? snapshot.find(d->getObjectID()) : nullptr;
		if (!rec || rec->containedBy == INVALID_ID)
		{
			continue;
		}
		Drawable *container = findByObject(rec->containedBy);
		if (!container)
		{
			continue;
		}
		const ModelConditionFlags shared = container->dependencySharedModelFlags();
		if (shared.any() && d->applyDependencyFlags(shared, container->getModelConditionFlags()))
		{
			++m_dependencyUpdates;
		}
	}
	// lane PERF-3: the part of every drawable's sync that reads only the drawable and its record (the interpolated pose and its trigonometry, the construction
	// look) runs on the client job pool, each chunk writing only its own slots; then, in slot order on this thread, what reaches shared state (the projectile
	// fade's first sight, the transform and its reaction: the footstep manager's lists). Per drawable the result is syncFromSnapshot's.
	const size_t slots = m_slots.size();
	if (!m_parallelSync)
	{
		for (size_t i = 1; i < slots; ++i)
		{
			if (Drawable *d = m_slots[i].get())
			{
				const ObjectSnapshot *rec = snapshot.find(d->getObjectID());
				d->setSyncedRecord(&snapshot, rec);
				if (rec)
				{
					d->startProjectileFade(*rec, snapshot);
					d->syncFromSnapshot(*rec, snapshot.frame, alpha, interpolate);
				}
			}
		}
		m_animationSounds.update();
		return;
	}
	if (m_syncScratch.size() < slots)
	{
		m_syncScratch.resize(slots + slots / 2);
	}
	// below m_parallelSyncMinimum slots (512) the work is one chunk, run on this thread (waking the pool would cost more than it saves)
	JobSystem::client().parallelFor(slots, slots < m_parallelSyncMinimum ? slots : 64, [&](size_t, size_t begin, size_t end) {
		for (size_t i = begin; i < end; ++i)
		{
			Drawable::SyncPrep &prep = m_syncScratch[i];
			prep.rec = nullptr;
			Drawable *d = i > 0 ? m_slots[i].get() : nullptr;
			if (!d)
			{
				continue;
			}
			const ObjectSnapshot *rec = snapshot.find(d->getObjectID());
			d->setSyncedRecord(&snapshot, rec); // lane PERF-3: the render side's lookup of the same record
			if (rec)
			{
				d->prepareSync(*rec, snapshot.frame, alpha, interpolate, prep);
			}
		}
	});
	for (size_t i = 1; i < slots; ++i)
	{
		prefetchAhead(i); // lane PERF-3
		Drawable *d = m_slots[i].get();
		const Drawable::SyncPrep &prep = m_syncScratch[i];
		if (d && prep.rec)
		{
			d->startProjectileFade(*prep.rec, snapshot); // lane PROJ-2: RW 0x85EE00 for a launch the drawable has not seen (DrawableFade.cpp)
			d->commitSync(prep);
		}
	}
	// lane AUDIO-4: TheAnimationSoundModuleManager's client update (RW 0x83F321), after the animations advanced and the drawables moved
	m_animationSounds.update();
}

void DrawableManager::applyPlacement(Object &obj, const MapObjectDrawable &placement)
{
	// RENDER-2 (review r1): the logic owns (and hashes) what the launch-bone query reads; the drawable copies it
	if (placement.flags.any())
	{
		Object::ModelConditionBits bits{};
		for (int b = 0; b < 19 * 32; ++b)
		{
			if (placement.flags.test(b))
			{
				bits[(size_t)b >> 5] |= 1u << (b & 31);
			}
		}
		obj.setPlacementConditionBits(bits);
	}
	if (obj.getContainedBy() == nullptr)
	{
		obj.setInstanceScale(placement.scale);
	}
	// SMOOTH-1: the drawable's part (flags, scale, mirror) is a client event
	if (ClientEventRecorder *r = dynamic_cast<ClientEventRecorder *>(obj.clientHooks()))
	{
		r->placement(obj, placement);
	}
}

DrawableManager::Report DrawableManager::report() const
{
	Report r;
	r.created = m_created;
	r.destroyed = m_destroyed;
	r.live = m_live;
	Sink sink = m_sink;
	for (size_t i = 1; i < m_slots.size(); ++i)
	{
		if (const Drawable *d = m_slots[i].get())
		{
			collect(*d, sink);
		}
	}
	r.errors = sink.errors;
	r.dataDefects = sink.dataDefects;
	r.hideMisses = sink.hideMisses;
	r.stops = sink.stops;
	r.stops.push_back(interpolationStopLine());
	r.stops.push_back(fadeStopLine()); // lane PROJ-2
	r.stops.push_back(flushStopLine()); // lane ANIM-1
	r.stops.push_back(wallFadeStopLine()); // lane BUILD-4
	for (const std::string &l : RenderInterpolation::stopLines())
	{
		r.stops.push_back(l); // SMOOTH-1
	}
	size_t unportedClient = 0;
	for (size_t i = 1; i < m_slots.size(); ++i)
	{
		if (const Drawable *d = m_slots[i].get())
		{
			for (const std::unique_ptr<DrawableModule> &m : d->clientModules())
			{
				unportedClient += m->isUnported() ? 1 : 0;
			}
		}
	}
	if (unportedClient)
	{
		r.stops.push_back("[S-140] client modules: " + std::to_string(unportedClient) + " live ClientUpdate / ClientBehavior modules are UnportedDrawableModules that do nothing (sway, beacon pulses, resource trees ...)");
	}
	r.scriptsRun = m_services.host.scriptsRun();
	for (const std::string &f : m_services.host.failures())
	{
		r.errors.push_back("BeginScript failed: " + f);
	}
	for (const LuaReportSink::Entry &e : m_services.host.reports().entries())
	{
		r.stops.push_back("[" + e.stop + "] " + e.text);
	}
	for (size_t i = 1; i < m_slots.size(); ++i)
	{
		const Drawable *d = m_slots[i].get();
		if (!d)
		{
			continue;
		}
		for (const std::unique_ptr<DrawableModule> &m : d->clientModules())
		{
			if (m->isUnported())
			{
				++r.unportedClientModules[m->getModuleClassName()];
			}
		}
		for (const DrawEntry &e : d->entries())
		{
			++r.byDrawClass[e.className];
			if (e.kind == W3D_DRAWKIND_MODEL)
			{
				++r.modelDraws;
				(e.animated ? r.animatedModelDraws : r.staticModelDraws) += 1;
				if (e.draw)
				{
					const W3DDrawFrame f = e.draw->frame();
					if (f.model && !f.modelName.empty() && !e.moduleHidden)
					{
						++r.distinctModels[AsciiStringUtil::lowered(f.modelName)];
						++r.modelsShown;
					}
				}
			}
			else if (!e.staticModel.empty() && !e.moduleHidden)
			{
				++r.staticModels;
				++r.modelsShown;
				++r.distinctModels[AsciiStringUtil::lowered(e.staticModel)];
			}
			else
			{
				++r.notDrawnEntries;
			}
		}
	}
	return r;
}
