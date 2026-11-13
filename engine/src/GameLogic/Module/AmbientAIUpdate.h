// OpenBFME. GPL-3.0.
#pragma once
#include "GameLogic/Module/AIUpdate.h"

// Idle wandering shared by AnimalAIUpdate and non-WildBeast WanderAIUpdate.
// Donor: Open-BFME-2 AnimalAIUpdate.cpp / WanderAIUpdate.cpp (see implementation).
class AmbientAIUpdate final : public AIUpdateInterface
{
public:
	AmbientAIUpdate(Thing *thing, const ModuleData *data, bool animal);
	UpdateSleepTime update() override;
	void crc(StateHasher &h) const override;
	OBFME_MODULE_XFER(AmbientAIUpdate)
	void xferAIStateBody(Xfer &x) override;
	static const char *stopLine();
private:
	bool m_animal;
	int m_percentage = 50, m_distance = 10, m_radius = 10, m_range = 20;
	// AnimalAIUpdateModuleData + 0x68 FleeDistance (default 100, data constructor RW 0x64BB4D) and + 0x78 UpdateTimer
	// (default 10, RW 0x64BB3D), both read by the enemy scan of RW AnimalAIUpdate::update 0x882058. UpdateTimer is an
	// unsigned int in the data (parser RW 0x42EC5E); held as int like the other fields of this module.
	int m_fleeDistance = 100, m_updateTimer = 10;
	int m_condition = -1;
	bool m_wildBeast = false, m_selectable = true;
	bool m_processed = false;
	Coord3D m_origin{ 0, 0, 0 };
};
