// OpenBFME. SMOOTH-1 review r4: the process-wide state the logic worker reaches beside the client, for the race detector (tools/smooth/
// sanitize_threaded.sh builds this file under TSan and ASan; without a sanitizer it checks the behaviour).
//
//   * the audio bindings (AudioEntryPoints.cpp): a worker's requests while the owner installs, replaces, removes and destroys the manager and the unit voice
//     handler; a queued request belongs to the registration it was made under and is dropped once that registration is gone (review r4 fix 1);
//   * the missing-audio / missing-voice / missing-Eva diagnostics bumped by worker requests while a client report reads them (fix 2);
//   * StancesBehavior's process-wide statistics bumped by two logic owners of two independent worlds (fix 3).
// Sol's review r4 probes (build-review-smooth1/r4-audio-races.cpp, r4-stances-race.cpp) in fixed form.
#include "doctest.h"
#include "IniTestUtil.h"
#include "LogicTestUtil.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "GameClient/LiveGameAudio.h"
#include "GameLogic/Module/StancesBehavior.h"

#include <atomic>
#include <memory>
#include <thread>

namespace
{
// a manager with one event "Click" (the event table isValidEvent reads)
struct MiniAudio
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	MiniAudio()
	{
		fx.mount({ { "data\\ini\\none.txt", "x" } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini",
			"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  SoundsExtension = wav\n  SampleCount2D = 2\n  SampleCount3D = 3\n"
			"  StreamCount = 3\nEnd\nAudioEvent Click\n  Sounds = click\n  Type = ui world everyone\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 1024u * 1024u);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 1u);
	}
};
// The test's own accesses to the thread-local store slot, kept out of line. Inlined into the test bodies, the statically linked test executable's linker
// relaxes the TLS access (initial exec -> local exec) from a flag-setting `add %fs:0, reg` into `mov %fs:0, reg; lea off(reg), reg`, and GCC's UBSan null
// check that follows branches on the flags of the instruction before it: a false "store to null pointer" (seen in the ASan / UBSan run of
// tools/smooth/sanitize_threaded.sh; the objdump of smooth1_asan_tests shows the lea / je pair). The noinline accessors are fully instrumented and pass.
#if defined(_MSC_VER)
#define SMOOTH1_NOINLINE __declspec(noinline)
#else
#define SMOOTH1_NOINLINE __attribute__((noinline))
#endif
SMOOTH1_NOINLINE void setStanceStore(StanceTemplateStore *store)
{
	TheStanceTemplateStore = store;
}
SMOOTH1_NOINLINE StanceTemplateStore *stanceStore()
{
	return TheStanceTemplateStore;
}
} // namespace

TEST_CASE("smooth1 r4: the missing-audio / missing-voice diagnostics count every worker request while a client report reads them")
{
	AudioApi::install(nullptr);
	AudioApi::installUnitVoiceHandler(AudioApi::UnitVoiceHandler());
	const LiveGameAudio::ApiCounters before = LiveGameAudio::apiCounters();
	const std::uint64_t evaBefore = AudioApi::callsWithoutEva();
	constexpr int kRequests = 20000;
	std::atomic<bool> go{ false }, done{ false };
	std::thread worker([&] {
		while (!go.load())
		{
		}
		for (int i = 0; i < kRequests; ++i)
		{
			AudioApi::postWeaponFireSound("fire", 1, 0, (std::uint32_t)i);
			AudioApi::postUnitVoice(0x7DA, 1);
			AudioApi::reportEva("UnitLost", nullptr);
		}
		done = true;
	});
	go = true;
	unsigned long long reads = 0, seen = 0;
	while (!done.load())
	{
		// the client's report beside the worker's requests (relaxed atomic loads)
		const LiveGameAudio::ApiCounters c = LiveGameAudio::apiCounters();
		seen = c.callsWithoutAudio + c.unitVoicesWithoutHandler + AudioApi::callsWithoutEva();
		++reads;
	}
	worker.join();
	const LiveGameAudio::ApiCounters after = LiveGameAudio::apiCounters();
	MESSAGE("reports read beside the worker: " << reads << " (last total " << seen << ")");
	CHECK(after.callsWithoutAudio == before.callsWithoutAudio + kRequests);
	CHECK(after.unitVoicesWithoutHandler == before.unitVoicesWithoutHandler + kRequests);
	CHECK(AudioApi::callsWithoutEva() == evaBefore + kRequests);
}

TEST_CASE("smooth1 r4: the voice handler is removed and replaced while the worker posts voices; the worker never calls a handler, stale voices are dropped")
{
	AudioApi::install(nullptr);
	std::atomic<bool> done{ false };
	std::atomic<int> workerCalls{ 0 };
	struct Hud
	{
		int played = 0;
	};
	int played = 0;
	std::thread worker([&] {
		for (int i = 0; i < 20000; ++i)
		{
			AudioApi::postUnitVoice(0x7DA, (std::uint32_t)i);
		}
		done = true;
	});
	int owners[2];
	for (int round = 0; !done.load(); ++round)
	{
		// the HUD the handler captures is destroyed right after its removal (InGameHud's destructor order)
		auto hud = std::make_unique<Hud>();
		const std::thread::id main = std::this_thread::get_id();
		AudioApi::installUnitVoiceHandler([h = hud.get(), &workerCalls, main](int, std::uint32_t, std::uint32_t) {
			if (std::this_thread::get_id() != main)
			{
				++workerCalls;
			}
			++h->played;
		}, &owners[round & 1]);
		if (round % 3 == 0)
		{
			AudioApi::drainDeferred(); // what this registration queued plays on its HUD
		}
		AudioApi::uninstallUnitVoiceHandler(&owners[round & 1]);
		played += hud->played;
		hud.reset();
	}
	worker.join();
	const std::uint64_t staleBefore = AudioApi::staleDeferredDropped();
	AudioApi::drainDeferred(); // everything left was queued under a removed registration
	CHECK(workerCalls.load() == 0);
	CHECK(AudioApi::staleDeferredDropped() >= staleBefore);
	MESSAGE("voices played on their own HUD: " << played);
}

TEST_CASE("smooth1 r4: a queued voice or sound belongs to its registration: after a replacement the drain drops it")
{
	AudioApi::install(nullptr);
	unsigned first = 0, second = 0;
	AudioApi::installUnitVoiceHandler([&](int, std::uint32_t, std::uint32_t) { ++first; }, &first);
	std::thread([] { AudioApi::postUnitVoice(0x7DA, 17); }).join();
	AudioApi::uninstallUnitVoiceHandler(&first);
	AudioApi::installUnitVoiceHandler([&](int, std::uint32_t, std::uint32_t) { ++second; }, &second);
	const std::uint64_t stale0 = AudioApi::staleDeferredDropped();
	AudioApi::drainDeferred();
	CHECK(first == 0);
	CHECK(second == 0); // review r4: the replacement's game never hears the old game's object 17
	CHECK(AudioApi::staleDeferredDropped() == stale0 + 1);

	// the same registration still gets its own queued voice
	std::thread([] { AudioApi::postUnitVoice(0x7DA, 18); }).join();
	AudioApi::drainDeferred();
	CHECK(second == 1);
	AudioApi::uninstallUnitVoiceHandler(&second);

	// a sound queued for manager A is not played by manager B
	MiniAudio a, b;
	AudioApi::install(a.mgr.get());
	std::thread([] { AudioApi::playSoundForObject("Click", 5); }).join();
	AudioApi::install(b.mgr.get());
	const std::uint64_t stale1 = AudioApi::staleDeferredDropped();
	AudioApi::drainDeferred();
	CHECK(AudioApi::staleDeferredDropped() == stale1 + 1);
	AudioApi::install(nullptr);
}

TEST_CASE("smooth1 r4: the worker's requests and event lookups while the owner installs, removes and destroys managers")
{
	AudioApi::install(nullptr);
	std::atomic<bool> done{ false };
	std::atomic<int> valid{ 0 };
	std::thread worker([&] {
		for (int i = 0; i < 4000; ++i)
		{
			if (AudioApi::isValidEvent("Click")) // Lua ObjectPlaySound's lookup on the worker
			{
				++valid;
			}
			AudioApi::playSoundForObject("Click", (std::uint32_t)i);
			AudioApi::postWeaponFireSound("Click", (std::uint32_t)i, 0, (std::uint32_t)i);
		}
		done = true;
	});
	int rounds = 0;
	while (!done.load())
	{
		auto m = std::make_unique<MiniAudio>();
		AudioApi::install(m->mgr.get());
		AudioApi::drainDeferred();
		AudioApi::install(nullptr); // GameAudio::shutdown's order: uninstall, then destroy
		m.reset();
		++rounds;
	}
	worker.join();
	AudioApi::drainDeferred();
	CHECK(rounds > 0);
	MESSAGE("manager rounds " << rounds << ", lookups that found the event " << valid.load());
}

TEST_CASE("smooth1 r4: StancesBehavior statistics of two logic owners (two independent worlds and stance stores)")
{
	logictest::LogicWorld a, b;
	StanceTemplateStore sa, sb;
	sa.registerBlock(a.w.fx.env.blocks);
	sb.registerBlock(b.w.fx.env.blocks);
	StanceTemplateStore *const saved = stanceStore();
	setStanceStore(&sa);
	REQUIRE(a.w.load("StanceTemplate X\nEnd\n").empty());
	setStanceStore(&sb);
	REQUIRE(b.w.load("StanceTemplate X\nEnd\n").empty());
	StancesBehavior::registerClass(a.w.modules);
	StancesBehavior::registerClass(b.w.modules);
	const char *objects = "Object Fighter\n  Behavior = StancesBehavior ModuleTag_Stance\n    StanceTemplate = X\n  End\nEnd\n";
	REQUIRE(a.w.load(objects).empty());
	REQUIRE(b.w.load(objects).empty());
	StancesBehavior *ma = StancesBehavior::of(*a.make("Fighter"));
	StancesBehavior *mb = StancesBehavior::of(*b.make("Fighter"));
	REQUIRE(ma);
	REQUIRE(mb);
	const unsigned long long before = StancesBehavior::stats().listenerNotices.load(std::memory_order_relaxed);
	constexpr int kSets = 10000;
	auto run = [](StanceTemplateStore *store, StancesBehavior *module) {
		setStanceStore(store); // this thread's slot (thread_local)
		for (int i = 0; i < kSets; ++i)
		{
			module->setStance(i % 2 ? STANCE_BATTLE : STANCE_AGGRESSIVE);
		}
		setStanceStore(nullptr);
	};
	std::thread ta(run, &sa, ma), tb(run, &sb, mb);
	ta.join();
	tb.join();
	setStanceStore(saved);
	// every set changes the stance class but possibly the first (the template's initial stance): no notice is lost
	const unsigned long long notices = StancesBehavior::stats().listenerNotices.load(std::memory_order_relaxed) - before;
	CHECK(notices >= 2ull * (kSets - 1));
	CHECK(notices <= 2ull * kSets);
}
