// OpenBFME unit tests: the stereo image of retail RotWK 2.01 (lane AUDIO-5). The channel gains of a voice follow the Miles Sound System
// RotWK ships (MSS 6.6g, MilesMix.h) and the listener is retail's microphone (RW recalculateMicrophone 0x45235B).
//
// The expected numbers come from tools/audio/miles_model.py, a separate implementation of the same binary facts (double precision); the
// camera is RotWK's default tactical framing (offset height 300 at 37.5 degrees, RW 0x5011D5) looking north at (1000, 1000, 0), the
// microphone the retail AudioSettings.ini values (MicrophonePreferredFractionCameraToGround 86%, Min 100, Max 300, Pull 60%).

#include "doctest.h"

#include "IniTestUtil.h"
#include "RetailTestMount.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioEventInfo.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/AudioSettings.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/MilesMix.h"
#include "Common/Audio/SimulatedAudioDevice.h"

#include <cmath>
#include <memory>

namespace
{
const Coord3D kCamera{ 1000.0f, 609.032388f, 300.0f };
const Coord3D kLookAt{ 1000.0f, 1000.0f, 0.0f };
const Coord3D kMic{ 1000.0f, 938.815356f, 117.371571f }; // miles_model.microphone(default_camera())
const Coord3D kNorth{ 0.0f, 1.0f, 0.0f };

void setRetailTacticalMicrophone(AudioSettings &s)
{
	MicrophoneSettings &m = s.microphone[0];
	m.preferredFractionCameraToGround = 0.86f;
	m.minDistanceToCamera = 100.0f;
	m.maxDistanceToCamera = 300.0f;
	m.pullTowardsTerrainLookAtPointPercent = 0.60f;
	m.preferredFractionCameraToGroundSq = m.preferredFractionCameraToGround * m.preferredFractionCameraToGround;
	m.minDistanceToCameraSq = m.minDistanceToCamera * m.minDistanceToCamera;
	m.maxDistanceToCameraSq = m.maxDistanceToCamera * m.maxDistanceToCamera;
	m.zoomMinDistance = 130.0f;
	m.zoomMaxDistance = 425.0f;
	m.zoomSoundVolumePercentageAmount = 0.20f;
	m.zoomMinDistanceSq = m.zoomMinDistance * m.zoomMinDistance;
	m.zoomMaxDistanceSq = m.zoomMaxDistance * m.zoomMaxDistance;
}

void checkGains(const MilesChannelGains &g, double left, double right)
{
	CHECK(g.left == doctest::Approx(left).epsilon(1e-4));
	CHECK(g.right == doctest::Approx(right).epsilon(1e-4));
}

struct ManagerRig
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;

	ManagerRig()
	{
		fx.mount({ { "data\\ini\\none.txt", "x" } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini",
			"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  SoundsExtension = wav\n  MinSampleVolume = 2%\n"
			"  GlobalMinRange = 5000\n  GlobalMaxRange = 5000000\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n  DefaultMusicVolume = 100%\n"
			"  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
			"AudioEvent Boom\n  Sounds = a\n  Volume = 100\n  MinRange = 100\n  MaxRange = 500\n  Type = world everyone\nEnd\n"
			"AudioEvent Click\n  Sounds = a\n  Volume = 100\n  Type = ui everyone\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
		setRetailTacticalMicrophone(ini.settings);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 1024u * 1024u);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 1);
	}
};
} // namespace

TEST_CASE("AUDIO-5: the microphone is retail's (RW 0x45235B): 300 from the camera towards the look-at, pulled 60% over it, facing the camera's heading")
{
	ManagerRig r;
	r.mgr->updateMicrophone(kCamera, kLookAt, true);
	const Coord3D &p = r.mgr->getListenerPosition();
	CHECK(p.x == doctest::Approx(kMic.x).epsilon(1e-5));
	CHECK(p.y == doctest::Approx(kMic.y).epsilon(1e-5));
	CHECK(p.z == doctest::Approx(kMic.z).epsilon(1e-5));
	CHECK(r.mgr->getListenerForward().x == doctest::Approx(0.0f));
	CHECK(r.mgr->getListenerForward().y == doctest::Approx(1.0f));
	CHECK(r.mgr->getListenerForward().z == 0.0f);

	// no terrain under the screen centre: no pull (the microphone stays on the camera ray)
	r.mgr->updateMicrophone(kCamera, kLookAt, false);
	CHECK(r.mgr->getListenerPosition().y == doctest::Approx(847.038f).epsilon(1e-5));

	// a camera closer than MicrophoneMinDistanceToCamera: the microphone is the look-at point itself (s = 1)
	r.mgr->updateMicrophone(Coord3D{ 1000.0f, 950.0f, 60.0f }, kLookAt, true);
	CHECK(r.mgr->getListenerPosition().x == doctest::Approx(1000.0f));
	CHECK(r.mgr->getListenerPosition().y == doctest::Approx(1000.0f));
	CHECK(r.mgr->getListenerPosition().z == doctest::Approx(0.0f));

	// a camera straight above the look-at keeps the last heading
	r.mgr->updateMicrophone(kCamera, kLookAt, true);
	r.mgr->updateMicrophone(Coord3D{ 1000.0f, 1000.0f, 400.0f }, kLookAt, true);
	CHECK(r.mgr->getListenerForward().y == doctest::Approx(1.0f));

	// the camera turned a quarter (looking west, eye east of the target): the face follows (lookAt - camera) horizontally
	r.mgr->updateMicrophone(Coord3D{ 1390.96761f, 1000.0f, 300.0f }, kLookAt, true);
	CHECK(r.mgr->getListenerForward().x == doctest::Approx(-1.0f));
	CHECK(r.mgr->getListenerForward().y == doctest::Approx(0.0f).epsilon(1e-6));
	CHECK(r.mgr->getListenerPosition().x == doctest::Approx(1061.184644f).epsilon(1e-5));
}

TEST_CASE("AUDIO-5: the zoom volume (RW 0x45264A -> 0x451946) at several camera heights matches the binary's formula; positional sounds take it")
{
	// RotWK's default framing (37.5 degrees) at offset heights 60 .. 700 looking at (1000, 1000, 0); expected values: miles_model.zoom_volume
	struct Row
	{
		float eyeY, eyeZ;
		double zoom;
	};
	const Row rows[] = {
		{ 921.806478f, 60.0f, 1.0 },      // camera within MicrophoneMinDistanceToCamera: the microphone is the look-at, |e| < ZoomMinDistance
		{ 804.516194f, 150.0f, 0.935488 },
		{ 674.193657f, 250.0f, 0.855393 },
		{ 609.032388f, 300.0f, 0.832560 }, // the default camera
		{ 413.548582f, 450.0f, 0.8 },      // beyond ZoomMaxDistance: 1 - ZoomSoundVolumePercentageAmount
		{ 87.742239f, 700.0f, 0.8 },
	};
	ManagerRig r;
	CHECK(r.mgr->getZoomVolume() == 1.0f); // before any microphone update
	for (const Row &row : rows)
	{
		r.mgr->updateMicrophone(Coord3D{ 1000.0f, row.eyeY, row.eyeZ }, kLookAt, true);
		CHECK_MESSAGE(r.mgr->getZoomVolume() == doctest::Approx(row.zoom).epsilon(1e-5), "camera height " << row.eyeZ);
	}

	// RW refreshPair 0x4516DE multiplies it into positional slider volume only: a positional sound at the microphone, a UI sound
	r.mgr->updateMicrophone(kCamera, kLookAt, true);
	AudioEventRTS world("Boom", kMic);
	world.setAudioEventInfo(r.ini.infos.find("Boom"));
	CHECK(r.mgr->getEffectiveVolume(world) == doctest::Approx(0.832560).epsilon(1e-5));
	AudioEventRTS click("Click");
	click.setAudioEventInfo(r.ini.infos.find("Click"));
	CHECK(r.mgr->getEffectiveVolume(click) == doctest::Approx(1.0f));

	// an amount of 0 leaves the multiplier as it was (RW 0x45196D)
	r.ini.settings.microphone[0].zoomSoundVolumePercentageAmount = 0.0f;
	r.mgr->updateMicrophone(Coord3D{ 1000.0f, 921.806478f, 60.0f }, kLookAt, true);
	CHECK(r.mgr->getZoomVolume() == doctest::Approx(0.832560).epsilon(1e-5));
}

TEST_CASE("AUDIO-5: Miles Fast 2D gains (msssoft 0x22401060) at fixed screen positions match the retail model")
{
	// full volume
	checkGains(milesFast2DGains(1.0f, kMic, kNorth, Coord3D{ 750, 1000, 0 }, 1000.0f), 0.845006, 0.154994);  // screen left, near
	checkGains(milesFast2DGains(1.0f, kMic, kNorth, Coord3D{ 1000, 1000, 0 }, 1000.0f), 0.5, 0.5);           // the look-at point
	checkGains(milesFast2DGains(1.0f, kMic, kNorth, Coord3D{ 1250, 1000, 0 }, 1000.0f), 0.154994, 0.845006); // screen right, near
	checkGains(milesFast2DGains(1.0f, kMic, kNorth, Coord3D{ 650, 1300, 0 }, 1000.0f), 0.737019, 0.262981);  // left, far up the screen
	checkGains(milesFast2DGains(1.0f, kMic, kNorth, Coord3D{ 1350, 1300, 0 }, 1000.0f), 0.262981, 0.737019); // right, far
	checkGains(milesFast2DGains(1.0f, kMic, kNorth, Coord3D{ 1000, 800, 0 }, 1000.0f), 0.375, 0.375);        // behind the microphone: x 0.75
	checkGains(milesFast2DGains(1.0f, kMic, kNorth, Coord3D{ 1400, kMic.y, kMic.z }, 1000.0f), 0.0, 1.0);    // due right at ear height: hard right
	// half volume: volume ^ (5/3) before the pan
	checkGains(milesFast2DGains(0.5f, kMic, kNorth, Coord3D{ 750, 1000, 0 }, 1000.0f), 0.266160, 0.048820);
	checkGains(milesFast2DGains(0.5f, kMic, kNorth, Coord3D{ 1000, 1000, 0 }, 1000.0f), 0.157490, 0.157490);
	checkGains(milesFast2DGains(0.5f, kMic, kNorth, Coord3D{ 1350, 1300, 0 }, 1000.0f), 0.082834, 0.232147);
	// beyond the maximum distance Miles mutes the sample; at it the sample still plays
	checkGains(milesFast2DGains(1.0f, kMic, kNorth, Coord3D{ 1000, kMic.y + 1000.5f, kMic.z }, 1000.0f), 0.0, 0.0);
	CHECK(milesFast2DGains(1.0f, kMic, kNorth, Coord3D{ 1000, kMic.y + 999.0f, kMic.z }, 1000.0f).left > 0.4f);
	// at the listener: centre
	checkGains(milesFast2DGains(1.0f, kMic, kNorth, kMic, 1000.0f), 0.5, 0.5);

	// the port before AUDIO-5 (miles_model.old_gains): listener on the ground at the target, forward (cos a, sin a) = the camera's right,
	// Godot's linear panner: screen left / right were centred (1, 1) and up / down the screen were hard left / hard right (2, 0) / (0, 2)
}

TEST_CASE("AUDIO-5: 2D samples and streams play at Miles' default pan: volume ^ (5/3) x 2^-0.3 per channel (mss32 0x2112AD60)")
{
	checkGains(milesSampleGains(1.0f), 0.8122522, 0.8122522);
	checkGains(milesSampleGains(0.7f), 0.4482507, 0.4482507);
	checkGains(milesSampleGains(0.7f, 0.25f), 0.5062307, 0.3640928);
	checkGains(milesSampleGains(0.0f), 0.0, 0.0);
}

TEST_CASE("AUDIO-5: Miles' maximum distance is the larger of MinRange and twice MaxRange (RW 0x45C0D8 through mss32 0x2111FE0D)")
{
	AudioSettings s;
	s.minSampleVolume = 0.02f;
	s.globalMaxRange = 5000000;
	AudioEventInfo info;
	info.minRange = 100.0f;
	info.maxRange = 500.0f;
	CHECK(milesMaxDistance(info, 0.0f, s) == 1000.0f);
	CHECK(milesMaxDistance(info, 0.02f, s) == 1000.0f); // MinVolume not above MinSampleVolume
	CHECK(milesMaxDistance(info, 0.2f, s) == 10000000.0f);
	info.type |= ST_GLOBAL;
	CHECK(milesMaxDistance(info, 0.0f, s) == 10000000.0f);
	info.type = 0;
	info.minRange = 5000.0f;
	CHECK(milesMaxDistance(info, 0.0f, s) == 5000.0f);
}

TEST_CASE("AUDIO-5 retail: the retail AudioSettings.ini microphone puts the listener where the model says")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set");
		return;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	INIEnvironment env;
	env.fileSystem = mount->fs.get();
	AudioIniState state;
	state.registerBlocks(env.blocks);
	INI ini(env);
	state.loadAll(ini);
	const MicrophoneSettings &m = state.settings.microphone[0];
	CHECK(m.preferredFractionCameraToGround == doctest::Approx(0.86f));
	CHECK(m.minDistanceToCamera == 100.0f);
	CHECK(m.maxDistanceToCamera == 300.0f);
	CHECK(m.pullTowardsTerrainLookAtPointPercent == doctest::Approx(0.60f));
	AudioAssetCache cache(mount->fs.get(), 1024u * 1024u);
	SimulatedAudioDevice device(&cache);
	AudioManager mgr(state, cache, device, RandomAlgorithm::ZH_CarryChain, 1);
	mgr.updateMicrophone(kCamera, kLookAt, true);
	CHECK(mgr.getListenerPosition().y == doctest::Approx(kMic.y).epsilon(1e-5));
	CHECK(mgr.getListenerPosition().z == doctest::Approx(kMic.z).epsilon(1e-5));
}
