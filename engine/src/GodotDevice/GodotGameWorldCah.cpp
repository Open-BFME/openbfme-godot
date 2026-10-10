// OpenBFME. GPL-3.0.
// Lane CAH-1: GameWorld's side of the Create-a-Hero builder (see GodotDevice/GodotGameWorld.h): the builder's map mode. RW 0x91A018 starts
// Maps\CreateAHero\CreateAHero.map behind CreateAHero.apt; the edited hero is applied to the map's preview object (RW 0x9C0E03: the record's object id is
// the location's object, then the screen's vslot 0x14 = RW 0x80ACE3) with TheCreateAHeroSystem + 0x18C set, so the object keeps its map mode upgrades.
// Never in a network game: the map mode is the client's own world.

#include "GodotDevice/GodotGameWorld.h"

#include "Common/CreateAHeroRecord.h"
#include "Common/Player.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <godot_cpp/core/class_db.hpp>
#include <cstdlib>
#include <vector>
#include <godot_cpp/variant/packed_byte_array.hpp>

using namespace godot;

void GameWorld::bindCreateAHeroMethods()
{
	ClassDB::bind_method(D_METHOD("cah_builder", "on"), &GameWorld::cah_builder);
	ClassDB::bind_method(D_METHOD("cah_preview_apply", "object_id", "record"), &GameWorld::cah_preview_apply);
	ClassDB::bind_method(D_METHOD("cah_preview_locations"), &GameWorld::cah_preview_locations);
	ClassDB::bind_method(D_METHOD("cah_view_info", "class", "subclass"), &GameWorld::cah_view_info);
}

bool GameWorld::cah_builder(bool on)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!TheCreateAHeroSystem)
	{
		return false;
	}
	TheCreateAHeroSystem->inBuilder = on; // RW 0x91A6F8: + 0x18C
	return true;
}

Dictionary GameWorld::cah_preview_apply(int64_t objectId, const PackedByteArray &record)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary out;
	out["ok"] = false;
	if (!m_game || !TheCreateAHeroSystem)
	{
		out["error"] = String("cah_preview_apply: no map is loaded");
		return out;
	}
	if (m_start || m_netHost || m_netClient || m_netStarted || m_netSession || m_game->logicThread())
	{
		out["error"] = String("cah_preview_apply: refused in a started skirmish, a network game or under the logic worker (the builder's map mode is the client's own world)");
		return out;
	}
	if (!TheCreateAHeroSystem->inBuilder)
	{
		out["error"] = String("cah_preview_apply: the builder is not up (cah_builder(true))");
		return out;
	}
	CreateAHeroHero hero;
	std::string error;
	std::vector<std::uint8_t> bytes((size_t)record.size());
	if (!bytes.empty())
	{
		memcpy(bytes.data(), record.ptr(), bytes.size());
	}
	if (!hero.load(bytes, &error))
	{
		out["error"] = String(("cah_preview_apply: the record does not load: " + error).c_str());
		return out;
	}
	::Object *obj = m_game->logic().findObjectByID((::ObjectID)objectId);
	::Player *player = obj ? obj->getControllingPlayer() : nullptr;
	if (!obj || !player)
	{
		out["error"] = String("cah_preview_apply: no such object with a player");
		return out;
	}
	m_game->logic().createAHeroes().applyBuilder(*player, hero, *obj);
	out["ok"] = true;
	return out;
}

Array GameWorld::cah_preview_locations() const
{
	// RW 0x9C09FC: every drawable's object whose map name holds a '_' (RW 0x9BFCBA: strstr "_" RW 0xBD3420, atoi of the rest) is the location of that
	// number: { object id (+0x74), angle (+0x44) }, the list grown to it (empty entries: object 0)
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Array out;
	if (!m_game)
	{
		return out;
	}
	std::vector<Dictionary> byIndex;
	for (const ::Object *o = m_game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		const std::string &name = o->getName();
		const size_t us = name.find('_');
		if (us == std::string::npos)
		{
			continue;
		}
		const int index = std::atoi(name.c_str() + us + 1);
		if (index < 0)
		{
			continue;
		}
		if (byIndex.size() <= (size_t)index)
		{
			byIndex.resize((size_t)index + 1);
		}
		Dictionary d;
		d["id"] = (int64_t)o->getID();
		d["name"] = String(name.c_str());
		d["template"] = String(o->getTemplate() ? o->getTemplate()->getName().c_str() : "");
		const Coord3D &p = *o->getPosition();
		d["x"] = p.x;
		d["y"] = p.y;
		d["z"] = p.z;
		d["angle"] = o->getOrientation();
		byIndex[(size_t)index] = d;
	}
	for (const Dictionary &d : byIndex)
	{
		if (d.is_empty())
		{
			Dictionary none;
			none["id"] = (int64_t)0;
			out.append(none);
		}
		else
		{
			out.append(d);
		}
	}
	return out;
}

Dictionary GameWorld::cah_view_info(int64_t cls, int64_t sub) const
{
	Dictionary out;
	out["ok"] = false;
	if (!m_world)
	{
		return out;
	}
	const CreateAHeroSubClass *s = m_world->createAHeroSystem().subClass((std::uint32_t)cls, (std::uint32_t)sub);
	if (!s)
	{
		return out;
	}
	const CreateAHeroViewInfo &v = s->viewInfo;
	auto five = [](float a, float b, float c, float d, float e) {
		Array r;
		r.append(a);
		r.append(b);
		r.append(c);
		r.append(d);
		r.append(e);
		return r;
	};
	out["ok"] = true;
	out["near"] = five(v.nearPitch, v.nearZoom, v.nearFloor, v.nearDist, v.nearShift);
	out["far"] = five(v.farPitch, v.farZoom, v.farFloor, v.farDist, v.farShift);
	out["close_up"] = five(v.closeUpPitch, v.closeUpZoom, v.closeUpFloor, v.closeUpDist, v.closeUpShift);
	out["portrait"] = five(v.portraitPitch, v.portraitZoom, v.portraitFloor, v.portraitDist, v.portraitShift);
	out["normal_cam"] = v.normalCam;
	out["camera_angle"] = v.cameraAngle;
	out["map_location"] = (int64_t)v.mapLocation;
	return out;
}
