// OpenBFME. GPL-3.0.
// EvaAnnounceClientCreate: RW 0x8CDE22, 0x8CDED6, 0x8CDF07 (S-001);
// BFME2 0x4C9A51 / 0x4C9A82, EvaAnnounceClientCreateSlots.cpp.
// Editions: both RotWK 2.01 and BFME2 1.06.
// Client presentation only. See S-7091 for snapshot timing, first-sight, radar
// and saves.
// CreateFakeRadarEvent (field row RW 0xC75958, parser RW 0x42E558, data +0x1B) is parsed and never read
// anywhere in RotWK 2.01 (clientUpdate RW 0x8CDF07, create slot RW 0x8CDED6 and xfer RW 0x8CDDD7 do not
// touch +0x1B). Kept parsed-only.
#pragma once
#include "Common/Module.h"
#include <cstdint>
class ModuleFactory;
class MultiIniFieldParse;
class EvaAnnounceClientCreateModuleData : public ModuleData
{
  public:
	static void buildFieldParse(MultiIniFieldParse &p);
	int enemy = -1, ally = -1, owner = -1;
	unsigned delay = 0;
	bool onlyVisible = false, firstSight = false, usePosition = false, fakeRadar = false;
};
class EvaAnnounceClientCreate : public DrawableModule
{
  public:
	EvaAnnounceClientCreate(Thing *thing, const EvaAnnounceClientCreateModuleData *data);
	OBFME_MODULE_XFER(EvaAnnounceClientCreate);
	void onBuildCompleteAt(std::uint32_t frame);
	// relationship: ENEMIES=0, NEUTRAL=1, ALLIES=2; owner selects owner event.
	int update(std::uint32_t frame, bool hidden, bool hasObject, bool owner, int relationship, bool &firstSight, bool alreadySighted = false);
	bool usePosition() const
	{
		return m_data->usePosition;
	}
	void restoreLoaded(bool started, std::uint32_t due)
	{
		m_started = started;
		m_due = due;
	}
	bool started() const { return m_started; }
	std::uint32_t due() const { return m_due; }
	void setStarted(bool started) { m_started = started; }
	void setDue(std::uint32_t due) { m_due = due; }
	static void registerClass(ModuleFactory &modules);

  private:
	const EvaAnnounceClientCreateModuleData *m_data;
	std::uint32_t m_due = 0;
	bool m_started = false;
};
