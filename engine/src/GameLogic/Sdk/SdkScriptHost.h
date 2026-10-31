// OpenBFME. GPL-3.0.
//
// SdkScriptHost (lane SDK-2): runs a mod's sim role Luau scripts (GameLogic/Sdk/LuauScriptState.h, SdkManifest.h) in a game, in lockstep.
// OpenBFME feature; retail's own Lua scripting (LiveScripting) is separate and unchanged.
//
// The scripts are the -mod folder's (its frozen loose files: ModLooseIndex). Their events, all delivered at the very end of a logic frame
// (GameLogic::addEndOfFrameHook), in this order:
//   onGameStart()                                  once, at the end of the first frame the host sees
//   onObjectCreated(id, template, player)          for each object that entered the world during the frame, in order
//   onObjectDestroyed(id, template, player)        for each object that left it, in order (player: the controlling player's index, -1 none)
//   onFrame(frame)                                 every frame
// A script defines the ones it wants. Events are queued where they happen and delivered at the frame's end, so a script never runs in the
// middle of an object's construction or destruction.
//
// The scripts' game API is the global table `game`: frame(), playerCount(), playerName(i), playerSide(i), money(i), addMoney(i, amount)
// (player indices as the engine's, 0 ..; amount a whole number 1 .. 1000000). math.random draws from the logic's synchronized generator.
//
// A script error is reported ("SDK <mod>: ...", reports()) and stops that mod's scripts from then on, at the same frame on every peer (the
// inputs are the same). A broken manifest or a script that fails to load stops the game from starting (start returns false). The calls
// made and the VM steps they took join the logic's state hash (a contributor), so a peer that ran its scripts differently is a desync.
//
// lane SDK-2b, saves: the block obfme:SdkScripts keeps the mod's "sdk sim <hex>" line, the calls / steps / stopped / started the state hash
// holds, and sdk.state's canonical bytes (LuauScriptState::serializeState, taken at the end of every frame and joining the state hash). A load
// with other sim scripts is refused, naming both lines. After every block has loaded, the scripts start afresh: their top level runs again,
// sdk.state is restored, and the next frame's end delivers onGameLoaded() first (its calls and steps are not counted, so a loaded game that
// only rebuilds its own variables there hashes as the uninterrupted one). Nothing else of a script survives a save. Because a top level reruns
// on a load, it may not change the game: game.addMoney and math.random there are an error, and so is a top level that changed sdk.state.
// sdk.state that cannot be serialized (a function in it, over 64 KB ...) is a script error at that frame's end, the same on every peer.
//
// Presentation scripts (the manifest's "presentation" entries) run in a state of their own after the sim scripts, with the same events but
// a read-only `game` (no addMoney; plus game.localPlayer(), the player this machine plays), their own math.random, and `ui.banner(text)`: a message shown in the middle of the screen
// (takeBanners(), drained by the client each frame; at most 8 per frame). They never join the state hash and never change the logic; an
// error stops only them (presentationReports()). A broken presentation script stops nothing else: it is reported when it is loaded.

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "GameLogic/Sdk/LuauScriptState.h"
#include "GameLogic/Sdk/SdkManifest.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class GameLogic;
class SdkModFiles;
class Xfer;

class SdkScriptHost
{
public:
	explicit SdkScriptHost(GameLogic &logic, SdkScriptLimits limits = SdkScriptLimits());
	~SdkScriptHost(); // removes its listeners from the logic
	SdkScriptHost(const SdkScriptHost &) = delete;
	SdkScriptHost &operator=(const SdkScriptHost &) = delete;

	// loads the mod's sim scripts (no openbfme/sdk/mod.json or no sim entry: inactive, true); false + error when the manifest or a script
	// cannot be loaded
	bool start(const ModLooseIndex &modFolder, std::string &error);
	bool active() const { return (bool)m_state; }
	bool stopped() const { return m_stopped; }
	const std::string &modName() const { return m_modName; }
	const std::vector<std::string> &reports() const { return m_reports; }
	std::uint64_t calls() const { return m_calls; }
	bool presentationActive() const { return (bool)m_presentation; }
	// the client side (any thread): banners and report lines since the last call
	std::vector<std::string> takeBanners();
	std::vector<std::string> takeNewReports();
	void pushBanner(const std::string &text); // ui.banner
	std::uint64_t steps() const { return m_steps; }

	// the game API's work (LuauScriptState's C functions; not for hosts)
	GameLogic &logic() { return m_logic; }
	// lane SDK-2b: "" when the sim scripts may change the game now (inside an event, outside a top level), else why not
	std::string actionRefusal() const;
	const std::string &simFingerprint() const { return m_fingerprint; } // "sdk sim <hex>" ("" without sim scripts)
	const std::string &stateBytes() const { return m_stateBytes; }      // sdk.state at the end of the last frame
	// the save block's work (obfme:SdkScripts; not for hosts): its xfer, and the scripts' restart after every block loaded (throws
	// XferException with the reason)
	void xferBlock(Xfer &x);
	void afterLoad();

private:
	struct Event
	{
		bool created;
		std::uint32_t id;
		std::string templateName;
		int player;
	};
	void endOfFrame(std::uint32_t frame);
	bool callIfDefined(const char *name, const std::function<int(lua_State *T)> &push, bool counted = true);
	bool buildSim(bool counted, std::string &error);
	void buildPresentation();
	void takeState(std::uint32_t frame);
	void presentationCall(const char *name, const std::function<int(lua_State *T)> &push);
	void report(const std::string &line);

	GameLogic &m_logic;
	SdkScriptLimits m_limits;
	std::unique_ptr<SdkModFiles> m_files;
	std::unique_ptr<LuauScriptState> m_state;
	std::unique_ptr<LuauScriptState> m_presentation;
	bool m_presentationStopped = false;
	size_t m_presentationPrinted = 0;
	unsigned m_bannersThisFrame = 0;
	std::mutex m_outboxMutex;
	std::vector<std::string> m_banners, m_newReports;
	static constexpr std::size_t kReportBytes = 1u << 20; // bytes of report lines kept (each with its string object), per queue
	std::size_t m_reportBytes = 0, m_newReportBytes = 0;
	bool m_reportsFull = false;
	std::string m_modName;
	SdkManifest m_manifest;
	std::string m_fingerprint, m_stateBytes;
	bool m_inEvent = false, m_loadedPending = false;
	struct Staged
	{
		std::uint64_t calls = 0, steps = 0;
		bool stopped = false, started = false;
		std::string state;
	} m_staged; // what the save block read, taken by afterLoad
	std::vector<Event> m_events;
	std::vector<std::string> m_reports;
	bool m_started = false;
	bool m_stopped = false;
	std::uint64_t m_calls = 0;
	std::uint64_t m_steps = 0;
	size_t m_printedSeen = 0;
	int m_worldHooks = 0, m_frameHook = 0, m_hashContributor = 0;
	class Snapshot *m_block = nullptr; // the save block's adapter (the registry owns it)
};
