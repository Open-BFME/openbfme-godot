// OpenBFME. GPL-3.0. See AptStrategicPlayerStatus.h for target and donor sources.
#include "GameClient/GUI/AptScreens/AptStrategicPlayerStatus.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/LoadScreenInfo.h"

AptStrategicPlayerStatus::AptStrategicPlayerStatus(WindowManager &windows, Shell &shell, const StrategicPlayerStatusData &data, const GameTextSource *text)
	: AptScreen(windows, shell, "StrategicPlayerStatus.apt", "StrategicPlayerStatus")
{
	registerCommand("StrategicPlayerStatus::OnInitialized", [](const std::string &) {});
	registerCommand("StrategicPlayerStatus::OnCloseWindow", [this](const std::string &) { m_closed = true; });
	int counts[2] = { 0, 0 };
	for (const auto &p : data.players)
	{
		const int side = p.allied ? 0 : 1;
		const std::string kind = side == 0 ? "Allied" : "Enemy";
		const std::string row = std::to_string(counts[side]++);
		const std::string color = std::to_string((std::int32_t)(p.color | 0xFF000000u));
		registerProvider("StrategicPlayerStatus::" + kind + "Color_" + row,
			[color](const std::string &, std::string &value, bool setting) { if (!setting) value = color; return true; });
		windows.setAptImage(kind + "FactionIcon_" + row, p.factionImage);
		windows.setAptText(kind + ":Name_" + row, p.name);
		windows.setAptText(kind + ":Territories_" + row, std::to_string(p.territories));
		windows.setAptText(kind + ":Regions_" + row, std::to_string(p.regions));
		windows.setAptText(kind + ":Status_" + row, loadScreenU16ToUtf8(fetchOrMissing(text,
			p.left ? "GUI:PlayerGone" : p.defeated ? "GUI:PlayerDead" : "GUI:PlayerAlive")));
	}
	const char *names[] = { "PlayerIndex", "NumAlliedPlayers", "NumEnemyPlayers" };
	const int values[] = { 0, counts[0], counts[1] }; // retail initializes PlayerIndex to zero
	for (int i = 0; i < 3; ++i)
	{
		const std::string value = std::to_string(values[i]);
		registerProvider(std::string("StrategicPlayerStatus::") + names[i],
			[value](const std::string &, std::string &out, bool setting) { if (!setting) out = value; return true; });
	}
	windows.setAptText("StrategicPlayerStatus:ScenarioName", data.scenarioName);
	windows.setAptText("StrategicPlayerStatus:Objectives", data.objectives);
	windows.setAptText("StrategicPlayerStatus:ScenarioFiction", data.fiction);
	windows.setAptText("StrategicPlayerStatus:GameType", data.gameType);
	windows.setAptText("StrategicPlayerStatus:NumPlayers", std::to_string(data.players.size()));
}
