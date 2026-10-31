// OpenBFME. GPL-3.0.
//
// The SAVE-4d blocks of GameLogic's snapshot (lane SAVE-4d, docs/SAVES.md; the registry is GameLogicSnapshot.cpp, SAVE-4a). In the registry's order (retail's
// list 0 puts CHUNK_ScriptEngine before CHUNK_VictorySystem and CHUNK_WeatherSystem, RW 0x6DF73E):
//   obfme:ScriptEngine     ScriptEngine::xfer (ScriptEngine/ScriptEngineXfer.cpp)                     "script engine"      lists 0 and 1 (retail's list 1 holds it)
//   obfme:Victory          VictoryConditions::xfer                                                    "victory"
//   obfme:Weather          GlobalWeatherSystem::xfer (retail's field order, RW 0x71A2E3)              "weather"
//   obfme:Economy          Economy::xfer (TerrainResourceManager in retail's layout, RW 0x75C7AE)     "economy"
//   obfme:CreateAHero      CreateAHeroGame::xfer (the records in retail's layout, RW 0x80B4F5)        "create-a-hero"
//   obfme:TerrainTrees     TerrainTrees::xfer (the records in retail's layout, RW 0x680753)           "terrain trees"
//   obfme:LargeGroupAudio  LargeGroupAudioLink::xfer                                                  "large group audio"
// Their references are player indices, names and ids: the indices and names are resolved and checked while loading; obfme:ScriptEngine (the sequential
// scripts' teams) and obfme:Economy (the resource claimants' objects) stage what they loaded and check those ids in their post-process, after every
// block loaded, before they take the state (a refusal leaves them unchanged); the other ids (the named object cache, a hero's object, a unit's sequential
// script) may name an object that is gone, as in retail, and are looked up when used. The stops are S-4250 .. S-4256 (GameLogic::save4dStopLines).

#include "GameLogic/GameLogic.h"

#include "Common/GameState.h"
#include "Common/Snapshot.h"
#include "Common/Xfer.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/VictoryConditions.h"

namespace
{
// a block whose xfer is a function of its owner; its crc transfers the same fields (XferCRC); `check` (optional) is its post-process: run after every block
// loaded, it validates the ids the block keeps and then commits the staged state (SAVES.md section 8's rule 4), or throws an XferException naming a missing one
class Save4dSnapshot : public Snapshot
{
public:
	Save4dSnapshot(GameState &state, std::function<void(Xfer &)> fn, std::function<void()> check)
		: m_state(state), m_fn(std::move(fn)), m_check(std::move(check))
	{
	}
	void crc(Xfer *x) override { m_fn(*x); }
	void xfer(Xfer *x) override
	{
		m_fn(*x);
		if (m_check && x->isLoading())
		{
			m_state.addPostProcessSnapshot(this);
		}
	}
	void loadPostProcess() override
	{
		if (m_check)
		{
			m_check();
		}
	}

private:
	GameState &m_state;
	std::function<void(Xfer &)> m_fn;
	std::function<void()> m_check;
};
} // namespace

void GameLogic::registerSave4dBlocks(GameState &gs)
{
	const unsigned saveLoad = 1u << SNAPSHOT_SAVELOAD;
	const unsigned saveLoadAndDeepCRC = saveLoad | (1u << SNAPSHOT_DEEPCRC_LOGICONLY);
	auto add = [&](const char *token, unsigned lists, std::function<void(Xfer &)> fn, std::function<void()> check, const char *section) {
		gs.addOwnedSnapshotBlock(token, std::make_unique<Save4dSnapshot>(gs, std::move(fn), std::move(check)), lists, "SAVE-4d", { section });
	};
	add("obfme:ScriptEngine", saveLoadAndDeepCRC, [this](Xfer &x) { m_scriptEngine->xfer(x); }, [this]() { m_scriptEngine->finishLoad(); },
		"script engine");
	add("obfme:Victory", saveLoad, [this](Xfer &x) { m_victory->xfer(x); }, nullptr, "victory");
	add("obfme:Weather", saveLoad, [this](Xfer &x) { m_weather->xfer(x); }, nullptr, "weather");
	add("obfme:Economy", saveLoad, [this](Xfer &x) { m_economy->xfer(x); }, [this]() { m_economy->finishLoad(); }, "economy");
	add("obfme:CreateAHero", saveLoad, [this](Xfer &x) { m_createAHeroes->xfer(x); }, nullptr, "create-a-hero");
	add("obfme:TerrainTrees", saveLoad, [this](Xfer &x) { m_trees->xfer(x); }, nullptr, "terrain trees");
	add("obfme:LargeGroupAudio", saveLoad, [this](Xfer &x) { m_largeGroupAudio.xfer(x); }, nullptr, "large group audio");
	// lane CAMPUI-1: retail's own blocks and layouts (MissionObjectiveTracker.h), list 0 only: retail's CRC xfer skips both and no hash section holds them
	gs.addOwnedSnapshotBlock("CHUNK_ObjectivesMenu", std::make_unique<Save4dSnapshot>(gs, [this](Xfer &x) { m_missionObjectives.xferMenu(x); }, nullptr),
		saveLoad, "CAMPUI-1", {});
	gs.addOwnedSnapshotBlock("CHUNK_MissionObjectives", std::make_unique<Save4dSnapshot>(gs, [this](Xfer &x) { m_missionObjectives.xfer(x); }, nullptr),
		saveLoad, "CAMPUI-1", {});
}

std::vector<std::string> GameLogic::save4dStopLines()
{
	return {
		"[S-4250] snapshot obfme:LargeGroupAudio (SAVE-4d): OpenBFME's own layout (version 1: the disabled byte + 0x38, the gate frame + 0x3C); retail's "
		"LargeGroupAudio::xfer (RW 0x60D899, (1, 5)) also holds the bytes + 0x3A / + 0x39 and TheLargeGroupAudio's audio maps, which are the client's here",
		"[S-4251] snapshot obfme:Weather (SAVE-4d): retail's GlobalWeatherSystem field order and window (RW 0x71A2E3, (1, 4); the AntiCategory mask by name, "
		"RW 0x71A1A7) except the audio handle + 0x14 (TheAudio vslot 0x160, the client's weather sound: not written) and the ObjectFilter (the port's layout, "
		"S-4121); the burn decay (RW 0xDE46A8 + 0x98, another subsystem's field in retail) follows in an OpenBFME trailer",
		"[S-4252] snapshot obfme:TerrainTrees (SAVE-4d): OpenBFME's own layout (retail saves the trees inside TerrainLogic::xfer, RW 0x683F8C (1, 2)): only "
		"the records changed since the map start, each by index in retail's record layout (RW 0x680753, (1, 2)); a load needs a game at its START; the "
		"client's tree events (topple, removal) are not saved, so a loaded game's tree buffer does not show the trees that fell before the save",
		"[S-4253] snapshot obfme:Economy (SAVE-4d): the GameData settings are configuration (their hash is checked, not saved), the game context is "
		"OpenBFME's layout, the TerrainResourceManager follows retail's DoXfer (RW 0x75C7AE, (1, 3)); RETAIL QUIRK NOT KEPT: retail does not save a "
		"claimant's complete flag (node + 0x11, left uninitialised by a load), the port saves it in a trailer",
		"[S-4254] snapshot obfme:Victory (SAVE-4d): OpenBFME's own layout; RotWK's VictoryConditions (vtable RW 0xC4F108) has no Snapshot part and is not "
		"saved by retail; the local slot and observer flags are this peer's and stay the loading game's",
		"[S-4255] snapshot obfme:CreateAHero (SAVE-4d): OpenBFME's own layout around retail's record xfer (RW 0x80B4F5, (1, 8)), which carries the record's "
		"stored checksum (retail's file save writes the one computed then); retail's own save of the game's hero slots (RW 0x61B083) is not identified",
		"[S-4256] snapshot obfme:ScriptEngine (SAVE-4d): OpenBFME's own layout (RotWK's ScriptEngine::xfer RW 0x60A45B (1, 5) is not identified field by "
		"field) with retail's Script (RW 0x7B3063 (1, 2)), ScriptGroup (RW 0x7B2F8A (1, 1)) and SequentialScript (RW 0x60331E (1, 1)) record layouts; a "
		"load needs the same map scripts (the sides, every script's and group's name in order); not saved: the client requests, the run-time polygon triggers (AIGateUpdate "
		"adds them with its object: left to the objects' block, SAVE-4b) and the Lua logic state, which retail does not save either (objects register again, RW 0x667BEF)",
	};
}
