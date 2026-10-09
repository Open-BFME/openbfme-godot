// OpenBFME. GPL-3.0.
//
// DrawableManager: the client side of object creation and destruction (ZH GameClient / TheGameClient::friend_createDrawable, lane LOGIC-1).
// It implements ObjectClientHooks: GameLogic calls objectCreated from Object::initObject (RW 0x628882 sendObjectCreated), the manager makes the
// Drawable, binds it to the object (Object::friend_bindToDrawable) and queues a "created" event for the device layer; objectDestroyed (from
// ~Object) drops the drawable and queues "destroyed". The device layer (GodotDevice/GodotGameWorld) reads the events, keeps one instance per
// model draw and, each render frame, calls advance (animations), syncTransforms (interpolated poses) and reads the draws.
//
// What a new drawable starts with: the model condition flags of GameLogicSettings (NIGHT when the map's time of day is night and
// ForceModelsToFollowTimeOfDay, SNOW likewise; ZH Object::friend_bindToDrawable), the template Scale, and the owner player's colour. The map
// object loop then applies the per object overrides (MapObjectLoop's afterCreate callback). Problems are collected, classified as
// GameClient/MapObjectRuntime does (retail data defects such as an animation no archive holds are counted, not errors), and reported.

#pragma once

#include "GameClient/ClientEvents.h"
#include "GameClient/AnimationSoundClientBehavior.h"
#include "GameClient/Drawable.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameLogic/GameLogic.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// SMOOTH-1 (S-810): the manager lives on the render side. It no longer implements ObjectClientHooks: the simulation's calls are recorded as ordered
// ClientEvents (GameClient/ClientEvents.h) and applied here in sequence, exactly once (applyEvents); the poses come from a completed frame's snapshot
// (syncTransforms). It never touches an Object.
class DrawableManager
{
public:
	DrawableManager(WW3DAssetManager &assets, GameLogic &logic); ///< the logic gives the module factory (immutable registries) only
	~DrawableManager();

	// the simulation's client events, in order: creation, destruction, model conditions, module / sub object visibility, placement
	void applyEvents(const std::vector<ClientEvent> &events);
	size_t eventsApplied() const { return m_eventsApplied; }

	Drawable *find(DrawableID id) const { return id >= 1 && id < m_slots.size() ? m_slots[id].get() : nullptr; }
	Drawable *findByObject(ObjectID id) const;
	size_t dependencyUpdates() const { return m_dependencyUpdates; } ///< lane COMBAT-4 (RW 0x4BF2D8)
	size_t liveCount() const { return m_live; }
	// ids are 1, 2, 3 ... in creation order; a destroyed id is never reused. Slot 0 is unused.
	size_t slotCount() const { return m_slots.size(); }
	// lane FX-2: the client effect player the draw modules call (null uninstalls)
	void setFXHost(DrawableFXHost *host) { m_services.fx = host; }
	// lane AUDIO-4: TheAnimationSoundModuleManager of these drawables (run at the end of syncTransforms, after the animations advanced)
	AnimationSoundModuleManager &animationSounds() { return m_animationSounds; }
	// lane PERF-3: syncTransforms runs each drawable's prepareSync on the client job pool (default); false: the loop of syncFromSnapshot in slot order, the
	// reference the determinism test compares with
	void setParallelSync(bool on) { m_parallelSync = on; }
	// below this many slots the parallel part runs as one chunk on the calling thread (default 512; the determinism test sets 0 to cut 64-slot chunks)
	void setParallelSyncMinimum(size_t slots) { m_parallelSyncMinimum = slots; }
	// lane PERF-3: fetches the blocks of the drawable `ahead` slots after `i` that a per-drawable loop reads (Common/Prefetch.h), in three steps: the object
	// at i + 12, its entry array and client modules at i + 8, the draw modules at i + 4 (each step reads only what the previous one fetched)
	void prefetchAhead(size_t i) const;

	struct Event
	{
		bool created = true;
		DrawableID id = 0;
	};
	// the created / destroyed events since the last call, in order
	std::vector<Event> takeEvents();

	// render side: animations by elapsed render milliseconds, then the interpolated poses (alpha in [0, 1))
	void advance(double elapsedMs);
	void syncTransforms(const LogicSnapshot &snapshot, double alpha, bool interpolate);

	struct Report
	{
		size_t created = 0, destroyed = 0, live = 0;
		size_t modelDraws = 0, animatedModelDraws = 0, staticModelDraws = 0, staticModels = 0, notDrawnEntries = 0;
		std::map<std::string, size_t> unportedClientModules;      ///< live ClientUpdate / ClientBehavior modules that are UnportedDrawableModules, by class (stop S-140)
		size_t modelsShown = 0;                                    ///< entries that show a model right now (model draws with a model + static models)
		std::map<std::string, size_t> byDrawClass;                 ///< entries per draw class
		std::map<std::string, size_t> distinctModels;              ///< lower-case model name -> entries (the first model of each entry)
		std::vector<std::string> errors;
		std::map<std::string, size_t> dataDefects, hideMisses;
		std::vector<std::string> stops;
		size_t scriptsRun = 0;
	};
	Report report() const;
	// MapObjectLoop's afterCreate: the per object overrides of the map (initial model condition flags incl. objectTime / objectWeather, the
	// prototype scale of a directly placed object (not of a horde's member: it keeps its template's), the mirror flag)
	static void applyPlacement(Object &obj, const MapObjectDrawable &placement);
	// the S-151 line and the stops of the draw runtime that were hit
	static std::string interpolationStopLine();
	// lane PROJ-2: the S-1001 line (GameClient/DrawableFade)
	static std::string fadeStopLine();
	// lane ANIM-1: the S-1583 line (the drawable's model condition flush points this port does not reproduce)
	static std::string flushStopLine();
	// lane BUILD-4: the S-1520 line (a wall span's fade-in and the draw scripts' module visibility)
	static std::string wallFadeStopLine();

private:
	// What the draw runtimes of drawables said: classified (classifyDrawMessage), counted, never dropped. The runtime of a live drawable keeps
	// collecting (the OnCreated hooks hide sub objects after the drawable exists), so a drawable's part is read when it dies and at report().
	struct Sink
	{
		std::vector<std::string> errors;
		std::map<std::string, size_t> dataDefects, hideMisses;
		std::set<std::string> stopKeys;
		std::vector<std::string> stops;
		void message(const std::string &object, const std::string &className, const std::string &text);
	};
	static void collect(const Drawable &d, Sink &sink);
	static W3DLuaDrawScriptHost::Options hostOptions(const std::uint32_t &frame);
	void created(const ClientEvent &ev);
	void destroyed(const ClientEvent &ev);

	std::uint32_t m_scriptFrame = 0; ///< the frame the drawable Lua state reports (before m_services: its options point here)
	bool m_parallelSync = true;
	size_t m_parallelSyncMinimum = 512;
	std::vector<Drawable::SyncPrep> m_syncScratch; ///< lane PERF-3: syncTransforms' per-slot results of the parallel part (kept: no allocation per frame)
	AnimationSoundModuleManager m_animationSounds; ///< lane AUDIO-4 (before the drawables: their modules leave its lists when they go)
	DrawServices m_services;
	std::map<ObjectID, DrawableID> m_byObject;
	size_t m_dependencyUpdates = 0; ///< lane COMBAT-4: dependent drawables whose shared flags changed (RW 0x4BF2D8)
	std::uint32_t m_nextSeq = 0;
	size_t m_eventsApplied = 0;
	std::vector<std::unique_ptr<Drawable>> m_slots; ///< index = drawable id
	size_t m_live = 0;
	size_t m_created = 0, m_destroyed = 0;
	std::vector<Event> m_events;
	Sink m_sink; ///< the drawables that are gone, and the problems of the constructors
	std::map<std::string, bool> m_modelExists; ///< lower-case model name -> registered in the archives
};
