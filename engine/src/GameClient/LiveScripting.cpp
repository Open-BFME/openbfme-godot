// OpenBFME. GPL-3.0.
// See GameClient/LiveScripting.h.

#include "GameClient/LiveScripting.h"
#include "GameLogic/Module/EmotionModules.h"

#include "Common/Audio/AudioLog.h"
#include "Common/Audio/AudioRequests.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/Upgrade.h"
#include "GameClient/MapCreationHooks.h"
#include "Common/Player.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/System/InvisibilityManager.h"

namespace
{
class ArchiveLuaFiles : public LuaFileSource
{
public:
	explicit ArchiveLuaFiles(ArchiveFileSystem &fs) : m_fs(fs) {}
	bool exists(const std::string &path) override { return m_fs.doesFileExist(path); } // RW 0xA14AEB
	bool read(const std::string &path, std::string *bytes) override                    // RW 0xA149A2
	{
		std::vector<std::uint8_t> raw;
		std::string error;
		if (!m_fs.readFile(path, raw, &error))
		{
			return false;
		}
		bytes->assign(raw.begin(), raw.end());
		return true;
	}

private:
	ArchiveFileSystem &m_fs;
};
} // namespace

LiveScripting::LiveScripting(GameLogic &logic, ArchiveFileSystem &fs, NameKeyGenerator &keys)
	: m_logic(logic)
	, m_fs(fs)
	, m_keys(keys)
{
}

LiveScripting::~LiveScripting() = default;

bool LiveScripting::start(const std::string &mapFolder, std::string *error)
{
	LuaScriptEngine::Config c;
	c.keys = &m_keys;
	c.logicRandom = &m_logic.random(); // the game's generator: the creation draw and the bindings use the same stream
	c.host = this;
	m_engine.reset(new LuaScriptEngine(c));
	ArchiveLuaFiles files(m_fs);
	if (!m_engine->startNewGame(files, mapFolder))
	{
		if (error)
		{
			*error = "the script engine could not read Data\\Scripts\\Scripts.lua / ScriptEvents.xml";
		}
		return false;
	}
	m_templates.clear();
	// ONE creation path: LuaScriptEngine::sendObjectCreated (draw, drawable, world entry, OnCreated)
	m_logic.setObjectCreatedProc([this](Object &obj, const std::function<void()> &bindDrawable) {
		LuaObjectInfo info;
		const bool dispatch = describe(obj, &info, true);
		++m_stats.creations;
		return LuaScriptEngine::sendObjectCreated(m_logic.random(), dispatch ? m_engine.get() : nullptr, dispatch ? &info : nullptr, bindDrawable);
	});
	m_logic.setWorldHooks(
		[this](Object &obj) {
			++m_stats.worldEntries;
			LuaObjectInfo info;
			if (describe(obj, &info, false))
			{
				m_engine->objectEnteredWorld(info);
			}
		},
		[this](Object &obj) {
			++m_stats.worldExits;
			LuaObjectInfo info;
			if (describe(obj, &info, false))
			{
				m_engine->objectLeftWorld(info);
			}
		});
	// lane HERO-2: the engine's other dispatch sites (RW 0x7379CB: SpecialAbilityUpdate's BeScary) reach the engine with the object's handler list
	m_logic.setScriptEventProc([this](int slot, Object &obj, const LuaEventArgs &args) {
		LuaObjectInfo info;
		if (slot >= 0 && slot < LUAEVENT_COUNT && describe(obj, &info, false))
		{
			++m_stats.engineEvents;
			m_engine->dispatchInternal((LuaInternalEvent)slot, info, args);
		}
	});
	// lane HERO-2: the ModelCondition events of the AI update (RW 0x663E32 -> 0x7378DA)
	m_logic.setModelConditionEventProc([this](Object &obj, const std::array<std::uint32_t, 19> &before) {
		LuaObjectInfo info;
		if (!describe(obj, &info, false))
		{
			return;
		}
		ModelConditionFlags now, snap;
		const Object::ModelConditionBits &bits = obj.getModelConditionBits();
		for (size_t w = 0; w < 19; ++w)
		{
			now.words()[w] = bits[w];
			snap.words()[w] = before[w];
		}
		++m_stats.modelConditionChanges;
		m_engine->updateModelConditionEvents(info, now, &snap, m_logic.getFrame());
	});
	m_attached = true;
	return true;
}

void LiveScripting::detach()
{
	if (m_attached)
	{
		m_logic.setObjectCreatedProc(GameLogic::ObjectCreatedProc());
		m_logic.setWorldHooks(GameLogic::WorldHook(), GameLogic::WorldHook());
		m_logic.setScriptEventProc(GameLogic::ScriptEventProc());
		m_logic.setModelConditionEventProc(GameLogic::ModelConditionEventProc());
		m_attached = false;
	}
}

bool LiveScripting::describe(Object &obj, LuaObjectInfo *out, bool count)
{
	const ThingTemplate *tt = obj.getTemplate();
	TemplateLists &t = m_templates[tt];
	if (!t.resolved)
	{
		t.resolved = true;
		const std::vector<MapCreationHook> lists = MapCreationHooks::templateEventLists(*tt);
		t.aiModules = lists.size();
		if (!lists.empty())
		{
			t.listName = lists.front().eventList;
			t.list = m_engine->findEventList(t.listName);
		}
	}
	if (!t.list)
	{
		if (count && !t.listName.empty() && AsciiStringUtil::compareNoCase(t.listName, "None") != 0)
		{
			m_stats.errors.push_back(tt->getName() + ": AILuaEventsList " + t.listName + " is not an EventList of ScriptEvents.xml");
		}
		return false;
	}
	out->id = (int)obj.getID();
	out->hasAI = true;
	out->forceLuaRegistration = false;
	out->dead = obj.isDestroyed();
	out->luaEvents = t.list;
	if (count)
	{
		if (t.aiModules > 1)
		{
			++m_stats.templatesWithSeveralLists;
		}
		++m_stats.creationListObjects;
		if (const LuaEventHandler *h = t.list->find(m_engine->events().internalKey(LUAEVENT_OnCreated)))
		{
			++m_stats.creationHookObjects;
			++m_stats.creationHandlers[h->function];
		}
	}
	return true;
}

bool LiveScripting::findObject(int id, LuaObjectInfo *out)
{
	if (id <= 0)
	{
		return false;
	}
	Object *obj = m_logic.findObjectByID((ObjectID)id);
	return obj && describe(*obj, out, false);
}

std::uint32_t LiveScripting::logicFrame()
{
	return (std::uint32_t)m_logic.getFrame();
}

bool LiveScripting::drawableShowModule(int id, const std::string &name, bool visible, bool permanent)
{
	Object *obj = m_logic.findObjectByID((ObjectID)id);
	ObjectClientHooks *c = obj ? obj->clientHooks() : nullptr; // SMOOTH-1: answered from the template, the change is a client event
	return c && c->showModule(*obj, name, visible, permanent);
}

void LiveScripting::drawableShowSubObject(int id, const std::string &name, bool visible, bool permanent)
{
	Object *obj = m_logic.findObjectByID((ObjectID)id);
	if (ObjectClientHooks *c = obj ? obj->clientHooks() : nullptr)
	{
		c->showSubObject(*obj, name, visible, permanent); // SMOOTH-1
	}
}

// ---- lane UPGRADE-1 -------------------------------------------------------------------------------------------------------------------
void LiveScripting::enterEmotion(int id, int type, int otherId)
{
	Object *obj = m_logic.findObjectByID((ObjectID)id);
	if (!obj)
	{
		return;
	}
	Object *other = otherId != 0 ? m_logic.findObjectByID((ObjectID)otherId) : nullptr;
	EmotionTrackerUpdate::requestEmotion(*obj, type, other, 1); // RW 0x736AD5: push 1 (ebp), the other, the type
}

LuaGameHost::UpgradeLookup LiveScripting::findUpgrade(const std::string &name)
{
	UpgradeLookup out;
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr; // RW 0x66F5E5
	out.found = u != nullptr;
	out.playerType = u && u->getUpgradeType() == UPGRADE_TYPE_PLAYER;
	return out;
}

bool LiveScripting::playerHasUpgradeComplete(int id, const std::string &name)
{
	Object *obj = m_logic.findObjectByID((ObjectID)id);
	const Player *p = obj ? obj->getControllingPlayer() : nullptr;
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
	return p && u && p->hasUpgradeComplete(u); // RW 0x6AC2AF
}

void LiveScripting::grantUpgrade(int id, const std::string &name, bool playerType)
{
	Object *obj = m_logic.findObjectByID((ObjectID)id);
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
	if (!obj || !u)
	{
		return;
	}
	if (playerType)
	{
		if (Player *p = obj->getControllingPlayer())
		{
			p->addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE, false); // RW 0x736E9B: addUpgrade(u, 2, 0)
		}
	}
	else
	{
		obj->giveUpgrade(u); // RW 0x736EB3 -> 0x69388B
	}
}

void LiveScripting::removeUpgrade(int id, const std::string &name, bool playerType)
{
	Object *obj = m_logic.findObjectByID((ObjectID)id);
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
	if (!obj || !u)
	{
		return;
	}
	if (playerType)
	{
		if (Player *p = obj->getControllingPlayer())
		{
			p->removeUpgrade(u, false); // RW 0x736EA6
		}
	}
	else
	{
		obj->removeUpgrade(u); // RW 0x736EBA -> 0x691438
	}
}

LiveScripting::Stats LiveScripting::stats() const
{
	Stats s = m_stats;
	s.stops.push_back("[S-147] live creation path: " + std::to_string(s.creations) + " objects went through LuaScriptEngine::sendObjectCreated (one creation draw each), " +
		std::to_string(s.creationListObjects) + " have an AILuaEventsList and were registered and sent OnCreated (" + std::to_string(s.creationHookObjects) +
		" with an OnCreated handler). Narrowed, not closed: an object's AI for the registry is its template's AILuaEventsList (no AIUpdateInterface exists yet), "
		"ForceLuaRegistration is not read from the template, the handlers' callees that need engine objects (upgrades, emotions, powers, the partition manager) are the "
		"script engine's S-124, and the other dispatch sites (OnDestroyed, OnDamaged, OnUnitEntered ...) have no call site in the live logic yet");
	if (m_engine)
	{
		for (const LuaReportSink::Entry &e : m_engine->reports().entries())
		{
			s.stops.push_back("[" + e.stop + "] " + e.text);
		}
		for (const std::string &a : m_engine->logic()->alerts())
		{
			s.errors.push_back("OnCreated handler failed: " + a);
		}
	}
	return s;
}

bool LiveScripting::playObjectSound(int id, const std::string &eventName, std::uint32_t *handle)
{
	// RW 0x736FCD: TheAudio's event info lookup (vtable + 0x12C; no info: nothing), then addAudioEvent (vtable + 0x64) of an AudioEventRTS carrying the object id
	if (!AudioApi::isValidEvent(eventName))
	{
		return false;
	}
	const Object *obj = m_logic.findObjectByID((ObjectID)id);
	const Player *owner = obj ? obj->getControllingPlayer() : nullptr;
	AudioLog::Scope logScope("lua ObjectPlaySound");
	const std::uint32_t h = AudioApi::playSoundForObject(eventName, (std::uint32_t)id, owner ? owner->getPlayerIndex() : -1);
	if (handle)
	{
		*handle = (std::uint32_t)h;
	}
	return true;
}

// ---- lane HERO-2 -----------------------------------------------------------------------------------------------------------------------
// RW 0xA39340 as the bindings call it. Each binding builds its own filter chain (read from the disassembly):
//   * ObjectBroadcastEventToEnemies RW 0x737D2D / ...ToCivilians RW 0x7380A2: the relationship filter RW 0xC11DC0 (the object's relationship to the candidate
//     in the mask, flag 0) then the visibility filter RW 0xC0F19C (RW 0x660D2C(object, 0): the controlling player's; allow RW 0x660D71: visible (RW 0x694CCC),
//     or a computer player and the candidate is firing (RW 0x68C89B)); distance type 1 (FROM_CENTER_3D), near to far;
//   * ...ToAllies RW 0x737F35: the relationship filter alone, distance type 1, near to far;
//   * ...ToUnits RW 0x738246: the visibility filter alone (RW 0x660D2C(object, 0); LUA-1 read it as a player filter), distance type 1, near to far;
//   * ObjectCountNearbyEnemies RW 0x737BF7: the relationship filter (ENEMIES), RW 0xC10E20 (alive), RW 0xC0F374 (the same Object + 0x458 bit 3: never set),
//     distance type 0 (FROM_CENTER_2D), near to far (the binding uses the count only).
// The query's centre is the object's position; RotWK's partition keeps the object itself (no filter refuses it: an enemy / ally relationship to itself is
// ALLIES, which only the allies broadcast passes).
std::vector<int> LiveScripting::objectsInRange(int id, float radius, LuaRangeOrder order, int relationship, bool notOfPlayer)
{
	std::vector<int> out;
	Object *obj = m_logic.findObjectByID((ObjectID)id);
	if (!obj)
	{
		return out;
	}
	const Player *player = obj->getControllingPlayer();
	PartitionFilterFn rel([&](Object &o) { return (relationship & (1 << (int)obj->getRelationship(o))) != 0; });
	PartitionFilterFn visible([player](Object &o) {
		bool v = EmotionModules::visibleToScans(o);
		if (!v && player && player->getPlayerType() == PLAYER_COMPUTER && InvisibilityManager::isFiring(o))
		{
			v = true;
		}
		return v;
	});
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	const Coord3D center = *obj->getPosition();
	PartitionManager &pm = m_logic.partition();
	auto collect = [&out](const PartitionHits &hits) {
		for (const PartitionHit &h : hits)
		{
			out.push_back((int)h.object->getID());
		}
	};
	if (notOfPlayer)
	{
		collect(pm.iterateObjectsInRange(center, radius, FROM_CENTER_3D, { &visible }, ITER_SORTED_NEAR_TO_FAR));
	}
	else if (order == LUA_RANGE_FASTEST)
	{
		collect(pm.iterateObjectsInRange(center, radius, FROM_CENTER_2D, { &rel, &alive }, ITER_SORTED_NEAR_TO_FAR));
	}
	else if (relationship == LUA_REL_ALLIES)
	{
		collect(pm.iterateObjectsInRange(center, radius, FROM_CENTER_3D, { &rel }, ITER_SORTED_NEAR_TO_FAR));
	}
	else
	{
		collect(pm.iterateObjectsInRange(center, radius, FROM_CENTER_3D, { &rel, &visible }, ITER_SORTED_NEAR_TO_FAR));
	}
	return out;
}
