// OpenBFME unit tests. GPL-3.0.
// Lane PERF-3: DrawableManager::syncTransforms runs each drawable's prepareSync (the interpolated pose and its trigonometry, the construction look) on the
// client job pool and commits the transforms in slot order. The client state it leaves must be the sequential syncFromSnapshot's: every drawable's
// position, angle, basis, change count, construction offsets and pose request, and the footstep manager's dirty / clean lists (their order decides which
// module updates first), in every render frame, with the pool at 1 thread and at N.
#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/JobSystem.h"
#include "GameClient/AnimationSoundClientBehavior.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameLogic/GameMessage.h"
#include "Libraries/WWVegas/WW3D2/w3dstops.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
struct Digest
{
	std::uint64_t h = 1469598103934665603ull;
	void bytes(const void *p, size_t n)
	{
		const unsigned char *b = static_cast<const unsigned char *>(p);
		for (size_t i = 0; i < n; ++i)
		{
			h = (h ^ b[i]) * 1099511628211ull;
		}
	}
	template <class T>
	void value(const T &v)
	{
		bytes(&v, sizeof(v));
	}
};

// every drawable's client state and the footstep lists after one render frame
std::uint64_t clientDigest(DrawableManager &dm, size_t &drawables)
{
	Digest d;
	std::map<const void *, std::uint32_t> moduleOwner;
	drawables = 0;
	for (size_t id = 1; id < dm.slotCount(); ++id)
	{
		const Drawable *dr = dm.find((DrawableID)id);
		if (!dr)
		{
			continue;
		}
		++drawables;
		d.value((std::uint32_t)id);
		d.bytes(dr->getPosition(), sizeof(Coord3D));
		d.value(dr->getOrientation());
		d.bytes(dr->getBasis(), 9 * sizeof(float));
		d.value(dr->getChangeCount());
		for (const DrawEntry &e : dr->entries())
		{
			d.value(e.constructionOffsetZ);
			if (e.draw)
			{
				W3DDrawPoseView v;
				e.draw->poseView(v);
				d.value(v.frame0);
				d.value(v.frame1);
				d.value(v.blendPercentage);
				d.bytes(v.clip0->data(), v.clip0->size());
			}
		}
		for (const std::unique_ptr<DrawableModule> &m : dr->clientModules())
		{
			moduleOwner[m.get()] = (std::uint32_t)id;
		}
	}
	for (bool dirty : { true, false })
	{
		d.value(dirty);
		for (const AnimationSoundClientBehavior *m : dm.animationSounds().listOrder(dirty))
		{
			auto it = moduleOwner.find(m);
			d.value(it == moduleOwner.end() ? 0xFFFFFFFFu : it->second);
		}
	}
	return d.h;
}

// a fight of hordes and archers (the same game every run): every render frame's digest
std::vector<std::uint64_t> runFight(bool parallel, int threads, size_t &maxDrawables)
{
	const int before = JobSystem::client().threadCount();
	JobSystem::client().setThreadCount(threads);
	std::vector<std::uint64_t> out;
	{
		hudtest::Rig rig(hudtest::shared());
		LiveGame &live = *rig.game;
		live.drawables().setParallelSync(parallel);
		live.drawables().setParallelSyncMinimum(0); // 64-slot chunks however few drawables the fight has
		const int ours = rig.local->getPlayerIndex();
		Player *enemy = live.players().findPlayerWithName("Player_2");
		REQUIRE(enemy != nullptr);
		const int theirs = enemy->getPlayerIndex();
		std::string err;
		std::vector<ObjectID> a, b;
		for (int i = 0; i < 8; ++i)
		{
			Object *x = live.createObject(i < 5 ? "GondorFighterHorde" : "GondorArcherHorde", ours, Coord3D{ 1200.0f + 80.0f * (float)(i % 4), 1100.0f + 110.0f * (float)(i / 4), 0 }, 0.0f, &err);
			REQUIRE_MESSAGE(x, err);
			a.push_back(x->getID());
			Object *y = live.createObject(i < 5 ? "MordorFighterHorde" : "MordorArcherHorde", theirs, Coord3D{ 1200.0f + 80.0f * (float)(i % 4), 1500.0f + 110.0f * (float)(i / 4), 0 }, 3.14f, &err);
			REQUIRE_MESSAGE(y, err);
			b.push_back(y->getID());
		}
		auto order = [&](int player, const std::vector<ObjectID> &ids, float y) {
			GameMessage select(MSG_CREATE_SELECTED_GROUP, player);
			select.appendBooleanArgument(true);
			for (ObjectID id : ids)
			{
				select.appendObjectIDArgument(id);
			}
			live.commands().append(select);
			GameMessage move(MSG_DO_ATTACKMOVETO, player);
			move.appendLocationArgument(Coord3D{ 1340.0f, y, 0 });
			live.commands().append(move);
		};
		order(ours, a, 1450.0f);
		order(theirs, b, 1150.0f);
		maxDrawables = 0;
		for (int f = 0; f < 90; ++f)
		{
			live.logic().runLogicFrame();
			// six client frames per logic frame, the sub-frame fraction 0, 1/6 .. 5/6 (test_build_render2_construction's client clock)
			for (int c = 0; c < 6; ++c)
			{
				live.refreshClient(1000.0 / 30.0, (double)c / 6.0);
				size_t n = 0;
				out.push_back(clientDigest(live.drawables(), n));
				maxDrawables = n > maxDrawables ? n : maxDrawables;
			}
		}
	}
	JobSystem::client().setThreadCount(before);
	return out;
}
} // namespace

TEST_CASE("perf3 client: the drawables' sync on the client job pool (1 and N threads) leaves every drawable and the footstep lists as the sequential syncFromSnapshot, every render frame")
{
	if (!hudtest::haveWorld("perf3 client sync"))
	{
		return;
	}
	size_t n0 = 0, n1 = 0, n2 = 0;
	const std::vector<std::uint64_t> reference = runFight(false, 1, n0);
	const int many = JobSystem::defaultThreadCount() > 1 ? JobSystem::defaultThreadCount() : 4;
	const std::vector<std::uint64_t> one = runFight(true, 1, n1);
	const std::vector<std::uint64_t> all = runFight(true, many, n2);
	INFO("drawables (most in a frame): " << n0 << "; render frames " << reference.size() << "; threads " << many);
	CHECK(n0 > 300); // the hordes' members, the archers' arrows
	REQUIRE(reference.size() == 540);
	REQUIRE(one.size() == reference.size());
	REQUIRE(all.size() == reference.size());
	size_t firstDiff1 = reference.size(), firstDiffN = reference.size();
	for (size_t i = 0; i < reference.size(); ++i)
	{
		if (one[i] != reference[i] && firstDiff1 == reference.size())
		{
			firstDiff1 = i;
		}
		if (all[i] != reference[i] && firstDiffN == reference.size())
		{
			firstDiffN = i;
		}
	}
	CHECK_MESSAGE(firstDiff1 == reference.size(), "1 thread: the first render frame that differs is " << firstDiff1);
	CHECK_MESSAGE(firstDiffN == reference.size(), many << " threads: the first render frame that differs is " << firstDiffN);
	CHECK(n1 == n0);
	CHECK(n2 == n0);
}

TEST_CASE("perf3: stop S-1730 (pose culling): the report the instancer makes when it leaves a pose pending, and the bound it tests")
{
	const W3DStopHit hit = W3D_Pose_Cull_Stop();
	CHECK(hit.Id == "S-1730");
	CHECK(hit.Message.rfind("[S-1730] poses of instances outside the camera's frustum are left pending", 0) == 0);
	CHECK(hit.Message.find("not retail's render-object bounds") != std::string::npos);
	// 1.5 * (the farthest bind-pose pivot + the largest mesh extent) + 30
	CHECK(W3D_Pose_Cull_Radius(10.0f, 5.0f) == 52.5f);
	CHECK(W3D_Pose_Cull_Radius(0.0f, 0.0f) == 30.0f);
}
