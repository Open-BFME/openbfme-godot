// OpenBFME unit tests. GPL-3.0.
// Lane EXIT-1 (QA-2 findings 1 and 2): motion invariants over computer-player games. QA-2 (godot/scripts/qa_player.gd on lane/qa2) counted, in 25 of 26
// matrix games, freshly produced soldiers standing at their barracks' exit in AI_FOLLOW_EXITPRODUCTION_PATH with the run animation, and idle units keeping
// MOVING. The causes, both read from the RotWK binary:
//   1. the horde hub's busy rule (RW 0x87471B .. 0x8747A3): a member whose state and the horde's are not both active is made busy (AI command 0x31) when the
//      HORDE moves (RW 0x874777: RW 0x664485 on the horde's AI) or the MEMBER is idle (vslot 0x1B8); the port tested the member's isMoving and !isIdle, so a
//      produced member of a parked horde (idle through its horde) kept its exit path state and the hub's slot goal fought it every other frame;
//   2. the locomotor's maintainCurrentPosition (RW 0x5E7CC7, from doLocomotor's goal type 0 branch RW 0x669A60) clears MOVING and the turn conditions of a
//      goal-less unit; the port did not run it, so a member whose walk ended without the move state's exit (released to a melee, parked) ran on the spot.
// The invariants are QA-2's: a "treadmill" unit has MOVING and has not moved for 3 samples of 10 frames; a "stuck" unit is AI-moving and has not moved for 15
// samples. The expectations are outcomes (no treadmill, every produced member leaves its exit path), never this engine's frame-exact output.

#include "doctest.h"

#include "StartTestUtil.h"
#include "Idle1TurnProbe.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Module.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameNetwork/LockstepDriver.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
int exitFaction(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

const std::vector<MapCacheEntry> &exitMapCache(starttest::Shared &s)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		std::string error;
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	return cache;
}

// QA-1 / QA-2's Tournament Udun 2 v 2: slot 0 an idle human with a computer ally, two computer enemies (`difficulty` SLOT_EASY_AI + 0..3)
NewGameMessage exitUdun(starttest::Shared &s, int difficulty, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp tournament udun/map mp tournament udun.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const char *factions[4] = { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard" };
	for (int i = 0; i < 4; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : (SlotState)(SLOT_EASY_AI + difficulty);
		slot.name = i == 0 ? u"Player0" : u"Player" + std::u16string(1, (char16_t)(u'0' + i));
		slot.playerTemplate = exitFaction(s.world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i < 2 ? 0 : 1;
	}
	return m;
}

// QA-2's motion invariants, sampled every 10 logic frames over every living unit with an AI (structures, horde objects and units inside a non-horde
// container skipped)
struct MotionProbe
{
	struct Track
	{
		float x = 0, y = 0;
		int still = 0;       ///< samples without movement
		int movingStill = 0; ///< consecutive samples with MOVING and without movement
		int aiMovingStill = 0; ///< consecutive samples AI-moving and without movement
		int exitSince = -1;  ///< the frame it was first sampled in AI_FOLLOW_EXITPRODUCTION_PATH (-1: not in it)
	};
	std::map<ObjectID, Track> tracks;
	std::map<unsigned, std::set<ObjectID>> treadmill, stuck; ///< AI state -> units
	std::set<ObjectID> treadmillInMelee;                     ///< treadmill units whose horde fights a melee (its Amoeba steps: lane MOVE-2 r3, S-1502)
	std::map<ObjectID, Track> riderTracks;                  ///< lane IDLE-1: riders, by their container's position
	std::set<ObjectID> riderApart;                           ///< riders whose own position is not their container's
	std::set<ObjectID> riderTreadmill;                       ///< riders with MOVING for 3 samples while their container stood
	int riderLines = 0;
	std::set<ObjectID> seen, exited;                         ///< every unit; the units sampled in the exit path state
	int longestExit = 0;                                     ///< the longest a unit stayed in the exit path state (frames, by sample)
	std::string longestExitName;
	std::ostringstream samples;
	int sampleLines = 0, stuckLines = 0;

	void sample(GameLogic &logic)
	{
		if ((logic.getFrame() % 10) != 0)
		{
			return;
		}
		static const int moving = AIUpdateInterface::modelConditionBit("MOVING");
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			AIUpdateInterface *ai = x->getAIUpdateInterface();
			if (!ai || x->isEffectivelyDead() || x->isKindOfName("STRUCTURE") || x->isKindOfName("HORDE"))
			{
				continue;
			}
			if (x->getContainedBy() && !x->getContainedBy()->isKindOfName("HORDE"))
			{
				// lane IDLE-1: a rider (a ram's crew) moves with its container: MOVING on a rider whose container stands is counted apart
				const Object *c = x->getContainedBy();
				Track &t = riderTracks[x->getID()];
				const float dx = c->getPosition()->x - t.x, dy = c->getPosition()->y - t.y;
				t.x = c->getPosition()->x;
				t.y = c->getPosition()->y;
				t.movingStill = dx * dx + dy * dy < 0.25f && x->testModelCondition(moving) ? t.movingStill + 1 : 0;
				// the rider's own position against its container's (QA-2 samples the rider's own: a rider left behind reads as standing still)
				const float ox = x->getPosition()->x - c->getPosition()->x, oy = x->getPosition()->y - c->getPosition()->y;
				if (ox * ox + oy * oy > 1.0f && riderApart.insert(x->getID()).second && riderApart.size() <= 2)
				{
					samples << "  rider apart f" << logic.getFrame() << " " << x->getTemplate()->getName() << " #" << x->getID() << " from " << c->getTemplate()->getName()
							<< " by " << (int)ox << "," << (int)oy << " MOVING " << x->testModelCondition(moving) << "\n";
				}
				if (t.movingStill >= 3 && riderTreadmill.insert(x->getID()).second && riderLines < 6)
				{
					++riderLines;
					samples << "  rider treadmill f" << logic.getFrame() << " " << x->getTemplate()->getName() << " #" << x->getID() << " in " << c->getTemplate()->getName() << " #" << c->getID()
							<< " state " << ai->currentStateId() << " aiMoving " << ai->isMoving() << " goal " << (int)ai->mover().goalType() << "\n";
				}
				continue;
			}
			seen.insert(x->getID());
			Track &t = tracks[x->getID()];
			const float dx = x->getPosition()->x - t.x, dy = x->getPosition()->y - t.y;
			const bool still = dx * dx + dy * dy < 0.25f;
			t.x = x->getPosition()->x;
			t.y = x->getPosition()->y;
			t.still = still ? t.still + 1 : 0;
			t.movingStill = still && x->testModelCondition(moving) ? t.movingStill + 1 : 0;
			t.aiMovingStill = still && ai->isMoving() ? t.aiMovingStill + 1 : 0;
			const unsigned state = ai->currentStateId();
			if (state == (unsigned)AI_FOLLOW_EXITPRODUCTION_PATH)
			{
				exited.insert(x->getID());
				if (t.exitSince < 0)
				{
					t.exitSince = (int)logic.getFrame();
				}
				const int age = (int)logic.getFrame() - t.exitSince;
				if (age > longestExit)
				{
					longestExit = age;
					longestExitName = x->getTemplate()->getName() + " #" + std::to_string(x->getID());
				}
			}
			else
			{
				t.exitSince = -1;
			}
			const HordeContain *horde = x->getContainedBy() && x->getContainedBy()->getContain()
				? dynamic_cast<const HordeContain *>(x->getContainedBy()->getContain()) : nullptr;
			if (t.movingStill >= 3 && horde && horde->meleeEngaged())
			{
				treadmillInMelee.insert(x->getID());
			}
			if (t.movingStill >= 3 && treadmill[state].insert(x->getID()).second && sampleLines < 12)
			{
				++sampleLines;
				samples << "  treadmill" << (horde && horde->meleeEngaged() ? " (melee)" : "") << " f" << logic.getFrame() << " " << x->getTemplate()->getName() << " #" << x->getID() << " (" << (int)t.x << ","
						<< (int)t.y << ") state " << state << " aiMoving " << ai->isMoving() << " idle " << ai->isIdle() << "\n";
			}
			if (t.aiMovingStill >= 15 && stuck[state].insert(x->getID()).second && stuckLines < 8)
			{
				++stuckLines;
				samples << "  stuck f" << logic.getFrame() << " " << x->getTemplate()->getName() << " #" << x->getID() << " (" << (int)t.x << "," << (int)t.y
						<< ") state " << state << " goal " << (int)ai->mover().goalType() << " busy " << ai->isBusy() << "\n";
			}
		}
	}
	size_t treadmillCount(unsigned state) const
	{
		auto it = treadmill.find(state);
		return it == treadmill.end() ? 0 : it->second.size();
	}
	size_t treadmillOutsideMelee(unsigned state) const
	{
		size_t n = 0;
		auto it = treadmill.find(state);
		if (it != treadmill.end())
		{
			for (ObjectID id : it->second)
			{
				n += treadmillInMelee.count(id) ? 0 : 1;
			}
		}
		return n;
	}
	size_t stuckCount(unsigned state) const
	{
		auto it = stuck.find(state);
		return it == stuck.end() ? 0 : it->second.size();
	}
	std::string report() const
	{
		std::ostringstream os;
		os << "rider treadmill " << riderTreadmill.size() << ", riders apart from their container " << riderApart.size() << "; ";
		os << "treadmill in a melee " << treadmillInMelee.size() << ", idle outside a melee " << treadmillOutsideMelee(AI_IDLE) << "; units " << seen.size() << ", through the exit path " << exited.size() << ", longest in it " << longestExit << " frames (" << longestExitName << ")\n";
		for (const auto &e : treadmill)
		{
			os << "treadmill state " << e.first << ": " << e.second.size() << "\n";
		}
		for (const auto &e : stuck)
		{
			os << "stuck state " << e.first << ": " << e.second.size() << "\n";
		}
		return os.str() + samples.str();
	}
};

struct ExitGame
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
};

void loadExitGame(starttest::Shared &s, const NewGameMessage &message, int localSlot, ExitGame &g)
{
	std::string error;
	g.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, exitMapCache(s), RandomAlgorithm::ZH_CarryChain, *g.start, &error), error);
	if (localSlot >= 0)
	{
		g.start->localSlot = localSlot;
	}
	g.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *g.assets, s.options);
	LiveGame::Options o;
	o.start = g.start.get();
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
}

// one line per frame for the objects in `ids`: position, AI state, goal, MOVING and the containing horde with the member's slot
void traceObjects(GameLogic &logic, const std::set<ObjectID> &ids, std::ostringstream &out)
{
	static const int moving = AIUpdateInterface::modelConditionBit("MOVING");
	for (ObjectID id : ids)
	{
		Object *x = logic.findObjectByID(id);
		AIUpdateInterface *ai = x ? x->getAIUpdateInterface() : nullptr;
		if (!ai)
		{
			continue;
		}
		const AIMover &mv = ai->mover();
		out << "f" << logic.getFrame() << " #" << id << " (" << x->getPosition()->x << "," << x->getPosition()->y << ") o " << x->getOrientation() << " st "
			<< ai->currentStateId() << " busy " << ai->isBusy() << " idle " << ai->isIdle() << " mv " << ai->isMoving() << " act " << ai->isStateActive()
			<< " MOVING " << x->testModelCondition(moving) << " goal " << (int)mv.goalType() << " (" << mv.goalPosition().x << "," << mv.goalPosition().y << ")";
		if (Object *h = x->getContainedBy())
		{
			out << " | horde #" << h->getID() << " (" << h->getPosition()->x << "," << h->getPosition()->y << ")";
			if (AIUpdateInterface *hai = h->getAIUpdateInterface())
			{
				out << " st " << hai->currentStateId() << " mv " << hai->isMoving();
			}
			if (HordeContainInterface *hci = h->getContain() ? h->getContain()->getHordeContainInterface() : nullptr)
			{
				const Coord3D f = hci->getMemberFormationPosition(x);
				out << " slot (" << f.x << "," << f.y << ")";
			}
		}
		out << "\n";
	}
}

std::set<ObjectID> traceIdsFromEnv()
{
	std::set<ObjectID> ids;
	if (const char *env = std::getenv("OPENBFME_EXIT1_TRACE"))
	{
		std::stringstream ss(env);
		std::string part;
		while (std::getline(ss, part, ','))
		{
			ids.insert((ObjectID)std::stoul(part));
		}
	}
	return ids;
}
} // namespace

TEST_CASE("exit1 retail: a Udun 2v2 Hard computer game: produced members leave their exit path and no unit runs on the spot (RW 0x87471B, RW 0x5E7CC7)")
{
	OPENBFME_REQUIRE_START(s);
	ExitGame g;
	loadExitGame(*s, exitUdun(*s, 2, 1), -1, g);
	MotionProbe probe;
	for (int i = 0; i < 2000; ++i)
	{
		g.game->advance(0.2);
		probe.sample(g.game->logic());
	}
	MESSAGE(probe.report());
	// the game produced its armies (else the test proves nothing): ca05ef35 had 101 units standing in the exit state with MOVING, 59 stuck there, and 40 idle
	// ones keeping MOVING
	CHECK(probe.seen.size() >= 500);
	CHECK(probe.treadmillCount(AI_FOLLOW_EXITPRODUCTION_PATH) == 0);
	CHECK(probe.stuckCount(AI_FOLLOW_EXITPRODUCTION_PATH) == 0);
	// a produced member is made busy by its horde's hub in its first frames (RW 0x87479C); a single unit (a siege engine) walks its exit path out: 12 game seconds
	CHECK(probe.longestExit <= 60);
	// no idle unit keeps MOVING, in a melee or not (lane IDLE-1 r2: the member pass runs after the members' AI updates, as RW's updates[1] after updates[0])
	CHECK(probe.treadmillOutsideMelee(AI_IDLE) == 0);
	CHECK(probe.treadmillInMelee.empty());
}

// lane IDLE-1 r2 (community FB-0001): the horde members that turn where they stand end the frame without MOVING. RW's scheduler (RW 0x62E982) runs updates[0]
// in phases 3 / 4 and updates[1] in phase 5: every AI update (vslot 0x30 RW 0x851E97: 0) runs before every HordeContain (RW 0x490AC4: 1). The member update (RW
// 0x66C960) sets MOVING while it takes an angle goal; the member pass that follows in the same frame clears it again (the hub's turn RW 0x8750B8, the active
// member's hold RW 0x877BB8), so a turning member is never drawn running. The port filed both in updates[2] in object order: a member created after its horde
// ran after the pass and kept MOVING (r1 took that for retail: wrong). Sampled every frame over a Udun 2v2 Hard game with melees, sieges and cavalry.
TEST_CASE("idle1 retail: horde members turning where they stand end the frame without MOVING (updates[0] before updates[1]: RW 0x851E97, RW 0x490AC4)")
{
	OPENBFME_REQUIRE_START(s);
	ExitGame g;
	loadExitGame(*s, exitUdun(*s, 2, 1), -1, g);
	TurnProbe probe;
	for (int i = 0; i < 2600; ++i)
	{
		g.game->advance(0.2);
		probe.sample(g.game->logic());
	}
	MESSAGE("member-frames turning on the spot " << probe.turned << " (with MOVING " << probe.turnedMoving << ", still the frame after outside the attack state " << probe.turnedMovingOutsideAttack << "); attacking " << probe.turnedAttacking << " ("
										   << probe.turnedAttackingMoving << "); cavalry " << probe.turnedCavalry << " (" << probe.turnedCavalryMoving
										   << "); members standing with MOVING for 5 frames " << probe.stillMovingStreaks << "\n"
										   << probe.samples.str());
	// not vacuous: the game turns members on the spot, attacking ones (melee and the buildings' attackers) among them (the riders: test_idle1_turns.cpp)
	CHECK(probe.turned >= 1000);
	CHECK(probe.turnedAttacking >= 100);
	// a turn's MOVING is gone the frame after (the hub's turn clears it, RW 0x8750B8, and the member update ends the angle goal without it, RW 0x66C9A7), except
	// the attack's own aim turn (RW 0x75232F's vslot 0x21C, cleared by the aim's exit RW 0x74BEE7 with vslot 0x220 before the member update clears MOVING: it goes
	// with the member's next update or its horde's pass, within the 5 frame rule below)
	CHECK(probe.turnedMovingOutsideAttack == 0);
	CHECK(probe.stillMovingStreaks == 0);
}

TEST_CASE("exit1 diagnostic: the motion probe over a Udun 2v2 Hard game (OPENBFME_EXIT1_PROBE=<seed>,<frames>, OPENBFME_EXIT1_TRACE=<id>,...)" * doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	unsigned seed = 1;
	int frames = 3000;
	if (const char *env = std::getenv("OPENBFME_EXIT1_PROBE"))
	{
		std::sscanf(env, "%u,%d", &seed, &frames);
	}
	ExitGame g;
	loadExitGame(*s, exitUdun(*s, 2, seed), -1, g);
	const std::set<ObjectID> ids = traceIdsFromEnv();
	std::ostringstream trace;
	MotionProbe probe;
	for (int i = 0; i < frames; ++i)
	{
		g.game->advance(0.2);
		probe.sample(g.game->logic());
		traceObjects(g.game->logic(), ids, trace);
	}
	MESSAGE(probe.report() << trace.str());
}

TEST_CASE("exit1 diagnostic: the motion probe over a recorded game (OPENBFME_EXIT1_REPLAY=<file.replay>[,<frames>]; QA-2's matrix replays)" * doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	const char *env = std::getenv("OPENBFME_EXIT1_REPLAY");
	REQUIRE_MESSAGE(env != nullptr, "set OPENBFME_EXIT1_REPLAY");
	std::string path = env;
	int frames = -1;
	const size_t comma = path.rfind(',');
	if (comma != std::string::npos && path.find(".replay", comma) == std::string::npos)
	{
		frames = std::atoi(path.substr(comma + 1).c_str());
		path = path.substr(0, comma);
	}
	ReplayFile replay;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(path, replay, &error), error);
	// the recording's engine identity differs from this build's (the simulation sources changed): the recorded commands are played anyway, the hash comparison
	// then names the first frame this build plays differently
	ExitGame g;
	loadExitGame(*s, replay.header.game, replay.header.recordingSlot, g);
	ReplayPlayback playback(replay);
	RegisterLogicCRCHandler(g.game->dispatch());
	g.game->setFrameDriver(&playback);
	const std::set<ObjectID> ids = traceIdsFromEnv();
	std::ostringstream trace;
	MotionProbe probe;
	while (!playback.finished(g.game->logic()) && (frames < 0 || (int)g.game->logic().getFrame() < frames))
	{
		g.game->advance(0.2);
		probe.sample(g.game->logic());
		traceObjects(g.game->logic(), ids, trace);
	}
	g.game->setFrameDriver(nullptr);
	MESSAGE(path << ": frames " << g.game->logic().getFrame() << " of " << replay.finalFrame << ", first hash difference at frame "
				 << (playback.hasMismatch() ? (int)playback.firstMismatch().frame : -1) << "\n"
				 << probe.report() << trace.str());
}
