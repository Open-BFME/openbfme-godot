// OpenBFME. GPL-3.0.
// See GameClient/MapObjectRuntime.h.

#include "GameClient/MapObjectRuntime.h"

#include "Common/AsciiString.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"

#include <chrono>
#include <set>

// The game host of the creation hooks: the objects of the map are its object table (id = drawable index + 1), and a hide / show of a
// sub object or a draw module goes to the draw runtimes of that drawable. Everything else a handler could ask is the reported stop S-124.
class MapObjectRuntime::LuaMapHost : public LuaGameHost
{
public:
	std::vector<const LuaEventList *> lists;            ///< per drawable index: the AILuaEventsList of its template (null: not an object with handlers)
	std::vector<std::vector<size_t>> modelsOfDrawable;  ///< per drawable index: indices in MapObjectRuntime::m_models
	std::vector<MapPlacedModel> *models = nullptr;
	const MapObjectDrawables *drawables = nullptr;

	bool findObject(int id, LuaObjectInfo *out) override
	{
		if (id <= 0 || (size_t)id > lists.size() || !lists[(size_t)id - 1])
		{
			return false;
		}
		out->id = id;
		out->hasAI = true;
		out->forceLuaRegistration = false;
		out->dead = false;
		out->luaEvents = lists[(size_t)id - 1];
		return true;
	}
	// RW 0x6789B4: the draw module whose tag NameKey equals the name (exact, case sensitive). A matched module takes the request (the module-first
	// rule: no fall through to a sub object of the same name); its visibility is applied to the placed model of that module (MapPlacedModel::moduleHidden:
	// the device layer does not instance a hidden module's model). A matched module that shows no model here (a static / client draw, or a module whose
	// state shows none) has nothing to hide: counted in moduleRequestsWithoutModel and reported (S-124).
	bool drawableShowModule(int id, const std::string &name, bool visible, bool permanent) override
	{
		const MapObjectDrawable &d = drawables->drawables[(size_t)id - 1];
		for (size_t mi = 0; mi < d.draws.size(); ++mi)
		{
			if (d.draws[mi].tag != name)
			{
				continue;
			}
			bool applied = false;
			for (size_t pi : modelsOfDrawable[(size_t)id - 1])
			{
				MapPlacedModel &p = (*models)[pi];
				if (p.module == mi)
				{
					p.moduleHidden = !visible;
					applied = true;
				}
			}
			++moduleRequests;
			if (!applied)
			{
				++moduleRequestsWithoutModel;
				unimplemented("Drawable::showModule (RW 0x6789B4): the matched draw module shows no model in this runtime, so its visibility has no placed model to change");
			}
			(void)permanent; // RW 0x736412 passes permanent = 1; this runtime never changes a drawable's state after map start, so permanent and temporary hides coincide
			return true;
		}
		return false;
	}
	// RW 0x672823: every draw module of the drawable that shows a model
	void drawableShowSubObject(int id, const std::string &name, bool visible, bool permanent) override
	{
		for (size_t mi : modelsOfDrawable[(size_t)id - 1])
		{
			MapPlacedModel &p = (*models)[mi];
			if (!p.draw)
			{
				continue;
			}
			if (permanent)
			{
				visible ? p.draw->showSubObjectPermanently(name) : p.draw->hideSubObjectPermanently(name);
			}
			else
			{
				visible ? p.draw->showSubObject(name) : p.draw->hideSubObject(name);
			}
		}
	}
	size_t moduleRequests = 0, moduleRequestsWithoutModel = 0;
};

MapObjectRuntime::MapObjectRuntime(WW3DAssetManager &assets)
	: m_assets(assets)
	, m_drawAssets(assets)
	, m_random(RandomAlgorithm::ZH_CarryChain)
	, m_logicRandom(RandomAlgorithm::ZH_CarryChain)
{
	m_random.seed(1); // a fixed client seed: the idle picks of a map are reproducible (the retail initial client seed is S-093)
	m_keys.init();
}

MapObjectRuntime::~MapObjectRuntime() = default;

DrawMessageClass classifyDrawMessage(const std::string &message)
{
	DrawMessageClass c;
	const std::string hideKey = "sub object ";
	const size_t notFound = message.find(" not found in model ");
	if (message.compare(0, hideKey.size(), hideKey) == 0 && notFound != std::string::npos)
	{
		c.kind = DrawMessageClass::HIDE_MISS;
		c.subObject = AsciiStringUtil::lowered(message.substr(hideKey.size(), notFound - hideKey.size()));
		return c;
	}
	// lane FX-3: a script's transition to a state the module does not define (retail logs this line, RW 0x4BE7F5 .. 0x4BE843, and continues)
	static const std::string noTransition = "W3DScriptedModelDraw::adjustAnimation: Unable to find transition state named ";
	if ((message.find("animation ") != std::string::npos && message.find(" is not registered") != std::string::npos) ||
		(message.compare(0, 6, "model ") == 0 && message.find(" is not available") != std::string::npos) || message.compare(0, noTransition.size(), noTransition) == 0)
	{
		c.kind = DrawMessageClass::DATA_DEFECT;
	}
	return c;
}

void MapObjectRuntime::classifyRuntimeError(const std::string &object, const std::string &className, const std::string &message)
{
	const DrawMessageClass c = classifyDrawMessage(message);
	switch (c.kind)
	{
	case DrawMessageClass::HIDE_MISS: ++m_report.hideMisses[c.subObject]; break;
	case DrawMessageClass::DATA_DEFECT: ++m_report.dataDefects[className + ": " + message]; break;
	default: m_report.errors.push_back(object + " " + className + ": " + message); break;
	}
}

// Creates the draw runtimes of one drawable's draw modules (the Drawable's constructor, RW 0x679FD7: its modules in template order).
void MapObjectRuntime::createDrawModules(const MapObjectDrawables &drawables, size_t di, bool createDrawRuntime)
{
	const MapObjectDrawable &d = drawables.drawables[di];
	m_host.setChunkName(d.info ? d.info->name : std::string());
	for (size_t mi = 0; mi < d.draws.size(); ++mi)
	{
		const MapDrawModule &m = d.draws[mi];
		if (m.model.empty())
		{
			continue;
		}
		MapPlacedModel p;
		p.drawable = di;
		p.module = mi;
		p.kind = m.kind;
		p.className = m.className;
		p.modelName = m.model;
		++m_report.placed;
		switch (m.kind)
		{
		case W3D_DRAWKIND_TREE:
			++m_report.treeDraws;
			if (const W3DTreeDrawModuleData *t = dynamic_cast<const W3DTreeDrawModuleData *>(m.data))
			{
				p.textureName = t->m_textureName;
			}
			break;
		case W3D_DRAWKIND_PROP:
		case W3D_DRAWKIND_FLOOR:
		{
			if (m.kind == W3D_DRAWKIND_PROP)
			{
				++m_report.propDraws;
			}
			else
			{
				++m_report.floorDraws;
			}
			// S-115: the draw fields of the static draws that change how the model is drawn and are stored, not applied
			if (const W3DPropDrawModuleData *pd = dynamic_cast<const W3DPropDrawModuleData *>(m.data))
			{
				if (!pd->m_distanceFog)
				{
					++m_report.ignoredDrawFields[m.className + " DistanceFog = No"];
				}
			}
			if (const W3DFloorDrawModuleData *fd = dynamic_cast<const W3DFloorDrawModuleData *>(m.data))
			{
				if (!fd->m_weatherTextures.empty())
				{
					++m_report.ignoredDrawFields[m.className + " WeatherTexture"];
				}
				if (fd->m_forceToBack)
				{
					++m_report.ignoredDrawFields[m.className + " ForceToBack"];
				}
				if (fd->m_staticModelLODMode)
				{
					++m_report.ignoredDrawFields[m.className + " StaticModelLODMode"];
				}
			}
			break;
		}
		case W3D_DRAWKIND_MODEL:
		{
			++m_report.modelDraws;
			if (!createDrawRuntime)
			{
				break;
			}
			const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(m.data);
			try
			{
				W3DScriptedModelDraw::Options o;
				o.scale = d.scale;
				o.buildBones = false; // turrets, barrels and particle bones are not drawn (S-095 / S-097)
				if (const W3DHordeModelDrawModuleData *hd = dynamic_cast<const W3DHordeModelDrawModuleData *>(m.data))
				{
					p.draw.reset(new W3DHordeModelDraw(*hd, 2, m_drawAssets, m_random, &m_host, o)); // LOD HIGH
				}
				else
				{
					p.draw.reset(new W3DScriptedModelDraw(*data, m_drawAssets, m_random, &m_host, o));
				}
				if (d.flags.any())
				{
					p.draw->setModelConditionFlags(d.flags);
				}
				const W3DDrawFrame f = p.draw->frame();
				if (AsciiStringUtil::compareNoCase(f.modelName, m.model) != 0)
				{
					m_report.errors.push_back(d.info->name + " " + m.className + ": the draw runtime chose model '" + f.modelName + "' where the state match gave '" + m.model + "'");
				}
				if (f.trackCount > 0 && f.tracks[0].anim && f.tracks[0].anim->Get_Num_Frames() > 1 && f.tracks[0].mode != W3D_ANIM_MODE_MANUAL)
				{
					p.animated = true;
				}
			}
			catch (const std::exception &e)
			{
				m_report.errors.push_back(d.info->name + " " + m.className + ": draw runtime failed: " + e.what());
				p.draw.reset();
			}
			if (p.animated)
			{
				++m_report.animated;
			}
			else
			{
				++m_report.staticModels;
			}
			break;
		}
		default: break;
		}
		if (p.kind != W3D_DRAWKIND_MODEL)
		{
			std::string err;
			if (!m_assets.Create_Render_Obj(m.model, &err))
			{
				p.missingModel = true;
				++m_report.dataDefects[m.className + ": model " + m.model + " is not available: " + err];
			}
		}
		else if (p.draw && p.draw->frame().model == nullptr)
		{
			p.missingModel = true;
		}
		++m_report.distinctModels[AsciiStringUtil::lowered(m.model)];
		if (m_luaHost)
		{
			m_luaHost->modelsOfDrawable[di].push_back(m_models.size());
		}
		m_models.push_back(std::move(p));
	}
}

void MapObjectRuntime::build(const MapObjectDrawables &drawables, bool createDrawRuntime)
{
	const auto t0 = std::chrono::steady_clock::now();
	std::set<std::string> stopKeys;
	m_report.creationScriptsGiven = m_scripts && m_scripts->rawLoaded; // the raw files; the report-only inventory scan does not gate the real engine
	m_lua.reset();
	m_luaHost.reset();
	if (createDrawRuntime && m_report.creationScriptsGiven)
	{
		m_luaHost.reset(new LuaMapHost());
		m_luaHost->lists.assign(drawables.drawables.size(), nullptr);
		m_luaHost->modelsOfDrawable.assign(drawables.drawables.size(), std::vector<size_t>());
		m_luaHost->models = &m_models;
		m_luaHost->drawables = &drawables;
		LuaScriptEngine::Config c;
		c.keys = &m_keys;
		c.logicRandom = m_logicRandomPtr;
		c.host = m_luaHost.get();
		m_lua.reset(new LuaScriptEngine(c));
		m_lua->loadLogicScripts(m_scripts->luaText, m_scripts->xmlText);
	}
	bool sawNormalFullObject = false;
	for (size_t di = 0; di < drawables.drawables.size(); ++di)
	{
		const MapObjectDrawable &d = drawables.drawables[di];
		const bool full = (d.fate == MAPOBJ_OBJECT || d.fate == MAPOBJ_OBJECT_BRIDGE) && d.info;
		if (!full)
		{
			createDrawModules(drawables, di, createDrawRuntime);
			continue;
		}
		// a full object, a bridge or a horde member: one creation function for all of them (RW 0x628882, LuaScriptEngine::sendObjectCreated)
		// retail's bridge / wall pass creates its objects before the main loop; here the drawable-list order decides. A bridge pass object that follows a
		// normal one therefore gets its creation draw (and OnCreated) at another place of the logic random sequence: counted and reported (S-110)
		if (d.info->isBridge || d.info->walkOnTopOfWall)
		{
			++m_report.creationBridgePassObjects;
			if (sawNormalFullObject)
			{
				++m_report.creationOrderDeviations;
			}
		}
		else
		{
			sawNormalFullObject = true;
		}
		const LuaEventList *list = nullptr;
		LuaObjectInfo objInfo;
		bool dispatch = false;
		if (m_lua && !d.info->aiEventLists.empty())
		{
			if (d.info->aiEventLists.size() > 1)
			{
				++m_report.creationHookTemplatesWithSeveralLists;
			}
			const std::string &listName = d.info->aiEventLists.front().eventList;
			list = m_lua->findEventList(listName);
			if (list)
			{
				m_luaHost->lists[di] = list;
				m_luaHost->findObject((int)di + 1, &objInfo);
				dispatch = true;
				++m_report.creationListObjects;
				if (const LuaEventHandler *h = list->find(m_lua->events().internalKey(LUAEVENT_OnCreated)))
				{
					++m_report.creationHookObjects;
					++m_report.creationHandlers[h->function];
				}
			}
			else if (AsciiStringUtil::compareNoCase(listName, "None") != 0)
			{
				m_report.errors.push_back(d.info->name + ": AILuaEventsList " + listName + " is not an EventList of scriptevents.xml");
			}
		}
		LuaScriptEngine::sendObjectCreated(*m_logicRandomPtr, dispatch ? m_lua.get() : nullptr, dispatch ? &objInfo : nullptr,
			[&]() { createDrawModules(drawables, di, createDrawRuntime); });
		++m_report.creationDraws;
	}
	// the draw runtimes' errors and stops, after the hooks ran (their hides can miss a sub object like the INI's own hides)
	for (const MapPlacedModel &p : m_models)
	{
		if (!p.draw)
		{
			continue;
		}
		const MapObjectDrawable &d = drawables.drawables[p.drawable];
		for (const std::string &e : p.draw->errors())
		{
			classifyRuntimeError(d.info->name, p.className, e);
		}
		for (const W3DStopHit &sh : p.draw->stops())
		{
			if (stopKeys.insert(sh.Id).second)
			{
				m_report.stops.push_back(sh.Message);
			}
		}
	}
	if (m_luaHost)
	{
		m_report.moduleRequests = m_luaHost->moduleRequests;
		m_report.moduleRequestsWithoutModel = m_luaHost->moduleRequestsWithoutModel;
	}
	for (const MapPlacedModel &p : m_models)
	{
		m_report.modulesHidden += p.moduleHidden ? 1 : 0;
	}
	if (m_report.creationDraws > 0)
	{
		// the creation draw is ported (RW 0x628892); which generator and seed the retail game has at map start is the stop S-080
		for (const std::string &u : m_logicRandomPtr->unverified())
		{
			if (stopKeys.insert(u).second)
			{
				m_report.stops.push_back(u);
			}
		}
	}
	if (m_report.creationOrderDeviations > 0)
	{
		m_report.stops.push_back("[S-110] " + std::to_string(m_report.creationOrderDeviations) + " bridge / wall pass object(s) (IsBridge or KindOf WALK_ON_TOP_OF_WALL) follow a normal object in the drawable list: "
			"their creation draw GetGameLogicRandomValue(1, 999) (RW 0x628892) and OnCreated run in drawable-list order, retail creates them in the earlier bridge / wall pass, so every later "
			"logic draw of this map can differ from retail");
	}
	if (m_scripts && m_scripts->rawLoaded && !m_scripts->loaded)
	{
		m_report.stops.push_back("[S-110] the report-only inventory scan of the creation scripts failed (" + m_scripts->inventoryError + "); the real Lua engine ran the files regardless");
	}
	m_report.scriptsRun = m_host.scriptsRun();
	for (const std::string &f : m_host.failures())
	{
		m_report.errors.push_back("BeginScript failed: " + f);
	}
	auto addReports = [&](const LuaReportSink &sink) {
		for (const LuaReportSink::Entry &e : sink.entries())
		{
			const std::string line = "[" + e.stop + "] " + e.text;
			if (stopKeys.insert(line).second)
			{
				m_report.stops.push_back(line);
			}
		}
	};
	if (m_host.scriptsRun() > 0 || m_lua)
	{
		addReports(m_host.reports());
	}
	if (m_lua)
	{
		addReports(m_lua->reports());
		for (const std::string &a : m_lua->logic()->alerts())
		{
			m_report.errors.push_back("OnCreated handler failed: " + a);
		}
		if (m_report.creationHookTemplatesWithSeveralLists > 0)
		{
			m_report.stops.push_back("[S-110] " + std::to_string(m_report.creationHookTemplatesWithSeveralLists) + " objects have an AILuaEventsList in more than one AI module; the first list is used (an object has one AI, RW Object+0x260)");
		}
	}
	else if (createDrawRuntime)
	{
		m_report.stops.push_back("[S-110] the OnCreated creation hooks were not run: no scriptevents.xml / scripts.lua were given to the runtime (setCreationScripts)");
	}
	m_report.buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
