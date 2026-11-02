// OpenBFME unit tests. GPL-3.0.
// Lane PERF-SNAP: the logic snapshot's records built on the logic job pool (GameClient/LogicSnapshot.cpp). The pool's build
// must equal the single-threaded one field for field: the big battle after 60 frames, a previous snapshot for lastMovedFrame, then one more frame
// and two builds of it, with 1 logic thread and with every hardware thread. Exact: every byte of every field of every record.

#include "doctest.h"
#include "HudTestUtil.h"
#include "PerfScenarios.h"
#include "RetailTestMount.h"

#include "Common/JobSystem.h"
#include "GameClient/LogicSnapshot.h"
#include "GameLogic/GameLogic.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
struct LogicThreadsGuard
{
	int before = JobSystem::logic().threadCount();
	~LogicThreadsGuard() { JobSystem::logic().setThreadCount(before); }
};

template <class T>
void put(std::vector<std::uint8_t> &out, const T &v)
{
	static_assert(std::is_trivially_copyable<T>::value, "a field's bytes");
	const size_t n = out.size();
	out.resize(n + sizeof(T));
	std::memcpy(out.data() + n, &v, sizeof(T));
}

// every field of a record as bytes (padding left out: the struct's bytes are not comparable, its fields are)
std::vector<std::uint8_t> bytesOf(const ObjectSnapshot &s)
{
	std::vector<std::uint8_t> b;
	put(b, s.id);
	put(b, s.tmpl);
	put(b, s.hasRecorded);
	put(b, s.recordedBasis);
	put(b, s.recordedAngle);
	put(b, s.recordedPos.x);
	put(b, s.recordedPos.y);
	put(b, s.recordedPos.z);
	put(b, s.basis);
	put(b, s.angle);
	put(b, s.position.x);
	put(b, s.position.y);
	put(b, s.position.z);
	put(b, s.previousPos.x);
	put(b, s.previousPos.y);
	put(b, s.previousPos.z);
	put(b, s.nextPos.x);
	put(b, s.nextPos.y);
	put(b, s.nextPos.z);
	put(b, s.hasNext);
	put(b, s.teleported);
	put(b, s.lastMovedFrame);
	put(b, s.instanceScale);
	put(b, s.moveSpeed);
	put(b, s.scriptTarget.hasObject);
	put(b, s.scriptTarget.ownPosition.x);
	put(b, s.scriptTarget.ownPosition.y);
	put(b, s.scriptTarget.ownPosition.z);
	put(b, s.scriptTarget.ownAngle);
	put(b, s.scriptTarget.targetPosition.x);
	put(b, s.scriptTarget.targetPosition.y);
	put(b, s.scriptTarget.targetPosition.z);
	put(b, s.scriptTarget.targetExists);
	put(b, s.scriptTarget.targetKindOf);
	put(b, s.constructionPercent);
	put(b, s.buildFrames);
	put(b, s.radarBlip);
	put(b, s.ownerColor);
	put(b, s.ownerIndex);
	put(b, s.structure);
	put(b, s.radar.listed);
	put(b, s.radar.local);
	put(b, s.radar.priority);
	put(b, s.radar.shape);
	put(b, s.radar.color);
	put(b, s.radar.indicator);
	put(b, s.radar.boundingRadius);
	put(b, s.shroudedForLocal);
	put(b, s.objectShroud);
	put(b, s.shroudRecord);
	put(b, s.effectivelyDead);
	put(b, s.occlusion);
	put(b, s.stealthLook);
	put(b, s.drawableHidden);
	put(b, s.phantomStructure);
	put(b, s.containedBy);
	put(b, s.stealthOpacityMin);
	put(b, s.stealthOpacityMax);
	put(b, s.stealthCycleSeconds);
	put(b, s.stealthParent);
	put(b, s.projectile);
	put(b, s.projectileFireFrame);
	put(b, s.projectileLauncher);
	put(b, s.projectileEnd.x);
	put(b, s.projectileEnd.y);
	put(b, s.projectileEnd.z);
	put(b, s.projectileSegments);
	return b;
}
} // namespace

TEST_CASE("perf2: the snapshot's records built on the logic job pool equal the single-threaded build field for field (the big battle, 60 frames)")
{
	if (!retailtest::pureMount())
	{
		retailtest::printSkip("perf2 snapshot on the pool");
		return;
	}
	LogicThreadsGuard guard;
	hudtest::SharedWorld w;
	GameLogicSettings settings;
	std::string error;
	REQUIRE_MESSAGE(perf1::loadFreshWorld(w, settings, error), error);
	auto scope = w.world->enterContext();
	hudtest::Rig rig(w, "map mp fall back 4p", "FactionMen", "FactionMen"); // an allied computer Player_3 on the local player's team (team vision below)
	GameLogic &logic = rig.logic();
	perf1::setUpBigBattle(rig);
	for (int f = 0; f < 60; ++f)
	{
		logic.runLogicFrame();
	}
	logic.shroud()->setDisplayed(true); // the shroud reads of the records (clientObjectStatus, hasRecord) run only while the shroud is displayed
	JobSystem::logic().setThreadCount(1);
	const std::shared_ptr<const LogicSnapshot> before = LogicSnapshot::build(logic, nullptr, false, 0);
	logic.runLogicFrame();
	const std::shared_ptr<const LogicSnapshot> serial = LogicSnapshot::build(logic, before.get(), false, 0);
	// the local player observed with its allies' vision (player 0 is the neutral player, which has no ally: its team vision is its own)
	const int observed = rig.local->getPlayerIndex();
	const std::shared_ptr<const LogicSnapshot> serialTeam = LogicSnapshot::build(logic, before.get(), false, 0, observed, true);
	const int threads = std::max(2, JobSystem::defaultThreadCount());
	JobSystem::logic().setThreadCount(threads);
	const JobSystem::Stats s0 = JobSystem::logic().stats();
	const std::shared_ptr<const LogicSnapshot> parallel = LogicSnapshot::build(logic, before.get(), false, 0);
	const JobSystem::Stats s1 = JobSystem::logic().stats();
	const std::shared_ptr<const LogicSnapshot> parallelTeam = LogicSnapshot::build(logic, before.get(), false, 0, observed, true);
	INFO(threads << " logic threads");
	CHECK(s1.dispatches - s0.dispatches == 2u); // the record pass and the lastMovedFrame pass, both on the pool
	CHECK(s1.inlineDispatches == s0.inlineDispatches);
	REQUIRE(serial->objects.size() == parallel->objects.size());
	REQUIRE(serial->objects.size() > 1000u); // tolerance: the big battle has thousands of objects; the exact count is the scenario's
	CHECK(serial->frame == parallel->frame);
	CHECK(serial->nextObjectId == parallel->nextObjectId);
	CHECK(serial->idToIndex == parallel->idToIndex);
	size_t moved = 0, shrouded = 0;
	for (size_t i = 0; i < serial->objects.size(); ++i)
	{
		const ObjectSnapshot &a = serial->objects[i];
		const ObjectSnapshot &b = parallel->objects[i];
		if (bytesOf(a) != bytesOf(b))
		{
			FAIL("record " << i << " (object " << a.id << ", " << (a.tmpl ? a.tmpl->getName() : std::string("?")) << ") differs between the builds");
		}
		moved += a.lastMovedFrame == serial->frame ? 1u : 0u;
		shrouded += a.shroudedForLocal ? 1u : 0u;
	}
	CHECK(moved > 0u);                          // tolerance: the battle marches, so some records moved this frame ...
	CHECK(moved < serial->objects.size());      // tolerance: ... and the structures did not (lastMovedFrame reads the previous snapshot)
	CHECK(shrouded > 0u);                       // tolerance: the far side of the battle is in the local player's shroud, so the shroud reads ran
	REQUIRE(serialTeam->shroud != nullptr);
	CHECK(serialTeam->shroud->visionPlayers.size() == 2u); // the observed player and its ally: the allies' loops of the record pass ran
	CHECK(parallelTeam->shroud->visionPlayers == serialTeam->shroud->visionPlayers);
	REQUIRE(serialTeam->objects.size() == parallelTeam->objects.size());
	for (size_t i = 0; i < serialTeam->objects.size(); ++i)
	{
		if (bytesOf(serialTeam->objects[i]) != bytesOf(parallelTeam->objects[i]))
		{
			FAIL("record " << i << " differs between the builds with team vision");
		}
	}
	// the same frame built on the pool twice gives the same records (no dependence on which thread filled a slot)
	const std::shared_ptr<const LogicSnapshot> again = LogicSnapshot::build(logic, before.get(), false, 0);
	for (size_t i = 0; i < serial->objects.size(); ++i)
	{
		if (bytesOf(serial->objects[i]) != bytesOf(again->objects[i]))
		{
			FAIL("record " << i << " differs between two builds on the pool");
		}
	}
}
