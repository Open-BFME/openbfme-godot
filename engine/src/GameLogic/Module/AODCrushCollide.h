// OpenBFME. GPL-3.0.
// Both editions: collision RW 0x8C0201 / BFME2 0x8BC0B8; update RW 0x8C042F /
// BFME2 0x8BC2E6. Horde notification parity stop S-6007 remains; two-object OCL ported in PORT-237 (S-6008 closed).
// Local 171: RW 0x650DBC (AODCrushCollide newInstance), RW 0x650DF4 (AODCrushCollide newData)
#pragma once
#include "Common/INI.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"
#include <string>

class ModuleFactory;
class AODCrushCollideModuleData : public ModuleData
{
public:
	std::string fx[3], ocl[3];
	int damageType = 1, deathType = 0;
	float damage = 0.0f;
	ObjectFilter specialObject;
	int specialDamageType = 1, specialDeathType = 0;
	float specialDamage = 0.0f;
	int selfDamageType = 1, selfDeathType = 0;
	float selfDamage = 0.0f;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AODCrushCollide : public UpdateModule, public CollideModuleInterface
{
public:
	OBFME_MODULE_XFER(AODCrushCollide)
	AODCrushCollide(Thing *thing, const AODCrushCollideModuleData *data);
	CollideModuleInterface *getCollide() override { return this; }
	bool wouldLikeToCollideWith(const Object &) const override { return false; }
	void onCollide(Object *other, const Coord3D *loc, const Coord3D *normal) override;
	UpdateSleepTime update() override;
	void crc(StateHasher &h) const override;
	static void registerClass(ModuleFactory &modules);
	static const char *hordeStop();
	static const char *oclStop();
private:
	const AODCrushCollideModuleData *m_data;
	UnsignedInt m_clearFrame = 0;
	ObjectID m_lastTarget = INVALID_ID;
	bool m_firing = false;
};
