// OpenBFME. GPL-3.0.
// RotWK strategic player status/objectives. Retail bindings RW 0x92A37E..0x92A81F,
// close RW 0x929F5F; BFME2 donor GUICallbacks/Apt/AptStrategicPlayerStatus.cpp
// (RVA 0x523825), StrategicPlayerStatusCallbacks.cpp (RVA 0x523481).
// S-001 applies to observations from the installed RotWK image.
#pragma once

#include "GameClient/GUI/AptScreen.h"
#include <cstdint>
#include <string>
#include <vector>
class GameTextSource;

struct StrategicPlayerStatusData
{
	struct Player
	{
		std::string name, factionImage;
		std::uint32_t color = 0;
		int territories = 0, regions = 0;
		bool allied = false, defeated = false, left = false;
	};
	std::vector<Player> players; // player order, including the local player
	std::string scenarioName, objectives, fiction, gameType; // localized texts
};

class AptStrategicPlayerStatus : public AptScreen
{
public:
	AptStrategicPlayerStatus(WindowManager &windows, Shell &shell, const StrategicPlayerStatusData &data, const GameTextSource *text);
	bool closed() const { return m_closed; }
private:
	bool m_closed = false;
};
