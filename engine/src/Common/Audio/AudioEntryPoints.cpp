// OpenBFME. GPL-3.0. See AudioEntryPoints.h.

#include "Common/Audio/AudioEntryPoints.h"

#include "GameClient/Eva.h"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace AudioApi
{
namespace
{
// SMOOTH-1 review r4 fix 1: the bindings (the manager, the voice handler, their owner threads and registration generations) and the deferred queue are
// guarded by ONE lock, taken before any binding is read. A call from a thread that does not own the binding decides and enqueues under that lock and never
// touches the manager or the HUD's handler; the owner runs the queue outside the lock (playback can re-enter these entry points). Every queued request
// carries the generation of the registration it was made under; drainDeferred discards it once that registration was removed or replaced, so a
// replacement game never hears a request made for another game's object ids.
std::mutex g_bindMutex;
AudioManager *g_manager = nullptr;
std::thread::id g_managerOwner;
std::uint64_t g_managerGeneration = 0;
UnitVoiceHandler g_voice;
const void *g_voiceOwner = nullptr;
std::thread::id g_voiceThread;
std::uint64_t g_voiceGeneration = 0;
enum class Binding
{
	Manager,
	Voice
};
struct Deferred
{
	Binding binding;
	std::uint64_t generation;
	std::function<void()> call;
};
std::vector<Deferred> g_deferred;
std::uint64_t g_deferredCalls = 0;
std::uint64_t g_staleDropped = 0;
// review r4 fix 2: diagnostics a worker request can bump while a client report reads them (no simulation data is published through them)
std::atomic<std::uint64_t> g_without{ 0 };
std::atomic<std::uint64_t> g_withoutEva{ 0 };
std::atomic<std::uint64_t> g_withoutVoice{ 0 };
Eva *g_eva = nullptr;
struct FireLoop
{
	AudioHandle handle = 0;
	std::uint32_t stopFrame = 0;
};
std::map<std::uint32_t, FireLoop> g_fireLoops; ///< shooter object id -> its looping fire sound (RW FiringTracker + 0x54 / + 0x58); owner thread only
std::uint64_t g_firePosted = 0;                ///< owner thread only
std::map<std::uint32_t, AudioHandle> g_heldSounds[HELD_SOUND_SLOTS]; ///< AUDIO-3: per slot, holder object id -> its kept sound (postHeldSound); owner thread only

std::uint64_t generationOf(Binding b)
{
	return b == Binding::Manager ? g_managerGeneration : g_voiceGeneration;
}

// what a request does on the calling thread (decided under the binding lock)
enum class Route
{
	Run,       ///< the calling thread owns the binding: run it here (with the manager read under the lock)
	Deferred,  ///< queued for the owner, tagged with the registration's generation
	Unbound    ///< nothing installed: counted, nothing runs
};
// S-814: the logic's request from another thread than the binding's owner (the logic worker) is queued in call order and run on the owner at
// drainDeferred (a worker-idle point), never on the worker
Route route(Binding b, const std::function<void()> &call, AudioManager **manager = nullptr)
{
	std::lock_guard<std::mutex> lock(g_bindMutex);
	const bool bound = b == Binding::Manager ? g_manager != nullptr : static_cast<bool>(g_voice);
	if (!bound)
	{
		return Route::Unbound;
	}
	if (std::this_thread::get_id() == (b == Binding::Manager ? g_managerOwner : g_voiceThread))
	{
		if (manager)
		{
			*manager = g_manager;
		}
		return Route::Run;
	}
	// AUDIO-3: the request keeps the caller's log tag (the request log names the module that asked, not the drain)
	if (AudioLog::enabled())
	{
		g_deferred.push_back({ b, generationOf(b), [origin = AudioLog::origin(), call]() {
			AudioLog::Scope scope(origin);
			call();
		} });
	}
	else
	{
		g_deferred.push_back({ b, generationOf(b), call }); // AUDIO-3 r2: no tag captured while the log is off
	}
	++g_deferredCalls;
	return Route::Deferred;
}
} // namespace

void drainDeferred()
{
	std::vector<Deferred> calls;
	{
		std::lock_guard<std::mutex> lock(g_bindMutex);
		calls.swap(g_deferred);
	}
	for (Deferred &c : calls)
	{
		{
			// checked per call: a request run earlier in this drain may have replaced a binding (owner thread)
			std::lock_guard<std::mutex> lock(g_bindMutex);
			if (c.generation != generationOf(c.binding))
			{
				++g_staleDropped;
				continue;
			}
		}
		c.call();
	}
}

std::uint64_t deferredCalls()
{
	std::lock_guard<std::mutex> lock(g_bindMutex);
	return g_deferredCalls;
}

std::uint64_t staleDeferredDropped()
{
	std::lock_guard<std::mutex> lock(g_bindMutex);
	return g_staleDropped;
}

void install(AudioManager *manager)
{
	std::lock_guard<std::mutex> lock(g_bindMutex);
	g_manager = manager;
	g_managerOwner = std::this_thread::get_id();
	++g_managerGeneration; // requests queued for the previous manager are stale
	g_fireLoops.clear();   // handles of another manager mean nothing here
	for (auto &held : g_heldSounds)
	{
		held.clear();
	}
}

AudioManager *current()
{
	std::lock_guard<std::mutex> lock(g_bindMutex);
	return g_manager;
}

std::uint64_t callsWithoutAudio()
{
	return g_without.load(std::memory_order_relaxed);
}

namespace
{
// the manager of the calling thread's request, or null (counted) when none is installed. For the client's own calls on the manager's owner thread; the
// owner also installs / uninstalls it, so the pointer it reads stays valid for its call.
AudioManager *ownerManager()
{
	AudioManager *m = current();
	if (!m)
	{
		g_without.fetch_add(1, std::memory_order_relaxed);
	}
	return m;
}
} // namespace

#define REQUIRE_AUDIO(retval)                   \
	AudioManager *const mgr = ownerManager(); \
	if (!mgr)                                   \
	{                                           \
		return retval;                          \
	}

AudioHandle playUiSound(const std::string &n)
{
	REQUIRE_AUDIO(AHSV_Error)
	return mgr->playSound(n);
}

AudioHandle playSoundAtPosition(const std::string &n, const Coord3D &p)
{
	REQUIRE_AUDIO(AHSV_Error)
	return mgr->playSoundAt(n, p);
}

AudioHandle playSoundForObject(const std::string &n, std::uint32_t id, int player)
{
	AudioManager *mgr = nullptr;
	switch (route(Binding::Manager, [n, id, player]() { playSoundForObject(n, id, player); }, &mgr))
	{
	case Route::Run:
		return mgr->playSoundForObject(n, id, player);
	case Route::Deferred:
		return AHSV_Error; // S-814: played on the audio owner at the next idle point; no handle comes back to a logic-thread caller
	case Route::Unbound:
		break;
	}
	g_without.fetch_add(1, std::memory_order_relaxed);
	return AHSV_Error;
}

AudioHandle playSoundForDrawable(const std::string &n, std::uint32_t id, int player)
{
	AudioManager *mgr = nullptr;
	switch (route(Binding::Manager, [n, id, player]() { playSoundForDrawable(n, id, player); }, &mgr))
	{
	case Route::Run:
		return mgr->playSoundForDrawable(n, id, player);
	case Route::Deferred:
		return AHSV_Error; // S-814
	case Route::Unbound:
		break;
	}
	g_without.fetch_add(1, std::memory_order_relaxed);
	return AHSV_Error;
}

AudioHandle playMusic(const std::string &n)
{
	REQUIRE_AUDIO(AHSV_Error)
	return mgr->playMusic(n);
}

void stopMusic(bool fade)
{
	REQUIRE_AUDIO()
	mgr->stopMusic(fade);
}

void stopSound(AudioHandle h)
{
	REQUIRE_AUDIO()
	mgr->removeAudioEvent(h);
}

bool isValidEvent(const std::string &n)
{
	// review r4 fix 1: the lookup runs under the binding lock (the logic worker asks it: Lua ObjectPlaySound), so an uninstall cannot destroy the manager
	// during it; isValidAudioEvent only reads the parsed event table
	std::lock_guard<std::mutex> lock(g_bindMutex);
	if (!g_manager)
	{
		g_without.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	return g_manager->isValidAudioEvent(n);
}
void installEva(Eva *eva)
{
	std::lock_guard<std::mutex> lock(g_bindMutex);
	g_eva = eva;
}

Eva *currentEva()
{
	std::lock_guard<std::mutex> lock(g_bindMutex);
	return g_eva;
}

std::uint64_t callsWithoutEva()
{
	return g_withoutEva.load(std::memory_order_relaxed);
}

bool reportEva(const std::string &n, const Coord3D *position)
{
	Eva *eva = nullptr;
	AudioManager *mgr = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_bindMutex);
		eva = g_eva;
		mgr = g_manager;
	}
	if (!eva || !mgr)
	{
		g_withoutEva.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	// Eva runs on the audio manager's client clock (presentation time, never the logic clock)
	return eva->reportEventByName(n, position, mgr->nowMs());
}
void installUnitVoiceHandler(UnitVoiceHandler handler, const void *owner)
{
	UnitVoiceHandler old;
	{
		std::lock_guard<std::mutex> lock(g_bindMutex);
		old.swap(g_voice);
		g_voice = std::move(handler);
		g_voiceOwner = g_voice ? owner : nullptr;
		g_voiceThread = std::this_thread::get_id(); // SMOOTH-1: the HUD that plays the voices owns them (distinct from the manager's owner)
		++g_voiceGeneration;                        // voices queued for the previous handler are stale
	}
	// the replaced handler (and what it captured) is destroyed outside the lock
}

void uninstallUnitVoiceHandler(const void *owner)
{
	UnitVoiceHandler old;
	{
		std::lock_guard<std::mutex> lock(g_bindMutex);
		if (g_voiceOwner != owner)
		{
			return; // not this owner's registration any more
		}
		old.swap(g_voice);
		g_voiceOwner = nullptr;
		++g_voiceGeneration;
	}
	// once this returns no thread can reach the removed handler: the worker only queues (under the lock) and the queue is discarded by generation
}

void postUnitVoice(int voiceEvent, std::uint32_t objectId, std::uint32_t producerId)
{
	UnitVoiceHandler handler;
	{
		std::lock_guard<std::mutex> lock(g_bindMutex);
		if (!g_voice)
		{
			g_withoutVoice.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (std::this_thread::get_id() != g_voiceThread)
		{
			// S-814: the HUD's voice picker runs on the render side at the next idle point
			if (AudioLog::enabled())
			{
				g_deferred.push_back({ Binding::Voice, g_voiceGeneration, [origin = AudioLog::origin(), voiceEvent, objectId, producerId]() {
										  AudioLog::Scope scope(origin);
										  postUnitVoice(voiceEvent, objectId, producerId);
									  } });
			}
			else
			{
				g_deferred.push_back({ Binding::Voice, g_voiceGeneration, [voiceEvent, objectId, producerId]() { postUnitVoice(voiceEvent, objectId, producerId); } });
			}
			++g_deferredCalls;
			return;
		}
		handler = g_voice; // the owner thread: run a copy outside the lock (the picker can re-enter these entry points)
	}
	AudioLog::Scope scope([&] { return "logic voice message " + std::to_string(voiceEvent); });
	handler(voiceEvent, objectId, producerId);
}

std::uint64_t unitVoicesWithoutHandler()
{
	return g_withoutVoice.load(std::memory_order_relaxed);
}
void postWeaponFireSound(const std::string &n, std::uint32_t objectId, std::uint32_t loopFrames, std::uint32_t frame)
{
	AudioManager *mgr = nullptr;
	switch (route(Binding::Manager, [n, objectId, loopFrames, frame]() { postWeaponFireSound(n, objectId, loopFrames, frame); }, &mgr))
	{
	case Route::Run:
		break;
	case Route::Deferred:
		return; // S-814
	case Route::Unbound:
		g_without.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	++g_firePosted;
	AudioLog::Scope scope("weapon FireSound");
	if (loopFrames == 0)
	{
		mgr->playSoundForObject(n, objectId);
		return;
	}
	// RW 0x8E341E: re-added when no loop is pending or the sound stopped playing; the stop frame moves on with every shot
	FireLoop &l = g_fireLoops[objectId];
	if (l.stopFrame == 0 || !mgr->isCurrentlyPlaying(l.handle))
	{
		l.handle = mgr->playSoundForObject(n, objectId);
	}
	l.stopFrame = frame + loopFrames;
}

void updateWeaponFireSounds(std::uint32_t frame)
{
	AudioManager *mgr = current();
	for (auto it = g_fireLoops.begin(); it != g_fireLoops.end();)
	{
		if (frame >= it->second.stopFrame)
		{
			if (mgr)
			{
				mgr->removeAudioEvent(it->second.handle);
			}
			it = g_fireLoops.erase(it);
		}
		else
		{
			++it;
		}
	}
}

size_t loopingWeaponFireSounds()
{
	return g_fireLoops.size();
}

std::uint64_t weaponFireSoundsPosted()
{
	return g_firePosted;
}

void postHeldSound(int slot, const std::string &n, std::uint32_t holderId, std::uint32_t objectId)
{
	AudioManager *mgr = nullptr;
	switch (route(Binding::Manager, [slot, n, holderId, objectId]() { postHeldSound(slot, n, holderId, objectId); }, &mgr))
	{
	case Route::Run:
		break;
	case Route::Deferred:
		return; // S-814
	case Route::Unbound:
		g_without.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (slot < 0 || slot >= HELD_SOUND_SLOTS)
	{
		return;
	}
	// the handle kept from an earlier start is removed first (RW 0x8568B3 / 0x88C4C4 / 0x748CC3: when it is a real handle, > 4)
	auto &held = g_heldSounds[slot];
	auto it = held.find(holderId);
	if (it != held.end())
	{
		mgr->removeAudioEvent(it->second);
		held.erase(it);
	}
	static const char *const kTag[HELD_SOUND_SLOTS] = { "building loop", "move loop" };
	AudioLog::Scope scope(kTag[slot]);
	const AudioHandle h = mgr->playSoundForObject(n, objectId);
	if (h >= AHSV_FirstHandle)
	{
		held[holderId] = h;
	}
}

void stopHeldSound(int slot, std::uint32_t holderId)
{
	AudioManager *mgr = nullptr;
	switch (route(Binding::Manager, [slot, holderId]() { stopHeldSound(slot, holderId); }, &mgr))
	{
	case Route::Run:
		break;
	case Route::Deferred:
		return; // S-814
	case Route::Unbound:
		g_without.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (slot < 0 || slot >= HELD_SOUND_SLOTS)
	{
		return;
	}
	auto &held = g_heldSounds[slot];
	auto it = held.find(holderId);
	if (it != held.end())
	{
		mgr->removeAudioEvent(it->second);
		held.erase(it);
	}
}

std::uint32_t heldSound(int slot, std::uint32_t holderId)
{
	if (slot < 0 || slot >= HELD_SOUND_SLOTS)
	{
		return 0;
	}
	auto it = g_heldSounds[slot].find(holderId);
	return it == g_heldSounds[slot].end() ? 0 : it->second;
}

size_t heldSounds(int slot)
{
	return slot >= 0 && slot < HELD_SOUND_SLOTS ? g_heldSounds[slot].size() : 0;
}
} // namespace AudioApi
