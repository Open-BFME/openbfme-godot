// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptScreenFactories.h.

#include "GameClient/GUI/AptScreens/AptScreenFactories.h"

#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptQuitMenu.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/AptScreens/AptTimeLine.h"
#include "GameClient/GUI/AptScreens/AptSaveLoad.h"
#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/AptScreens/AptCreateAHero.h"

void registerAptScreenFactories(AptScreenFactoryTable &table)
{
	table.registerFactory("MainMenu.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptMainMenu>(c.windows, c.shell, c.services, &c.environment);
	});
	table.registerFactory("Skirmish.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptSkirmish>(c.windows, c.shell, c.environment);
	});
	table.registerFactory("LoadScreen.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptLoadScreen>(c.windows, c.shell, c.environment);
	});
	table.registerFactory("Options.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptOptionsScreen>(c.windows, c.shell, c.environment, c.services);
	});
	table.registerFactory("GuiFX.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptGuiFXScreen>(c.windows, c.shell);
	});
	table.registerFactory("Palantir.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		auto p = std::make_unique<AptPalantir>(c.windows, c.shell);
		p->setServices(&c.services); // lane END-2: the options button's quit menu
		return p;
	});
	table.registerFactory("Background.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptBackgroundScreen>(c.windows, c.shell);
	});
	// lane END-1: the score screen of a skirmish / LAN game (RW 0x927898 pushes TimeLine.apt)
	table.registerFactory("TimeLine.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptTimeLine>(c.windows, c.shell, c.environment);
	});
	// lane MP-2: the replay page of the load screen (the main menu's LoadReplay: RW 0x91C36C -> RW 0x816655)
	table.registerFactory("SaveLoad.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptSaveLoad>(c.windows, c.shell, c.environment);
	});
	// lane MP-2: the LAN lobby (the main menu's LAN; LanOpenPlay.swf, then MpGameSetup.swf)
	table.registerFactory("LanLobby.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptLanLobby>(c.windows, c.shell, c.environment);
	});
	// lane END-2: the in-game menu (RW 0x6D2D18 makes an AptQuitMenu, RW 0x921B0F pushes QuitMenu.apt)
	table.registerFactory("QuitMenu.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptQuitMenu>(c.windows, c.shell, c.environment);
	});
	// lane PLAY-1: the Palantir's flag in a skirmish / multiplayer game (RW 0x914EF0 pushes PlayerTribute.apt)
	table.registerFactory("PlayerTribute.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptPlayerTribute>(c.windows, c.shell, c.services, c.environment);
	});
	// lane CAH-1: the Create-a-Hero builder (the main menu's CreateAHero, RW 0x91A018)
	table.registerFactory("CreateAHero.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptCreateAHero>(c.windows, c.shell, c.environment);
	});
	table.registerFactory("AptLevel0.apt", [](AptScreenContext &c) -> std::unique_ptr<AptScreen> {
		return std::make_unique<AptLevel0Screen>(c.windows, c.shell);
	});
}
