// OpenBFME. GPL-3.0. GiveUpgradeUpdate, RotWK 2.01 (the BFME2 editions' module data has one more field, see the .cpp).
// The module's vtable is RW 0xC67B58 (getModuleName RW 0x0089FB79, getModuleNameKey RW 0x0089FBF4, dtor RW 0x0089FC39 / RW 0x0089FB7F, xfer RW 0x0089FB9F): slot 0x34 finishAbility RW 0x89FD5C, slot 0x38 approachTarget RW 0x8A006C,
// slot 0x44 triggerAbilityEffect RW 0x8A01B2; update RW 0x8A00C2 is in the UpdateModule table. Its own helpers:
// releaseTarget RW 0x89FD15, the delivery's closing part RW 0x89FE01, the provider lookup RW 0x89FD77, the search
// RW 0x89FEBD and the give test RW 0x89FE64. Field table RW 0xC67AE8 (SpawnOutFX + 0xE0, FadeOutSpeed + 0xE4,
// DeliverUpgrade + 0xE8).
#pragma once
#include "GameLogic/Module/SpecialAbilityModules.h"

class UpgradeTemplate;
class HordeContainInterface;

class GiveUpgradeUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	// GiveUpgradeEffect is NOT a field of this module in RotWK 2.01 (its table is RW 0xC67AE8, three fields): the
	// game parses it nowhere, so it has no reader here either. It stays parseable because mod INI blocks carry it
	// and an unknown field stops the INI loader; nothing reads the value (stop S-6011).
	std::string m_giveFX;
	std::string m_spawnFX;      // SpawnOutFX, module data + 0xE0 (RW 0xC67AE8, parser RW 0x73A302)
	float m_fadeSpeed = 0.025f; // FadeOutSpeed, + 0xE4 (parser RW 0x42ED00)
	bool m_deliver = false;     // DeliverUpgrade, + 0xE8 (parser RW 0x42E558)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class GiveUpgradeUpdate : public SpecialAbilityUpdate
{
public:
	OBFME_MODULE_XFER(GiveUpgradeUpdate)
	GiveUpgradeUpdate(Thing *thing, const GiveUpgradeUpdateModuleData *data);
	void onObjectCreated() override;
	bool canGiveUpgradeTo(Object &target) const; // RW 0x89FE64
	void initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *targetObj, const Coord3D *loc, unsigned options, int extra) override; // RW 0x0089FC9D (SP vtable slot 0; search RW 0x8A006C / RW 0x89FEBD)
	bool allowsSpecialPowerUse() const override; // RW 0x0089FCD6 (SP vtable slot 8 / +0x20; DONOR BFME2 0x0049C553)
	UpdateSleepTime update() override;                        // RW 0x8A00C2
	void finishAbility(bool ownerDying, bool aborted) override; // RW 0x89FD5C (vtable slot 0x34)
	void crc(StateHasher &h) const override;
	static void registerClass(ModuleFactory &factory);
	static const char *stopLine();
protected:
	void triggerAbilityEffect() override; // RW 0x8A01B2 (vtable slot 0x44)
private:
	const UpgradeTemplate *upgrade() const;
	void findDeliveryTarget(bool ordered = false);                    // RW 0x89FEBD (ordered: the ability was given by a player or a script, not by the module's own search)
	void releaseTarget();                                           // RW 0x89FD15
	void finishDelivery();                                          // RW 0x89FE01
	HordeContainInterface *hordeProviderOf(const Object &target) const; // RW 0x89FD77
	const GiveUpgradeUpdateModuleData *m_giveData;
	bool m_fading = false, m_searching = false;
	float m_opacity = 1.0f;
};
