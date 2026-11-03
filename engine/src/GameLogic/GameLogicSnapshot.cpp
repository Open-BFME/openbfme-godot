// OpenBFME. GPL-3.0.
//
// GameLogic's snapshot (lane SAVE-4a, docs/SAVES.md; Common/GameState.h): the block registry of every simulation subsystem, the core blocks and the save /
// load entry points. The registry is GameState::init's counterpart (RW 0x6DF73E registers retail's blocks in its lists; ZH GameState::init); the blocks are
// OpenBFME's native ones (decision D3: none of these classes is a 1:1 port of its retail counterpart).
//
// The core blocks (this lane), in the order of retail's list 0 (CHUNK_TeamFactory, CHUNK_Players, CHUNK_GameLogic):
//   obfme:TeamFactory  the team id counters, the prototypes (checked), every team and its state; the members linked again in the post-process
//   obfme:Players      every player (Player::xfer)
//   obfme:GameLogic    GameLogic::xferCore
//   obfme:Random       the logic generator's state (GameLogicRandom::xfer)
// Every other subsystem is DECLARED with the lane that ports its xfer and the state hash sections it holds, so a save is refused naming them until each
// exists (no partial snapshots, SAVES.md T4). State the load rebuilds from the game's START and the data is declared by its hash section with the reason.

#include "GameLogic/GameLogic.h"
#include "Common/ModEngineFeatures.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/MpGameRules.h"

#include "Common/GameState.h"
#include "Common/Snapshot.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameLogic/System/ShroudManager.h"

namespace
{
// a block whose xfer is a function of its owner; its crc transfers the same fields (XferCRC); `post` is the post-process it queues while loading
class FunctionSnapshot : public Snapshot
{
public:
	FunctionSnapshot(GameState &state, std::function<void(Xfer &)> fn, std::function<void()> post = nullptr)
		: m_state(state), m_fn(std::move(fn)), m_post(std::move(post))
	{
	}
	void crc(Xfer *x) override { m_fn(*x); }
	void xfer(Xfer *x) override
	{
		m_fn(*x);
		if (m_post && x->isLoading())
		{
			m_state.addPostProcessSnapshot(this);
		}
	}
	void loadPostProcess() override
	{
		if (m_post)
		{
			m_post();
		}
	}

private:
	GameState &m_state;
	std::function<void(Xfer &)> m_fn;
	std::function<void()> m_post;
};

const unsigned kSaveLoad = 1u << SNAPSHOT_SAVELOAD;
const unsigned kSaveLoadAndDeepCRC = (1u << SNAPSHOT_SAVELOAD) | (1u << SNAPSHOT_DEEPCRC_LOGICONLY); // retail's list 1 has TeamFactory, Players, GameLogic
} // namespace

void GameLogic::registerSnapshotBlocks()
{
	m_gameState = std::make_unique<GameState>();
	GameState &gs = *m_gameState;
	auto add = [&](const char *token, std::function<void(Xfer &)> fn, std::function<void()> post, const std::vector<std::string> &sections) {
		gs.addOwnedSnapshotBlock(token, std::make_unique<FunctionSnapshot>(gs, std::move(fn), std::move(post)), kSaveLoadAndDeepCRC, "SAVE-4a", sections);
	};
	// ---- the core (SAVE-4a) ----
	add("obfme:TeamFactory", [this](Xfer &x) { m_players.teams().xfer(x); },
		[this]() { m_players.teams().loadPostProcess([this](ObjectID id) { return findObjectByID(id); }); }, { "players and teams" });
	add("obfme:Players", [this](Xfer &x) { m_players.xfer(x); }, nullptr, { "players and teams" });
	add("obfme:GameLogic", [this](Xfer &x) { xferCore(x); }, nullptr, { "frame, object ids, RNG" });
	add("obfme:Random", [this](Xfer &x) { m_random.xfer(x); }, nullptr, { "frame, object ids, RNG" });
	// ---- SAVE-4b: the objects and the module runtimes ----
	gs.declareBlockWithoutXfer("obfme:Objects", kSaveLoadAndDeepCRC, "SAVE-4b", { "objects", "frame, object ids, RNG" }); // + the object count and list order
	gs.declareBlockWithoutXfer("obfme:UpdateScheduler", kSaveLoadAndDeepCRC, "SAVE-4b", { "scheduler" });
	gs.declareBlockWithoutXfer("obfme:TunnelTracker", kSaveLoad, "SAVE-4b", { "objects" }); // Player + 0x308, hashed by TunnelContain::crc
	gs.declareBlockWithoutXfer("obfme:Combat", kSaveLoad, "SAVE-4b", { "combat" });
	gs.declareBlockWithoutXfer("obfme:SellList", kSaveLoad, "SAVE-4b", { "sell list" });
	gs.declareBlockWithoutXfer("obfme:Experience", kSaveLoad, "SAVE-4b", { "experience" });
	gs.declareBlockWithoutXfer("obfme:Invisibility", kSaveLoad, "SAVE-4b", { "invisibility" });
	gs.declareBlockWithoutXfer("obfme:DamageOverTime", kSaveLoad, "SAVE-4b", { "damage over time" });
	// ---- SAVE-4c: AI, pathfinder, partition, shroud (PartitionManager.cpp, EmotionSystemSnapshot.cpp, AIWorldSnapshot.cpp, ShroudManagerSnapshot.cpp,
	// SkirmishAISnapshot.cpp). The shroud and the AI world belong to the game that attached them (null in a game without a map): the block says whether the
	// subsystem exists, and a load into a game that differs is refused ----
	auto add4c = [&](const char *token, unsigned lists, std::function<void(Xfer &)> fn, std::function<void()> post, const std::vector<std::string> &sections) {
		gs.addOwnedSnapshotBlock(token, std::make_unique<FunctionSnapshot>(gs, std::move(fn), std::move(post)), lists, "SAVE-4c", sections);
	};
	auto present = [](Xfer &x, bool own, const char *what) {
		bool saved = own;
		x.xferBool(&saved);
		if (saved != own)
		{
			throw XferException(std::string("the snapshot's game ") + (saved ? "has" : "has no") + " " + what + ", this game " + (own ? "has one" : "has none"));
		}
		return own;
	};
	add4c("obfme:Partition", kSaveLoadAndDeepCRC, [this](Xfer &x) { m_partition->xfer(x); }, [this]() { m_partition->loadPostProcess(); },
		{ "partition manager" });
	add4c("obfme:Emotions", kSaveLoad, [this](Xfer &x) { m_emotions->xfer(x); }, [this]() { m_emotions->loadPostProcess(); }, { "emotion system" });
	// the AI update modules are hashed with their objects ("objects", obfme:Objects' section) and saved here (AIWorldSnapshot.cpp)
	add4c("obfme:AIWorld", kSaveLoad,
		[this, present](Xfer &x) {
			XferVersion v{ 1, 1 };
			x.xferVersion(v);
			if (present(x, m_aiWorld != nullptr, "AI world"))
			{
				m_aiWorld->xfer(x);
			}
		},
		nullptr, { "pathfinder and AI movers", "objects" });
	add4c("obfme:Shroud", kSaveLoadAndDeepCRC,
		[this, present](Xfer &x) {
			XferVersion v{ 1, 1 };
			x.xferVersion(v);
			if (present(x, m_shroud != nullptr, "shroud manager"))
			{
				m_shroud->xfer(x);
			}
		},
		[this]() {
			if (m_shroud)
			{
				m_shroud->loadPostProcess();
			}
		},
		{ "shroud" });
	add4c("obfme:SkirmishAI", kSaveLoadAndDeepCRC, [this](Xfer &x) { m_skirmishAI->xfer(x); }, nullptr, { "skirmish AI" });
	// ---- SAVE-4d: the script engine, victory, weather and the rest (GameLogicSnapshot4d.cpp) ----
	registerSave4dBlocks(gs);
	// ---- rebuilt from the START and the data ----
	gs.declareRebuiltSection("settings", "GameLogicSettings: GameData and the map's settings, set before the game starts; no simulation code writes them");
	registerObjectSnapshotBlocks(); // lane SAVE-4b: the xfers of its declared blocks (GameLogicObjectSnapshot.cpp)
}

std::vector<std::string> GameLogic::aiSnapshotStopLines()
{
	return {
		"[S-4220] snapshots: SAVE-4c's blocks (obfme:Partition, obfme:Emotions, obfme:AIWorld, obfme:Shroud, obfme:SkirmishAI) are OpenBFME's native layouts "
		"(decision D3: none of their classes is a 1:1 port of its retail counterpart): retail's StateMachine::xfer RW 0x8DBAEC (1, 4), AIStateMachine::xfer "
		"RW 0x755174 (1, 2), AIUpdateInterface::xfer RW 0x667416 (1, 6), TurretAI::xfer RW 0x8DC2C3 (1, 1), DozerAIUpdate::xfer RW 0x88C966 (1, 2), the AI "
		"states' xfers, LocomotorSet RW 0x5E7F12 / 0x5E68A4, AIPlayer::xfer RW 0x8F5756 (1, 2), AISkirmishPlayer::xfer RW 0x8F3F6C (1, 1), AIBuilder::DoXfer "
		"RW 0x8F03FA (1, 5), the shroud's RW 0xB51A50 (the grid, then every cell through RW 0xB52D50) and PathfindZoneManager::DoXfer RW 0x93AC0F hold other "
		"field sets; CHUNK_Partition's xfer RW 0xA39260 transfers its version (1, 1) and the implementation's RW 0xA3B040, a version (1, 1) alone (retail "
		"rebuilds the trees from the objects); the AI update modules travel in obfme:AIWorld by object id, not in Object::xfer's module list (RW 0x697F28) "
		"as in retail; retail import (SAVE-7) needs a translator per class",
		"[S-4221] snapshots: saved state retail rebuilds (INFERENCE that a rebuild would not give it back): the pathfinder's cells and occupant chains in "
		"order, the partition's node lists in order (retail's CHUNK_Partition, RW 0xA39260 -> 0xA3B040, saves versions only); the shroud's cells are saved "
		"as retail saves them (RW 0xB51A50 -> 0xB52D50, a different cell layout); an AI's pending-position frame stamp is saved (the crush logic reads it); "
		"not saved, rebuilt (INFERENCE): the pathfinder's search scratch (a cell info's costs and parent, the free lists of the cell info and occupant pools: "
		"a search writes them before it reads them), the template caches (movement infos, partition geometry, zone tables), the audio and client bookkeeping "
		"(an AI's move loop flag, the shroud's local player, callback and display levels); a live object an AI acts through (a unit or dozer factory, a "
		"free dozer, a team member, an info record's object) must be its player's at a load while its AI runs: the events keep it so (a factory enters "
		"the unit builder in its object event, RW 0x8F06BF -> 0x9A0838, and leaves it at its death or change of owner, RW 0x8F0C69 -> 0x9A051D; an object "
		"changing owner leaves the other AIs' tactic teams and free dozers); a shut-down AI (RW 0x6C7981) gets no more events and keeps ids of objects "
		"gone or captured since in its builder lists, as retail's state does: there they are issued ids only; a dispatched unit is an issued id only",
		"[S-4222] snapshots: SAVE-4c's blocks are checked by loading them into an identical game; since SAVE-4e the whole snapshot is T1-tested in a "
		"fresh process (a 4-AI skirmish, a campaign mission) and T3 runs against a Windows peer under Wine (S-5013); obfme:AIWorld fills the AI modules "
		"their objects made, so obfme:Objects must load first (its place in the list)",
	};
}

std::vector<std::string> GameLogic::stateHashSectionNames() const
{
	std::vector<StateHashSection> sections;
	computeStateHashBreakdown(sections, nullptr);
	std::vector<std::string> names;
	for (const StateHashSection &s : sections)
	{
		names.push_back(s.name);
	}
	return names;
}

bool GameLogic::saveSnapshot(std::vector<std::uint8_t> &out, std::string *error)
{
	if (m_inUpdate || m_depth != 0)
	{
		if (error)
		{
			*error = "the snapshot is refused: a logic frame is running (a snapshot is taken between frames)";
		}
		return false;
	}
	return m_gameState->saveSnapshot(stateHashSectionNames(), out, error);
}

bool GameLogic::loadSnapshot(const std::vector<std::uint8_t> &bytes, std::string *error)
{
	if (m_inUpdate || m_depth != 0)
	{
		if (error)
		{
			*error = "a snapshot cannot be loaded while a logic frame is running";
		}
		return false;
	}
	return m_gameState->loadSnapshot(stateHashSectionNames(), bytes, error);
}

// OpenBFME native layout of the frame state (retail's CHUNK_GameLogic RW 0x6308F3, window (1, 8), starts with two ints and holds the object TOC and the
// objects; its fields are not identified one by one, SAVES-RETAIL.md section 2). Every field the "frame, object ids, RNG" section hashes except the RNG
// (obfme:Random) and the object count (the objects' block, SAVE-4b). The phase bookkeeping (m_inUpdate, m_lastPhase, m_depth, m_current) is constant
// between frames, where a snapshot is taken.
void GameLogic::xferCore(Xfer &x)
{
	// lane CHEAT-1: version 2 adds the Cheat Mode block at the end; lane BFME2CAMP1: version 3 adds the buildability overrides (GameLogic.h, RW GameLogic
	// + 0x10) and the campaign's carry-over after it; WOTR2 milestone 7: version 4 adds the War of the Ring battle's army tags after those (it was
	// version 3 on feature/wotr before the archive merge: never released). Each version is written only when its state is there, so every other game's
	// snapshot is byte-identical to version 1's (an older snapshot loads with none of the later state)
	const bool campaignState = !m_buildableOverrides.empty() || m_campaignCarryover.has_value();
	XferVersion v{ 1, (std::uint8_t)(x.isLoading() || m_livingWorldTags.inUse() ? 4 : campaignState ? 3 : m_cheatMode.enabled() ? 2 : 1) };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_frame);
	x.xferBool(&m_frameAdvance);
	x.xferInt(&m_gameDifficulty);
	for (std::int32_t &r : m_gameRules)
	{
		x.xferInt(&r);
	}
	x.xferInt(&m_legendaryHeroes);
	if (x.isLoading()) // lane SAVE-4e (closed sets): the Legendary Heroes factor 1 .. 4
	{
		SnapshotXfer::checkRange(m_legendaryHeroes, LegendaryHeroes::kOff, LegendaryHeroes::kMax, "the game's Legendary Heroes factor");
	}
	x.xferEnum(&m_simulationProfile);
	if (m_simulationProfile != SimulationProfile::Faithful && m_simulationProfile != SimulationProfile::Uber)
	{
		throw XferException("an execution profile of " + std::to_string((int)m_simulationProfile));
	}
	x.xferObjectID(&m_nextObjectID);
	if (ModEngineFeatures::has(ModEngineFeature::EdainUnchainedUniqueProductionId))
	{
		x.xferUnsignedInt(&m_sharedProductionID); // lane MODS-3: a game with the feature (its identity names it; a retail save has no such field)
	}
	if (v.current >= 2)
	{
		m_cheatMode.xfer(x);
	}
	else if (x.isLoading())
	{
		m_cheatMode = CheatMode::State();
	}
	if (v.current >= 3)
	{
		// the overrides by name, sorted (a load replaces this game's); a save before version 3 has none
		std::uint32_t n = (std::uint32_t)m_buildableOverrides.size();
		x.xferUnsignedInt(&n);
		if (x.isLoading())
		{
			std::map<std::string, std::int32_t> loaded;
			for (std::uint32_t i = 0; i < n; ++i)
			{
				std::string name;
				std::int32_t status = 0;
				x.xferAsciiString(&name);
				x.xferInt(&status);
				SnapshotXfer::checkRange(status, 0, 3, "a buildability override");
				if (!loaded.emplace(name, status).second)
				{
					throw XferException("the buildability override of " + name + " is saved twice");
				}
			}
			m_buildableOverrides = std::move(loaded);
		}
		else
		{
			for (auto &e : m_buildableOverrides)
			{
				std::string name = e.first;
				x.xferAsciiString(&name);
				x.xferInt(&e.second);
			}
		}
	}
	else if (x.isLoading())
	{
		m_buildableOverrides.clear();
	}
	if (v.current >= 3)
	{
		// the carry-over as its text form (CampaignCarryover::State::serialize; parse refuses anything malformed); a save before version 3 has none
		bool present = m_campaignCarryover.has_value();
		x.xferBool(&present);
		if (present)
		{
			std::string text = m_campaignCarryover ? m_campaignCarryover->serialize() : std::string();
			x.xferAsciiString(&text);
			if (x.isLoading())
			{
				CampaignCarryover::State st;
				std::string why;
				if (!CampaignCarryover::State::parse(text, st, &why))
				{
					throw XferException(why);
				}
				m_campaignCarryover = std::move(st);
			}
		}
		else if (x.isLoading())
		{
			m_campaignCarryover.reset();
		}
	}
	else if (x.isLoading())
	{
		m_campaignCarryover.reset();
	}
	if (v.current >= 4)
	{
		xferLivingWorldTags(x);
	}
	else if (x.isLoading())
	{
		m_livingWorldTags.clear();
	}
}

// WOTR2 milestone 7: LivingWorldBattleTags (GameLogic/LivingWorld/LivingWorldBattleTags.h)
void GameLogic::xferLivingWorldTags(Xfer &x)
{
	LivingWorldBattleTags &t = m_livingWorldTags;
	x.xferBool(&t.active);
	auto xferTag = [&](LivingWorldBattleTags::Tag &tag) {
		x.xferInt(&tag.armyId);
		if (x.isLoading() && tag.armyId < 0)
		{
			throw XferException("the snapshot's War of the Ring armyId " + std::to_string(tag.armyId));
		}
		x.xferAsciiString(&tag.spawnId);
		x.xferInt(&tag.buildingId);
		if (x.isLoading() && tag.buildingId < 0)
		{
			throw XferException("the snapshot's War of the Ring buildingId " + std::to_string(tag.buildingId));
		}
	};
	std::uint32_t count = (std::uint32_t)t.objects.size();
	x.xferCount(&count, 16, sizeof(LivingWorldBattleTags::Tag) + 48);
	if (x.isLoading())
	{
		t.objects.clear();
		for (std::uint32_t i = 0; i < count; ++i)
		{
			ObjectID id = INVALID_ID;
			x.xferObjectID(&id);
			LivingWorldBattleTags::Tag tag;
			xferTag(tag);
			t.objects[(std::uint32_t)id] = tag;
		}
	}
	else
	{
		for (auto &kv : t.objects)
		{
			ObjectID id = (ObjectID)kv.first;
			x.xferObjectID(&id);
			xferTag(kv.second);
		}
	}
	count = (std::uint32_t)t.heroes.size();
	x.xferCount(&count, 20, sizeof(LivingWorldBattleTags::Tag) + 96);
	if (x.isLoading())
	{
		t.heroes.clear();
		for (std::uint32_t i = 0; i < count; ++i)
		{
			std::int32_t player = 0;
			std::string name;
			x.xferInt(&player);
			SnapshotXfer::checkRange(player, 0, 19, "a War of the Ring hero tag's player");
			x.xferAsciiString(&name);
			LivingWorldBattleTags::Tag tag;
			xferTag(tag);
			t.heroes[{ player, name }] = tag;
		}
	}
	else
	{
		for (auto &kv : t.heroes)
		{
			std::int32_t player = kv.first.first;
			std::string name = kv.first.second;
			x.xferInt(&player);
			SnapshotXfer::checkRange(player, 0, 19, "a War of the Ring hero tag's player");
			x.xferAsciiString(&name);
			xferTag(kv.second);
		}
	}
	x.xferVectorSize(t.destroyedBuildings, 4);
	for (int &buildingId : t.destroyedBuildings)
	{
		x.xferInt(&buildingId);
		if (x.isLoading() && buildingId <= 0)
		{
			throw XferException("the snapshot's destroyed War of the Ring buildingId " + std::to_string(buildingId));
		}
	}
	// milestone 7 (step 5): the reinforcement armies and the call-ins so far
	x.xferVectorSize(t.reinforcements, 24);
	for (LivingWorldBattleTags::Reinforcement &r : t.reinforcements)
	{
		x.xferInt(&r.armyId);
		if (x.isLoading() && r.armyId <= 0)
		{
			throw XferException("the snapshot's War of the Ring reinforcement armyId " + std::to_string(r.armyId));
		}
		x.xferInt(&r.playerIndex);
		SnapshotXfer::checkRange(r.playerIndex, 0, 19, "a War of the Ring reinforcement's player");
		x.xferInt(&r.state);
		SnapshotXfer::checkRange(r.state, 2, 4, "a War of the Ring reinforcement's state");
		x.xferUnsignedInt(&r.arrivalFrame);
		x.xferVectorSize(r.members, 24);
		for (LivingWorldBattleTags::Member &m : r.members)
		{
			x.xferAsciiString(&m.thingTemplate);
			x.xferInt(&m.memberIndex);
			if (x.isLoading() && m.memberIndex < 0)
			{
				throw XferException("the snapshot's War of the Ring reinforcement memberIndex " + std::to_string(m.memberIndex));
			}
			x.xferInt(&m.quantity);
			if (x.isLoading() && (m.quantity < 0 || m.quantity > 1000))
			{
				throw XferException("the snapshot's War of the Ring reinforcement quantity " + std::to_string(m.quantity));
			}
			x.xferReal(&m.experience);
			x.xferVectorSize(m.upgrades, 4);
			for (std::string &u : m.upgrades)
			{
				x.xferAsciiString(&u);
			}
			x.xferAsciiString(&m.spawnId);
		}
		x.xferVectorSize(r.spawnPoints, 8);
		for (auto &p : r.spawnPoints)
		{
			x.xferAsciiString(&p.first);
			x.xferAsciiString(&p.second);
		}
	}
	x.xferVectorSize(t.arrivals, 4);
	for (int &armyId : t.arrivals)
	{
		x.xferInt(&armyId);
		if (x.isLoading() && armyId <= 0)
		{
			throw XferException("the snapshot's War of the Ring arrival armyId " + std::to_string(armyId));
		}
	}
}
