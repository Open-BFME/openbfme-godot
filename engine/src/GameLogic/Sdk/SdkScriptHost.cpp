// OpenBFME. GPL-3.0. See GameLogic/Sdk/SdkScriptHost.h (lane SDK-2).

#include "GameLogic/Sdk/SdkScriptHost.h"

#include "Common/GameState.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Snapshot.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Sdk/SdkManifest.h"

#include "luau/lua.h"
#include "luau/lualib.h"

namespace
{

SdkScriptHost *hostOf(lua_State *L)
{
	return static_cast<SdkScriptHost *>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}

Player *playerArg(lua_State *L, int arg)
{
	const int i = luaL_checkinteger(L, arg);
	Player *p = hostOf(L)->logic().players().getNthPlayer(i);
	if (!p)
		luaL_error(L, "game: there is no player %d (0 .. %d)", i, hostOf(L)->logic().players().getPlayerCount() - 1);
	return p;
}

int gameFrame(lua_State *L)
{
	lua_pushinteger(L, (int)hostOf(L)->logic().getFrame());
	return 1;
}

int gamePlayerCount(lua_State *L)
{
	lua_pushinteger(L, hostOf(L)->logic().players().getPlayerCount());
	return 1;
}

int gamePlayerName(lua_State *L)
{
	lua_pushstring(L, playerArg(L, 1)->getPlayerName().c_str());
	return 1;
}

int gamePlayerSide(lua_State *L)
{
	lua_pushstring(L, playerArg(L, 1)->getSide().c_str());
	return 1;
}

int gameMoney(lua_State *L)
{
	lua_pushinteger(L, (int)playerArg(L, 1)->getMoney()->countMoney());
	return 1;
}

int gameAddMoney(lua_State *L)
{
	const std::string refused = hostOf(L)->actionRefusal(); // lane SDK-2b
	if (!refused.empty())
		luaL_error(L, "game.addMoney: %s", refused.c_str());
	Player *p = playerArg(L, 1);
	const int amount = luaL_checkinteger(L, 2);
	if (amount < 1 || amount > 1000000)
		luaL_error(L, "game.addMoney: the amount must be a whole number 1 .. 1000000, not %d", amount);
	p->getMoney()->deposit((std::uint32_t)amount, nullptr, false);
	return 0;
}

// presentation only: the player this machine plays (-1: none, an observer); differs between peers, so a sim script never sees it
int gameLocalPlayer(lua_State *L)
{
	const Player *p = hostOf(L)->logic().players().getLocalPlayer();
	lua_pushinteger(L, p ? p->getPlayerIndex() : -1);
	return 1;
}

int uiBanner(lua_State *L)
{
	hostOf(L)->pushBanner(luaL_checkstring(L, 1));
	return 0;
}

void registerGame(lua_State *L, SdkScriptHost *host, bool sim)
{
	const luaL_Reg api[] = { { "frame", gameFrame }, { "playerCount", gamePlayerCount }, { "playerName", gamePlayerName },
		{ "playerSide", gamePlayerSide }, { "money", gameMoney }, { sim ? "addMoney" : nullptr, gameAddMoney }, { nullptr, nullptr } };
	lua_newtable(L);
	for (const luaL_Reg *f = api; f->name; ++f)
	{
		lua_pushlightuserdata(L, host);
		lua_pushcclosure(L, f->func, f->name, 1);
		lua_setfield(L, -2, f->name);
	}
	lua_setglobal(L, "game");
	if (!sim)
	{
		lua_getglobal(L, "game");
		lua_pushlightuserdata(L, host);
		lua_pushcclosure(L, gameLocalPlayer, "localPlayer", 1);
		lua_setfield(L, -2, "localPlayer");
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushlightuserdata(L, host);
		lua_pushcclosure(L, uiBanner, "banner", 1);
		lua_setfield(L, -2, "banner");
		lua_setglobal(L, "ui");
	}
}

// lane SDK-2b: the save block obfme:SdkScripts, an adapter of the host's xfer; its post-process restarts the scripts
class SdkScriptsSnapshot : public Snapshot
{
public:
	SdkScriptsSnapshot(SdkScriptHost &host, GameState &state) : m_host(&host), m_state(state) {}
	void crc(Xfer *x) override { host().xferBlock(*x); }
	void xfer(Xfer *x) override
	{
		host().xferBlock(*x);
		if (x->isLoading())
			m_state.addPostProcessSnapshot(this);
	}
	void loadPostProcess() override { host().afterLoad(); }
	void hostGone() { m_host = nullptr; } // the registry keeps the adapter (Sol r1): a transfer after the host is a named error

private:
	SdkScriptHost &host() const
	{
		if (!m_host)
			throw XferException("obfme:SdkScripts: the mod's script host is gone");
		return *m_host;
	}
	SdkScriptHost *m_host;
	GameState &m_state;
};

} // namespace

SdkScriptHost::SdkScriptHost(GameLogic &logic, SdkScriptLimits limits) : m_logic(logic), m_limits(limits)
{
}

SdkScriptHost::~SdkScriptHost()
{
	if (m_block)
		static_cast<SdkScriptsSnapshot *>(m_block)->hostGone();
	if (m_worldHooks)
		m_logic.removeWorldHooks(m_worldHooks);
	if (m_frameHook)
		m_logic.removeEndOfFrameHook(m_frameHook);
	if (m_hashContributor)
		m_logic.removeStateHashContributor(m_hashContributor);
}

bool SdkScriptHost::start(const ModLooseIndex &modFolder, std::string &error)
{
	m_files = std::make_unique<SdkModFiles>(modFolder);
	if (!m_files->has(SdkManifest::path()))
		return true;
	std::string json;
	SdkManifest manifest;
	if (!m_files->read(SdkManifest::path(), json, error) || !SdkManifest::parse(json, manifest, error))
		return false;
	m_modName = manifest.name;
	m_manifest = manifest;
	if (manifest.sim.empty() && manifest.presentation.empty())
		return true;
	if (!manifest.sim.empty())
	{
		if (!SdkSimFingerprint(manifest, m_files->reader(), m_files->lister(), m_fingerprint, error))
			return false;
		m_fingerprint = "sdk sim " + m_fingerprint;
		if (!buildSim(true, error))
			return false;
	}
	buildPresentation();
	m_worldHooks = m_logic.addWorldHooks(
		[this](Object &o) {
			const Player *p = o.getControllingPlayer();
			m_events.push_back(Event{ true, (std::uint32_t)o.getID(), o.getTemplate() ? o.getTemplate()->getName() : std::string(), p ? p->getPlayerIndex() : -1 });
		},
		[this](Object &o) {
			const Player *p = o.getControllingPlayer();
			m_events.push_back(Event{ false, (std::uint32_t)o.getID(), o.getTemplate() ? o.getTemplate()->getName() : std::string(), p ? p->getPlayerIndex() : -1 });
		});
	m_frameHook = m_logic.addEndOfFrameHook([this](UnsignedInt frame) { endOfFrame(frame); });
	if (m_state)
	{
		// lane SDK-2b: saved (sdk.state and the counters; the scripts restart on a load)
		auto block = std::make_unique<SdkScriptsSnapshot>(*this, m_logic.gameState());
		m_block = block.get();
		m_logic.gameState().addOwnedSnapshotBlock("obfme:SdkScripts", std::move(block), 1u << SNAPSHOT_SAVELOAD, "SDK-2b", { "sdk scripts" });
		m_hashContributor = m_logic.addStateHashContributor(
			[this](StateHasher &h) {
				h.addU64(m_calls);
				h.addU64(m_steps);
				h.addBool(m_stopped);
				h.addString(m_stateBytes);
			},
			"sdk scripts");
	}
	return true;
}

bool SdkScriptHost::buildSim(bool counted, std::string &error)
{
	auto state = std::make_unique<LuauScriptState>(SdkScriptRole::Sim, m_manifest.name, m_limits, m_files->reader(),
		[this] { return m_logic.random().randomValue(); }, [this](lua_State *L) { registerGame(L, this, true); });
	state->setActionGate([this] { return actionRefusal(); });
	m_state = std::move(state); // the gate asks m_state while its top level runs
	for (const std::string &entry : m_manifest.sim)
	{
		if (!m_state->loadFile(entry, error))
		{
			error = "SDK " + error;
			m_state.reset();
			return false;
		}
		if (counted)
			m_steps += m_state->stepsLastCall();
	}
	for (const std::string &line : m_state->printed())
		report("SDK " + m_modName + ": " + line);
	m_printedSeen = m_state->printed().size();
	if (!m_state->serializeState(m_stateBytes, error))
	{
		error = "SDK " + m_modName + ": " + error;
		m_state.reset();
		return false;
	}
	return true;
}

void SdkScriptHost::buildPresentation()
{
	m_presentation.reset();
	m_presentationStopped = false;
	m_presentationPrinted = 0;
	if (m_manifest.presentation.empty())
		return;
	m_presentation = std::make_unique<LuauScriptState>(SdkScriptRole::Presentation, m_manifest.name, m_limits, m_files->reader(),
		LuauScriptState::Random(), [this](lua_State *L) { registerGame(L, this, false); });
	for (const std::string &entry : m_manifest.presentation)
	{
		std::string presentationError;
		if (!m_presentation->loadFile(entry, presentationError))
		{
			report("SDK presentation " + presentationError + " (the mod's presentation scripts are stopped)");
			m_presentationStopped = true;
			break;
		}
	}
}

std::string SdkScriptHost::actionRefusal() const
{
	if (!m_inEvent || (m_state && m_state->runningTopLevel()))
		return "called from a script's top level, which runs again when a save is loaded (change the game from an event: onGameStart, "
			   "onGameLoaded, onFrame, ...)";
	return std::string();
}

void SdkScriptHost::takeState(std::uint32_t frame)
{
	if (!m_state || m_stopped)
		return;
	std::string bytes, error;
	// the layout a restore builds: pairs runs alike in a loaded game (Sol r1); both stay inside protected calls (Sol r2)
	if (m_state->serializeState(bytes, error) && m_state->canonicalizeState(error))
	{
		m_stateBytes = std::move(bytes);
		return;
	}
	m_stopped = true; // the same bytes on every peer: the same error at the same frame
	report("SDK " + m_modName + ": " + error + " (frame " + std::to_string(frame) + "; the mod's scripts are stopped)");
}

void SdkScriptHost::xferBlock(Xfer &x)
{
	const bool loading = x.isLoading();
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	if (!loading && !m_events.empty())
		throw XferException("obfme:SdkScripts: a save in the middle of a frame (the scripts' events of the frame are queued)");
	std::string fingerprint = loading ? std::string() : m_fingerprint;
	std::uint64_t calls = loading ? 0 : m_calls, steps = loading ? 0 : m_steps;
	bool stopped = loading ? false : m_stopped, started = loading ? false : m_started;
	x.xferAsciiString(&fingerprint);
	x.xferUnsignedInt64(&calls);
	x.xferUnsignedInt64(&steps);
	x.xferBool(&stopped);
	x.xferBool(&started);
	std::uint32_t n = loading ? 0 : (std::uint32_t)m_stateBytes.size();
	x.xferUnsignedInt(&n);
	if (loading && n > LuauScriptState::kStateBytes)
		throw XferException("obfme:SdkScripts: sdk.state of " + std::to_string(n) + " bytes (at most " + std::to_string(LuauScriptState::kStateBytes) + ")");
	std::string bytes = loading ? std::string(n, '\0') : m_stateBytes;
	if (n > 0)
		x.xferUser(&bytes[0], n);
	if (!loading)
		return;
	if (fingerprint != m_fingerprint)
		throw XferException("obfme:SdkScripts: the save's mod scripts ('" + fingerprint + "') are not the loaded mod's ('" + m_fingerprint + "', the mod " +
			m_modName + "): load the save with the mod it was made with");
	std::string error;
	if (!LuauScriptState::checkStateBytes(bytes, error))
		throw XferException("obfme:SdkScripts: " + error);
	m_staged = Staged{ calls, steps, stopped, started, std::move(bytes) };
}

void SdkScriptHost::afterLoad()
{
	m_events.clear(); // the replaced game's
	m_bannersThisFrame = 0;
	std::string error;
	if (!buildSim(false, error))
		throw XferException("obfme:SdkScripts: the mod's sim scripts do not start again: " + error);
	if (!m_state->restoreState(m_staged.state, error))
		throw XferException("obfme:SdkScripts: " + error);
	std::string again;
	if (!m_state->serializeState(again, error) || again != m_staged.state)
		throw XferException("obfme:SdkScripts: sdk.state restored to other bytes" + (error.empty() ? std::string() : " (" + error + ")"));
	m_calls = m_staged.calls;
	m_steps = m_staged.steps;
	m_stopped = m_staged.stopped;
	m_started = m_staged.started;
	m_stateBytes = m_staged.state;
	m_loadedPending = true;
	buildPresentation();
}

bool SdkScriptHost::callIfDefined(const char *name, const std::function<int(lua_State *T)> &push, bool counted)
{
	if (m_stopped || !m_state->hasFunction(name))
		return true;
	std::string error;
	if (counted)
		++m_calls;
	m_inEvent = true;
	const bool ok = m_state->callWith(name, push, error);
	m_inEvent = false;
	if (counted)
		m_steps += m_state->stepsLastCall();
	for (size_t i = m_printedSeen; i < m_state->printed().size(); ++i)
		report("SDK " + m_modName + ": " + m_state->printed()[i]);
	m_printedSeen = m_state->printed().size();
	if (!ok)
	{
		m_stopped = true;
		report("SDK " + error + " (frame " + std::to_string(m_logic.getFrame()) + "; the mod's scripts are stopped)");
	}
	return ok;
}

void SdkScriptHost::presentationCall(const char *name, const std::function<int(lua_State *T)> &push)
{
	if (!m_presentation || m_presentationStopped || !m_presentation->hasFunction(name))
		return;
	std::string error;
	const bool ok = m_presentation->callWith(name, push, error);
	for (size_t i = m_presentationPrinted; i < m_presentation->printed().size(); ++i)
		report("SDK presentation " + m_modName + ": " + m_presentation->printed()[i]);
	m_presentationPrinted = m_presentation->printed().size();
	if (!ok)
	{
		m_presentationStopped = true;
		report("SDK presentation " + error + " (the mod's presentation scripts are stopped)");
	}
}

void SdkScriptHost::report(const std::string &line)
{
	// bounded by bytes (Sol r1, r2): the scripts' own output is limited (LuauScriptState::kPrintTotal) and the mod's name is at most 64
	// characters; this caps the copies the host keeps (each line counted with its string object), the overflow notice included
	const std::size_t cost = line.size() + 64;
	const std::size_t noticeRoom = 128; // Sol r3: the overflow notice fits inside the budget too
	if (m_reportBytes + cost > kReportBytes - noticeRoom)
	{
		if (!m_reportsFull)
		{
			m_reportsFull = true;
			m_reports.push_back("SDK: more report lines than the host keeps; the rest are not kept");
		}
		return;
	}
	m_reportBytes += cost;
	m_reports.push_back(line);
	std::lock_guard<std::mutex> lock(m_outboxMutex);
	if (m_newReportBytes + cost <= kReportBytes)
	{
		m_newReportBytes += cost;
		m_newReports.push_back(line);
	}
}

void SdkScriptHost::pushBanner(const std::string &text)
{
	if (m_bannersThisFrame >= 8)
		return;
	++m_bannersThisFrame;
	std::lock_guard<std::mutex> lock(m_outboxMutex);
	m_banners.push_back(text.substr(0, 200));
}

std::vector<std::string> SdkScriptHost::takeBanners()
{
	std::lock_guard<std::mutex> lock(m_outboxMutex);
	std::vector<std::string> out;
	out.swap(m_banners);
	return out;
}

std::vector<std::string> SdkScriptHost::takeNewReports()
{
	std::lock_guard<std::mutex> lock(m_outboxMutex);
	std::vector<std::string> out;
	out.swap(m_newReports);
	m_newReportBytes = 0;
	return out;
}

void SdkScriptHost::endOfFrame(std::uint32_t frame)
{
	m_bannersThisFrame = 0;
	const std::vector<Event> events = std::move(m_events);
	m_events.clear();
	const bool first = !m_started;
	const bool loaded = m_loadedPending; // lane SDK-2b: the first frame after a load
	m_loadedPending = false;
	m_started = true;
	// the sim scripts first (lockstep), then the presentation scripts (this player's screen only); the same events, in the same order
	for (int pass = 0; pass < 2; ++pass)
	{
		const bool sim = pass == 0;
		if (sim && (!m_state || m_stopped))
			continue;
		auto call = [this, sim](const char *name, const std::function<int(lua_State *T)> &push, bool counted = true) {
			if (sim)
				return callIfDefined(name, push, counted);
			presentationCall(name, push);
			return true;
		};
		// lane SDK-2b: onGameLoaded is the load's, not the game's: its calls and steps are not counted
		bool ok = !loaded || call("onGameLoaded", [](lua_State *) { return 0; }, false);
		ok = ok && (!first || call("onGameStart", [](lua_State *) { return 0; }));
		for (size_t i = 0; ok && i < events.size(); ++i)
		{
			const Event &e = events[i];
			ok = call(e.created ? "onObjectCreated" : "onObjectDestroyed", [&e](lua_State *T) {
				lua_pushinteger(T, (int)e.id);
				lua_pushstring(T, e.templateName.c_str());
				lua_pushinteger(T, e.player);
				return 3;
			});
		}
		if (ok)
			call("onFrame", [frame](lua_State *T) {
				lua_pushinteger(T, (int)frame);
				return 1;
			});
		if (sim)
			takeState(frame); // sdk.state as the frame left it (the state hash and a save take these bytes)
	}
}
