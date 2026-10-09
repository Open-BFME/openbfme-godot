// OpenBFME retail tests (lane BUILD-1): the castle base layouts of Bases.big / bases.big. Every .bse of the pure 2.01 mount parses with the engine's map reader and its
// CastleTemplates chunk equals the independent survey (tools/castle/castle_survey.py: its own grammar decode, engine/tests/data/castle-survey.json).  GPL-3.0.

#include "doctest.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/MiniJson.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "RetailTestMount.h"

#include "BuildTestUtil.h"

#include "Common/Player.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace
{
std::string lowerStr(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}
} // namespace

TEST_CASE("castle layouts: every .bse of the pure 2.01 mount parses and equals the independent survey")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("castle layouts");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<unsigned char> bytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/castle-survey.json", bytes, &err), err);
	JsonValue survey;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), survey, &err), err);
	const JsonValue *bases = survey.get("bases");
	REQUIRE(bases);
	// the survey's files by path
	std::map<std::string, const JsonValue *> expected;
	for (const auto &kv : bases->object)
	{
		expected[kv.first.substr(kv.first.find(':') + 1)] = &kv.second;
	}
	FilenameList list;
	mount->fs->getFileListInDirectory("", "", "*.bse", list, true);
	std::set<std::string> mounted;
	for (const std::string &p : list)
	{
		std::string q = lowerStr(p);
		std::replace(q.begin(), q.end(), '/', '\\');
		mounted.insert(q);
	}
	std::set<std::string> oracle;
	for (const auto &kv : expected)
	{
		oracle.insert(kv.first);
	}
	CHECK(mounted.size() == 196); // 207 files in the archives, 11 names in both RotWK's Bases.big (wins, mounted first) and BFME2's bases.big
	CHECK(mounted == oracle);

	CastleTemplateStore store;
	store.setLoader(CastleTemplateStore::fileSystemLoader(*mount->fs));
	size_t keyDiffers = 0;
	for (const auto &kv : expected)
	{
		const std::string &path = kv.first; // bases\<name>\<name>.bse
		const size_t a = path.find('\\') + 1, b = path.find('\\', a);
		const std::string name = path.substr(a, b - a);
		INFO(path);
		const JsonValue &e = *kv.second;
		if (lowerStr(e.get("key")->string) != name)
		{
			++keyDiffers;
			continue; // retail's store is keyed by the chunk's key: a name that is not it finds nothing (not asked for by any INI)
		}
		std::string error;
		const CastleTemplate *t = store.find(name, &error);
		REQUIRE_MESSAGE(t, error);
		CHECK(t->version == (int)e.get("version")->number);
		CHECK(t->entries.size() == (size_t)e.get("entries")->number);
		CHECK(t->lines.size() == (size_t)e.get("lines")->number);
		size_t points = 0;
		for (const CastleTemplateLine &l : t->lines)
		{
			points += l.points.size();
		}
		CHECK(points == (size_t)e.get("points")->number);
		std::map<std::string, size_t> counts;
		size_t firsts = 0;
		for (const CastleTemplateEntry &en : t->entries)
		{
			++counts[en.templateName];
			firsts += en.firstName.empty() ? 0 : 1;
		}
		CHECK(firsts == (size_t)e.get("firstNames")->number);
		const JsonValue *tm = e.get("templates");
		REQUIRE(tm);
		CHECK(counts.size() == tm->object.size());
		for (const auto &c : tm->object)
		{
			CHECK(counts[c.first] == (size_t)c.second.number);
		}
	}
	std::printf("  info: %zu base files, %zu whose chunk key differs from the file name\n", expected.size(), keyDiffers);
	// a name that is not a base is an error, never a default layout
	std::string error;
	CHECK(store.find("no_such_base", &error) == nullptr);
	CHECK(!error.empty());
	CHECK(store.errors().size() == 1);
}

namespace
{
// retail facts for the starting fortress of each faction: the PlayerTemplate, the base file the castle unpacks (CastleToUnpackForFaction of its fortress), the keep
struct Fortress
{
	const char *faction;
	const char *base;      // "Bases\\<base>\\<base>.bse"
	const char *centre;    // the StartingBuilding
	const char *keep;      // the Citadel the layout contains
};
const Fortress kFortresses[] = {
	{ "FactionMen", "fortress_men", "MenFortress", "MenFortressCitadel" },
	{ "FactionElves", "fortress_elves", "ElvenFortress", "ElvenCitadel" },
	{ "FactionDwarves", "fortress_dwarven", "DwarvenFortress", "DwarvenFortressCitadel" },
	{ "FactionIsengard", "fortress_isengard", "IsengardFortress", "IsengardFortressCitadel" },
	{ "FactionMordor", "fortress_mordor", "MordorFortress", "MordorFortressCitadel" },
	{ "FactionWild", "fortress_wild", "WildFortress", "WildFortressCitadel" },
	{ "FactionAngmar", "fortress_angmar", "AngmarFortress", "AngmarFortressCitadel" },
};

void checkFortress(const Fortress &f)
{
	OPENBFME_REQUIRE_START(s);
	INFO(f.faction);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, f.faction, "FactionMen", 77, g, &error), error);
	LiveGame &live = *g.live;
	CHECK(live.report().startErrors.empty());
	for (const std::string &e : live.report().startErrors)
	{
		MESSAGE("start error: " << e);
	}
	const GameLogic::Report rep = live.logic().report();
	for (const std::string &e : rep.errors)
	{
		MESSAGE("logic error: " << e);
	}
	CHECK(rep.errors.empty());
	// the independent survey of the base
	std::vector<unsigned char> bytes;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/castle-survey.json", bytes, &error), error);
	JsonValue survey;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), survey, &error), error);
	const JsonValue *expect = nullptr;
	for (const auto &kv : survey.get("bases")->object)
	{
		if (kv.first.find(std::string("bases\\") + f.base + "\\") != std::string::npos)
		{
			expect = &kv.second;
		}
	}
	REQUIRE(expect);
	// the player's starting structure
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(player);
	::Object *centre = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		::Object *o = p.structure ? live.logic().findObjectByID(p.id) : nullptr;
		if (o && o->getControllingPlayer() == player)
		{
			centre = o;
			CHECK(p.templateName == f.centre);
		}
	}
	REQUIRE(centre);
	CastleBehavior *castle = dynamic_cast<CastleBehavior *>(centre->findModule("CastleBehavior"));
	REQUIRE(castle);
	CHECK(castle->state() == CastleBehavior::STATE_UNPACKED);
	CHECK(castle->lastError().empty());
	CHECK(castle->baseNameFor(*player) == (std::string)expect->get("key")->string);
	// every entry of the layout became one object of the castle (the entries of an OPTIMIZED_PROP are not made: none in a fortress), owned by the player
	std::map<std::string, size_t> made;
	for (ObjectID id : castle->ownedObjects())
	{
		const ::Object *o = live.logic().findObjectByID(id);
		REQUIRE(o);
		++made[o->getTemplate()->getName()];
		CHECK(o->getControllingPlayer() == player);
		CHECK(!o->isUnderConstruction());
	}
	std::map<std::string, size_t> want;
	for (const auto &kv : expect->get("templates")->object)
	{
		want[kv.first] = (size_t)kv.second.number;
	}
	CHECK(made == want);
	CHECK(castle->ownedObjects().size() == (size_t)expect->get("entries")->number);
	CHECK(made[f.keep] == 1);
	// the plots are build plots: FoundationAIUpdate with the retail BuildVariation, a CastleMemberBehavior that knows the castle
	size_t plots = 0;
	for (ObjectID id : castle->ownedObjects())
	{
		::Object *o = live.logic().findObjectByID(id);
		FoundationAIUpdate *found = nullptr;
		for (const auto &m : o->modules())
		{
			if (FoundationAIUpdate *fa = m->getFoundationAIUpdate())
			{
				found = fa;
			}
		}
		if (o->isKindOfName("BASE_FOUNDATION"))
		{
			++plots;
			REQUIRE(found);
			CHECK((found->foundationData()->m_buildVariation == 1 || found->foundationData()->m_buildVariation == 2));
		}
		if (CastleMemberBehavior *cm = dynamic_cast<CastleMemberBehavior *>(o->findModule("CastleMemberBehavior")))
		{
			CHECK(cm->castleId() == centre->getID());
		}
	}
	CHECK(plots == made.size() * 0 + (size_t)expect->get("entries")->number - 1);
	CHECK(centre->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE")));
}
} // namespace

TEST_CASE("castle: the starting fortress of FactionMen unpacks the Bases.big layout") { checkFortress(kFortresses[0]); }
TEST_CASE("castle: the starting fortress of FactionElves unpacks the Bases.big layout") { checkFortress(kFortresses[1]); }
TEST_CASE("castle: the starting fortress of FactionDwarves unpacks the Bases.big layout") { checkFortress(kFortresses[2]); }
TEST_CASE("castle: the starting fortress of FactionIsengard unpacks the Bases.big layout") { checkFortress(kFortresses[3]); }
TEST_CASE("castle: the starting fortress of FactionMordor unpacks the Bases.big layout") { checkFortress(kFortresses[4]); }
TEST_CASE("castle: the starting fortress of FactionWild unpacks the Bases.big layout") { checkFortress(kFortresses[5]); }
TEST_CASE("castle: the starting fortress of FactionAngmar unpacks the Bases.big layout") { checkFortress(kFortresses[6]); }
