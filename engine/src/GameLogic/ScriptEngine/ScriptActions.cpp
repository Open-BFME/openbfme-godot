// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/ScriptEngine/ScriptActions.h.

#include "GameLogic/ScriptEngine/ScriptActions.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"

#include <set>

namespace
{

const ScriptParameter &param(const ScriptActionRec &a, size_t i)
{
	static const ScriptParameter kEmpty;
	return i < a.params.size() ? a.params[i] : kEmpty;
}

int ordinal(const char *name)
{
	return ScriptTemplates::findAction(name);
}

// Parameter::ParameterType values (the templates' parameter types)
constexpr int PARAM_WAYPOINT = 7;
constexpr int PARAM_CAMERA_POSITION = 51; ///< MOVE_CAMERA_TO's first parameter (a waypoint or named camera name in the corpus)

// The actions recorded as client requests (S-1182): the camera, the screen, the UI, audio, movies and input. Retail runs them on TheTacticalView,
// TheInGameUI, TheAudio ... from inside the logic frame; here the logic records them and the client applies them in frame order.
const std::set<int> &clientActions()
{
	static const std::set<int> s = [] {
		static const char *const names[] = {
			"MOVE_CAMERA_TO", "MOVE_CAMERA_ALONG_WAYPOINT_PATH", "MOVE_CAMERA_ALONG_SPLINE_PATH", "MOVE_CAMERA_LOCATOR_ALONG_SPLINE_PATH",
			"ROTATE_CAMERA", "RESET_CAMERA", "ZOOM_CAMERA", "PITCH_CAMERA", "ROLL_CAMERA", "ROTATE_CAMERA_LOCKED", "CAMERA_FOLLOW_NAMED",
			"CAMERA_STOP_FOLLOW", "CAMERA_LOOK_TOWARD_OBJECT", "CAMERA_LOOK_TOWARD_WAYPOINT", "CAMERA_MOD_SET_FINAL_ZOOM", "CAMERA_MOD_SET_FINAL_PITCH",
			"CAMERA_MOD_FREEZE_TIME", "CAMERA_MOD_FREEZE_ANGLE", "CAMERA_MOD_FINAL_LOOK_TOWARD", "CAMERA_MOD_LOOK_TOWARD", "CAMERA_MOD_SET_ROLLING_AVERAGE",
			"CAMERA_MOD_FINAL_SPEED_MULTIPLIER", "CAMERA_MOD_FINAL_TIME_MULTIPLIER", "CAMERA_LETTERBOX_BEGIN", "CAMERA_LETTERBOX_END", "CAMERA_BW_MODE_BEGIN",
			"CAMERA_BW_MODE_END", "CAMERA_MOTION_BLUR", "CAMERA_MOTION_BLUR_JUMP", "CAMERA_MOTION_BLUR_FOLLOW", "CAMERA_MOTION_BLUR_END_FOLLOW",
			"CAMERA_FADE_ADD", "CAMERA_FADE_SUBTRACT", "CAMERA_FADE_SATURATE", "CAMERA_FADE_MULTIPLY", "CAMERA_SET_DEFAULT", "CAMERA_MOVE_HOME",
			"CAMERA_TETHER_NAMED", "CAMERA_STOP_TETHER_NAMED", "CAMERA_ENABLE_SLAVE_MODE", "CAMERA_DISABLE_SLAVE_MODE", "CAMERA_ADD_SHAKER_AT",
			"CAMERA_SET_AUDIBLE_DISTANCE", "SETUP_CAMERA", "SHOW_MILITARY_CAPTION", "DISPLAY_TEXT", "DISPLAY_CINEMATIC_TEXT", "DISPLAY_COUNTER",
			"HIDE_COUNTER", "DISPLAY_COUNTDOWN_TIMER", "HIDE_COUNTDOWN_TIMER", "DISPLAY_NOTIFICATION_BOX", "DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE",
			"SHOW_MISSION_OBJECTIVE", "HIDE_MISSION_OBJECTIVE", "MARK_MISSION_OBJECTIVE_COMPLETED", "MARK_MISSION_OBJECTIVE_NOT_COMPLETED",
			"PLAY_SOUND_EFFECT", "PLAY_SOUND_EFFECT_AT", "PLAY_SOUND_EFFECT_AT_TEAM", "SOUND_PLAY_NAMED", "SPEECH_PLAY", "MUSIC_SET_TRACK",
			"MUSIC_SET_VOLUME", "SOUND_SET_VOLUME", "SPEECH_SET_VOLUME", // lane SCRIPT-3: RW 0x7BCAAE (TheAudio's script volume, client)
			"MUSIC_PLAY_TRACK_FINITE_TIMES", "MOVIE_PLAY_FULLSCREEN", "MOVIE_PLAY_RADAR", "PLAY_MOVIE_IN_GAME", "DISABLE_INPUT", "ENABLE_INPUT",
			"HIDE_UI", "SHOW_UI", "DESELECT", "SELECT_OBJECT", "EVA_SET_ENABLED_DISABLED", "AUDIO_MAKE_SOUND_IMMUNE_TO_FADE", "AUDIO_FADE_VOLUME",
			"AUDIO_RESTORE_VOLUME_ALL_TYPE", "AUDIO_OVERRIDE_VOLUME_TYPE", "AUDIO_RESTORE_VOLUME_TYPE", "AUDIO_SET_VOLUME_TYPE", "CAMEO_FLASH",
			"NAMED_FLASH", "TEAM_FLASH", "OBJECT_CREATE_RADAR_EVENT", "ENABLE_HOUSE_COLOR", "DRAW_SKYBOX_BEGIN", "DRAW_SKYBOX_END", "SUSPEND_BACKGROUND_SOUNDS",
			"RESUME_BACKGROUND_SOUNDS", "SET_VISUAL_SPEED_MULTIPLIER", "SHOW_WEATHER", "SCREEN_SHAKE", "SELECT_BUILDER_BUTTON_FLASH",
			"CLOSE_OBJECTIVES_SCREEN", "PLAYER_SELECT_SKILLSET", "MAP_REVEAL_AT_WAYPOINT", "MAP_SHROUD_AT_WAYPOINT", "MAP_REVEAL_PERMANENTLY_AT_WAYPOINT",
			"MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT", "MAP_REVEAL_PERMANENTLY_IN_TRIGGER", "MAP_UNDO_REVEAL_PERMANENTLY_IN_TRIGGER",
			// lane SCRIPT-2: the victory screen (RW 0x7BF05C: TheShell's victory window, no logic), the objectives button flash (RW 0x7BDF4E), the command
			// bar edits (RW 0x7C6793 ...: TheControlBar), the radar event of a team (RW 0x7C...: TheRadar)
			"VICTORY_SCREEN", "FLASH_OBJECTIVES_BUTTON", "COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE", "COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT",
			"TEAM_CREATE_RADAR_EVENT", "TOGGLE_AVI_CAPTURE", "ENABLE_OBJECT_SOUND", "DISABLE_OBJECT_SOUND",
		};
		std::set<int> out;
		for (const char *n : names)
		{
			const int o = ordinal(n);
			if (o >= 0)
			{
				out.insert(o);
			}
		}
		return out;
	}();
	return s;
}

bool positionOf(ScriptEngine &engine, const std::string &name, Coord3D &out)
{
	if (const TerrainLogic *terrain = engine.logic().terrain())
	{
		if (const Waypoint *w = terrain->findWaypointByName(name))
		{
			out = w->location;
			return true;
		}
	}
	if (const NamedCamera *c = engine.findNamedCamera(name))
	{
		out = c->position;
		return true;
	}
	return false;
}

// RW 0x7C5B3B
void createOnTeamAtWaypoint(ScriptEngine &engine, const std::string &name, const std::string &templateName, const std::string &teamName,
	const std::string &waypointName)
{
	GameLogic &logic = engine.logic();
	Object *existing = engine.getUnitNamed(name);
	if (existing && !existing->isEffectivelyDead())
	{
		engine.note("WARNING: Object with name " + name + " already exists.  Failed Create.");
		return;
	}
	Team *team = ScriptConditions::team(engine, teamName);
	if (!team)
	{
		engine.note("***WARNING: Team not found:*** " + teamName);
		return;
	}
	const Waypoint *wp = logic.terrain() ? logic.terrain()->findWaypointByName(waypointName) : nullptr;
	if (!wp)
	{
		// RW 0x7C5C6C .. 0x7C5CD5: a waypoint name that is an object type places at the nearest such object (not ported)
		engine.note("***WARNING: Waypoint not found (the object-type waypoint form is not ported):*** " + waypointName);
		return;
	}
	const ThingTemplate *tt = logic.things().findTemplate(templateName);
	if (!tt)
	{
		engine.note("***WARNING: object template not found:*** " + templateName);
		return;
	}
	if (!engine.host())
	{
		engine.note("[S-1180] no object creation host: " + templateName);
		return;
	}
	// the name goes on before the object enters the cache (RW 0x7C5D30: + 0x88, then RW 0x60A1D5); the host makes the full object (ZH
	// ScriptActions::doCreateObject: newObject, position, pathfinder registration)
	Object *obj = engine.host()->createObject(*tt, *team, wp->location, 0.0f);
	if (obj && !name.empty())
	{
		obj->setName(name); // the cache entry (Object::setName -> ScriptEngine::objectNamed)
	}
}

} // namespace

bool ScriptActions::isClientAction(int o)
{
	return clientActions().count(o) != 0;
}

void ScriptActions::clientRequest(ScriptEngine &engine, const ScriptActionRec &a)
{
	ScriptClientRequest r;
	r.action = ScriptTemplates::action(a.resolved)->name;
	r.params = a.params;
	for (const ScriptParameter &p : a.params)
	{
		if ((p.type == PARAM_WAYPOINT || p.type == PARAM_CAMERA_POSITION) && !r.hasPosition)
		{
			r.hasPosition = positionOf(engine, p.stringValue, r.position);
		}
		if (p.type == 14 && r.objectId == 0) // a named unit: the client follows / sounds at it
		{
			if (const Object *o = engine.getUnitNamed(p.stringValue))
			{
				r.objectId = o->getID();
			}
		}
	}
	engine.addClientRequest(std::move(r));
}

// RW 0x7CAFA5
void ScriptActions::execute(ScriptEngine &engine, const ScriptActionRec &a)
{
	if (!a.enabled)
	{
		return; // RW 0x7CAFB7: + 0x41 == 0
	}
	const ScriptTemplate *t = ScriptTemplates::action(a.resolved);
	if (!t || !t->retailCase)
	{
		return; // retail's default: nothing
	}
	if (isClientAction(a.resolved))
	{
		clientRequest(engine, a);
		return;
	}
	GameLogic &logic = engine.logic();
	const std::string &name = t->name;
	if (name == "VICTORY" || name == "QUICKVICTORY")
	{
		engine.addEndRequest(true, name); // RW 0x7C45E0 / 0x7BE0A2 (the client's end screen)
		return;
	}
	if (name == "DEFEAT")
	{
		engine.addEndRequest(false, name); // RW 0x7BF15E
		return;
	}
	if (name == "PLAYER_GRANT_SCIENCE" || name == "PLAYER_PURCHASE_SCIENCE") // RW 0x7CE037 / 0x7CE05C -> RW 0x7BD1BA / 0x7BD210(param 0 player, param 1 science)
	{
		// TheScienceStore by name (RW 0x5FEF8F; an unknown name does nothing), then every player of the parameter: grantScience (RW 0x6AE3A7) or
		// attemptToPurchaseScience (RW 0x6AE36F)
		const ScienceType st = TheScienceStore ? TheScienceStore->getScienceFromInternalName(param(a, 1).stringValue) : SCIENCE_INVALID;
		if (st == SCIENCE_INVALID)
		{
			return;
		}
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			if (name == "PLAYER_GRANT_SCIENCE")
			{
				p->science().grantScience(st);
			}
			else
			{
				p->science().attemptToPurchaseScience(st);
			}
		}
		return;
	}
	if (name == "OBJECTLIST_ADDOBJECTTYPE" || name == "OBJECTLIST_REMOVEOBJECTTYPE") // RW 0x7CE11A / 0x7CE11E -> RW 0x759D77(param 0, param 1, add)
	{
		engine.objectListAdd(param(a, 0).stringValue, param(a, 1).stringValue, name == "OBJECTLIST_ADDOBJECTTYPE");
		return;
	}
	if (name == "CREATE_NAMED_ON_TEAM_AT_WAYPOINT")
	{
		createOnTeamAtWaypoint(engine, param(a, 0).stringValue, param(a, 1).stringValue, param(a, 2).stringValue, param(a, 3).stringValue);
		return;
	}
	if (name == "CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT")
	{
		createOnTeamAtWaypoint(engine, std::string(), param(a, 0).stringValue, param(a, 1).stringValue, param(a, 2).stringValue);
		return;
	}
	if (name == "PLAYER_SET_MONEY") // RW 0x7BCAFB (player, amount)
	{
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			Money *m = p->getMoney();
			m->withdraw(m->countMoney(), nullptr, true);
			m->deposit((std::uint32_t)param(a, 1).intValue, nullptr, true);
		}
		return;
	}
	if (name == "PLAYER_GIVE_MONEY") // RW 0x7BCB5A (player, amount)
	{
		const std::int32_t amount = param(a, 1).intValue;
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			if (amount < 0)
			{
				p->getMoney()->withdraw((std::uint32_t)(-amount), nullptr, true);
			}
			else
			{
				p->getMoney()->deposit((std::uint32_t)amount, nullptr, true);
			}
		}
		return;
	}
	if (name == "PLAYER_RELATES_PLAYER") // RW 0x7CC779: RW 0x7BC1C6(param 0, param 2's int, param 1): the first player of parameter 0 relates to parameter 1's
	{
		Player *p1 = ScriptConditions::firstPlayer(engine, param(a, 0).stringValue);
		Player *p2 = ScriptConditions::firstPlayer(engine, param(a, 1).stringValue);
		const int r = param(a, 2).intValue; // RELATION: 0 enemy, 1 neutral, 2 friend (ZH Parameter::RELATION)
		if (p1 && p2 && r >= ENEMIES && r <= ALLIES)
		{
			p1->setPlayerRelationship(p2, (Relationship)r);
		}
		return;
	}
	if (name == "UNIT_SET_TEAM") // RW 0x7BF794 (the AI's team hook, vslot 0x54, is not ported)
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		Team *tm = ScriptConditions::team(engine, param(a, 1).stringValue);
		if (o && tm)
		{
			o->setTeam(tm);
		}
		return;
	}
	if (name == "TEAM_TRANSFER_TO_PLAYER") // RW 0x7C9B79
	{
		Team *tm = ScriptConditions::team(engine, param(a, 0).stringValue);
		Player *p = ScriptConditions::firstPlayer(engine, param(a, 1).stringValue);
		if (tm && p && p->getDefaultTeam())
		{
			// INFERENCE (S-1180): retail gives the TEAM to the player (RW 0x7A00DA); the port moves its members to the player's default team, which
			// runs the same owner change per object; the members then go idle (RW 0x5E821A)
			engine.note("[S-1180] TEAM_TRANSFER_TO_PLAYER moves the members to the player's default team");
			for (Object *o : ScriptConditions::members(*tm))
			{
				o->setTeam(p->getDefaultTeam());
				AICommand idle;
				idle.type = AICMD_IDLE;
				idle.source = CMD_FROM_SCRIPT;
				idle.object = o->getID();
				idle.frame = logic.getFrame();
				logic.aiCommands().issue(*o, idle);
			}
		}
		return;
	}
	if (name == "NAMED_DELETE") // RW 0x7BBEF7: the object is removed without dying
	{
		if (Object *o = engine.getUnitNamed(param(a, 0).stringValue))
		{
			logic.destroyObject(o);
		}
		return;
	}
	if (name == "NAMED_KILL") // RW 0x7BBF53
	{
		if (Object *o = engine.getUnitNamed(param(a, 0).stringValue))
		{
			if (!o->isEffectivelyDead())
			{
				o->kill(DEATH_NORMAL);
			}
		}
		return;
	}
	if (name == "TEAM_KILL" || name == "TEAM_DELETE") // RW 0x7C07ED / 0x7C0556
	{
		if (Team *tm = ScriptConditions::team(engine, param(a, 0).stringValue))
		{
			for (Object *o : ScriptConditions::members(*tm))
			{
				if (name == "TEAM_DELETE")
				{
					logic.destroyObject(o);
				}
				else if (!o->isEffectivelyDead())
				{
					o->kill(DEATH_NORMAL);
				}
			}
		}
		return;
	}
	if (name == "MOVE_NAMED_UNIT_TO" || name == "MOVE_TEAM_TO") // RW 0x7C84CC / 0x7BF4A3: aiMoveToPosition from the script
	{
		Coord3D pos;
		if (!positionOf(engine, param(a, 1).stringValue, pos))
		{
			engine.note("***WARNING: Waypoint not found:*** " + param(a, 1).stringValue);
			return;
		}
		std::vector<Object *> objs;
		if (name == "MOVE_NAMED_UNIT_TO")
		{
			if (Object *o = engine.getUnitNamed(param(a, 0).stringValue))
			{
				objs.push_back(o);
			}
		}
		else if (Team *tm = ScriptConditions::team(engine, param(a, 0).stringValue))
		{
			objs = ScriptConditions::members(*tm);
		}
		for (Object *o : objs)
		{
			if (o->isEffectivelyDead())
			{
				continue;
			}
			AICommand move;
			move.type = AICMD_MOVE_TO_POSITION;
			move.source = CMD_FROM_SCRIPT;
			move.object = o->getID();
			move.path.push_back(pos);
			move.frame = logic.getFrame();
			logic.aiCommands().issue(*o, move);
		}
		return;
	}
	if (name == "NAMED_FACE_WAYPOINT") // RW 0x7CA509: the AI turns to face the waypoint (RW 0x7C821E)
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		Coord3D pos;
		if (o && positionOf(engine, param(a, 1).stringValue, pos))
		{
			// INFERENCE (S-1180): the face command is not an AI command here; the object turns at once
			engine.note("[S-1180] NAMED_FACE_WAYPOINT turns the object at once (retail: the AI's face-position command)");
			const Coord3D *p = o->getPosition();
			const double angle = SimMath::atan2d(SimMath::sseSub(pos.y, p->y), SimMath::sseSub(pos.x, p->x));
			o->setOrientation(SimMath::fstpDword(angle)); // the stored float
		}
		return;
	}
	if (name == "PLAYER_DISABLE_BASE_CONSTRUCTION" || name == "PLAYER_ENABLE_BASE_CONSTRUCTION" || name == "PLAYER_DISABLE_UNIT_CONSTRUCTION" ||
		name == "PLAYER_ENABLE_UNIT_CONSTRUCTION")
	{
		const bool enable = name.find("ENABLE") == 7;
		const bool base = name.find("BASE") != std::string::npos;
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			if (base)
			{
				p->setCanBuildBase(enable);
			}
			else
			{
				p->setCanBuildUnits(enable);
			}
		}
		return;
	}
	if (name == "MAP_REVEAL_ALL" || name == "MAP_REVEAL_ALL_PERM" || name == "MAP_REVEAL_ALL_UNDO_PERM" || name == "MAP_SHROUD_ALL")
	{
		ShroudManager *shroud = logic.shroud();
		if (!shroud)
		{
			engine.note("[S-1180] " + name + ": the game has no shroud manager");
			return;
		}
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			const int i = p->getPlayerIndex();
			if (name == "MAP_REVEAL_ALL") shroud->revealMapForPlayer(i);
			else if (name == "MAP_REVEAL_ALL_PERM") shroud->revealMapForPlayerPermanently(i);
			else if (name == "MAP_REVEAL_ALL_UNDO_PERM") shroud->undoRevealMapForPlayerPermanently(i);
			else shroud->shroudMapForPlayer(i);
		}
		return;
	}
	if (executeUnitAction(engine, a, name))
	{
		return;
	}
	engine.noteUnportedAction(name);
}
