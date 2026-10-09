// OpenBFME. GPL-3.0.
// Lane UPGRADE-1: the UpgradeCenter (Common/Upgrade.h) and the UpgradeMux (GameLogic/Module/UpgradeModule.h) on synthetic data.

#include "doctest.h"

#include "IniTestUtil.h"

#include <algorithm>

#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/UpgradeModule.h"

namespace
{
struct CenterFixture
{
	initest::Fixture fx;
	UpgradeCenter center;
	UpgradeCenter *saved = TheUpgradeCenter;
	std::vector<std::string> sounds = { "ResearchDone" };
	CenterFixture()
	{
		center.init();
		center.registerBlock(fx.env.blocks);
		UpgradeParseServices s;
		s.audioEventExists = [this](const std::string &n) { return std::find(sounds.begin(), sounds.end(), n) != sounds.end(); };
		s.evaEventIndex = [](const std::string &n) { return n == "UpgradeComplete" ? 7 : -1; };
		center.setParseServices(s);
		TheUpgradeCenter = &center;
	}
	~CenterFixture() { TheUpgradeCenter = saved; }
	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *code = nullptr)
	{
		return initest::loadError(fx.env, "upgrade.ini", text, type, code);
	}
};

const char *kUpgrades =
	"Upgrade DefaultUpgrade\n"
	"  Type = PLAYER\n"
	"  ButtonImage = SCTempDefaultInventory\n"
	"End\n"
	"Upgrade Upgrade_HeavyArmor\n"
	"  DisplayName = UPGRADE:HeavyArmor\n"
	"  BuildTime = 30.0\n"
	"  BuildCost = 1000\n"
	"  ResearchSound = ResearchDone\n"
	"  ResearchCompleteEvaEvent = UpgradeComplete\n"
	"  SkirmishAIHeuristic = AI_UPGRADEHEURISTIC_FORTRESS\n"
	"End\n"
	"Upgrade Upgrade_Level2\n"
	"  Type = OBJECT\n"
	"  BuildTime = 60\n"
	"  BuildCost = 500\n"
	"End\n"
	"Upgrade Upgrade_Pack\n"
	"  Type = OBJECT\n"
	"  SubUpgradeTemplateNames = Upgrade_HeavyArmor Upgrade_Level2\n"
	"End\n";
} // namespace

TEST_CASE("upgrade center: the veterancy upgrades take bits 0..2, a block copies DefaultUpgrade, the first definition wins")
{
	CenterFixture f;
	REQUIRE(f.center.size() == 3);
	REQUIRE(f.center.findUpgrade("Upgrade_Veterancy_VETERAN") != nullptr);
	CHECK(f.center.findUpgrade("Upgrade_Veterancy_VETERAN")->getMaskBit() == 0);
	CHECK(f.center.findUpgrade("Upgrade_Veterancy_HEROIC")->getMaskBit() == 2);
	CHECK(f.center.findUpgrade("Upgrade_Veterancy_ELITE")->getUpgradeType() == UPGRADE_TYPE_OBJECT);
	CHECK(f.center.findUpgrade("Upgrade_Veterancy_REGULAR") == nullptr); // RW 0x66FDD3 makes no REGULAR upgrade
	REQUIRE(f.load(kUpgrades).empty());
	std::string err;
	REQUIRE(f.center.resolveSubUpgrades(&err));
	const UpgradeTemplate *armor = f.center.findUpgrade("Upgrade_HeavyArmor");
	REQUIRE(armor);
	CHECK(armor->getMaskBit() == 4); // DefaultUpgrade is bit 3
	CHECK(armor->getUpgradeType() == UPGRADE_TYPE_PLAYER);
	CHECK(armor->m_buttonImage == "SCTempDefaultInventory"); // copied from DefaultUpgrade (RW 0x66FC5C)
	CHECK(armor->getBuildCost() == 1000);
	CHECK(armor->getBuildTimeSeconds() == doctest::Approx(30.0f));
	CHECK(armor->m_researchCompleteEvaEvent == 7);
	CHECK(armor->m_skirmishAIHeuristic == 3);
	CHECK(f.center.findUpgrade("upgrade_heavyarmor") == nullptr); // name keys are case sensitive
	const UpgradeTemplate *pack = f.center.findUpgrade("Upgrade_Pack");
	REQUIRE(pack);
	REQUIRE(pack->getSubUpgrades().size() == 2);
	UpgradeMaskType m = pack->grantMask(); // RW 0x693817: the sub upgrades' bits, not its own
	CHECK(m.test(4));
	CHECK(m.test(5));
	CHECK_FALSE(m.test((unsigned)pack->getMaskBit()));
	// a second definition is parsed (its errors throw) and discarded
	REQUIRE(f.load("Upgrade Upgrade_Level2\n  BuildCost = 7\nEnd\n").empty());
	CHECK(f.center.findUpgrade("Upgrade_Level2")->getBuildCost() == 500);
	CHECK_FALSE(f.load("Upgrade Upgrade_Level2\n  Bogus = 7\nEnd\n").empty());
	// load type 5 (reload) replaces the template and keeps its bit
	REQUIRE(f.load("Upgrade Upgrade_Level2\n  BuildCost = 9\nEnd\n", INI_LOAD_RELOAD).empty());
	CHECK(f.center.findUpgrade("Upgrade_Level2")->getBuildCost() == 9);
	CHECK(f.center.findUpgrade("Upgrade_Level2")->getMaskBit() == 5);
	CHECK(f.center.findUpgradeByMaskBit(5) == f.center.findUpgrade("Upgrade_Level2"));
}

TEST_CASE("upgrade center: retail-exact acceptance of the field values")
{
	CenterFixture f;
	int code = 0;
	CHECK_FALSE(f.load("Upgrade A\n  Type = SOMETHING\nEnd\n", INI_LOAD_OVERWRITE, &code).empty());
	CHECK_FALSE(f.load("Upgrade B\n  ResearchSound = NotASound\nEnd\n", INI_LOAD_OVERWRITE, &code).empty());
	CHECK(f.load("Upgrade C\n  ResearchSound = NoSound\n  UnitSpecificSound = nosound\nEnd\n").empty());
	CHECK_FALSE(f.load("Upgrade D\n  ResearchCompleteEvaEvent = NotAnEvent\nEnd\n").empty());
	CHECK(f.load("Upgrade E\n  ResearchCompleteEvaEvent = None\n  SkirmishAIHeuristic = ai_upgradeheuristic_fortress\nEnd\n").empty());
	CHECK(f.center.findUpgrade("E")->m_skirmishAIHeuristic == -1); // case sensitive, unknown is -1 without an error
	CHECK_FALSE(f.load("Upgrade F\n  Tracer = 1\nEnd\n").empty()); // an unknown field
	f.center.setParseServices(UpgradeParseServices{});
	CHECK_FALSE(f.load("Upgrade G\n  ResearchSound = ResearchDone\nEnd\n", INI_LOAD_OVERWRITE, &code).empty());
	CHECK(code == 8); // no audio lookup installed: loud, never accepted
	REQUIRE(f.load("Upgrade H\n  SubUpgradeTemplateNames = Missing\nEnd\n").empty());
	std::string err;
	CHECK_FALSE(f.center.resolveSubUpgrades(&err));
	CHECK(err.find("Missing") != std::string::npos);
}

namespace
{
struct MuxProbe : UpgradeMux
{
	explicit MuxProbe(const UpgradeModuleData *d) : UpgradeMux(nullptr, d) {}
	int ran = 0, removed = 0;
	void upgradeImplementation() override { ++ran; }
	void processUpgradeRemoval() override { ++removed; }
};

UpgradeModuleData *g_parseTarget = nullptr;

UpgradeModuleData parseMux(CenterFixture &f, const std::string &fields)
{
	UpgradeModuleData d;
	g_parseTarget = &d;
	if (!f.fx.env.blocks.find("Behavior"))
	{
		f.fx.env.blocks.registerBlock("Behavior", [](INI *i) {
			MultiIniFieldParse p;
			UpgradeModuleData::buildFieldParse(p);
			i->initFromINIMulti(g_parseTarget, p);
		});
	}
	INI ini(f.fx.env);
	const std::string text = "Behavior\n" + fields + "End\n";
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	ini.loadMemory("mux.ini", bytes, INI_LOAD_OVERWRITE);
	return d;
}
} // namespace

TEST_CASE("upgrade mux: TriggeredBy / ConflictsWith masks, RequiresAll, the executed flag, Permanent (RW 0x8D26F3, 0x8D278A, 0x8D2688)")
{
	CenterFixture f;
	REQUIRE(f.load(kUpgrades).empty());
	const unsigned armor = (unsigned)f.center.findUpgrade("Upgrade_HeavyArmor")->getMaskBit();
	const unsigned level2 = (unsigned)f.center.findUpgrade("Upgrade_Level2")->getMaskBit();

	UpgradeModuleData any = parseMux(f, "  TriggeredBy = Upgrade_HeavyArmor Upgrade_Level2\n  ConflictsWith = Upgrade_Pack\n");
	CHECK(any.m_activationMask.test(armor));
	CHECK(any.m_triggeredBy.size() == 2);
	MuxProbe m(&any);
	UpgradeMaskType mask;
	CHECK_FALSE(m.attemptUpgrade(mask));
	mask.set(level2);
	CHECK(m.attemptUpgrade(mask));
	CHECK(m.ran == 1);
	CHECK(m.isAlreadyUpgraded());
	CHECK_FALSE(m.attemptUpgrade(mask)); // once
	mask.set((unsigned)f.center.findUpgrade("Upgrade_Pack")->getMaskBit());
	MuxProbe c(&any);
	CHECK_FALSE(c.wouldUpgrade(mask)); // a conflicting bit blocks
	UpgradeMaskType single;
	single.set(armor);
	CHECK(m.resetUpgrade(single)); // any activation bit resets an executed mux
	CHECK_FALSE(m.isAlreadyUpgraded());
	m.removeUpgrade();
	CHECK(m.removed == 1);

	UpgradeModuleData all = parseMux(f, "  TriggeredBy = Upgrade_HeavyArmor Upgrade_Level2\n  RequiresAllTriggers = Yes\n  Permanent = Yes\n");
	MuxProbe a(&all);
	UpgradeMaskType one;
	one.set(armor);
	CHECK_FALSE(a.wouldUpgrade(one));
	one.set(level2);
	CHECK(a.attemptUpgrade(one));
	a.removeUpgrade();
	CHECK(a.removed == 0); // Permanent: no removal
	CHECK(a.isAlreadyUpgraded());

	// "None" is skipped, an unknown name throws (RW 0xC10C90)
	CHECK_NOTHROW(parseMux(f, "  TriggeredBy = None\n"));
	CHECK_THROWS(parseMux(f, "  TriggeredBy = Upgrade_Nope\n"));
	// CustomAnimAndDuration (RW 0x851412)
	UpgradeModuleData anim = parseMux(f, "  TriggeredBy = Upgrade_Level2\n  CustomAnimAndDuration = AnimState:UPGRADE_GARRISON AnimTime:3000 TriggerTime:1400\n");
	CHECK(anim.m_customAnimCondition >= 0);
	CHECK(anim.m_customAnimFrames == 15);
	CHECK(anim.m_customTriggerFrames == 7);
	CHECK_THROWS(parseMux(f, "  CustomAnimAndDuration = AnimTime:3000\n"));
	// RW 0x4B3B5B: an unknown model condition is -1 and parsing goes on to the durations
	UpgradeModuleData unknown = parseMux(f, "  TriggeredBy = Upgrade_Level2\n  CustomAnimAndDuration = AnimState:NOT_A_CONDITION AnimTime:2000 TriggerTime:1000\n");
	CHECK(unknown.m_customAnimCondition == -1);
	CHECK(unknown.m_customAnimFrames == 10);
	CHECK(unknown.m_customTriggerFrames == 5);
}

TEST_CASE("upgrade center: the sub upgrade cost sum wraps like the retail int add (2000000000 + 2000000000 -> -294967296)")
{
	CenterFixture f;
	REQUIRE(f.load("Upgrade Big1\n  Type = OBJECT\n  BuildCost = 2000000000\nEnd\n"
				   "Upgrade Big2\n  Type = OBJECT\n  BuildCost = 2000000000\nEnd\n"
				   "Upgrade Pack2\n  Type = OBJECT\n  SubUpgradeTemplateNames = Big1 Big2\nEnd\n")
			.empty());
	std::string err;
	REQUIRE(f.center.resolveSubUpgrades(&err));
	Player player(0);
	CHECK(f.center.findUpgrade("Pack2")->calcCostToBuild(&player, nullptr) == -294967296);
}
