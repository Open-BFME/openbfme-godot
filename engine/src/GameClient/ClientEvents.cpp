// OpenBFME. GPL-3.0.
//
// ClientEventRecorder (lane SMOOTH-1): see ClientEvents.h. Runs on the simulation owner's thread; it reads the object (template, owner, transform)
// and writes only its own event list, never simulation state.

#include "GameClient/ClientEvents.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

void ClientEventRecorder::observerEvent(Object &obj, ObserverEvent::Kind kind, const Coord3D *at, const std::string &name, float weight)
{
	if (!m_observing)
	{
		return;
	}
	ObserverEvent event;
	event.kind = kind;
	event.frame = m_logic.getFrame();
	event.object = obj.getID();
	event.player = obj.getControllingPlayer() ? obj.getControllingPlayer()->getPlayerIndex() : -1;
	event.position = at ? *at : *obj.getPosition();
	event.name = name.empty() ? obj.getTemplate()->getName() : name;
	event.hero = obj.isKindOfName("HERO");
	event.structure = obj.isKindOfName("STRUCTURE");
	event.base = obj.isKindOfName("COMMANDCENTER") || obj.isKindOfName("CASTLE_KEEP");
	event.weight = weight;
	m_timeline.add(event);
}

ClientEvent &ClientEventRecorder::push(ClientEvent::Kind kind, const Object &obj)
{
	m_events.emplace_back();
	ClientEvent &e = m_events.back();
	e.kind = kind;
	e.frame = m_logic.getFrame();
	e.seq = m_seq++;
	e.object = obj.getID();
	return e;
}

void ClientEventRecorder::objectCreated(Object &obj)
{
	if (obj.isKindOfName("CAPTUREFLAG")) m_animationConditions[obj.getID()] = obj.getModelConditionBits();
	ClientEvent &e = push(ClientEvent::CREATED, obj);
	e.tmpl = obj.getTemplate();
	// ZH Object::friend_bindToDrawable: new drawables follow the map's time of day and weather (was DrawableManager::objectCreated)
	const GameLogicSettings &settings = m_logic.settings();
	if (settings.forceModelsToFollowTimeOfDay && settings.night)
	{
		e.flags.set(ModelCondition::indexOf("NIGHT"));
	}
	if (settings.forceModelsToFollowWeather && settings.snowy)
	{
		e.flags.set(ModelCondition::indexOf("SNOW"));
	}
	if (const Player *p = obj.getControllingPlayer())
	{
		if (p->hasTeamColor())
		{
			e.hasHouseColor = true;
			// lane WEATHER-1: Drawable::friend_bindToObject (RW 0x674566) colours the drawable with Object::getNightIndicatorColor (RW 0x68B71F: the
			// player's + 0x2A4) when TheGlobalData's time of day is NIGHT (+ 0x134 == 4), else getIndicatorColor (RW 0x68B6F5, + 0x2A0)
			e.houseColor = settings.night ? p->getPlayerNightColor() : p->getPlayerColor();
		}
	}
	// the client modules of the drawable, resolved here on the logic owner in the drawable's creation order (review r2: the render side must not
	// intern into the logic's name keys or count in its factory; the key allocation order is the one the synchronous drawable constructor had)
	for (ModuleType type : { MODULETYPE_CLIENT_UPDATE, MODULETYPE_CLIENT_BEHAVIOR })
	{
		for (const ThingTemplate::Nugget &n : obj.getTemplate()->moduleList(type).nuggets())
		{
			ClientEvent::ClientModule cm;
			cm.name = n.name;
			cm.data = n.data.get();
			cm.resolved = m_logic.modules().resolveClientModule(n.name, type);
			e.clientModules.push_back(std::move(cm));
		}
	}
	e.position = *obj.getPosition();
	e.kindOf = obj.getKindOf();
	e.angle = obj.getOrientation();
	obj.friend_bindToClient(this);
}

void ClientEventRecorder::objectDestroyed(Object &obj)
{
	m_animationConditions.erase(obj.getID());
	if (obj.clientHooks() != this)
	{
		return; // never bound (the object died before it was announced)
	}
	if (m_dirtySet.erase(obj.getID()) != 0) // lane ANIM-1: a drawable that goes needs no flush
	{
		for (size_t i = 0; i < m_dirty.size(); ++i)
		{
			if (m_dirty[i] == obj.getID())
			{
				m_dirty.erase(m_dirty.begin() + (std::ptrdiff_t)i);
				break;
			}
		}
	}
	push(ClientEvent::DESTROYED, obj);
}

// lane STEALTH-2 (RW 0x776F03): the drawable of `tmpl` replaces the object's, with the object's model conditions (RW 0x444D7D / 0x68D64A), position and angle
void ClientEventRecorder::fadeIn(Object &obj, UnsignedInt frames)
{
	ClientEvent &e = push(ClientEvent::FADE_IN, obj);
	e.fadeFrames = frames;
}

void ClientEventRecorder::setCustomColors(Object &obj, int kind, std::uint32_t c0, std::uint32_t c1, std::uint32_t c2)
{
	ClientEvent &e = push(ClientEvent::CUSTOM_COLORS, obj);
	e.customKind = kind;
	e.customColors[0] = c0;
	e.customColors[1] = c1;
	e.customColors[2] = c2;
}

void ClientEventRecorder::heroAttacked(Object &obj)
{
	push(ClientEvent::HERO_ATTACKED, obj);
}

void ClientEventRecorder::flashHeroButton(const std::string &templateName, int frames)
{
	m_events.emplace_back();
	ClientEvent &e = m_events.back();
	e.kind = ClientEvent::HERO_FLASH;
	e.frame = m_logic.getFrame();
	e.seq = m_seq++;
	e.name = templateName;
	e.flashFrames = frames;
}

void ClientEventRecorder::replaceDrawable(Object &obj, const ThingTemplate *tmpl, bool hasColor, std::uint32_t color)
{
	if (!tmpl)
	{
		return;
	}
	ClientEvent &e = push(ClientEvent::REPLACED, obj);
	e.tmpl = tmpl;
	for (int bit = 0; bit < 19 * 32 && bit < e.flags.size(); ++bit)
	{
		if (obj.testModelCondition(bit))
		{
			e.flags.set(bit);
		}
	}
	e.hasHouseColor = hasColor;
	e.houseColor = color;
	for (ModuleType type : { MODULETYPE_CLIENT_UPDATE, MODULETYPE_CLIENT_BEHAVIOR })
	{
		for (const ThingTemplate::Nugget &n : tmpl->moduleList(type).nuggets())
		{
			ClientEvent::ClientModule cm;
			cm.name = n.name;
			cm.data = n.data.get();
			cm.resolved = m_logic.modules().resolveClientModule(n.name, type);
			e.clientModules.push_back(std::move(cm));
		}
	}
	e.position = *obj.getPosition();
	e.kindOf = obj.getKindOf();
	e.angle = obj.getOrientation();
}

void ClientEventRecorder::modelConditionChanged(Object &obj, int bit, bool on)
{
	ClientEvent &e = push(ClientEvent::MODEL_CONDITION, obj);
	e.bit = bit;
	e.on = on;
	if (m_dirtySet.insert(obj.getID()).second) // lane ANIM-1: RW 0x679612 .. 0x67961F, the drawable is dirty until its flags are flushed
	{
		m_dirty.push_back(obj.getID());
	}
}

// lane ANIM-1: the flush of one drawable (RW 0x67449C with force 0: nothing when it is not dirty)
void ClientEventRecorder::flushModelConditions(Object &obj)
{
	if (m_dirtySet.erase(obj.getID()) == 0)
	{
		return;
	}
	for (size_t i = 0; i < m_dirty.size(); ++i)
	{
		if (m_dirty[i] == obj.getID())
		{
			m_dirty.erase(m_dirty.begin() + (std::ptrdiff_t)i);
			break;
		}
	}
	pushFlush(obj);
}

void ClientEventRecorder::loadedObject(Object &obj)
{
	if (obj.clientHooks() != this)
	{
		return; // never bound
	}
	ClientEvent &e = push(ClientEvent::MODEL_CONDITIONS, obj);
	for (int bit = 0; bit < 19 * 32 && bit < e.flags.size(); ++bit)
	{
		if (obj.testModelCondition(bit))
		{
			e.flags.set(bit);
		}
	}
	if (m_dirtySet.erase(obj.getID()) != 0) // the flush below covers the changes recorded before
	{
		for (size_t i = 0; i < m_dirty.size(); ++i)
		{
			if (m_dirty[i] == obj.getID())
			{
				m_dirty.erase(m_dirty.begin() + (std::ptrdiff_t)i);
				break;
			}
		}
	}
	pushFlush(obj);
}

void ClientEventRecorder::pushFlush(Object &obj)
{
	static const int captureFlagKind = ObjectTemplateInfoBuilder::kindOfIndex("CAPTUREFLAG");
	const bool captureFlag = captureFlagKind >= 0 && obj.isKindOf((unsigned)captureFlagKind);
	if (captureFlag)
	{
		auto &before = m_animationConditions[obj.getID()];
		// Dispatch before pushing the flush: Lua can append subobject events.
		if (before != obj.getModelConditionBits()) m_logic.animationConditionEvents(obj, before);
		before = obj.getModelConditionBits();
	}
	ClientEvent &e = push(ClientEvent::MODEL_FLUSH, obj);
	if (captureFlag)
	{
		// RW 0x853D31's capture colour: the raising flag belongs visually to the
		// capturer before ownership transfers. Cancel returns to the current owner.
		const Object *capturer = m_logic.findObjectByID(obj.getCapturerID());
		const Player *player = capturer ? capturer->getControllingPlayer() : obj.getControllingPlayer();
		e.hasHouseColor = true;
		e.houseColor = player ? player->getPlayerColor() : 0;
	}
	e.scriptTarget = DrawableScriptTarget::capture(m_logic, obj); // lane FX-3: what the state scripts this flush starts ask about (RW 0x73667D / 0x734A75)
	if (const ObjectWeapons *w = obj.getWeapons())
	{
		e.hasWeaponTiming = w->drawWeaponTimingFrames(e.weaponTimingFrames); // lane ANIM-1: read when the draw applies the flags (RW 0x4BEE31)
	}
}

bool ClientEventRecorder::templateHasDrawModuleTag(const ThingTemplate &tt, const std::string &tag)
{
	for (const ThingTemplate::Nugget &n : tt.drawModules().nuggets())
	{
		if (n.tag == tag)
		{
			return true;
		}
	}
	return false;
}

bool ClientEventRecorder::showModule(Object &obj, const std::string &tag, bool visible, bool permanent)
{
	if (!templateHasDrawModuleTag(*obj.getTemplate(), tag))
	{
		return false; // RW 0x6789B4: no module has the tag, nothing changes
	}
	ClientEvent &e = push(ClientEvent::SHOW_MODULE, obj);
	e.name = tag;
	e.visible = visible;
	e.permanent = permanent;
	return true;
}

void ClientEventRecorder::showSubObject(Object &obj, const std::string &name, bool visible, bool permanent)
{
	ClientEvent &e = push(ClientEvent::SHOW_SUB_OBJECT, obj);
	e.name = name;
	e.visible = visible;
	e.permanent = permanent;
}

void ClientEventRecorder::placement(Object &obj, const MapObjectDrawable &placement)
{
	ClientEvent &e = push(ClientEvent::PLACEMENT, obj);
	if (placement.flags.any())
	{
		e.setFlags = true;
		e.flags = placement.flags;
	}
	if (obj.getContainedBy() == nullptr)
	{
		e.setScale = true;
		e.scale = placement.scale;
	}
	e.mirror = placement.drawsInMirror;
}

std::vector<ClientEvent> ClientEventRecorder::take(bool endOfFrame)
{
	// lane ANIM-1: the end of the logic frame, RW 0x6759C4: every dirty drawable's flags reach its draw modules once
	if (endOfFrame)
	{
		// Lua callbacks can append dirty objects; never retain vector iterators.
		for (size_t i = 0; i < m_dirty.size(); ++i)
		{
			const ObjectID id = m_dirty[i];
			if (Object *o = m_logic.findObjectByID(id))
			{
				pushFlush(*o);
			}
		}
		m_dirty.clear();
		m_dirtySet.clear();
	}
	std::vector<ClientEvent> out;
	out.swap(m_events);
	return out;
}
