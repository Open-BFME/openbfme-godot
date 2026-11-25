// OpenBFME. GPL-3.0.
// Both editions: ctor RW 0x008BCBCB (0x8BCBD0), dtor RW 0x008BCB05 / scalar deleting dtor RW 0x008BCC06, getModuleNameKey RW 0x008BCB1E, name getter RW 0x008BCB63,
// xfer RW 0x008BCB69, beginFadeOut RW 0x008BCC50, update RW 0x8BCC82; DONOR BFME2 0x8B89BF / 0x8B89D8 / 0x8B8A1D / 0x8B8A23 / 0x8B8A8A / 0x8B8AC5 /
// 0x8B8B0F / 0x8B8B41 (Rva004B89C4Update.cpp, Rva004B8B0F.cpp).
// Local 171: RW 0x6506F8 (OathbreakersFadeAwayBehavior newInstance)
#pragma once
#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

class ModuleFactory;
class OathbreakersFadeAwayBehaviorModuleData : public ModuleData
{
public:
	unsigned m_fadeOutTime = 1;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class OathbreakersFadeAwayBehavior : public UpdateModule
{
public:
	OBFME_MODULE_XFER(OathbreakersFadeAwayBehavior)
	OathbreakersFadeAwayBehavior(Thing *thing, const OathbreakersFadeAwayBehaviorModuleData *data);
	UpdateSleepTime update() override;
	void crc(StateHasher &h) const override;
	bool fadingStarted() const { return m_started; }
	float opacity() const { return m_fraction; }
	static const char *hordeStop();
	static void registerClass(ModuleFactory &modules);
private:
	const OathbreakersFadeAwayBehaviorModuleData *m_data;
	bool m_started = false;
	float m_fraction = 1.0f;
};
