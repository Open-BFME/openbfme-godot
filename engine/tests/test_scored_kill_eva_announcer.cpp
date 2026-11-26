// OpenBFME. GPL-3.0.
// ScoredKillEvaAnnouncer controller and ScoredKillTracker: thresholds, player masks, expiry and xfer (RW 0x8256E4 / 0x825995 / 0x961353).
// Synthetic Eva.ini and objects only; no retail files.

#include "doctest.h"
#include "ProdTestUtil.h"

#include "Common/INI.h"

#include <cstdint>
#include <string>
#include <vector>

#include "Common/Audio/AudioIni.h"
#include "Common/Audio/ScoredKillEvaAnnouncer.h"
#include "Common/XferSave.h"
#include "Common/XferLoad.h"

// RW 0x8256E4 (addMoreTrackersForNewPlayers) & RW 0x825995 (update) & RW 0x961353 (ScoredKillTracker::accepts):
// exercises CountOnlyKillsByLocalPlayer, CountOnlyKillsAgainstLocalPlayer, MinimumCountForAnnouncement,
// MaximumTimeForAnnouncementMS and xfer round-trip.
TEST_CASE("ScoredKillEvaAnnouncer: thresholds, player masks, expiry and xfer (RW 0x8256E4 / 0x825995)")
{
	const std::string evaText =
		"NewEvaEvent TestKillStreak\n"
		"  Priority = 5\n"
		"End\n"
		"ScoredKillEvaAnnouncer LocalSlayer\n"
		"  EvaEvent = TestKillStreak\n"
		"  CountOnlyKillsByLocalPlayer = Yes\n"
		"  CountOnlyKillsAgainstLocalPlayer = No\n"
		"  MinimumCountForAnnouncement = 2\n"
		"  MaximumTimeForAnnouncementMS = 1000\n"
		"  ObjectFilter = ALL\n"
		"End\n"
		"ScoredKillEvaAnnouncer LocalVictim\n"
		"  EvaEvent = TestKillStreak\n"
		"  CountOnlyKillsByLocalPlayer = No\n"
		"  CountOnlyKillsAgainstLocalPlayer = Yes\n"
		"  MinimumCountForAnnouncement = 1\n"
		"  MaximumTimeForAnnouncementMS = 1000\n"
		"  ObjectFilter = ALL\n"
		"End\n";

	AudioEventInfoStore audioStore;
	EvaEventStore evaStore;
	INIEnvironment env;
	env.blocks.registerBlock("NewEvaEvent", [&](INI *ini) { evaStore.parseNew(ini, audioStore, false); });
	env.blocks.registerBlock("ScoredKillEvaAnnouncer", [&](INI *ini) { evaStore.parseScoredKillAnnouncer(ini); });
	INI ini(env);
	ini.loadMemory("eva.ini", std::vector<std::uint8_t>(evaText.begin(), evaText.end()), INI_LOAD_OVERWRITE);

	REQUIRE(evaStore.announcers().size() == 2);
	const int eventIdx = evaStore.findIndex("TestKillStreak", false);
	REQUIRE(eventIdx == EvaEventStore::kPredefinedCount);

	prodtest::ProdWorld w;
	w.w.load(
		"Object ScoredKillTestUnit\n"
		"  KindOf = INFANTRY\n"
		"End\n");

	Player *p0 = w.logic->players().getNthPlayer(0);
	Player *p1 = w.logic->players().getNthPlayer(1);
	REQUIRE(p0 != nullptr);
	REQUIRE(p1 != nullptr);

	Object *victimOfP0 = w.make("ScoredKillTestUnit", p1->getDefaultTeam());
	Object *victimOfP1 = w.make("ScoredKillTestUnit", p0->getDefaultTeam());
	REQUIRE(victimOfP0 != nullptr);
	REQUIRE(victimOfP1 != nullptr);
	const Coord3D p0Pos{ 10.0f, 20.0f, 0.0f };
	victimOfP0->setPosition(&p0Pos);
	const Coord3D p1Pos{ 30.0f, 40.0f, 0.0f };
	victimOfP1->setPosition(&p1Pos);

	ScoredKillEvaAnnouncerController ctrl;
	ctrl.loadFromStore(evaStore);
	ctrl.setLocalPlayerIndex(0);

	// Initialize trackers via first update (RW 0x825995)
	auto fired0 = ctrl.update(*w.logic, 0);
	CHECK(fired0.empty());

	// 1st kill by local player (p0 kills p1's unit): below threshold 2 for LocalSlayer, not against local player
	ctrl.onObjectKilled(*w.logic, 0, *victimOfP0, 0);
	auto fired1 = ctrl.update(*w.logic, 0);
	CHECK(fired1.empty());

	// 2nd kill by local player within 1000ms window: meets MinimumCountForAnnouncement=2 -> fires TestKillStreak
	ctrl.onObjectKilled(*w.logic, 0, *victimOfP0, 0);
	int reportedEvent = -1;
	Coord3D reportedPos{};
	auto fired2 = ctrl.update(*w.logic, 0, [&](int ev, const Coord3D *pos) {
		reportedEvent = ev;
		if (pos) reportedPos = *pos;
	});
	REQUIRE(fired2.size() == 1);
	CHECK(reportedEvent == eventIdx);
	CHECK(reportedPos.x == 10.0f);
	CHECK(reportedPos.y == 20.0f);

	// Save & load xfer round-trip preserves tracked kills
	XferSave xs;
	ctrl.xfer(xs);
	ScoredKillEvaAnnouncerController restored;
	restored.loadFromStore(evaStore);
	XferLoad xl(xs.data());
	restored.xfer(xl);
	CHECK(restored.announcers()[0].trackers.size() == 1);
	CHECK(restored.announcers()[0].trackers[0].count() == 2);

	// Advance past MaximumTimeForAnnouncementMS (1000ms = 5 frames at 5fps): kills expire
	for (int i = 0; i < 10; ++i) w.logic->update(1);
	auto firedAfterExpiry = ctrl.update(*w.logic, 0);
	CHECK(firedAfterExpiry.empty());

	// Kill against local player (p1 kills p0's unit): triggers LocalVictim (MinimumCountForAnnouncement=1)
	ctrl.onObjectKilled(*w.logic, 1, *victimOfP1, 0);
	auto firedVictim = ctrl.update(*w.logic, 0);
	REQUIRE(firedVictim.size() == 1);
	CHECK(firedVictim[0].evaEvent == eventIdx);
	CHECK(firedVictim[0].position.x == 30.0f);
}
