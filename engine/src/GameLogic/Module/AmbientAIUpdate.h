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
	int m_condition = -1;
	bool m_wildBeast = false, m_selectable = true;
	bool m_processed = false;
	Coord3D m_origin{ 0, 0, 0 };
};
