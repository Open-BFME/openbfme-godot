// OpenBFME behaviour driver (work order MECH-D): Classic vs Classic skirmishes for the before / after comparison of the wall-climbing port. Skipped by
// default; run with -tc="mech-d classic*" and OPENBFME_MECHD_OUT=<dir> (OPENBFME_MECHD_SEED, OPENBFME_MECHD_FRAMES). Each game is retail Mordor (start 0)
// against retail Men (start 2), both medium computer players, on the walled Amon Sul Fortress map with 1500 starting cash. Every LOGICFRAMES_PER_SECOND frames it writes one CSV row
// per living mobile unit of a playable side that is not a horde member: frame, player, id, template, horde, x, y, attacking (IS_ATTACKING / IS_MELEE_ATTACKING,
// or for a horde one of its members), idle (AI isIdle), leaving (IS_LEAVING_FACTORY); and the state hash at the end.

#include "doctest.h"

#include "PerfBenchUtil.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
int templateIndexOf(const PlayerTemplateStore &store, const std::string &name)
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
} // namespace

TEST_CASE("mech-d classic: Classic vs Classic behaviour trace (Mordor vs Men, medium, Amon Sul Fortress)" * doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	const char *outDir = std::getenv("OPENBFME_MECHD_OUT");
	REQUIRE_MESSAGE(outDir != nullptr, "set OPENBFME_MECHD_OUT");
	const int seed = perfbench::envInt("OPENBFME_MECHD_SEED", 1);
	const int frames = perfbench::envInt("OPENBFME_MECHD_FRAMES", 9000);
	auto scope = s->world->enterContext();
	std::vector<MapCacheEntry> cache;
	std::string error;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	NewGameMessage m;
	m.game.mapName = "maps/map mp amon sul fortress/map mp amon sul fortress.map";
	m.game.seed = (std::uint32_t)seed;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMordor", "FactionMen" };
	const int starts[2] = { 0, 2 };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_MED_AI;
		slot.name = std::u16string(u"Player") + (char16_t)(u'0' + i);
		slot.playerTemplate = templateIndexOf(s->world->playerTemplates(), factions[i]);
		REQUIRE(slot.playerTemplate >= 0); // tolerance: an index lookup, -1 means the faction name is unknown
		slot.startPos = starts[i];
		slot.color = i;
		slot.teamNumber = i;
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	const std::string path = std::string(outDir) + "/seed" + std::to_string(seed) + ".csv";
	std::FILE *out = std::fopen(path.c_str(), "w");
	REQUIRE_MESSAGE(out != nullptr, "cannot write " << path);
	std::fprintf(out, "frame,player,id,template,horde,x,y,attacking,idle,leaving\n");
	const int attacking = CombatNames::status("IS_ATTACKING");
	const int melee = CombatNames::status("IS_MELEE_ATTACKING");
	const int leaving = ObjectTemplateInfoBuilder::objectStatusIndex("IS_LEAVING_FACTORY");
	const int hordeKind = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	const int structureKind = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	REQUIRE(attacking >= 0); // tolerance: an index lookup, -1 means the name is unknown
	REQUIRE(melee >= 0); // tolerance: an index lookup, -1 means the name is unknown
	REQUIRE(leaving >= 0); // tolerance: an index lookup, -1 means the name is unknown
	REQUIRE(hordeKind >= 0); // tolerance: an index lookup, -1 means the name is unknown
	REQUIRE(structureKind >= 0); // tolerance: an index lookup, -1 means the name is unknown
	auto isAttacking = [&](const Object &x) { return x.testStatus((unsigned)attacking) || x.testStatus((unsigned)melee); };
	for (int f = 0; f < frames; ++f)
	{
		REQUIRE(game.advance(0.2) == 1);
		if ((logic.getFrame() % 5u) != 0u)
		{
			continue;
		}
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			const Player *p = x->getControllingPlayer();
			AIUpdateInterface *ai = x->getAIUpdateInterface();
			if (!p || !p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide || !ai || x->isEffectivelyDead() || x->getContainedBy() ||
				x->isKindOf((unsigned)structureKind))
			{
				continue;
			}
			const bool horde = x->isKindOf((unsigned)hordeKind);
			bool fighting = isAttacking(*x);
			if (horde && x->getContain() && x->getContain()->getContainedItemsList())
			{
				for (const Object *mem : *x->getContain()->getContainedItemsList())
				{
					fighting = fighting || (mem && isAttacking(*mem));
				}
			}
			std::fprintf(out, "%u,%d,%u,%s,%d,%.1f,%.1f,%d,%d,%d\n", logic.getFrame(), p->getPlayerIndex(), (unsigned)x->getID(), x->getTemplate()->getName().c_str(),
				horde ? 1 : 0, x->getPosition()->x, x->getPosition()->y, fighting ? 1 : 0, ai->isIdle() ? 1 : 0, x->testStatus((unsigned)leaving) ? 1 : 0);
		}
	}
	std::fprintf(out, "#hash,%u,%u\n", logic.getFrame(), (unsigned)logic.computeStateHash());
	std::fclose(out);
	std::printf("  info: mech-d classic seed %d: %u frames, end hash %u -> %s\n", seed, logic.getFrame(), (unsigned)logic.computeStateHash(), path.c_str());
}
