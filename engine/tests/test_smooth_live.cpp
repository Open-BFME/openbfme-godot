// OpenBFME. SMOOTH-1 review r2 (S-810): the client's per-render-frame work while the logic worker runs a frame, as the device layer does it, for the
// race detector (build the core and this test with -fsanitize=thread: docs in the test case; without TSan it checks the behaviour).
//
// A live skirmish fight on its worker (twenty hordes, melee and archers: damage and projectile launches) with the render side doing, between and during
// worker frames and WITHOUT waiting for the worker: the radar's blips from the presented snapshot (the Palantir draw callback), the tactical camera's
// 30 Hz frames following a moving horde through the snapshot object source (and losing it when the horde dies), the worker timing diagnostic, the
// drawables' presentation (advance), and the HUD's input update only when the logic is idle (InGameHud::update's rule). Creation and destruction churn
// happen in between, from the main thread, through the waiting accessors.
// Sol's review r2 probe (build-review-smooth1/r2-live-race.cpp) read the live objects here and raced the worker; this is its fixed form, kept in the suite.
#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "GameClient/FXPlayback.h"
#include "GameClient/LiveFX.h"
#include "GameClient/Radar.h"
#include "GameClient/TacticalCamera.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"

#include <atomic>
#include <chrono>
#include <thread>

TEST_CASE("smooth1 retail: the render side's per-frame work (radar, camera follow, timing, idle-gated HUD update) runs beside the logic worker")
{
	if (!hudtest::haveWorld("smooth1 live worker"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared());
	const int oursIndex = rig.local->getPlayerIndex();
	Player *enemyPlayer = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemyPlayer != nullptr);
	const int enemyIndex = enemyPlayer->getPlayerIndex();
	REQUIRE(oursIndex != enemyIndex);
	REQUIRE(rig.local->getRelationship(enemyPlayer->getDefaultTeam()) == ENEMIES);
	std::string err;
	std::vector<ObjectID> ours, enemies;
	for (int i = 0; i < 10; ++i)
	{
		Object *a = rig.game->createObject(i < 6 ? "GondorFighterHorde" : "GondorArcherHorde", oursIndex, Coord3D{ 1200.0f + 70.0f * (float)(i % 5), 1100.0f + 110.0f * (float)(i / 5), 0 }, 0, &err);
		REQUIRE_MESSAGE(a, err);
		ours.push_back(a->getID());
		Object *b = rig.game->createObject(i < 6 ? "MordorFighterHorde" : "MordorArcherHorde", enemyIndex, Coord3D{ 1200.0f + 70.0f * (float)(i % 5), 1600.0f + 110.0f * (float)(i / 5), 0 }, 3.14f, &err);
		REQUIRE_MESSAGE(b, err);
		enemies.push_back(b->getID());
	}
	auto order = [&](int player, const std::vector<ObjectID> &ids, float y) {
		GameMessage select(MSG_CREATE_SELECTED_GROUP, player);
		select.appendBooleanArgument(true);
		for (ObjectID id : ids)
		{
			select.appendObjectIDArgument(id);
		}
		rig.game->commands().append(select);
		GameMessage move(MSG_DO_MOVETO, player);
		move.appendLocationArgument(Coord3D{ 1400, y, 0 });
		rig.game->commands().append(move);
	};
	order(oursIndex, ours, 1650);
	order(enemyIndex, enemies, 1100);
	{
		// our archers attack the first enemy horde (projectile launches: their FX events, fire sounds)
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, oursIndex);
		sel.appendBooleanArgument(true);
		for (size_t i = 6; i < ours.size(); ++i)
		{
			sel.appendObjectIDArgument(ours[i]);
		}
		rig.game->commands().append(sel);
		GameMessage atk(MSG_DO_ATTACK_OBJECT, oursIndex);
		atk.appendObjectIDArgument(enemies.front());
		rig.game->commands().append(atk);
	}
	{
		// a sure projectile launcher (FX-2's trebuchet scenario): a Gondor trebuchet shelling a Mordor orc pit 450 units away
		Object *treb = rig.game->createObject("GondorTrebuchet", oursIndex, Coord3D{ 800.0f, 1400.0f, 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(treb != nullptr, err);
		Object *pit = rig.game->createObject("MordorOrcPit", enemyIndex, Coord3D{ 1250.0f, 1400.0f, 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(pit != nullptr, err);
		rig.game->advance(0.2);
		REQUIRE(treb->getAIUpdateInterface() != nullptr);
		REQUIRE(treb->getAIUpdateInterface()->aiAttackObject(pit, CMD_FROM_PLAYER));
	}
	// merge with FX-2 / VIS-1 / AUDIO-2: the live effect player (its logic events captured on the worker, played here at idle points), the shroud shown
	// (the snapshot's ShroudView: radar texels and status lookups beside the worker), the logic's audio calls deferred to the audio owner (this thread)
	FXPlayback playback(*rig.sh.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(playback.loadRetailData().empty());
	LiveFX fx(*rig.game, playback);
	rig.game->shroud().setDisplayed(true);
	const std::thread::id mainThread = std::this_thread::get_id();
	std::atomic<unsigned long long> voices{ 0 }, voicesOffThread{ 0 };
	AudioApi::installUnitVoiceHandler([&](int, std::uint32_t, std::uint32_t) {
		++voices;
		voicesOffThread += std::this_thread::get_id() != mainThread ? 1 : 0;
	}, &voices);
	const std::uint64_t deferredBefore = AudioApi::deferredCalls();
	unsigned long long shroudRebuilds = 0, shroudLookups = 0, fxFlushes = 0;
	unsigned long long lastShroud = ~0ull;
	Radar radar(rig.input->context());
	REQUIRE(radar.setupFromTerrain());
	CameraSettings settings;
	REQUIRE(CameraSettings::load(*rig.sh.mount->fs, settings, &err));
	TacticalCamera camera(settings);
	const LoadedMap &map = rig.game->map();
	camera.startMap(rig.game->logic(), map.heightMap, map.chunks.hasWorldInfo ? &map.chunks.worldInfo : nullptr, Coord3D{ 1400, 1400, 0 });
	LiveGame *game = rig.game.get();
	camera.setObjectSource([game](ObjectID id, Coord3D &pos) { return game->presentedObjectPosition(id, pos); }); // as GodotInGameHud installs it
	const ObjectID followed = enemies.back(); // destroyed at frame 35: the camera loses it
	camera.setCameraLock(followed);
	rig.input->attachCamera(camera);
	rig.game->setLogicThread(true);
	REQUIRE(rig.game->logicThread());
	unsigned long long samples = 0, blips = 0, hudUpdates = 0, cameraFrames = 0, lockedFrames = 0;
	double timing = 0.0;
	bool lostLock = false;
	for (int frame = 0; frame < 70; ++frame)
	{
		if (frame == 35)
		{
			REQUIRE(rig.game->createObject("GondorFighterHorde", oursIndex, Coord3D{ 1300, 1300, 0 }, 0, &err)); // waits for the worker
			if (Object *victim = rig.game->logic().findObjectByID(followed))
			{
				rig.game->logic().destroyObject(victim);
			}
		}
		rig.game->advance(0.2); // requests the frame; the worker runs it; the drawables are posed from the presented snapshot
		const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(12);
		const auto cap = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		bool sawIdle = false; // render frames go on at least until the worker's frame completed (a loaded machine: no flaky idle-point counts)
		do
		{
			// the device layer's render-frame work, no wait for the worker
			const std::shared_ptr<const LogicSnapshot> snap = rig.game->presentedSnapshot();
			REQUIRE(snap.get() != nullptr);
			blips += radar.blips(*snap, 128, oursIndex).size();
			rig.input->cameraFrame((unsigned)(samples * 33));
			++cameraFrames;
			lockedFrames += camera.getCameraLock() == followed ? 1 : 0;
			timing += rig.game->lastWorkerFrameMs();
			if (const ShroudView *view = snap->shroud.get())
			{
				if (view->version != lastShroud)
				{
					lastShroud = view->version;
					shroudRebuilds += radar.shroudAlpha(*view).empty() ? 0 : 1; // the Palantir's ScrollShroud texels (HUD-2)
				}
				(void)view->statusAt(1400.0f, 1400.0f); // GameWorld's shroud texture reads the same view
				++shroudLookups;
			}
			if (rig.game->logicIdle())
			{
				rig.input->update(); // InGameHud::update's rule: the live HUD only while the worker is idle
				++hudUpdates;
				AudioApi::drainDeferred(); // GameWorld::advance's idle point: the logic's audio calls,
				fx.flushPending();         // the logic's effects,
				fx.updateAttachedSystems(); // the drawables' particle systems,
				playback.step();           // the particles
				++fxFlushes;
				sawIdle = true;
			}
			rig.game->advance(0.0); // a render frame with no new logic frame: presentation only
			std::atomic_signal_fence(std::memory_order_seq_cst);
			++samples;
		} while ((std::chrono::steady_clock::now() < until || !sawIdle) && std::chrono::steady_clock::now() < cap);
		lostLock = lostLock || (frame > 36 && camera.getCameraLock() != followed);
	}
	rig.game->waitIdle();
	AudioApi::drainDeferred();
	fx.flushPending();
	AudioApi::uninstallUnitVoiceHandler(&voices);
	const CombatState::Counters &c = rig.game->logic().combat().counters();
	const LiveFX::Stats &fs = fx.stats();
	unsigned long long played = 0;
	for (const auto &kv : fs.played)
	{
		played += kv.second;
	}
	std::printf("  info: smooth1 live (merge): %llu effects played (%zu sites), %llu attached systems, %llu shroud rebuilds, %llu shroud lookups, %llu fx flushes, %llu deferred audio calls, %llu voices (%llu off the main thread), %llu uncaptured filters\n",
		played, fs.played.size(), fs.attachedCreated, shroudRebuilds, shroudLookups, fxFlushes, (unsigned long long)(AudioApi::deferredCalls() - deferredBefore), voices.load(),
		voicesOffThread.load(), fs.uncapturedFilters);
	CHECK(played > 0);
	CHECK(fs.played.count("FireFX"));
	CHECK(shroudRebuilds > 0);
	CHECK(fxFlushes > 0);
	CHECK(voicesOffThread == 0);
	{
		// S-814: a logic-side audio call from another thread than the owner's (as the worker makes them: ProductionUpdate's VoiceCreated, fire sounds,
		// Lua object sounds) is queued, not run there; the owner runs it in order at its idle point
		AudioApi::installUnitVoiceHandler([&](int, std::uint32_t, std::uint32_t) {
			++voices;
			voicesOffThread += std::this_thread::get_id() != mainThread ? 1 : 0;
		}, &voices);
		const unsigned long long v0 = voices.load();
		const std::uint64_t d0 = AudioApi::deferredCalls();
		std::thread poster([&]() {
			AudioApi::postUnitVoice(0x7DA, (std::uint32_t)ours.front());
			AudioApi::postUnitVoice(0x7E2, (std::uint32_t)ours.front());
		});
		poster.join();
		CHECK(voices.load() == v0); // not run on the posting thread
		CHECK(AudioApi::deferredCalls() == d0 + 2);
		AudioApi::drainDeferred();
		CHECK(voices.load() == v0 + 2);
		CHECK(voicesOffThread == 0);
		AudioApi::uninstallUnitVoiceHandler(&voices);
	}
	CHECK(fs.uncapturedFilters == 0);
	std::printf("  info: smooth1 live: frame %u, %llu render samples, %llu blips, %llu camera frames (%llu locked), %llu idle HUD updates, damage %llu, kills %llu, launches %llu\n",
		rig.game->frame(), samples, blips, cameraFrames, lockedFrames, hudUpdates, c.damageApplications, c.kills, c.projectilesLaunched);
	CHECK(rig.game->frame() == 72);
	CHECK(blips > 0);
	CHECK(lockedFrames > 0);
	CHECK(lostLock); // the followed horde died: the camera lost its lock through the snapshot
	CHECK(hudUpdates > 0);
	CHECK(c.damageApplications > 0);
	CHECK(c.projectilesLaunched > 0);
	rig.game->setLogicThread(false);

	// review r3: the highest object id destroyed is GONE (the camera loses its lock), a new id above the snapshot is not published yet (the lock stays),
	// and an empty snapshot answers "not published yet" for every id, never "gone"
	{
		Coord3D p;
		const std::shared_ptr<const LogicSnapshot> snap = rig.game->presentedSnapshot();
		REQUIRE(snap.get() != nullptr);
		CHECK(snap->nextObjectId > snap->objects.back().id);
		CHECK(rig.game->presentedObjectPosition(snap->nextObjectId, p) == -1);
		CHECK(rig.game->presentedObjectPosition(snap->nextObjectId + 1000, p) == -1);
		const ObjectID highest = snap->objects.back().id;
		Object *top = rig.game->logic().findObjectByID(highest);
		REQUIRE(top != nullptr);
		CHECK(rig.game->presentedObjectPosition(highest, p) == 1);
		camera.setCameraLock(highest);
		rig.game->logic().destroyObject(top);
		rig.game->advance(0.2);
		rig.game->advance(0.2);
		rig.game->refreshClient(0.0, 1.0);
		CHECK(rig.game->presentedObjectPosition(highest, p) == 0);
		rig.input->cameraFrame(100000);
		CHECK(camera.getCameraLock() != highest); // it lost the lock, it does not stay locked to a dead highest id
	}
	{
		// an empty snapshot: nothing is published, nothing is gone
		LogicSnapshot empty;
		CHECK(empty.objects.empty());
		CHECK(empty.find(5) == nullptr);
		CHECK(empty.nextObjectId == 1);
	}
}
