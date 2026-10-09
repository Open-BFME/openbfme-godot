// OpenBFME tests (lane HERO-2): Create-a-Hero. The record and its .cah file without data; with ROTWK_INSTALL / BFME2_INSTALL set: the 8 system heroes of
// Data1.big, TheCreateAHeroSystem of the retail CreateAHeroSystem.ini, and a system hero recruited from a skirmish fortress (its upgrades, its per-rank command
// set and the build surcharge). Independent expectations: the record layout and the CRC-32 checksum were decoded from RW 0x80B4F5 and checked against the
// 8 retail files with a separate Python reader (every stored checksum matches); the bling lists and the costs are recomputed here from the INI data.

#include "doctest.h"

#include "GameNetwork/GameInfo.h"

#include "Common/BuildAssistant.h"
#include "Common/MiniJson.h"
#include "Common/NumericState.h"
#include "Common/CreateAHeroRecord.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "Common/SpecialPower.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/HeroSystem.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/SimMath.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
CreateAHeroHero sampleHero()
{
	CreateAHeroHero h;
	h.objectID = 57;
	h.name = u"Fhaleen";
	h.classIndex = 5;
	h.subClassIndex = 1;
	h.primaryColor = 0xFFEF3946u;
	h.secondaryColor = 0xFFF83446u;
	h.tertiaryColor = 0xFFEE3D4Au;
	h.powers[0] = { "Command_CreateAHero_CM_ToggleWeapon", 0, 1 };
	h.powers[1] = { "Command_CreateAHeroAssassin_Level1", 2, 3 };
	h.setBling("CreateAHero_Weapon", 1);
	h.setBling("CreateAHero_ArmorAttribute", 7);
	h.uniqueID = "47C6206B5C124324A54A2DA3";
	h.isSystemHero = true;
	return h;
}
} // namespace

TEST_CASE("create-a-hero: CRC-32 is the reflected 0xEDB88320 polynomial (RW 0xA2D770, the table RW 0xDBBB70)")
{
	const char *s = "123456789";
	CHECK(CreateAHeroRecord::crc32(s, 9, 0) == 0xCBF43926u); // the standard check value
	// chaining: two halves equal the whole
	CHECK(CreateAHeroRecord::crc32(s + 4, 5, CreateAHeroRecord::crc32(s, 4, 0)) == 0xCBF43926u);
}

TEST_CASE("create-a-hero: a record saves as a 2STR stream and loads back with a valid checksum (RW 0x80B4F5, RW 0xA200A3)")
{
	const CreateAHeroHero h = sampleHero();
	const std::vector<std::uint8_t> bytes = h.save();
	REQUIRE(bytes.size() > 17);
	CHECK(std::string(bytes.begin(), bytes.begin() + 8) == "ALAE2STR");
	CHECK(bytes[8] == 1);  // the version word
	CHECK(bytes[12] == 0); // not a block stream
	CHECK(bytes[16] == 8); // the record version
	CreateAHeroHero back;
	std::string err;
	REQUIRE_MESSAGE(back.load(bytes, &err), err);
	CHECK(back.valid);
	CHECK(back.checksum == h.computeChecksum());
	CHECK(back.flags == 0x2FFu);
	CHECK(back.name == h.name);
	CHECK(back.classIndex == 5);
	CHECK(back.subClassIndex == 1);
	CHECK(back.primaryColor == h.primaryColor);
	CHECK(back.powers[1].commandButton == "Command_CreateAHeroAssassin_Level1");
	CHECK(back.powers[1].expLevel == 2);
	CHECK(back.powers[1].buttonIndex == 3);
	REQUIRE(back.findBling("CreateAHero_ArmorAttribute"));
	CHECK(*back.findBling("CreateAHero_ArmorAttribute") == 7);
	CHECK(back.uniqueID == h.uniqueID);
	CHECK(back.isSystemHero);
	CHECK(back.save() == bytes);

	// a changed field keeps the stored checksum: the record loads, not valid (RW 0x80BA7C)
	std::vector<std::uint8_t> edited = bytes;
	edited[17] ^= 1; // the low byte of ID
	CreateAHeroHero bad;
	REQUIRE(bad.load(edited, &err));
	CHECK_FALSE(bad.valid);
	// the unique id is outside the checksum
	CreateAHeroHero other = h;
	other.uniqueID = "SOMETHINGELSE";
	CHECK(other.computeChecksum() == h.computeChecksum());
	// a wrong header, a truncated record, trailing bytes and an empty group name are errors
	std::vector<std::uint8_t> wrong = bytes;
	wrong[0] = 'X';
	CHECK_FALSE(CreateAHeroHero().load(wrong, &err));
	std::vector<std::uint8_t> shortened(bytes.begin(), bytes.end() - 3);
	CHECK_FALSE(CreateAHeroHero().load(shortened, &err));
	std::vector<std::uint8_t> longer = bytes;
	longer.push_back(0);
	CHECK_FALSE(CreateAHeroHero().load(longer, &err));
	CreateAHeroHero empty = h;
	empty.bling.emplace_back("", 0);
	CHECK_FALSE(CreateAHeroHero().load(empty.save(), &err));
	CHECK(err.find("DoXfer") != std::string::npos);
}

namespace
{
struct Shared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
};

Shared &shared()
{
	static Shared s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
		}
	}
	return s;
}

#define REQUIRE_RETAIL(sh)                                                               \
	Shared &sh = shared();                                                               \
	if (!sh.mount || !sh.mount->fs)                                                      \
	{                                                                                    \
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail Create-a-Hero test)"); \
		return;                                                                          \
	}                                                                                    \
	REQUIRE_MESSAGE(sh.world, sh.error);                                                 \
	auto contextScope = sh.world->enterContext()

const CreateAHeroHero *heroNamed(const std::vector<CreateAHeroHero> &all, const std::u16string &name)
{
	for (const CreateAHeroHero &h : all)
	{
		if (h.name == name)
		{
			return &h;
		}
	}
	return nullptr;
}

unsigned groupOrder(const CreateAHeroSystem &sys, int bling)
{
	const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(sys.bling(bling)->upgradeName);
	REQUIRE(u);
	return u->m_groupOrder;
}
} // namespace

TEST_CASE("create-a-hero retail: the 8 system heroes of Data1.big load with valid checksums and save back byte for byte")
{
	REQUIRE_RETAIL(sh);
	std::vector<std::string> errors;
	const std::vector<CreateAHeroHero> heroes = CreateAHeroLibrary::systemHeroes(*sh.mount->fs, &errors);
	CHECK(errors.empty());
	REQUIRE(heroes.size() == 8);
	std::set<std::u16string> names;
	for (const CreateAHeroHero &h : heroes)
	{
		names.insert(h.name);
		CHECK(h.valid);
		CHECK(h.isSystemHero);
		CHECK(h.version == 8);
		CHECK_FALSE(h.uniqueID.empty());
	}
	CHECK(names == std::set<std::u16string>{ u"Alcarin", u"Berethor", u"Fhaleen", u"Hadhod", u"Idrial", u"Krashnak", u"Morwen", u"Thrugg" });
	// byte identity: each file is exactly what save() writes
	FilenameList files;
	sh.mount->fs->getFileListInDirectory("", "", "*.cah", files, true);
	int compared = 0;
	for (const std::string &f : files)
	{
		std::vector<std::uint8_t> bytes;
		std::string err;
		REQUIRE_MESSAGE(sh.mount->fs->readFile(f, bytes, &err), err);
		CreateAHeroHero h;
		REQUIRE_MESSAGE(h.load(bytes, &err), f << ": " << err);
		CHECK_MESSAGE(h.save() == bytes, f);
		++compared;
	}
	CHECK(compared == 8);
	const CreateAHeroHero *b = heroNamed(heroes, u"Berethor");
	REQUIRE(b);
	CHECK(b->classIndex == 0);
	CHECK(b->subClassIndex == 0);
	CHECK(b->powers[0].commandButton == "Command_CreateAHero_HotW_SummonAllies_Level1");
	CHECK(b->bling.size() == 12);
}

TEST_CASE("create-a-hero retail: TheCreateAHeroSystem parses CreateAHeroSystem.ini (RW 0x61A10F)")
{
	REQUIRE_RETAIL(sh);
	for (const SubsystemLoadReport::FileError &e : sh.world->report().errors)
	{
		CHECK_MESSAGE(e.file.find("CreateAHero") == std::string::npos, e.file << ": " << e.message);
	}
	const CreateAHeroSystem &sys = sh.world->createAHeroSystem();
	REQUIRE(sys.loaded());
	CHECK(sys.gameModeUpgradeName == "Upgrade_CreateAHeroGameMode");
	CHECK(sys.mapModeUpgradeName == "Upgrade_CreateAHeroMapMode");
	CHECK(sys.canBuildUpgradeName == "Upgrade_AllowBuildCreateAHero");
	CHECK(sys.commandSetTemplate == "CreateAHeroCommandSetTemplate");
	CHECK(sys.heroRevivalDiscount == 75);
	CHECK(sys.specialPowerDiscountPerLevel == 10);
	CHECK(sys.weaponGroupName == "CreateAHero_Weapon");
	// the classes in #include order
	REQUIRE(sys.classes().size() == 7);
	CHECK(sys.classes()[0].upgradeName == "Upgrade_CreateAHero_ClassHeroOfTheWest");
	CHECK(sys.binders().size() == 12); // 5 ATTRIBUTE + 7 APPEARANCE binders
	int attributeBinders = 0;
	for (const CreateAHeroBlingBinder &b : sys.binders())
	{
		attributeBinders += b.blingType == CAH_BLING_ATTRIBUTE ? 1 : 0;
	}
	CHECK(attributeBinders == 5);
	// Captain of Gondor: the weapon list of its BlingUpgrades, ordered by the upgrades' GroupOrder
	const CreateAHeroSubClass *captain = sys.subClass(0, 0);
	REQUIRE(captain);
	CHECK(captain->upgradeName == "Upgrade_CreateAHero_SubClass_0");
	CHECK(captain->defaultFaction == 0); // Men
	CHECK(captain->usableFactions == std::vector<int>{ 0, 1, 2 });
	CHECK(captain->spendableAttributePoints == 30);
	const std::vector<int> *weapons = captain->findGroup("CreateAHero_Weapon");
	REQUIRE(weapons);
	std::multiset<std::string> names;
	for (int i : *weapons)
	{
		names.insert(sys.bling(i)->upgradeName);
	}
	CHECK(names == std::multiset<std::string>{ "Upgrade_CHW03", "Upgrade_CHW04", "Upgrade_CHW05", "Upgrade_CHW06", "Upgrade_CHW27" });
	for (size_t i = 1; i < weapons->size(); ++i)
	{
		CHECK(groupOrder(sys, (*weapons)[i - 1]) <= groupOrder(sys, (*weapons)[i]));
	}
	// an attribute group: every Upgrade_ArmorAttributeNN, by GroupOrder (0 .. 19)
	const std::vector<int> *armor = captain->findGroup("CreateAHero_ArmorAttribute");
	REQUIRE(armor);
	REQUIRE(armor->size() == 20);
	for (size_t i = 0; i < armor->size(); ++i)
	{
		CHECK(groupOrder(sys, (*armor)[i]) == (unsigned)i);
	}
	CHECK(sys.bling((*armor)[15])->upgradeName == "Upgrade_ArmorAttribute16");
	CHECK(captain->findAttribute("CreateAHero_ArmorAttribute")->defaultValueUpgrade == "Upgrade_ArmorAttribute16");
}

namespace
{
struct Game
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	CommandList list;
	std::unique_ptr<GameLogicDispatch> dispatch;
	Player *me = nullptr;
	Player *enemy = nullptr;
	explicit Game(Shared &sh, const std::string &faction)
		: players(sh.world->nameKeys(), sh.world->playerTemplates(), teams)
	{
		SkirmishSetup setup;
		setup.players.push_back({ "Tester", faction, true, 0, 0, 0 });
		setup.players.push_back({ "Enemy", "FactionMordor", false, 1, 0, 1 });
		setup.startingMoney = 100000000;
		setup.defaultStartingCash = 100000000;
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(sh.world->things(), sh.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		logic->random().seedRandom(1);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*sh.mount->fs, logic->settings(), &err), err);
		logic->productionSettings() = sh.world->productionSettings();
		logic->setUpgradeTypes(&sh.world->upgradeTypes());
		REQUIRE_MESSAGE(EconomySettings::load(*sh.mount->fs, logic->economy().settings(), &err), err);
		logic->economy().initAllCommandPoints();
		me = players.findPlayerWithName("Tester");
		enemy = players.findPlayerWithName("Enemy");
		REQUIRE(me);
		REQUIRE(enemy);
		me->commandPoints().setFromScript(100000000, 100000000);
		dispatch = std::make_unique<GameLogicDispatch>(*logic);
		dispatch->attach(list);
	}
	~Game()
	{
		if (logic)
		{
			logic->reset();
		}
	}
	Object *make(const ThingTemplate *tt, Player *owner, float x, float y)
	{
		Object *o = logic->newObject(tt, owner->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		const Coord3D at = { x, y, 0.0f };
		o->setPosition(&at);
		return o;
	}
};

// RW 0x75CD7A with SpecialPowerDiscountPerLevel 10, recomputed
int expectedPowerCost(const CreateAHeroHero &h, const CommandStore &commands)
{
	int total = 0;
	for (int i = 0; i < CreateAHeroHero::POWER_COUNT; ++i)
	{
		const CommandButton *b = h.powers[(size_t)i].commandButton.empty() ? nullptr : commands.findCommandButton(h.powers[(size_t)i].commandButton);
		if (!b || b->m_createAHeroUICostIfSelected <= 0)
		{
			continue;
		}
		const int level = i + 1, cost = b->m_createAHeroUICostIfSelected, minimum = b->m_createAHeroUIMinimumLevel;
		total += level < minimum || level == minimum ? cost : cost - cost * (level - minimum) * 10 / 100;
	}
	return total;
}
} // namespace

TEST_CASE("create-a-hero retail: a system hero assigned to a skirmish player is built with its upgrades, rank command set and surcharge")
{
	REQUIRE_RETAIL(sh);
	std::vector<std::string> errors;
	const std::vector<CreateAHeroHero> heroes = CreateAHeroLibrary::systemHeroes(*sh.mount->fs, &errors);
	const CreateAHeroHero *berethor = heroNamed(heroes, u"Berethor");
	REQUIRE(berethor);
	Game g(sh, "FactionMen");
	const ThingTemplate *cah = sh.world->things().findTemplate("CreateAHero");
	REQUIRE(cah);
	const int base = BuildAssistant::calcCostToBuild(*cah, g.me, nullptr, -1);

	g.logic->createAHeroes().assign(*g.me, *berethor);
	g.logic->createAHeroes().startGame(); // RW 0x61B103
	CHECK(g.me->hasUpgradeComplete("Upgrade_AllowBuildCreateAHero"));
	CHECK_FALSE(g.enemy->hasUpgradeComplete("Upgrade_AllowBuildCreateAHero"));
	const int surcharge = expectedPowerCost(*berethor, sh.world->commands());
	CHECK(surcharge > 0);
	CHECK(g.me->getCreateAHeroSurcharge() == surcharge);
	CHECK(BuildAssistant::calcCostToBuild(*cah, g.me, nullptr, -1) == base + surcharge);
	CHECK(BuildAssistant::calcCostToBuild(*cah, g.me, nullptr, (int)BuildAssistant::buildCost(*cah)) == base + surcharge);
	CHECK(BuildAssistant::calcCostToBuild(*cah, g.enemy, nullptr, -1) == base);
	CHECK(BuildAssistant::calcCostToBuild(*cah, g.me, nullptr, 7) == 7); // another override: no surcharge (RW 0x73C2AD)

	// the hero is made: its record's upgrades
	Object *hero = g.make(cah, g.me, 500.0f, 500.0f);
	CreateAHeroHero *rec = g.logic->createAHeroes().heroOf(*g.me);
	REQUIRE(rec);
	CHECK(rec->objectID == hero->getID());
	CHECK(hero->hasUpgrade("Upgrade_CreateAHeroGameMode"));
	CHECK_FALSE(hero->hasUpgrade("Upgrade_CreateAHeroMapMode"));
	CHECK(hero->hasUpgrade("Upgrade_CreateAHero_ClassHeroOfTheWest"));
	CHECK(hero->hasUpgrade("Upgrade_CreateAHero_SubClass_0"));
	const CreateAHeroSystem &sys = sh.world->createAHeroSystem();
	const CreateAHeroSubClass *captain = sys.subClass(0, 0);
	REQUIRE(captain);
	int checked = 0;
	for (const auto &b : berethor->bling)
	{
		const std::vector<int> *list = captain->findGroup(b.first);
		REQUIRE_MESSAGE(list, b.first);
		REQUIRE(b.second < list->size());
		const std::string upgrade = sys.bling((*list)[b.second])->upgradeName;
		CHECK_MESSAGE(hero->hasUpgrade(upgrade), b.first << " " << b.second << " -> " << upgrade);
		++checked;
	}
	CHECK(checked == 12);
	CHECK(hero->hasUpgrade("Upgrade_ArmorAttribute16")); // ArmorAttribute index 15
	CHECK(rec->flags == 0xF0u); // 0x2FF: 4 -> 8, 3 -> 0x80, 8 and 0x200 consumed; 0x10 .. 0x40 are the builder screen's

	// RW 0x80AA38: the tracker lists the hero's own level chain; level 1 carries the unlock upgrade of power slot 0's button (RW 0x80A190 / 0x68AB07)
	ExperienceTracker *tracker = hero->getExperienceTracker();
	REQUIRE(tracker);
	CHECK(tracker->levelTargetName() == "CreateAHero_" + berethor->uniqueID);
	const ExperienceLevelTemplate *l1 = sh.world->experienceLevels().findLevel("CreateAHeroLevel1_" + berethor->uniqueID);
	REQUIRE(l1);
	CHECK(l1->m_targetNames == std::vector<std::string>{ "CreateAHero_" + berethor->uniqueID });
	REQUIRE(l1->m_upgrades.size() == 1);
	const CommandButton *b0 = sh.world->commands().findCommandButton(berethor->powers[0].commandButton);
	REQUIRE(b0);
	MESSAGE("power 0 " << b0->m_name << " -> " << b0->m_specialPowerName << " unlocked by " << l1->m_upgrades[0]->getUpgradeName());
	const ExperienceLevelTemplate *l10 = sh.world->experienceLevels().findLevel("CreateAHeroLevel10_" + berethor->uniqueID);
	REQUIRE(l10);
	const std::vector<const ExperienceLevelTemplate *> *chain = sh.world->experienceLevels().levelsFor("CreateAHero_" + berethor->uniqueID);
	REQUIRE(chain);
	CHECK(chain->size() == 10);

	// the first level's grant (phase 5) builds the rank command set
	for (int i = 0; i < 3; ++i)
	{
		g.logic->runLogicFrame();
	}
	ExperienceTracker *t = hero->getExperienceTracker();
	REQUIRE(t);
	const std::string setName = "CommandSet_" + hero->getName() + "_" + berethor->uniqueID + "_rank_" + std::to_string(t->getRank());
	CHECK(hero->getCommandSetOverride() == setName);
	const CommandSet *set = sh.world->commands().findCommandSet(setName);
	REQUIRE(set);
	REQUIRE(set->getCommandButton(1));
	CHECK(set->getCommandButton(1)->m_name == "Command_CreateAHero_HotW_SummonAllies_Level1");
	REQUIRE(set->getCommandButton(2));
	CHECK(set->getCommandButton(2)->m_name == "Command_CreateAHero_SpecialAbilityCreateAHeroTrainAllies_Level_1");
	REQUIRE(set->getCommandButton(5));
	CHECK(set->getCommandButton(5)->m_name == "Command_CreateAHero_HotW_CrippleStrikeMelee_Level1");
	REQUIRE(set->getCommandButton(16));
	CHECK(set->getCommandButton(16)->m_name == "Command_AttackMove");
	// the template's other slots are copied
	const CommandSet *tmpl = sh.world->commands().findCommandSet(sys.commandSetTemplate);
	REQUIRE(tmpl);
	for (int i = 6; i < CommandSet::MAX_BUTTONS; ++i)
	{
		if (i != 16)
		{
			CHECK(set->getCommandButton(i) == tmpl->getCommandButton(i));
		}
	}
	// the level grant gave level 1's upgrade: power slot 0's special power is unpaused (UnpauseSpecialPowerUpgrade)
	CHECK(hero->hasUpgrade(l1->m_upgrades[0]));
	const SpecialPowerTemplate *p0 = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(b0->m_specialPowerName) : nullptr;
	REQUIRE(p0);
	const SpecialPowerModule *sp0 = dynamic_cast<const SpecialPowerModule *>(SpecialPowerModules::findModule(*hero, p0));
	REQUIRE(sp0);
	CHECK(sp0->pauseCount() == 0);
	// a later rank: the higher power levels replace their slot (ExpLevel below the rank)
	g.logic->createAHeroes().buildCommandSet(*hero, 10);
	const CommandSet *ten = sh.world->commands().findCommandSet("CommandSet_" + hero->getName() + "_" + berethor->uniqueID + "_rank_10");
	REQUIRE(ten);
	CHECK(ten->getCommandButton(1)->m_name == "Command_CreateAHero_HotW_SummonAllies_Level4");
	CHECK(ten->getCommandButton(2)->m_name == "Command_CreateAHero_SpecialAbilityCreateAHeroTrainAllies_Level_2");
	CHECK(ten->getCommandButton(5)->m_name == "Command_CreateAHero_HotW_CrippleStrikeMelee_Level2");

	// a CreateAHero of a player without a hero gets nothing
	Object *plain = g.make(cah, g.enemy, 900.0f, 900.0f);
	CHECK_FALSE(plain->hasUpgrade("Upgrade_CreateAHero_ClassHeroOfTheWest"));
	CHECK(g.logic->createAHeroes().stats().noRecord >= 1);

	bool stop = false;
	for (const std::string &s : g.logic->report().stops)
	{
		stop = stop || s.rfind("[S-1226]", 0) == 0;
	}
	CHECK(stop);
}

TEST_CASE("create-a-hero retail: the fortress recruits the player's Create-a-Hero through its REVIVE slot at the surcharged cost")
{
	REQUIRE_RETAIL(sh);
	std::vector<std::string> errors;
	const std::vector<CreateAHeroHero> heroes = CreateAHeroLibrary::systemHeroes(*sh.mount->fs, &errors);
	const CreateAHeroHero *berethor = heroNamed(heroes, u"Berethor");
	REQUIRE(berethor);
	Game g(sh, "FactionMen");
	g.logic->createAHeroes().assign(*g.me, *berethor);
	g.logic->createAHeroes().startGame();
	HeroSystem::initPlayer(*g.logic, *g.me);
	const ThingTemplate *cah = sh.world->things().findTemplate("CreateAHero");
	REQUIRE(cah);
	const int index = g.me->heroes().findIndex(*cah, 0xFFFFFFFFu, 0);
	REQUIRE(index == 0); // BuildableHeroesMP starts with CreateAHero
	// the fortress: the faction's structure with ProductionUpdate whose CommandSet has the most REVIVE buttons (as test_hero_retail.cpp picks it)
	std::vector<unsigned char> census;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/hero/faction_heroes.json", census, &err), err);
	JsonValue json;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(census.begin(), census.end()), json, &err), err);
	const JsonValue *structures = json.get("FactionMen") ? json.get("FactionMen")->get("structures") : nullptr;
	REQUIRE(structures);
	const ThingTemplate *fortress = nullptr;
	int best = 0;
	for (const JsonValue &v : structures->array)
	{
		const ThingTemplate *tt = sh.world->things().findTemplate(v.string);
		const FieldValue *f = tt ? tt->getFinalOverride()->findField("CommandSet") : nullptr;
		const std::string *setName = f ? std::get_if<std::string>(f) : nullptr;
		const CommandSet *set = setName ? sh.world->commands().findCommandSet(*setName) : nullptr;
		bool production = false;
		for (const ThingTemplate::Nugget &n : tt ? tt->getFinalOverride()->behaviorModules().nuggets() : std::vector<ThingTemplate::Nugget>())
		{
			production = production || n.name == "ProductionUpdate";
		}
		if (!set || !production)
		{
			continue;
		}
		int revives = 0;
		for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
		{
			revives += set->getCommandButton(i) && set->getCommandButton(i)->m_command == GUI_COMMAND_REVIVE ? 1 : 0;
		}
		if (revives > best)
		{
			best = revives;
			fortress = tt;
		}
	}
	REQUIRE(fortress);
	Object *keep = g.make(fortress, g.me, 2000.0f, 2000.0f);
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, g.me->getPlayerIndex());
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(keep->getID());
	g.list.append(sel);
	g.logic->runLogicFrame();
	GameMessage m(MSG_QUEUE_UNIT_CREATE, g.me->getPlayerIndex());
	m.appendBooleanArgument(true);
	m.appendIntegerArgument(index);
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(false);
	m.appendBooleanArgument(false);
	g.list.append(m);
	g.logic->runLogicFrame();
	ProductionUpdateInterface *pu = keep->getProductionUpdate();
	REQUIRE(pu);
	REQUIRE(pu->getProductionCount() == 1);
	const int surcharge = g.me->getCreateAHeroSurcharge();
	CHECK(surcharge > 0);
	const int expected = SimMath::truncToInt32(NumericState::pc24Mul((float)BuildAssistant::calcCostToBuild(*cah, g.me, nullptr, (int)BuildAssistant::buildCost(*cah)), pu->heroCostMultiplier(false)));
	CHECK(pu->firstProduction()->cost == expected);
	Object *hero = nullptr;
	for (int i = 0; i < 3000 && !hero; ++i)
	{
		g.logic->runLogicFrame();
		for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate() == cah && o->getControllingPlayer() == g.me)
			{
				hero = o;
			}
		}
	}
	REQUIRE(hero);
	CHECK(hero->hasUpgrade("Upgrade_CreateAHero_SubClass_0"));
	for (int i = 0; i < 5; ++i)
	{
		g.logic->runLogicFrame();
	}
	CHECK(hero->getCommandSetOverride().rfind("CommandSet_", 0) == 0);

	// its death: the revive record costs calcCostToBuild (with the surcharge) * HeroRevivalDiscount 75 / 100 (RW 0x780F21)
	const unsigned full = (unsigned)BuildAssistant::calcCostToBuild(*cah, g.me, nullptr, -1);
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_amount = 100000.0f;
	info.m_input.m_kill = true;
	hero->attemptDamage(info);
	for (int i = 0; i < 5; ++i)
	{
		g.logic->runLogicFrame();
	}
	bool found = false;
	for (const HeroRecord &r : g.me->heroes().records())
	{
		if (r.dead && r.templateName == "CreateAHero")
		{
			found = true;
			CHECK(r.cost == full * 75u / 100u);
		}
	}
	CHECK(found);
}

TEST_CASE("create-a-hero retail: every chosen field of the slot hero is in the world hash (powers, unlock ranks, button slots, the unique id, the bling)")
{
	REQUIRE_RETAIL(sh);
	std::vector<std::string> errors;
	const std::vector<CreateAHeroHero> heroes = CreateAHeroLibrary::systemHeroes(*sh.mount->fs, &errors);
	const CreateAHeroHero *berethor = heroNamed(heroes, u"Berethor");
	REQUIRE(berethor);
	Game g(sh, "FactionMen");
	g.logic->createAHeroes().assign(*g.me, *berethor);
	const std::uint32_t base = g.logic->computeStateHash();
	auto with = [&](const std::string &what, const std::function<void(CreateAHeroHero &)> &change) {
		CreateAHeroHero h = *berethor;
		change(h);
		g.logic->createAHeroes().assign(*g.me, h);
		CHECK_MESSAGE(g.logic->computeStateHash() != base, what);
		g.logic->createAHeroes().assign(*g.me, *berethor);
		CHECK_MESSAGE(g.logic->computeStateHash() == base, what << " (restored)");
	};
	with("a power's unlock rank", [](CreateAHeroHero &h) { h.powers[1].expLevel += 3; });
	with("a power's button slot", [](CreateAHeroHero &h) { h.powers[1].buttonIndex = 7; });
	with("a chosen power", [](CreateAHeroHero &h) { h.powers[2].commandButton = "Command_CreateAHeroAssassin_Level1"; });
	with("an empty power slot filled", [](CreateAHeroHero &h) { h.powers[12].commandButton = "Command_CreateAHeroLeadership"; });
	with("the unique id", [](CreateAHeroHero &h) { h.uniqueID += "X"; });
	with("a bling index", [](CreateAHeroHero &h) { h.bling[0].second += 1; });
	with("a bling group added", [](CreateAHeroHero &h) { h.bling.emplace_back("CreateAHero_Extra", 0); });
	with("a colour", [](CreateAHeroHero &h) { h.tertiaryColor ^= 1; });
	with("the name", [](CreateAHeroHero &h) { h.name += u"s"; });
}

TEST_CASE("create-a-hero retail: a game setup's slot hero is checked field by field (CreateAHeroSystem::validateHero): the 8 system heroes pass, each "
		  "broken field is named")
{
	REQUIRE_RETAIL(sh);
	std::vector<std::string> errors;
	const std::vector<CreateAHeroHero> heroes = CreateAHeroLibrary::systemHeroes(*sh.mount->fs, &errors);
	REQUIRE(heroes.size() == 8);
	const CreateAHeroSystem &sys = sh.world->createAHeroSystem();
	const CommandStore &commands = sh.world->commands();
	std::string why;
	for (const CreateAHeroHero &h : heroes)
	{
		CHECK_MESSAGE(sys.validateHero(h, commands, &why), why);
	}
	const CreateAHeroHero *berethor = heroNamed(heroes, u"Berethor");
	REQUIRE(berethor);
	// a changed field, the record then saved and loaded again (a checksum of its own), as a setup would carry it
	auto refused = [&](const std::string &what, const std::function<void(CreateAHeroHero &)> &change) {
		CreateAHeroHero h = *berethor;
		change(h);
		CreateAHeroHero loaded;
		std::string e;
		REQUIRE_MESSAGE(loaded.load(h.save(), &e), e);
		CHECK_MESSAGE(!sys.validateHero(loaded, commands, &why), what);
		CHECK_MESSAGE(why.find(what) != std::string::npos, what << ": " << why);
	};
	refused("no name", [](CreateAHeroHero &h) { h.name.clear(); });
	refused("class 9 subclass 0", [](CreateAHeroHero &h) { h.classIndex = 9; });
	refused("class 0 subclass 7", [](CreateAHeroHero &h) { h.subClassIndex = 7; });
	refused("power 3 names the unknown button Command_NoSuchPower", [](CreateAHeroHero &h) { h.powers[3].commandButton = "Command_NoSuchPower"; });
	refused("power 4 unlock rank 15", [](CreateAHeroHero &h) { h.powers[4].expLevel = 15; });
	refused("power 5 command set slot 33", [](CreateAHeroHero &h) { h.powers[5].buttonIndex = 33; });
	refused("power 12 has no button but a rank or slot", [](CreateAHeroHero &h) { h.powers[12].buttonIndex = 2; });
	refused("the bling group CreateAHero_NoSuchGroup is not one of the subclass", [](CreateAHeroHero &h) { h.bling.emplace_back("CreateAHero_NoSuchGroup", 0u); });
	refused("the bling group CreateAHero_Helmet index 99", [](CreateAHeroHero &h) { h.setBling("CreateAHero_Helmet", 99); });
	refused("no unique id", [](CreateAHeroHero &h) { h.uniqueID.clear(); });
	refused("a unique id character outside printable ASCII", [](CreateAHeroHero &h) { h.uniqueID += " x"; });
	// the record itself: a checksum that does not match, a record that was not loaded
	CreateAHeroHero bad = *berethor;
	bad.valid = false;
	CHECK_FALSE(sys.validateHero(bad, commands, &why));
	CHECK(why.find("checksum") != std::string::npos);
	bad = *berethor;
	bad.primaryColor ^= 1; // changed after the load: the stored checksum is stale
	CHECK_FALSE(sys.validateHero(bad, commands, &why));
	// a duplicate group (setBling never makes one; a file can hold one)
	CreateAHeroHero dup = *berethor;
	dup.bling.push_back(dup.bling.front());
	CreateAHeroHero dupLoaded;
	std::string e;
	REQUIRE(dupLoaded.load(dup.save(), &e));
	CHECK(dupLoaded.bling.size() == berethor->bling.size()); // the load merges it (RW 0x80A73B)
	// the stored checksum covered both entries: the merged record is not its own wire form and is refused (a setup carries setCreateAHero's form)
	CHECK_FALSE(sys.validateHero(dupLoaded, commands, &why));
	SkirmishGameSlot slot;
	CHECK_FALSE(slot.setCreateAHeroBytes(dup.save(), &why));
	CHECK(why.find("wire form") != std::string::npos);
}
