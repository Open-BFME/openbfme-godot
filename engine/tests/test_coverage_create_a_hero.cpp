// Synthetic, no-game Create-a-Hero screen coverage. GPL-3.0.
#include "AptPlayerTestUtil.h"
#include "IniTestUtil.h"

#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/CreateAHeroHeroList.h"
#include "GameClient/GUI/AptScreens/AptCreateAHero.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/CreateAHero/AptMyHero.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameLogic/CreateAHeroSystem.h"

#include "doctest.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace apttest;

// Both the INI and the system hero live in memory. No installation, profile folder,
// wall-clock value, random ID, or on-disk fixture is needed.
struct CahFx
{
	// A failed REQUIRE in the fixture constructor must also restore both globals.
	struct SavedGlobals
	{
		UpgradeCenter *upgrades = TheUpgradeCenter;
		CommandStore *commands = TheCommandStore;
		~SavedGlobals()
		{
			TheUpgradeCenter = upgrades;
			TheCommandStore = commands;
		}
	} savedGlobals;
	AptStubHost host;
	initest::Fixture ini;
	UpgradeCenter upgrades;
	CommandStore commands;
	CreateAHeroSystem system;
	CreateAHeroHeroList heroes;
	CreateAHeroScreenContext context;
	MemorySource source;
	RecordingShellServices services;
	ShellEnvironment env;
	AptScreenFactoryTable factories;
	std::unique_ptr<WindowManager> windows;
	std::unique_ptr<Shell> shell;
	AptCreateAHero *screen = nullptr;

	CahFx()
	{
		TheUpgradeCenter = &upgrades;
		TheCommandStore = &commands;
		upgrades.init();
		upgrades.registerBlock(ini.env.blocks);
		ini.env.blocks.registerBlock("CommandButton", [](INI *p) { CommandStore::parseCommandButtonDefinitionGlobal(p); });
		ini.env.blocks.registerBlock("CommandSet", [](INI *p) { CommandStore::parseCommandSetDefinitionGlobal(p); });
		REQUIRE(initest::loadError(ini.env, "synthetic.ini",
			"Upgrade Upgrade_CahSynthetic\nEnd\n"
			"Upgrade Upgrade_CahType\nEnd\n"
			"Upgrade Upgrade_CahHair0\nEnd\n"
			"Upgrade Upgrade_CahHair1\nEnd\n") == "");
		CreateAHeroClass cls;
		cls.upgradeName = "Upgrade_CahSynthetic";
		cls.nameTag = "CAH:SyntheticClass";
		cls.iconImage = "SyntheticIcon";
		CreateAHeroSubClass sub;
		sub.upgradeName = "Upgrade_CahType";
		sub.nameTag = "CAH:SyntheticType";
		sub.descriptionTag = "CAH:SyntheticDescription";
		sub.defaultPrimaryColor = 123;
		sub.blingGroups.push_back({ "SyntheticHair", { 0, 1 } });
		sub.blingDefaults.push_back({ "SyntheticHair", 0 });
		cls.subClasses.push_back(sub);
		system.addClass(cls, nullptr);
		system.addBinder({ "SyntheticHair", "CAH:Hair", "CAH:HairInfo", 0, CAH_BLING_APPEARANCE });
		system.addBling({ "CAH:Hair0", "", "Upgrade_CahHair0", "SyntheticHair" }, nullptr);
		system.addBling({ "CAH:Hair1", "", "Upgrade_CahHair1", "SyntheticHair" }, nullptr);
		const std::string buttons =
			"CommandButton Command_SpecialAbilityNoPowerDummy\nEnd\n"
			"CommandButton Command_CahA\n  CreateAHeroUIAllowableUpgrades = Upgrade_CahSynthetic\n"
			"  CreateAHeroUIMinimumLevel = 1\nEnd\n"
			"CommandButton Command_CahB\n  CreateAHeroUIAllowableUpgrades = Upgrade_CahSynthetic\n"
			"  CreateAHeroUIPrerequisiteButtonName = Command_CahA\n"
			"  CreateAHeroUIMinimumLevel = 2\nEnd\n"
			"CommandButton Command_CahC\n  CreateAHeroUIAllowableUpgrades = Upgrade_CahSynthetic\n"
			"  CreateAHeroUIMinimumLevel = 3\nEnd\n";
		REQUIRE(initest::loadError(ini.env, "commands.ini", buttons) == "");

		CreateAHeroHero h;
		h.name = u"System Hero";
		h.uniqueID = "SYSTEMSYNTHETIC";
		h.classIndex = 0;
		h.subClassIndex = 0;
		auto bytes = h.save();
		ini.mount({ { "data/systemheroes/MyHero_SYSTEMSYNTHETIC.cah", std::string(bytes.begin(), bytes.end()) } });
		std::vector<std::string> errors;
		heroes.load(&ini.fsys, "", &errors);
		REQUIRE(errors.empty());
		REQUIRE(heroes.size() == 1);
		REQUIRE(heroes.at(0)->system);
		context.system = &system;
		context.heroes = &heroes;
		context.random = [](int lo, int) { return lo; };
		env.createAHero = &context;
		env.services = &services;

		// A real, known, synthetic screen movie: its root action enters the
		// manager page through the registered FSCommand, not a failure-only load.
		for (const char *name : { "AptLevel0", "GuiFX", "CreateAHero" })
		{
			TestMovie movie;
			if (std::string(name) == "CreateAHero")
			{
				auto action = program(movie, [](Asm &a) {
					fscmd(a, "AptCreateAHero::OnInitialized");
					a.getURL("FSCommand:AptCreateAHero::OnShowScreen", "M");
				});
				movie.setRootFrames({ { movie.addActionItem(action) } });
			}
			else
			{
				movie.setRootFrames({ {} });
			}
			source.add(name, movie);
		}
		windows = std::make_unique<WindowManager>(source, services);
		shell = std::make_unique<Shell>(*windows, factories, services, env);
		factories.registerFactory("CreateAHero.apt", [](AptScreenContext &c) {
			return std::make_unique<AptCreateAHero>(c.windows, c.shell, c.environment);
		});
		factories.registerFactory("GuiFX.apt", [](AptScreenContext &c) {
			return std::make_unique<AptGuiFXScreen>(c.windows, c.shell);
		});
		windows->init();
		shell->push("CreateAHero.apt");
		screen = dynamic_cast<AptCreateAHero *>(shell->top());
		REQUIRE(screen != nullptr);
		REQUIRE(screen->level() != -1); // exact no-window sentinel
		tick();
		REQUIRE(screen->currentPage() == 'M'); // the movie's root action ran
	}
	~CahFx()
	{
		shell.reset();
		windows.reset();
		TheCommandStore = savedGlobals.commands;
		TheUpgradeCenter = savedGlobals.upgrades;
	}
	void tick(int count = 2)
	{
		for (int i = 0; i < count; ++i)
		{
			windows->update(33);
		}
	}
	void command(const std::string &name, const std::string &argument = "")
	{
		REQUIRE(windows->invokeCallback(name, argument));
	}
	std::string get(const std::string &name)
	{
		std::string value;
		const auto result = windows->getExtern(name, value);
		REQUIRE((result == AptExternResult::Value || result == AptExternResult::Number));
		return value;
	}
	void set(const std::string &name, const std::string &value)
	{
		REQUIRE(windows->setExtern(name, value));
	}
};
} // namespace

TEST_CASE("coverage1: movie-driven screen pages, rotation, zoom, demo and promo providers")
{
	CahFx f;
	CHECK(f.screen->myHero() != nullptr);
	CHECK(f.get("CreateAHeroDemo") == "1");
	CHECK(f.get("AptCreateAHero::SuppressCAHPromo") == "0");
	f.set("AptCreateAHero::SuppressCAHPromo", "true");
	CHECK(f.screen->suppressPromo());
	CHECK(f.get("AptCreateAHero::SuppressCAHPromo") == "1");
	f.set("AptCreateAHero::SuppressCAHPromo", "no");
	CHECK_FALSE(f.screen->suppressPromo());
	f.command("AptCreateAHero::OnShowScreen", "C");
	CHECK(f.screen->currentPage() == 'C');
	CHECK(f.screen->previousPage() == 'M');
	f.command("AptCreateAHero::OnShowScreen", "X");
	f.command("AptCreateAHero::OnShowScreen", "C");
	CHECK(f.screen->currentPage() == 'C');
	f.command("AptCreateAHero::OnShowScreen", "A");
	CHECK(f.screen->previousPage() == 'C');
	f.command("AptCreateAHero::RotateLeft", "true");
	f.command("AptCreateAHero::RotateRight", "t");
	f.command("AptCreateAHero::ZoomIn", "true");
	f.command("AptCreateAHero::ZoomOut", "true");
	CHECK(f.screen->rotateLeft());
	CHECK(f.screen->rotateRight());
	CHECK(f.screen->zoomIn());
	CHECK(f.screen->zoomOut());
	for (const char *n : { "RotateLeft", "RotateRight", "ZoomIn", "ZoomOut" })
	{
		f.command(std::string("AptCreateAHero::") + n, "false");
	}
	CHECK_FALSE(f.screen->rotateLeft());
	CHECK_FALSE(f.screen->rotateRight());
	CHECK_FALSE(f.screen->zoomIn());
	CHECK_FALSE(f.screen->zoomOut());
}

TEST_CASE("coverage1: class and subclass callbacks reject malformed selections and update the preview")
{
	CahFx f;
	f.screen->showPage('C');
	CHECK(f.get("CahClass::NumClasses") == "1");
	CHECK(f.get("CahClass::NumClassTypes") == "0");
	f.command("AptCreateAHero::Class::SetClassAndType", "garbage");
	f.command("AptCreateAHero::Class::SetClassAndType", "-1 0");
	f.command("AptCreateAHero::Class::SetClassAndType", "0 300");
	CHECK(f.screen->currentPage() == 'C');
	f.command("AptCreateAHero::Class::SetClassAndType", "0 0");
	CHECK(f.get("CahClass::NumClassTypes") == "1");
	REQUIRE(f.screen->displayedHero());
	CHECK(f.screen->displayedHero()->classIndex == 0);
	CHECK(f.screen->displayedHero()->subClassIndex == 0);
	CHECK(f.screen->currentPage() == 'C');
	f.screen->showPage('A');
	CHECK(f.screen->currentPage() == 'A');
}

TEST_CASE("coverage1: appearance controls, deterministic randomize, colors and numeric providers")
{
	CahFx f;
	f.screen->showPage('C');
	f.screen->setClassAndType(0, 0);
	f.screen->showPage('A');
	REQUIRE(f.screen->myHero());
	auto &my = *f.screen->myHero();
	CHECK(f.get("CahAppearance::ShowNamePrompt") == "1");
	CHECK(f.get("CahAppearance::HeroNameSet") == "0");
	f.command("AptCreateAHero::Appearance::NextAppearance", "0");
	CHECK(my.currentIndex(AptMyHero::KIND_APPEARANCE, 0) == 1);
	f.command("AptCreateAHero::Appearance::PrevAppearance", "0");
	CHECK(my.currentIndex(AptMyHero::KIND_APPEARANCE, 0) == 0);
	f.command("AptCreateAHero::Appearance::PrevAppearance", "900");
	f.command("AptCreateAHero::Appearance::IncreaseAttribute", "-1");
	f.command("AptCreateAHero::Appearance::AutoChangeBttn", "AppearanceRandom");
	CHECK(my.currentIndex(AptMyHero::KIND_APPEARANCE, 0) == 0);
	f.command("AptCreateAHero::Appearance::NextAppearance", "0");
	f.command("AptCreateAHero::Appearance::AutoChangeBttn", "AppearanceDefault");
	CHECK(my.currentIndex(AptMyHero::KIND_APPEARANCE, 0) == 0);
	CHECK(my.hero().primaryColor == 123);
	f.command("AptCreateAHero::Appearance::OnHairColor", "42");
	f.command("AptCreateAHero::Appearance::OnSkinColor", "24");
	f.command("AptCreateAHero::Appearance::OnPaintColor", "12");
	CHECK(f.get("CahAppearance::HairColor") == "42");
	CHECK(f.get("CahAppearance::SkinColor") == "24");
	CHECK(f.get("CahAppearance::PaintColor") == "12");
	CHECK(my.hero().primaryColor == 42);
	CHECK(my.hero().secondaryColor == 24);
	CHECK(my.hero().tertiaryColor == 12);
}

TEST_CASE("coverage1: appearance name providers distinguish blank, duplicate and available names")
{
	CahFx f;
	f.screen->showPage('C');
	f.screen->setClassAndType(0, 0);
	f.screen->showPage('A');
	CHECK_FALSE(f.screen->typeName(u"Unnamed")); // no synthetic entry gadget: fail explicitly, not a retail-dependent skip
	CHECK(f.get("CahAppearance::ShowNamePrompt") == "1");
	f.command("AptCreateAHero::Appearance::NamePrompt");
	CHECK(f.screen->lastMessage().find("APT:EnterNameError") != std::string::npos);
	REQUIRE(f.screen->messageBox());
	f.screen->myHero()->hero().name = u"system hero";
	CHECK(f.get("CahAppearance::HeroNameSet") == "0");
	f.command("AptCreateAHero::Appearance::NamePrompt");
	CHECK(f.screen->lastMessage().find("LAN:ErrorDuplicateName") != std::string::npos);
	f.screen->myHero()->hero().name = u"Synthetic New Hero";
	CHECK(f.get("CahAppearance::ShowNamePrompt") == "0");
	CHECK(f.get("CahAppearance::HeroNameSet") == "1");
	f.screen->myHero()->hero().uniqueID = "NEWSYNTHETIC"; // never request a random ID
	CHECK(f.screen->completeAppearance());
	CHECK_FALSE(f.screen->completePowers()); // the in-memory hero list has no profile folder
	CHECK(f.screen->myHero()->hero().name == u"Synthetic New Hero");
	CHECK(f.heroes.size() == 1);
}

TEST_CASE("coverage1: manager selects synthetic system heroes, sorts and protects them from deletion")
{
	CahFx f;
	f.screen->showPage('M');
	CHECK_FALSE(f.screen->removeSelectedHero());
	CHECK_FALSE(f.screen->deleteSelectedHero());
	f.screen->selectHero(999);
	CHECK(f.screen->selectedHero() == -1);
	f.screen->selectHero(0);
	CHECK(f.screen->selectedHero() == 0);
	REQUIRE(f.screen->displayedHero());
	CHECK(f.screen->displayedHero()->name == u"System Hero");
	CHECK_FALSE(f.screen->deleteSelectedHero());
	CHECK_FALSE(f.screen->removeSelectedHero());
	f.command("Mission::OnSortName");
	CHECK(f.screen->heroSortKey0() == 2);
	f.command("Mission::OnSortName");
	CHECK(f.screen->heroSortKey0() == 3);
	f.command("Mission::OnSortType");
	CHECK(f.screen->heroSortKey0() == 4);
	CHECK(f.screen->heroSortKey1() == 3);
	f.command("Mission::OnSortIcon");
	CHECK(f.screen->heroSortKey0() == 0);
	CHECK(f.heroes.size() == 1);
}

TEST_CASE("coverage1: synthetic power matrix prerequisites, chosen ranks, no-power and book boundary")
{
	CahFx f;
	CreateAHeroHero hero;
	hero.classIndex = 0;
	hero.subClassIndex = 0;
	CahPowers powers;
	powers.build(hero, f.system);
	CHECK(powers.rowCount() == 2);
	CHECK(powers.cell(-1, 0) == nullptr);
	CHECK(powers.cell(0, -1) == nullptr);
	CHECK(powers.cell(20, 0) == nullptr);
	REQUIRE(powers.cell(0, 0));
	REQUIRE(powers.cell(0, 1));
	CHECK(powers.cell(0, 1)->state == CahPowers::STATE_LEVEL_TOO_LOW);
	CHECK(powers.cell(0, 0)->state == CahPowers::STATE_AVAILABLE);
	REQUIRE(powers.select(powers.cell(0, 0), hero));
	CHECK(hero.powers[0].commandButton == "Command_CahA");
	CHECK(powers.cell(0, 0)->state == CahPowers::STATE_SELECTED);
	CHECK_FALSE(powers.select(powers.cell(0, 0), hero));
	CHECK(powers.cell(0, 1)->state == CahPowers::STATE_AVAILABLE);
	REQUIRE(powers.select(powers.cell(0, 1), hero));
	CHECK(hero.powers[1].commandButton == "Command_CahB");
	CHECK(hero.powers[1].buttonIndex == hero.powers[0].buttonIndex);
	powers.truncate(1, hero);
	CHECK(hero.powers[1].commandButton.empty());
	CHECK(powers.cell(0, 1)->state == CahPowers::STATE_AVAILABLE);
	powers.truncate(-1, hero);
	CHECK(powers.chosenCount() == 0);
	CHECK(hero.powers[0].commandButton.empty());
	for (int i = 0; i < CahPowers::MAX_POWERS; ++i)
	{
		REQUIRE(powers.select(nullptr, hero));
	}
	CHECK(powers.chosenCount() == CahPowers::MAX_POWERS);
	CHECK_FALSE(powers.select(nullptr, hero));
	CHECK(powers.cell(0, 0)->state == CahPowers::STATE_BOOK_FULL);
	powers.truncate(0, hero);
	CHECK(powers.cell(0, 0)->state == CahPowers::STATE_AVAILABLE);
}

TEST_CASE("coverage1: screen power callbacks, extern queries, malformed coordinates and deferred selection")
{
	CahFx f;
	f.screen->setClassAndType(0, 0);
	f.screen->showPage('P');
	CHECK(f.get("NumPowerRows") == "2");
	CHECK(f.get("NumCurrentPowers") == "0");
	CHECK(f.get("DisablePowerInstructions") == "0");
	f.set("CurrentPowerIndex", "bogus");
	CHECK(f.get("CurrentPowerIndex") == "0");
	f.command("AptCreateAHero::OnPowerSelect", "wrong");
	CHECK_FALSE(f.screen->choosePower(-1, 900));
	f.command("AptCreateAHero::OnPowerSelect", "1,1");
	CHECK(f.get("NumCurrentPowers") == "0");
	f.tick();
	CHECK(f.get("NumCurrentPowers") == "1");
	f.set("CurrentPowerIndex", "1 1");
	CHECK(f.get("CurrentPowerIndex") == "1");
	f.command("AptCreateAHero::OnMyPowerSelect", "1");
	f.tick();
	CHECK(f.get("NumCurrentPowers") == "0");
	f.command("AptCreateAHero::OnNoPowerSelect");
	f.tick();
	CHECK(f.get("NumCurrentPowers") == "1");
	f.command("AptCreateAHero::OnResetBttn");
	f.tick();
	CHECK(f.get("NumCurrentPowers") == "0");
}

TEST_CASE("coverage1: leaving power page discards a queued choice before rebuilding its cells")
{
	CahFx f;
	f.screen->setClassAndType(0, 0);
	f.screen->showPage('P');
	REQUIRE(f.screen->choosePower(0, 0));
	CHECK(f.screen->powers().chosenCount() == 0); // deliberately do not tick yet
	f.screen->showPage('M');
	f.tick();
	CHECK(f.screen->powers().chosenCount() == 0);
	CHECK(f.screen->myHero()->hero().powers[0].commandButton.empty());
	f.screen->showPage('P'); // rebuild invalidates the previous matrix's cell addresses
	f.tick();
	CHECK(f.screen->powers().chosenCount() == 0);
	CHECK(f.screen->myHero()->hero().powers[0].commandButton.empty());
}

TEST_CASE("coverage1: picture guard and capture report successful and failed device saves")
{
	CahFx f;
	f.screen->setClassAndType(0, 0);
	f.screen->myHero()->hero().name = u"A/B:Hero";
	AptCreateAHero::PictureTime time{ 2025, 3, 4, 13, 2, 5 };
	const std::u16string file = AptCreateAHero::pictureFileName(u"A/B:Hero", time);
	CHECK(file.find(u'/') == std::u16string::npos);
	CHECK(file.find(u':') == std::u16string::npos);
	f.command("AptCreateAHero::OnTakePicture");
	CHECK_FALSE(f.screen->takePicturePending());
	f.screen->pictureGuardDrawn();
	f.command("AptCreateAHero::OnTakePicture");
	CHECK_FALSE(f.screen->takePicturePending());
	f.screen->pictureGuardDrawn();
	f.command("AptCreateAHero::OnTakePicture");
	CHECK(f.screen->takePicturePending());
	CHECK(f.screen->beginPicture(time) == file);
	CHECK_FALSE(f.screen->takePicturePending());
	f.screen->pictureTaken(u"Synthetic/" + file, "");
	CHECK(f.screen->lastMessage().find("GUI:OnPictureTakenTitle") != std::string::npos);
	REQUIRE(f.screen->messageBox());
	f.command("AptCreateAHero::PrepareToTakePicture");
	CHECK(f.screen->pictureFrames() == 0);
	f.screen->pictureGuardDrawn();
	f.screen->pictureGuardDrawn();
	f.command("AptCreateAHero::OnTakePicture");
	REQUIRE_FALSE(f.screen->beginPicture(time).empty());
	f.screen->pictureTaken(u"", "synthetic device failed");
	CHECK(f.screen->lastMessage().find("synthetic device failed") != std::string::npos);
}
