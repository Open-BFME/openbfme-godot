// OpenBFME. GPL-3.0.
// Lane SCRIPT-1: GameWorld's view of the map script engine (see GodotDevice/GodotGameWorld.h): the client requests (camera, letterbox, captions,
// fades), the script camera director that applies them on the render clock, and the engine's report. Nothing here writes logic state.

#include "GodotDevice/GodotGameWorld.h"

#include "Common/ArchiveFileSystem.h"
#include "GameClient/GameText.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LiveGameAudio.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameClient/MapChunks.h"
#include "GameClient/ScriptCameraDirector.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "Common/Team.h"
#include "GodotDevice/GodotRetailFileSystem.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace
{
String g(const std::string &s)
{
	return String::utf8(s.c_str());
}

// the game text the captions show: data/lotr.str (the table the shell reads), loaded once per process
const GameTextTable *gameText(ArchiveFileSystem *fs)
{
	static GameTextTable table;
	static bool tried = false;
	if (!tried && fs)
	{
		tried = true;
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (fs->readFile("data/lotr.str", bytes, &error))
		{
			table.parse(bytes, &error);
		}
	}
	return tried ? &table : nullptr;
}
} // namespace

void GameWorld::bindScriptMethods()
{
	ClassDB::bind_method(D_METHOD("take_script_requests"), &GameWorld::take_script_requests);
	ClassDB::bind_method(D_METHOD("update_script_view", "delta_ms", "camera"), &GameWorld::update_script_view);
	ClassDB::bind_method(D_METHOD("get_script_report"), &GameWorld::get_script_report);
	ClassDB::bind_method(D_METHOD("get_waypoint", "name"), &GameWorld::get_waypoint);
	ClassDB::bind_method(D_METHOD("debug_script_place", "name", "where"), &GameWorld::debug_script_place);
	ClassDB::bind_method(D_METHOD("debug_script_kill", "name"), &GameWorld::debug_script_kill);
	ClassDB::bind_method(D_METHOD("debug_script_unit", "name"), &GameWorld::debug_script_unit);
	ClassDB::bind_method(D_METHOD("debug_script_team", "name"), &GameWorld::debug_script_team);
	ClassDB::bind_method(D_METHOD("debug_script_place_at", "name", "x", "y"), &GameWorld::debug_script_place_at);
	ClassDB::bind_method(D_METHOD("debug_script_kill_team", "name"), &GameWorld::debug_script_kill_team);
}

Dictionary GameWorld::get_waypoint(const String &name) const
{
	Dictionary d;
	d["ok"] = false;
	if (!m_game)
	{
		return d;
	}
	const TerrainLogic *terrain = m_game->logic().terrain();
	const Waypoint *w = terrain ? terrain->findWaypointByName(name.utf8().get_data()) : nullptr;
	if (w)
	{
		d["ok"] = true;
		d["x"] = w->location.x;
		d["y"] = w->location.y;
		d["z"] = w->location.z;
	}
	return d;
}

Array GameWorld::take_script_requests()
{
	Array out;
	if (!m_game)
	{
		return out;
	}
	if (!m_scriptView)
	{
		m_scriptView = std::make_unique<ScriptCameraDirector>();
	}
	for (const ScriptClientRequest &r : m_game->takeScriptRequests())
	{
		Dictionary d;
		d["frame"] = (int64_t)r.frame;
		d["action"] = g(r.action);
		d["player"] = (int64_t)r.playerIndex;
		d["script"] = g(r.script);
		Array params;
		for (const ScriptParameter &p : r.params)
		{
			Dictionary pd;
			pd["type"] = (int64_t)p.type;
			pd["int"] = (int64_t)p.intValue;
			pd["real"] = (double)p.realValue;
			pd["string"] = g(p.stringValue);
			params.push_back(pd);
		}
		d["params"] = params;
		d["has_position"] = r.hasPosition;
		d["x"] = r.position.x;
		d["y"] = r.position.y;
		d["z"] = r.position.z;
		d["object"] = (int64_t)r.objectId;
		out.push_back(d);
		if (m_audio)
		{
			m_audio->applyScriptRequest(r); // lane SCRIPT-2: the script's sounds, speech and music (S-1182)
		}
		m_pendingScriptRequests.push_back(r);
	}
	return out;
}

Dictionary GameWorld::update_script_view(double delta_ms, const Dictionary &camera)
{
	if (!m_scriptView)
	{
		m_scriptView = std::make_unique<ScriptCameraDirector>();
	}
	if (m_game)
	{
		take_script_requests();
	}
	Coord3D cur;
	cur.x = (float)(double)camera.get("x", 0.0);
	cur.y = (float)(double)camera.get("y", 0.0);
	cur.z = (float)(double)camera.get("z", 0.0);
	const float angle = (float)(double)camera.get("angle", 0.0);
	for (const ScriptClientRequest &r : m_pendingScriptRequests)
	{
		m_scriptView->apply(r, cur, angle);
	}
	m_pendingScriptRequests.clear();
	m_scriptView->update(delta_ms);
	const ScriptCameraDirector &v = *m_scriptView;
	Dictionary d;
	d["moving"] = v.moving();
	d["has_target"] = v.hasTarget();
	d["x"] = v.target().x;
	d["y"] = v.target().y;
	d["z"] = v.target().z;
	// lane SCRIPT-2: CAMERA_FOLLOW_NAMED: the followed object's position wins over the director's target
	if (v.followId() != 0 && m_game)
	{
		if (const ::Object *o = m_game->logic().findObjectByID(v.followId()))
		{
			d["has_target"] = true;
			d["x"] = o->getPosition()->x;
			d["y"] = o->getPosition()->y;
			d["z"] = o->getPosition()->z;
		}
	}
	d["following"] = (int64_t)v.followId();
	d["zoom"] = v.zoom();
	d["pitch"] = v.pitch();
	{
		std::string note;
		if (!v.notificationLabel().empty())
		{
			const GameTextTable *table = gameText(m_fs.is_valid() ? m_fs->archive_fs() : nullptr);
			if (!table || !table->lookup(v.notificationLabel(), note))
			{
				note = "MISSING: '" + v.notificationLabel() + "'";
			}
		}
		d["notification_text"] = g(note);
		Array objectives;
		for (const auto &o : v.objectives())
		{
			Dictionary od;
			od["index"] = (int64_t)o.first;
			od["completed"] = o.second;
			objectives.push_back(od);
		}
		d["objectives"] = objectives;
	}
	d["has_angle"] = v.hasAngle();
	d["angle"] = v.angle();
	d["reset_view"] = v.resetView();
	d["letterbox"] = v.letterbox();
	d["input_disabled"] = v.inputDisabled();
	d["caption"] = g(v.captionLabel());
	std::string text;
	if (!v.captionLabel().empty())
	{
		const GameTextTable *table = gameText(m_fs.is_valid() ? m_fs->archive_fs() : nullptr);
		if (!table || !table->lookup(v.captionLabel(), text))
		{
			text = "MISSING: '" + v.captionLabel() + "'"; // ZH GameText::fetch of an unknown label
		}
	}
	d["caption_text"] = g(text);
	d["caption_ms"] = v.captionMsLeft();
	d["fade"] = v.fade();
	return d;
}

namespace
{
void killThroughMembers(::Object &o)
{
	if (o.isEffectivelyDead() || o.isDestroyed())
	{
		return;
	}
	if (o.isKindOfName("HORDE") && o.getContain() && o.getContain()->getContainedItemsList())
	{
		const std::vector<::Object *> members(o.getContain()->getContainedItemsList()->begin(), o.getContain()->getContainedItemsList()->end());
		for (::Object *m : members)
		{
			killThroughMembers(*m);
		}
		return;
	}
	if (o.getBodyModule())
	{
		o.getBodyModule()->setIndestructible(false);
	}
	o.kill(DEATH_NORMAL);
}
} // namespace

Dictionary GameWorld::debug_script_place(const String &name, const String &where)
{
	Dictionary out;
	out["ok"] = false;
	if (!m_game || !m_testHooks || m_netHost || m_netClient || m_netStarted || m_netSession)
	{
		out["error"] = "refused (no game, test_hooks off or a network game)";
		return out;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	GameLogic &logic = m_game->logic();
	ScriptEngine &e = logic.scriptEngine();
	::Object *o = e.getUnitNamed(name.utf8().get_data());
	if (!o)
	{
		out["error"] = "no unit of that name";
		return out;
	}
	const std::string w = where.utf8().get_data();
	Coord3D p{ 0.0f, 0.0f, 0.0f };
	if (const TriggerArea *t = e.findTrigger(w))
	{
		for (const Point2F &q : t->points)
		{
			p.x += q.x / (float)t->points.size();
			p.y += q.y / (float)t->points.size();
		}
	}
	else if (const Waypoint *wp = logic.terrain() ? logic.terrain()->findWaypointByName(w) : nullptr)
	{
		p = wp->location;
	}
	else
	{
		out["error"] = "no trigger area or waypoint of that name";
		return out;
	}
	p.z = logic.getGroundHeight(p.x, p.y);
	o->setPosition(&p);
	out["ok"] = true;
	return out;
}

Dictionary GameWorld::debug_script_kill(const String &name)
{
	Dictionary out;
	out["ok"] = false;
	if (!m_game || !m_testHooks || m_netHost || m_netClient || m_netStarted || m_netSession)
	{
		out["error"] = "refused (no game, test_hooks off or a network game)";
		return out;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Object *o = m_game->logic().scriptEngine().getUnitNamed(name.utf8().get_data());
	if (!o)
	{
		out["error"] = "no unit of that name";
		return out;
	}
	killThroughMembers(*o);
	out["ok"] = true;
	return out;
}

// ---- lane SCRIPT-3: the hooks of the campaign drives (map_viewer --cine-drive) -------------------------------------------------------------------

namespace
{
bool hooksRefused(const LiveGame *game, bool testHooks, bool net, Dictionary &out)
{
	if (!game || !testHooks || net)
	{
		out["error"] = "refused (no game, test_hooks off or a network game)";
		return true;
	}
	return false;
}
} // namespace

Dictionary GameWorld::debug_script_unit(const String &name)
{
	Dictionary out;
	out["ok"] = false;
	if (hooksRefused(m_game.get(), m_testHooks, m_netHost || m_netClient || m_netStarted || m_netSession, out))
	{
		return out;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	const ::Object *o = m_game->logic().scriptEngine().getUnitNamed(name.utf8().get_data());
	if (o)
	{
		out["ok"] = true;
		out["x"] = o->getPosition()->x;
		out["y"] = o->getPosition()->y;
		out["z"] = o->getPosition()->z;
		out["dead"] = o->isEffectivelyDead();
	}
	return out;
}

Dictionary GameWorld::debug_script_team(const String &name)
{
	Dictionary out;
	out["ok"] = false;
	if (hooksRefused(m_game.get(), m_testHooks, m_netHost || m_netClient || m_netStarted || m_netSession, out))
	{
		return out;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Team *t = ScriptConditions::team(m_game->logic().scriptEngine(), name.utf8().get_data());
	if (t && t->getFirstMember())
	{
		out["ok"] = true;
		out["x"] = t->getFirstMember()->getPosition()->x;
		out["y"] = t->getFirstMember()->getPosition()->y;
		out["z"] = t->getFirstMember()->getPosition()->z;
		out["count"] = (int64_t)t->getMemberCount();
	}
	return out;
}

Dictionary GameWorld::debug_script_place_at(const String &name, double x, double y)
{
	Dictionary out;
	out["ok"] = false;
	if (hooksRefused(m_game.get(), m_testHooks, m_netHost || m_netClient || m_netStarted || m_netSession, out))
	{
		return out;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Object *o = m_game->logic().scriptEngine().getUnitNamed(name.utf8().get_data());
	if (o)
	{
		Coord3D p{ (float)x, (float)y, 0.0f };
		p.z = m_game->logic().getGroundHeight(p.x, p.y);
		o->setPosition(&p);
		out["ok"] = true;
	}
	return out;
}

Dictionary GameWorld::debug_script_kill_team(const String &name)
{
	Dictionary out;
	out["ok"] = false;
	if (hooksRefused(m_game.get(), m_testHooks, m_netHost || m_netClient || m_netStarted || m_netSession, out))
	{
		return out;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Team *t = ScriptConditions::team(m_game->logic().scriptEngine(), name.utf8().get_data());
	if (t)
	{
		for (::Object *o : ScriptConditions::members(*t))
		{
			killThroughMembers(*o);
		}
		out["ok"] = true;
	}
	return out;
}

Dictionary GameWorld::get_script_report() const
{
	Dictionary d;
	d["loaded"] = false;
	if (!m_game)
	{
		return d;
	}
	const GameLogic &logic = m_game->logic();
	const ScriptEngine &e = logic.scriptEngine();
	d["loaded"] = e.loaded();
	const ScriptEngine::Stats &s = e.stats();
	d["scripts"] = (int64_t)s.scripts;
	d["groups"] = (int64_t)s.groups;
	d["updates"] = (int64_t)s.updates;
	d["scripts_evaluated"] = (int64_t)s.scriptsEvaluated;
	d["scripts_fired"] = (int64_t)s.scriptsFired;
	d["false_fired"] = (int64_t)s.falseFired;
	d["actions_run"] = (int64_t)s.actionsRun;
	d["conditions_evaluated"] = (int64_t)s.conditionsEvaluated;
	auto counts = [](const std::map<std::string, unsigned long long> &m) {
		Dictionary out;
		for (const auto &kv : m)
		{
			out[g(kv.first)] = (int64_t)kv.second;
		}
		return out;
	};
	d["unported_conditions"] = counts(s.unportedConditions);
	d["unported_actions"] = counts(s.unportedActions);
	d["client_requests"] = counts(s.clientRequests);
	d["notes"] = counts(s.notes);
	Array sides;
	for (const ScriptEngine::RSide &side : e.sides())
	{
		Dictionary sd;
		sd["name"] = g(side.name);
		sd["scripts"] = (int64_t)side.root.scripts.size();
		sd["groups"] = (int64_t)side.root.groups.size();
		sides.push_back(sd);
	}
	d["sides"] = sides;
	Array ends;
	for (const ScriptEngine::EndRequest &r : e.endRequests())
	{
		Dictionary ed;
		ed["frame"] = (int64_t)r.frame;
		ed["player"] = (int64_t)r.playerIndex;
		ed["victory"] = r.victory;
		ed["action"] = g(r.action);
		ends.push_back(ed);
	}
	d["end_requests"] = ends;
	if (m_audio) // lane SCRIPT-2: the script audio played on this client
	{
		d["audio_played"] = (int64_t)m_audio->scriptAudioStats().played;
		PackedStringArray last;
		for (const std::string &n : m_audio->scriptAudioStats().lastEvents)
		{
			last.push_back(g(n));
		}
		d["audio_last"] = last;
	}
	PackedStringArray setup;
	for (const std::string &l : m_game->report().mapScripts)
	{
		setup.push_back(g(l));
	}
	d["setup"] = setup;
	return d;
}
