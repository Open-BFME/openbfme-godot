// OpenBFME. GPL-3.0. See docs/CRATE_PICKUPS.md for target/donor evidence and limits.
#pragma once
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/MoneyEventModules.h"

class GameLogic;

class VeterancyCrateCollideModuleData : public CrateCollideModuleData
{
public:
	unsigned m_effectRange = 0;
	bool m_addsOwnerVeterancy = false;
	bool m_isPilot = false;
	int m_affectsUpToLevel = 10;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CrateCollide : public BehaviorModule, public CollideModuleInterface
{
public:
	CrateCollide(Thing *thing, const CrateCollideModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	CollideModuleInterface *getCollide() override { return this; }
	void onCollide(Object *other, const Coord3D *, const Coord3D *) override;
	bool wouldLikeToCollideWith(const Object &other) const override;
	void crc(StateHasher &h) const override;
	bool executed() const { return m_executed; }
	static void processContacts(GameLogic &logic);
	static const char *contactStop();
	static const char *limitationsStop();
protected:
	virtual bool valid(const Object &other) const;
	virtual bool execute(Object &other) = 0;
	void xferCrate(Xfer &x);
	const CrateCollideModuleData *m_data;
private:
	bool m_executed = false;
};

class SalvageCrateCollide : public CrateCollide
{
public:
	OBFME_MODULE_XFER(SalvageCrateCollide)
	SalvageCrateCollide(Thing *thing, const SalvageCrateCollideModuleData *data) : CrateCollide(thing, data), m_salvage(data) {}
protected:
	bool valid(const Object &other) const override;
	bool execute(Object &other) override;
private:
	const SalvageCrateCollideModuleData *m_salvage;
};

class VeterancyCrateCollide : public CrateCollide
{
public:
	OBFME_MODULE_XFER(VeterancyCrateCollide)
	VeterancyCrateCollide(Thing *thing, const VeterancyCrateCollideModuleData *data) : CrateCollide(thing, data), m_veterancy(data) {}
protected:
	bool valid(const Object &other) const override;
	bool execute(Object &other) override;
private:
	const VeterancyCrateCollideModuleData *m_veterancy;
};
