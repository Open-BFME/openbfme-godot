// OpenBFME. GPL-3.0.
//
// AnnounceBirthAndDeathBehavior (lane MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06):
// Announces a hero's initial spawn on onCreate() and death on onDie() to the in-game UI notification box.
//
// TARGET FACTS:
// - Registry: RW 0x6592CA / BFME2 0x658038 ("AnnounceBirthAndDeathBehavior", interface mask 10 = 0x2 | 0x8,
//   MODULEINTERFACE_DIE | MODULEINTERFACE_CREATE).
// - AnnounceBirthAndDeathBehaviorModuleData: createData RW 0x654AD2 / BFME2 0x654AE0 (8B, vtable RW 0xC07A30 / BFME2 0xBF2858),
//   buildFieldParse RW 0x63F3BF / BFME2 0x4B3FD0 (empty table, no INI fields).
// - AnnounceBirthAndDeathBehavior: create RW 0x64C4CA / BFME2 0x64C7C7 (0x1CB),
//   ctor RW 0x8883B1 / BFME2 0x885125 over CreateModule (RW 0x8BCE38 / BFME2 0x8B8CF7, initializes m_needToRunOnBuildComplete = true at +0x14);
//   primary vtable RW 0xC60ECC / BFME2 0xC4A61C, CreateModuleInterface vtable RW 0xC60E00 / BFME2 0xC4A550 (+0x8),
//   DieModuleInterface vtable RW 0xC60DFC / BFME2 0xC4A54C (+0x10).
// - onCreate(): CreateModuleInterface slot 0, RW 0x888439 / BFME2 0x8851AD:
//   when TheInGameUI (RW 0xDE4830 / BFME2 0xDFEDF0) and m_object ([this - 8]) are non-null, calls
//   InGameUI::announceHeroInitialSpawn(m_object) (RW 0x6A1C9E / BFME2 0x6A123F), which passes notification type
//   "HeroInitialSpawn" (RW 0xC13408 / BFME2 0xBFD0D8), message label InGameUI + 0x9D0, and timeout InGameUI + 0x9D4
//   to InGameUI::announceHeroEvent (RW 0x6A03A9 / BFME2 0x69F954).
// - onBuildComplete(): CreateModuleInterface slot 1, RW 0x6507E3 / BFME2 0x650A49 (CreateModule::onBuildComplete):
//   sets m_needToRunOnBuildComplete = false.
// - onDie(const DieModuleInterface::Event &): DieModuleInterface slot 0, RW 0x888453 / BFME2 0x8851C7:
//   when TheInGameUI and m_object ([this - 0x10]) are non-null, calls
//   InGameUI::announceHeroDeath(m_object) (RW 0x6A1CE2 / BFME2 0x6A1283), which passes notification type
//   "HeroDeath" (RW 0xC13428 / BFME2 0xBFD0F8), message label InGameUI + 0x9E0, and timeout InGameUI + 0x9E4
//   to InGameUI::announceHeroEvent (RW 0x6A03A9 / BFME2 0x69F954).
// - InGameUI::announceHeroEvent (RW 0x6A03A9 / BFME2 0x69F954):
//   gates on obj.isKindOf("HERO") ([tt + 0x113] & 4) and obj.getControllingPlayer() == localPlayer (RW 0x6AA7B2 / BFME2 0x6A9CD5),
//   formats the localized message with obj.getDisplayName() (RW 0x68FA0F), and opens the InGameNotificationBox (InGameUI + 0x9CC, RW 0x8EB66D).
// - xfer: RW 0x8BD56C (calls CreateModule::xfer RW 0x8BCE8F) / BFME2 0x8851E3 (calls CreateModule::xfer BFME2 0x8B8D4E):
//   version 1, xferBehaviorModule, xferBool(&m_needToRunOnBuildComplete).

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DieModule.h"

#include <string>

class ModuleFactory;
class MultiIniFieldParse;
class Object;
class Player;

class AnnounceBirthAndDeathBehaviorModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AnnounceBirthAndDeathBehavior : public BehaviorModule, public CreateModuleInterface, public DieModuleInterface
{
public:
	OBFME_MODULE_XFER(AnnounceBirthAndDeathBehavior)

	static constexpr const char *kHeroInitialSpawnType = "HeroInitialSpawn"; ///< RW 0xC13408 / BFME2 0xBFD0D8
	static constexpr const char *kHeroDeathType = "HeroDeath"; ///< RW 0xC13428 / BFME2 0xBFD0F8

	AnnounceBirthAndDeathBehavior(Thing *thing, const AnnounceBirthAndDeathBehaviorModuleData *data);

	CreateModuleInterface *getCreate() override { return this; }
	~AnnounceBirthAndDeathBehavior() override = default; // RW 0x00888434 / RW 0x0065470B
	DieModuleInterface *getDie() override { return this; } // RW 0x00863C00

	void onCreate() override; // RW 0x888439 / BFME2 0x8851AD
	void onBuildComplete() override; // RW 0x6507E3 / BFME2 0x650A49
	bool shouldAttemptOnBuildComplete() const { return m_needToRunOnBuildComplete; }

	void onDie(const DieModuleInterface::Event &event) override; // RW 0x888453 / BFME2 0x8851C7

	void crc(StateHasher &h) const override;

	int birthAnnouncementCount() const { return m_birthAnnouncements; }
	int deathAnnouncementCount() const { return m_deathAnnouncements; }
	const std::string &lastNotificationType() const { return m_lastNotificationType; }

	// RW 0x6A03BA .. 0x6A03DD / BFME2 0x69F965 .. 0x69F988:
	// InGameUI::announceHeroEvent gate: true when the object is KindOf HERO and controlled by localPlayer.
	static bool shouldAnnounceHeroEvent(const Object &obj, const Player *localPlayer);

	static void registerClass(ModuleFactory &factory); // getModuleName RW 0x008883E9, getModuleNameKey RW 0x008883EF
	static const char *stopLine();

private:
	bool m_needToRunOnBuildComplete = true; ///< CreateModule + 0x14 (RW 0x8BCE38 / BFME2 0x8B8CF7)
	int m_birthAnnouncements = 0;
	int m_deathAnnouncements = 0;
	std::string m_lastNotificationType;
};
