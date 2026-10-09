// OpenBFME. SMOOTH-1 review r1 (item 3, with the PHYS-1 review's step 6): the posed meshes of fighting horde members against their logic footprints.
//
// Retail does not equate a mesh's outline with its pathing shape. This measures, in a live fight of two hordes per side (melee and archers), for every
// horde member drawable: the skinned vertices of its posed model (the draw module's pose of the moment: HTreeClass::Anim_Pose / Blend_Pose of the
// frame's motions, then Deform_Position - the CPU reference of the instancer's GPU skinning - times the drawable's instance scale) against the
// object's footprint (ObjectGeometry shape 0's major radius). Reported per member template: the footprint radius, the largest horizontal reach of
// the posed mesh from the object's position, and the share of vertices inside the footprint circle. Nothing is expected of the numbers (no retail
// capture exists); the test pins that every member template was measured, so the comparison stays available.
#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/LiveGame.h"
#include "Common/Player.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"

#include <cmath>
#include <map>
#include <string>

namespace
{
struct Reach
{
	float footprint = 0.0f;
	float scale = 1.0f;
	float maxReach = 0.0f;
	double sumReach = 0.0;
	size_t instances = 0;
	size_t vertices = 0, inside = 0;
};
} // namespace

TEST_CASE("smooth1 retail: the posed meshes of fighting horde members against their footprints (measured, PHYS-1 review step 6)")
{
	if (!hudtest::haveWorld("smooth1 footprints"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 77;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	const Coord3D centre{ 1400.0f, 1400.0f, 0.0f };
	// the two lobby players by name (review r2: index 0 is the neutral player), opponents
	Player *pa = game.players().findPlayerWithName("Player_1");
	Player *pb = game.players().findPlayerWithName("Player_2");
	REQUIRE(pa != nullptr);
	REQUIRE(pb != nullptr);
	REQUIRE(pa->getRelationship(pb) == ENEMIES);
	const int p0 = pa->getPlayerIndex(), p1 = pb->getPlayerIndex();
	std::vector<ObjectID> ours, theirs;
	const char *mine[] = { "GondorFighterHorde", "GondorArcherHorde" };
	const char *enemy[] = { "MordorFighterHorde", "MordorArcherHorde" };
	for (int i = 0; i < 2; ++i)
	{
		Object *a = game.createObject(mine[i], p0, Coord3D{ centre.x - 200.0f, centre.y + 150.0f * (float)i, 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(a, err);
		ours.push_back(a->getID());
		Object *b = game.createObject(enemy[i], p1, Coord3D{ centre.x + 200.0f, centre.y + 150.0f * (float)i, 0.0f }, 3.14159f, &err);
		REQUIRE_MESSAGE(b, err);
		theirs.push_back(b->getID());
	}
	auto order = [&](int player, const std::vector<ObjectID> &ids, const Coord3D &to) {
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
		sel.appendBooleanArgument(true);
		for (ObjectID id : ids)
		{
			sel.appendObjectIDArgument(id);
		}
		game.commands().append(sel);
		GameMessage mv(MSG_DO_MOVETO, player);
		mv.appendLocationArgument(to);
		game.commands().append(mv);
	};
	order(p0, ours, Coord3D{ centre.x + 200.0f, centre.y + 75.0f, 0.0f }); // through each other's place: they meet and fight
	order(p1, theirs, Coord3D{ centre.x - 200.0f, centre.y + 75.0f, 0.0f });
	std::map<const MeshModelClass *, MeshRenderData> meshes;
	std::map<std::string, Reach> byTemplate;
	for (int f = 0; f < 100; ++f)
	{
		game.advance(0.2);
		if (f % 10 != 9)
		{
			continue;
		}
		// sample: every horde member drawable, its pose of this render frame
		for (Object *obj = game.logic().getFirstObject(); obj; obj = obj->getNextObject())
		{
			if (!obj->getContainedBy())
			{
				continue;
			}
			const Drawable *d = game.drawables().findByObject(obj->getID());
			const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*obj->getTemplate());
			if (!d || shapes.empty())
			{
				continue;
			}
			Reach &r = byTemplate[obj->getTemplate()->getName()];
			r.footprint = shapes[0].majorRadius;
			r.scale = d->getInstanceScale();
			float reach = 0.0f;
			for (const DrawEntry &e : d->entries())
			{
				if (!e.draw || e.moduleHidden)
				{
					continue;
				}
				const W3DDrawFrame fr = e.draw->frame();
				if (!fr.model || !fr.model->Tree)
				{
					continue;
				}
				const HTreeClass &tree = *fr.model->Tree;
				HTreePose pose;
				pose.Resize(tree.Num_Pivots());
				if (fr.motion0 && fr.blending && fr.motion1)
				{
					tree.Blend_Pose(Matrix3D(), fr.motion0, fr.frame0, fr.motion1, fr.frame1, fr.blendPercentage, pose);
				}
				else if (fr.motion0)
				{
					tree.Anim_Pose(Matrix3D(), fr.motion0, fr.frame0, pose);
				}
				else
				{
					tree.Base_Pose(Matrix3D(), pose);
				}
				for (const RenderSubObject &sub : fr.model->SubObjects)
				{
					if (sub.Type != RenderSubObject::SUB_MESH || !sub.Mesh)
					{
						continue;
					}
					auto it = meshes.find(sub.Mesh);
					if (it == meshes.end())
					{
						MeshRenderData data;
						std::string merr;
						if (!Build_Mesh_Render_Data(*sub.Mesh, data, &merr))
						{
							continue;
						}
						it = meshes.emplace(sub.Mesh, std::move(data)).first;
					}
					const MeshRenderData &m = it->second;
					if (m.Hidden)
					{
						continue;
					}
					for (size_t v = 0; v < m.NumVertices; ++v)
					{
						const Vector3 p = Deform_Position(m, v, pose, m.Skin ? -1 : sub.BoneIndex);
						const float rr = std::sqrt(p.X * p.X + p.Y * p.Y) * r.scale;
						reach = std::max(reach, rr);
						++r.vertices;
						r.inside += rr <= r.footprint ? 1 : 0;
					}
				}
			}
			r.maxReach = std::max(r.maxReach, reach);
			r.sumReach += reach;
			++r.instances;
		}
	}
	REQUIRE(byTemplate.size() >= 4); // the members of the four hordes (each faction's fighter and archer)
	CHECK(game.logic().combat().counters().damageApplications > 0); // the two sides fought (the poses sampled are fight poses)
	for (const auto &kv : byTemplate)
	{
		const Reach &r = kv.second;
		CAPTURE(kv.first);
		CHECK(r.footprint > 0.0f);
		CHECK(r.instances > 0);
		CHECK(r.vertices > 0);
		std::printf("  info: %-24s footprint radius %5.1f, scale %.2f: posed mesh reach mean %5.1f max %5.1f (%.2fx the footprint), %4.1f%% of vertices inside the footprint (%zu samples)\n",
			kv.first.c_str(), r.footprint, r.scale, r.sumReach / (double)r.instances, r.maxReach, r.maxReach / r.footprint, 100.0 * (double)r.inside / (double)r.vertices,
			r.instances);
	}
}
