// OpenBFME. GPL-3.0.
// See GodotDevice/GodotGameWorld.h.
//
// Client-side model prewarm and the S-029 fade warm-up. This translation unit is
// excluded from the simulation audit (tools/sim/sim_policy.json): it builds W3D
// models/textures/materials and keeps them in the draw cache, reads the logic's
// templates and objects, and never writes simulation state. It uses wall-clock
// timing for load reports and wave pacing, which is presentation-only.

#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotPrewarmPolicy.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotTiming.h"
#include "GodotDevice/GodotW3DInstancer.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/MapUtil.h"
#include "GameClient/PlacementGhost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameLogic/Object/Object.h"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace godot
{

namespace
{
String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

double nowMs()
{
	return timing::nowMs();
}

W3DInstancer *instancerOf(uint64_t id)
{
	if (id == 0 || !UtilityFunctions::is_instance_id_valid(id))
	{
		return nullptr;
	}
	return Object::cast_to<W3DInstancer>(UtilityFunctions::instance_from_id(id));
}
} // namespace

// S-029 (1): no pivot-fade dither for the first two seconds of a game (the black dither pixels are conspicuous on snowy maps).
void GameWorld::begin_fade_warmup()
{
	W3D_Set_Fade_Warmup(2000); // the instancer reads Godot's engine clock for both ends of the warm-up
}

void GameWorld::prewarm_match_models(W3DInstancer &dyn, Dictionary &timings)
{
	// Lane PERF-4 (owner report 2026-11-10: the first Uruk / the first build flag stutters). The models are built exactly as the live
	// drawables ask for them (the same instancer, house colours already enabled) and kept under their plain, lower-cased key in
	// `m_dynamicModels` with no instance: nothing draws, and the first drawable that needs one reuses it instead of paying the W3D,
	// texture and material build at the moment the unit appears or the building is placed.
	const double t0 = nowMs();
	timings["model_preload_ms"] = 0.0;
	if (std::getenv("OPENBFME_NO_MODEL_PRELOAD") != nullptr)
	{
		return; // A/B and profile runs can keep the old first-use cost
	}
	const DisplayServer *display = DisplayServer::get_singleton();
	if (display && display->get_name() == String("headless"))
	{
		return; // a headless game draws nothing: building its models would only slow the run down
	}
	if (!m_game || !m_world)
	{
		return;
	}
	const auto worldContext = m_world->enterContext(); // commands() / the templates read this world's stores
	std::set<std::string> seenTemplates;
	std::set<std::string> models; // lower-cased W3D names, the m_dynamicModels key
	std::set<std::string> remainingSeen; // the other model-condition states, for the time waves
	std::vector<PrewarmJob> remaining;   // in encounter order (the local player's graph first)
	std::vector<std::pair<const ThingTemplate *, bool>> pending; // template, essential (a live object's states load now)
	auto addModel = [&](const std::string &name) {
		if (!name.empty() && AsciiStringUtil::compareNoCase(name, "None") != 0)
		{
			models.insert(AsciiStringUtil::lowered(name));
		}
	};
	auto loadsEarly = [](const ModelConditionInfo &state) {
		// A building's first rubble/death look and its construction look are paid at the moment they first appear; a
		// time wave may not have reached them. Load them with the main pass (owner report 2026-11-11: the first
		// destruction of a building type stuttered).
		static const int rubble = ModelCondition::indexOf("RUBBLE");
		static const int die = ModelCondition::indexOf("DIE");
		static const int death = ModelCondition::indexOf("DEATH");
		static const int postCollapse = ModelCondition::indexOf("POST_COLLAPSE");
		static const int constructing = ModelCondition::indexOf("ACTIVELY_BEING_CONSTRUCTED");
		return (rubble >= 0 && state.conditions.test(rubble)) || (die >= 0 && state.conditions.test(die)) || (death >= 0 && state.conditions.test(death))
			|| (postCollapse >= 0 && state.conditions.test(postCollapse)) || (constructing >= 0 && state.conditions.test(constructing));
	};
	auto addRemaining = [&](const std::string &name, const std::vector<TextureSwap> &swaps) {
		if (!name.empty() && AsciiStringUtil::compareNoCase(name, "None") != 0)
		{
			const std::string model = AsciiStringUtil::lowered(name);
			const std::string key = model + W3DInstancer::Texture_Swap_Suffix(swaps);
			if (remainingSeen.insert(key).second)
			{
				remaining.push_back(PrewarmJob{ model, swaps });
			}
		}
	};
	std::function<void(const ThingTemplate *, bool)> addTemplate = [&](const ThingTemplate *tt, bool essential) {
		if (!tt || !seenTemplates.insert(tt->getName()).second)
		{
			return;
		}
		addModel(toNative(get_template_model(toGodot(tt->getName()))));
		// every other model-condition state of the same template (upgrades, rubble, damage, ...), queued for a later wave
		for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->drawModules().nuggets())
		{
			if (const W3DModelDrawModuleData *draw = dynamic_cast<const W3DModelDrawModuleData *>(n.data.get()))
			{
				for (const ModelConditionInfo &state : draw->m_conditionStates)
				{
					for (const std::string &name : state.modelNames)
					{
						if (essential || loadsEarly(state))
						{
							addModel(name); // a live object's states, or a building's destruction / construction look: load them now
						}
						else
						{
							addRemaining(name, {}); // a production / upgrade state: a later wave
						}
						if (!state.textures.empty())
						{
							addRemaining(name, state.textures); // its `Texture = A B` variant (an upgrade / snow look)
						}
					}
				}
			}
		}
		// a horde's default model is only its marker; the visible soldiers are its HordeContain payload members
		for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->behaviorModules().nuggets())
		{
			if (const HordeContainModuleData *horde = dynamic_cast<const HordeContainModuleData *>(n.data.get()))
			{
				for (const InitialPayloadEntry &entry : horde->m_transport.m_initialPayload)
				{
					if (const ThingTemplate *member = m_world->things().findTemplate(entry.name))
					{
						pending.push_back({ member, essential });
					}
				}
				// owner report 2026-11-13: the banner carrier is spawned at runtime (BannerCarriersAllowed), so its model was built on the first
				// appearance and the carrier took seconds to show. Preload the banner template with its horde.
				for (const std::string &bannerName : horde->m_bannerCarriersAllowed)
				{
					if (const ThingTemplate *banner = m_world->things().findTemplate(bannerName))
					{
						pending.push_back({ banner, essential });
					}
				}
			}
		}
		// the placement cursor (the flag the owner sees over the building site): the BUILD_PLACEMENT_CURSOR model condition
		addModel(PlacementGhost::lookOf(*tt).model);
		std::string commandSet;
		if (const FieldValue *v = tt->findField("CommandSet"))
		{
			if (const std::string *name = std::get_if<std::string>(v))
			{
				commandSet = *name;
			}
		}
		if (!commandSet.empty())
		{
			if (const CommandSet *set = m_world->commands().findCommandSet(commandSet))
			{
				for (int i = 0; i < CommandSet::slotCapacity(); ++i)
				{
					if (const CommandButton *button = set->getCommandButton(i))
					{
						if (const ThingTemplate *built = button->getThingTemplate())
						{
							pending.push_back({ built, false });
						}
					}
				}
			}
		}
	};
	// The match's live objects seed the graph: every player's starting fortress / units (local and enemies) and the map's
	// neutral creatures and lairs. Their CommandSets then give every buildable unit / building of those factions; the model
	// set is deduplicated, so factions that share troops pay for them once. The local player's graph goes first, so its
	// remaining states are the first waves.
	const ::Player *localPlayer = m_game->players().getLocalPlayer();
	for (const ::Object *o = m_game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (!localPlayer || o->getControllingPlayer() != localPlayer)
		{
			pending.push_back({ o->getTemplate(), true });
		}
	}
	for (int i = 0; i < m_game->players().getPlayerCount(); ++i)
	{
		const ::Player *player = m_game->players().getNthPlayer(i);
		if (!player || player->isObserver())
		{
			continue;
		}
		if (const PlayerTemplate *playerTemplate = player->getPlayerTemplate())
		{
			for (const std::string &name : playerTemplate->m_buildableHeroesMP)
			{
				if (const ThingTemplate *tt = m_world->things().findTemplate(name))
				{
					pending.push_back({ tt, false });
				}
			}
		}
	}
	// The map's own objects and build lists: the neutral creatures, lairs and camp flags are not live objects at load time, so
	// a map script that reveals one would otherwise build its model (the CAPFLAG_SKN family has every faction's flag) during play.
	const LoadedMap &map = m_game->map();
	for (const MapObject &object : map.chunks.objects)
	{
		if (const ThingTemplate *tt = m_world->things().findTemplate(object.m_objectName))
		{
			pending.push_back({ tt, false });
		}
	}
	for (const SidesInfo &side : map.sides.sides)
	{
		for (const BuildListInfo &entry : side.buildList)
		{
			if (const ThingTemplate *tt = m_world->things().findTemplate(entry.templateName))
			{
				pending.push_back({ tt, false });
			}
		}
	}
	for (const FactionBuildList &buildList : map.sides.factionBuildLists)
	{
		for (const BuildListInfo &entry : buildList.entries)
		{
			if (const ThingTemplate *tt = m_world->things().findTemplate(entry.templateName))
			{
				pending.push_back({ tt, false });
			}
		}
	}
	// Pushed last, so the local player's graph is visited first by the LIFO walk: its remaining states lead the waves.
	if (localPlayer)
	{
		for (const ::Object *o = m_game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == localPlayer)
			{
				pending.push_back({ o->getTemplate(), true });
			}
		}
	}
	while (!pending.empty())
	{
		const std::pair<const ThingTemplate *, bool> entry = pending.back();
		pending.pop_back();
		addTemplate(entry.first, entry.second);
	}
	CameraSettings camera;
	std::string cameraError;
	if (m_fs.is_valid() && m_fs->archive_fs() && CameraSettings::load(*m_fs->archive_fs(), camera, &cameraError))
	{
		addModel(camera.moveHintName); // the in-game UI's move hint (retail SCMoveHint)
	}
	for (const std::string &model : models)
	{
		if (m_dynamicModels.count(model))
		{
			continue;
		}
		const int64_t id = dyn.add_model(toGodot(model));
		if (id < 0)
		{
			continue; // the instancer's report names why; a missing optional model must not fail the load
		}
		m_dynamicModels[model] = id;
	}
	// The rest of the present factions' / map's model-condition states go to the time waves, one spare-time model each.
	m_prewarmQueue.clear();
	m_prewarmCursor = 0;
	m_prewarmBuilt = m_prewarmFailed = 0;
	// a few minutes of waves; OPENBFME_PREWARM_WAVES_MAX tunes it (0: every remaining model)
	const size_t waveLimit = prewarm::waveLimit(std::getenv("OPENBFME_PREWARM_WAVES_MAX"), remaining.size());
	for (const PrewarmJob &job : remaining)
	{
		if (m_prewarmQueue.size() >= waveLimit)
		{
			break;
		}
		const std::string key = job.model + W3DInstancer::Texture_Swap_Suffix(job.swaps);
		if (!m_dynamicModels.count(key))
		{
			m_prewarmQueue.push_back(job);
		}
	}
	m_prewarmNextMs = nowMs() + 2500.0; // let the first game frames (shaders, terrain, HUD) settle
	if (!m_prewarmQueue.empty())
	{
		UtilityFunctions::print("GAME model preload waves: ", (int64_t)m_prewarmQueue.size(), " models queued after the load (", (int64_t)remaining.size(),
			" states found)");
	}
	timings["model_preload_ms"] = timing::elapsedMs(t0);
}

void GameWorld::prewarm_wave_tick()
{
	if (m_prewarmCursor >= m_prewarmQueue.size() || !m_game || m_dynamicId == 0)
	{
		return;
	}
	if (std::getenv("OPENBFME_NO_MODEL_PRELOAD") != nullptr)
	{
		return;
	}
	const DisplayServer *display = DisplayServer::get_singleton();
	if (display && display->get_name() == String("headless"))
	{
		return;
	}
	const double now = nowMs();
	if (now < m_prewarmNextMs)
	{
		return;
	}
	// One wave per spell of spare main-thread time: a busy frame (a battle, the HUD, a load transition) defers the wave.
	if (!m_paused && m_lastMainCpuMs > 8.0)
	{
		m_prewarmNextMs = now + 500.0;
		return;
	}
	W3DInstancer *dyn = instancerOf(m_dynamicId);
	if (!dyn)
	{
		return;
	}
	const PrewarmJob job = m_prewarmQueue[m_prewarmCursor++];
	const std::string key = job.model + W3DInstancer::Texture_Swap_Suffix(job.swaps);
	if (m_dynamicModels.count(key))
	{
		m_prewarmNextMs = now + 100.0;
		return;
	}
	if (!job.swaps.empty())
	{
		dyn->Set_Build_Texture_Swaps(job.swaps); // taken by the add below (stop S-5015)
	}
	const double t0 = nowMs();
	const int64_t id = dyn->add_model(toGodot(job.model));
	const double builtMs = timing::elapsedMs(t0);
	if (id >= 0)
	{
		m_dynamicModels[key] = id;
		++m_prewarmBuilt;
	}
	else
	{
		++m_prewarmFailed;
	}
	// A 10 ms model waits 2 s; a 100 ms one waits 5.5 s. Paused frames can afford a shorter gap (no game frame is disturbed).
	const double minDelay = m_paused ? 250.0 : 2000.0;
	m_prewarmNextMs = now + std::min(15000.0, std::max(minDelay, 500.0 + builtMs * 50.0));
	UtilityFunctions::print("GAME model preload wave: ", toGodot(job.model), " (", builtMs, " ms), ", m_prewarmBuilt, " built, ",
		(int64_t)(m_prewarmQueue.size() - m_prewarmCursor), " left");
	if (m_prewarmCursor >= m_prewarmQueue.size())
	{
		UtilityFunctions::print("GAME model preload waves complete: ", m_prewarmBuilt, " built, ", m_prewarmFailed, " failed");
	}
}

} // namespace godot
