// OpenBFME. GPL-3.0.
// See GodotDevice/GodotPathfindView.h.

#include "GodotDevice/GodotPathfindView.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapPathfindObjects.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindConfig.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Map/TerrainPathfindSource.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GodotDevice/GodotRetailFileSystem.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

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
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

bool optBool(const Dictionary &o, const char *key, bool def)
{
	return o.has(key) ? (bool)o[key] : def;
}

double optReal(const Dictionary &o, const char *key, double def)
{
	return o.has(key) ? (double)o[key] : def;
}

Vector3 toGodotPos(float x, float y, float z)
{
	return Vector3(x, z, -y);
}

struct EmptyWorld : PathfindWorld
{
	PathfindObject *findObjectByID(PathfindObjectID) const override { return nullptr; }
	unsigned getFrame() const override { return 100; }
};
} // namespace

struct PathfindView::Impl
{
	std::unique_ptr<RetailObjectWorld> world;
	Ref<RetailFileSystem> fs;
	PathfindConfig config;
	bool haveConfig = false;
	std::unique_ptr<LoadedMap> map;
	std::unique_ptr<TerrainLogic> terrain;
	std::unique_ptr<TerrainPathfindSource> source;
	std::unique_ptr<MapObjectDrawables> drawables;
	MapPathfindObjectSet objects;
	EmptyWorld emptyWorld;
	std::unique_ptr<Pathfinder> pathfinder;
};

void PathfindView::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("setup", "fs"), &PathfindView::setup);
	ClassDB::bind_method(D_METHOD("build", "fs", "map_name", "options"), &PathfindView::build, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("get_overlay", "options"), &PathfindView::get_overlay, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("find_path", "from", "to", "options"), &PathfindView::find_path, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("pick_endpoints", "options"), &PathfindView::pick_endpoints, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("get_bounds"), &PathfindView::get_bounds);
}

PathfindView::PathfindView() : m_impl(new Impl()) {}
PathfindView::~PathfindView() = default;

Dictionary PathfindView::setup(const Ref<RetailFileSystem> &fs)
{
	Dictionary result;
	Array errors;
	if (fs.is_null() || !fs->is_mounted() || fs->archive_fs() == nullptr)
	{
		errors.push_back("retail file system is not mounted");
	}
	else if (!m_impl->world || m_impl->fs != fs)
	{
		m_impl->fs = fs;
		m_impl->drawables.reset();
		m_impl->map.reset();
		m_impl->world.reset(new RetailObjectWorld(*fs->archive_fs()));
		std::string error;
		const double t0 = nowMs();
		if (!m_impl->world->load(&error))
		{
			errors.push_back(toGodot(error));
			m_impl->world.reset();
		}
		else if (!PathfindConfigLoader::load(*fs->archive_fs(), m_impl->config, &error))
		{
			errors.push_back(toGodot(error));
			m_impl->world.reset();
		}
		else
		{
			m_impl->haveConfig = true;
			result["seconds"] = (nowMs() - t0) / 1000.0;
		}
	}
	result["ok"] = errors.is_empty();
	result["errors"] = errors;
	return result;
}

Dictionary PathfindView::build(const Ref<RetailFileSystem> &fsRef, const String &map_name, const Dictionary &options)
{
	Dictionary report;
	Array errors, stops;
	Dictionary timings;
	report["errors"] = errors;
	report["stops"] = stops;
	report["timings_ms"] = timings;
	report["map"] = map_name;
	auto fail = [&](const std::string &message) -> Dictionary {
		errors.push_back(toGodot(message));
		report["ok"] = false;
		return report;
	};
	if (fsRef.is_null() || !fsRef->is_mounted())
	{
		return fail("retail file system is not mounted");
	}
	const bool withObjects = optBool(options, "objects", true);
	Impl &I = *m_impl;
	if (withObjects && (!I.world || I.fs != fsRef))
	{
		const Dictionary s = setup(fsRef);
		if (!(bool)s["ok"])
		{
			const Array e = s["errors"];
			for (int64_t i = 0; i < e.size(); ++i) errors.push_back(e[i]);
			report["ok"] = false;
			return report;
		}
		timings["object_world_load"] = (double)s["seconds"] * 1000.0;
	}
	if (!I.haveConfig)
	{
		std::string err;
		if (!PathfindConfigLoader::load(*fsRef->archive_fs(), I.config, &err))
		{
			return fail(err);
		}
		I.haveConfig = true;
	}
	I.pathfinder.reset();
	I.source.reset();
	I.terrain.reset();
	I.drawables.reset();
	I.objects = MapPathfindObjectSet();
	I.map.reset();

	RetailFileSystem &rfs = *fsRef.ptr();
	const std::string lowerName = AsciiStringUtil::lowered(toNative(map_name));
	const std::string mapDir = "maps\\" + lowerName;
	const std::string path = mapDir + "\\" + lowerName + ".map";
	double t0 = nowMs();
	std::vector<uint8_t> bytes;
	std::string error;
	if (!rfs.readBytes(path, bytes, &error))
	{
		return fail(error);
	}
	I.map.reset(new LoadedMap());
	{
		MapReadOptions mro;
		if (!MapReader::load(bytes, path, mro, *I.map, &error))
		{
			return fail(error);
		}
	}
	timings["load_map"] = nowMs() - t0;
	I.terrain.reset(new TerrainLogic());
	I.terrain->init(I.map->heightMap, I.map->chunks, nullptr);
	I.source.reset(new TerrainPathfindSource(*I.terrain, I.map->heightMap, I.map->chunks));
	if (withObjects)
	{
		t0 = nowMs();
		const auto worldContext = I.world->enterContext(); // this world's stores through the object build below
		const RetailObjectWorld::MapIniResult mi = I.world->applyMapIni(mapDir);
		for (const std::string &e : mi.errors) errors.push_back(toGodot(e));
		MapObjectOptions mo;
		if (!MapObjectGameData::load(*rfs.archive_fs(), mo, &error))
		{
			return fail(error);
		}
		I.drawables.reset(new MapObjectDrawables());
		MapObjectCreation::build(*I.map, lowerName, I.world->things(), *I.terrain, mo, *I.drawables);
		MapPathfindObjects::build(*I.drawables, I.objects);
		for (const std::string &e : I.objects.errors) errors.push_back(toGodot(e));
		timings["objects"] = nowMs() - t0;
	}
	t0 = nowMs();
	const std::vector<PathfindObject *> ptrs = I.objects.pointers();
	I.pathfinder.reset(new Pathfinder(I.config, &I.emptyWorld));
	I.pathfinder->newMap(*I.source, withObjects ? &ptrs : nullptr);
	timings["grid"] = nowMs() - t0;

	const PathfindGridStats s = I.pathfinder->gridStats();
	report["width"] = s.width;
	report["height"] = s.height;
	Dictionary cells;
	static const char *names[8] = { "clear", "water", "cliff", "rubble", "obstacle", "bridge_impassable", "impassable", "deep_water" };
	for (int i = 0; i < 8; ++i) cells[names[i]] = s.types[i];
	cells["pinched"] = s.pinched;
	cells["impassable_to_players"] = s.impassableToPlayers;
	cells["extra_pass"] = s.extraPass;
	report["cells"] = cells;
	report["structures"] = (int64_t)I.objects.structures;
	report["fences"] = (int64_t)I.objects.fences;
	for (const std::string &st : I.pathfinder->stops()) stops.push_back(toGodot(st));
	for (const std::string &st : I.source->stops()) stops.push_back(toGodot(st));
	report["ok"] = errors.is_empty();
	return report;
}

Dictionary PathfindView::get_bounds() const
{
	Dictionary d;
	if (m_impl->pathfinder)
	{
		d["min"] = Vector2(0, 0);
		d["max"] = Vector2((m_impl->pathfinder->getExtent()->x + 1) * 10.0f, (m_impl->pathfinder->getExtent()->y + 1) * 10.0f);
	}
	return d;
}

Dictionary PathfindView::get_overlay(const Dictionary &options)
{
	Dictionary out;
	PackedVector3Array verts;
	PackedColorArray colors;
	Impl &I = *m_impl;
	if (!I.pathfinder)
	{
		out["vertices"] = verts;
		out["colors"] = colors;
		out["count"] = 0;
		return out;
	}
	const float lift = (float)optReal(options, "lift", 1.0);
	const bool showPinched = optBool(options, "pinched", false);
	const bool showPlanes = optBool(options, "planes", false);
	const ICoord2D *ext = I.pathfinder->getExtent();
	int64_t count = 0;
	const float a = (float)optReal(options, "alpha", 0.55);
	for (int j = 0; j <= ext->y; ++j)
	{
		for (int i = 0; i <= ext->x; ++i)
		{
			const PathfindCell *c = I.pathfinder->cellAt(i, j);
			Color col;
			bool draw = true;
			switch (c->getType())
			{
			case PathfindCell::CELL_WATER: col = Color(0.15f, 0.45f, 1.0f, a); break;
			case PathfindCell::CELL_DEEP_WATER: col = Color(0.05f, 0.15f, 0.7f, a); break;
			case PathfindCell::CELL_CLIFF: col = Color(1.0f, 0.15f, 0.1f, a); break;
			case PathfindCell::CELL_RUBBLE: col = Color(1.0f, 0.55f, 0.1f, a); break;
			case PathfindCell::CELL_OBSTACLE: col = Color(1.0f, 0.95f, 0.1f, a); break;
			case PathfindCell::CELL_IMPASSABLE:
			case PathfindCell::CELL_BRIDGE_IMPASSABLE: col = Color(0.2f, 1.0f, 0.2f, a); break;
			default:
				draw = false;
				if (showPinched && c->getPinched())
				{
					col = Color(0.1f, 0.9f, 0.9f, a);
					draw = true;
				}
				else if (showPlanes && (c->getImpassableToPlayers() || c->getExtraPass()))
				{
					col = c->getImpassableToPlayers() ? Color(0.9f, 0.2f, 0.9f, a) : Color(0.9f, 0.9f, 0.9f, a);
					draw = true;
				}
				break;
			}
			if (!draw) continue;
			const float x0 = (float)i * 10.0f, y0 = (float)j * 10.0f, x1 = x0 + 10.0f, y1 = y0 + 10.0f;
			const float z00 = I.terrain->getGroundHeight(x0, y0) + lift, z10 = I.terrain->getGroundHeight(x1, y0) + lift;
			const float z11 = I.terrain->getGroundHeight(x1, y1) + lift, z01 = I.terrain->getGroundHeight(x0, y1) + lift;
			const Vector3 p00 = toGodotPos(x0, y0, z00), p10 = toGodotPos(x1, y0, z10), p11 = toGodotPos(x1, y1, z11), p01 = toGodotPos(x0, y1, z01);
			// two triangles, counter-clockwise seen from above (Godot +Y up)
			verts.push_back(p00); verts.push_back(p10); verts.push_back(p11);
			verts.push_back(p00); verts.push_back(p11); verts.push_back(p01);
			for (int k = 0; k < 6; ++k) colors.push_back(col);
			++count;
		}
	}
	out["vertices"] = verts;
	out["colors"] = colors;
	out["count"] = count;
	return out;
}

Dictionary PathfindView::pick_endpoints(const Dictionary &options)
{
	(void)options;
	Dictionary out;
	out["ok"] = false;
	Impl &I = *m_impl;
	if (!I.pathfinder)
	{
		return out;
	}
	Pathfinder &pf = *I.pathfinder;
	ICoord2D lo, hi;
	pf.getLogicalExtent(lo, hi);
	struct Cand
	{
		int x, y;
	};
	std::vector<Cand> cands;
	for (int x = lo.x + 2; x <= hi.x - 2; x += 3)
	{
		for (int y = lo.y + 2; y <= hi.y - 2; y += 3)
		{
			const PathfindCell *c = pf.cellAt(x, y);
			if (c && c->getType() == PathfindCell::CELL_CLEAR && !c->getPinched())
			{
				cands.push_back({ x, y });
			}
		}
	}
	if (cands.size() < 2)
	{
		return out;
	}
	std::vector<Cand> anchors;
	auto addNearest = [&](int cx, int cy) {
		std::vector<Cand> v = cands;
		std::sort(v.begin(), v.end(), [&](const Cand &a, const Cand &b) {
			const int da = std::abs(a.x - cx) + std::abs(a.y - cy), db = std::abs(b.x - cx) + std::abs(b.y - cy);
			return da != db ? da < db : (a.x != b.x ? a.x < b.x : a.y < b.y);
		});
		for (size_t i = 0; i < 5 && i < v.size(); ++i) anchors.push_back(v[i]);
	};
	addNearest(lo.x, lo.y);
	addNearest(hi.x, hi.y);
	addNearest(lo.x, hi.y);
	addNearest(hi.x, lo.y);
	addNearest(lo.x, (lo.y + hi.y) / 2);
	addNearest(hi.x, (lo.y + hi.y) / 2);
	addNearest((lo.x + hi.x) / 2, lo.y);
	addNearest((lo.x + hi.x) / 2, hi.y);
	PathfindLocomotorInfo loco;
	loco.validSurfaces = LOCOMOTORSURFACE_GROUND;
	int best = -1;
	Cand bf{ 0, 0 }, bt{ 0, 0 };
	for (size_t i = 0; i < anchors.size(); ++i)
	{
		for (size_t j = i + 1; j < anchors.size(); ++j)
		{
			Coord3D f, t;
			f.x = (float)anchors[i].x * 10.0f + 5.0f; f.y = (float)anchors[i].y * 10.0f + 5.0f; f.z = 0.0f;
			t.x = (float)anchors[j].x * 10.0f + 5.0f; t.y = (float)anchors[j].y * 10.0f + 5.0f; t.z = 0.0f;
			if (std::hypot(t.x - f.x, t.y - f.y) < 1500.0f || !pf.clientSafeQuickDoesPathExist(loco, &f, &t)) continue;
			bool partial = false;
			Path *p = pf.findPath(nullptr, loco, &f, &t, &partial);
			// the most turns first, then the longest way round
			int nodes = 0;
			float len = 0.0f;
			if (p)
			{
				for (const PathNode *n = p->getFirstNode(); n; n = n->getNextOptimized())
				{
					++nodes;
					if (n->getNextOptimized()) len += std::hypot(n->getNextOptimized()->getPosition()->x - n->getPosition()->x, n->getNextOptimized()->getPosition()->y - n->getPosition()->y);
				}
			}
			const int used = nodes * 100000 + (int)(len * 0.1f);
			if (p && !partial && used > best)
			{
				best = used;
				bf = anchors[i];
				bt = anchors[j];
			}
			delete p;
		}
	}
	if (best < 0)
	{
		return out;
	}
	out["ok"] = true;
	out["from"] = Vector2((float)bf.x * 10.0f + 5.0f, (float)bf.y * 10.0f + 5.0f);
	out["to"] = Vector2((float)bt.x * 10.0f + 5.0f, (float)bt.y * 10.0f + 5.0f);
	return out;
}

Dictionary PathfindView::find_path(const Vector2 &from, const Vector2 &to, const Dictionary &options)
{
	Dictionary out;
	Impl &I = *m_impl;
	out["found"] = false;
	if (!I.pathfinder)
	{
		return out;
	}
	PathfindLocomotorInfo loco;
	loco.validSurfaces = LOCOMOTORSURFACE_GROUND;
	if (optBool(options, "water", false)) loco.validSurfaces = LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_DEEP_WATER;
	if (optBool(options, "cliff", false)) loco.validSurfaces |= LOCOMOTORSURFACE_CLIFF;
	Coord3D f, t;
	f.x = from.x; f.y = from.y; f.z = I.terrain->getGroundHeight(f.x, f.y);
	t.x = to.x; t.y = to.y; t.z = I.terrain->getGroundHeight(t.x, t.y);
	const double t0 = nowMs();
	const int cellsBefore = I.pathfinder->cumulativeCellsAllocated();
	bool partial = false;
	Path *p = I.pathfinder->findPath(nullptr, loco, &f, &t, &partial);
	out["seconds"] = (nowMs() - t0) / 1000.0;
	out["cells_examined"] = I.pathfinder->cumulativeCellsAllocated() - cellsBefore;
	out["partial"] = partial;
	if (p)
	{
		PackedVector3Array opt, all;
		float length = 0.0f;
		const PathNode *prev = nullptr;
		for (const PathNode *n = p->getFirstNode(); n; n = n->getNextOptimized())
		{
			const Coord3D &pos = *n->getPosition();
			opt.push_back(toGodotPos(pos.x, pos.y, pos.z + 2.0f));
			if (prev)
			{
				length += std::sqrt((pos.x - prev->getPosition()->x) * (pos.x - prev->getPosition()->x) + (pos.y - prev->getPosition()->y) * (pos.y - prev->getPosition()->y));
			}
			prev = n;
		}
		for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
		{
			const Coord3D &pos = *n->getPosition();
			all.push_back(toGodotPos(pos.x, pos.y, pos.z + 1.5f));
		}
		out["found"] = true;
		out["optimized"] = opt;
		out["cells"] = all;
		out["length"] = length;
		delete p;
	}
	Array stops;
	for (const std::string &st : I.pathfinder->stops()) stops.push_back(toGodot(st));
	out["stops"] = stops;
	return out;
}

} // namespace godot
