// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/Object/Update/DockUpdate/DockUpdate.cpp,
// SupplyCenterDockUpdate.cpp).
//
// DockUpdate and SupplyCenterDockUpdate (lane LUMBER1): the dock a worker brings its lumber to (the lumber mills), the approach queue in front of it and the
// income of a delivery.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; logs/lumber1 research, every body read in the disassembly):
//   * data: DockUpdateModuleData (RW 0x991731, table RW 0xC87674): NumberApproachPositions (parseInt, + 0x08, default 0), AllowsPassthrough (parseBool,
//     + 0x0C, default 1); SupplyCenterDockUpdateModuleData (RW 0x8A479C, table RW 0xC68478): ValueMultiplier (parseReal, + 0x10, default 1.0),
//     BonusScience (parseScience RW 0x73AFE3, + 0x14, default -1 = none), BonusScienceMultiplier (parsePercentToReal, + 0x18, default 1.0).
//   * the instance (RW 0x99230C; SupplyCenterDockUpdate RW 0x8A4493 adds nothing): the enter / dock / exit positions (bone offsets, zero), the approach
//     position count (the data's; -1: as many as come), the approach bone count (-1 until loaded), positions loaded, three parallel vectors (approach
//     positions, their owners, reached flags; 10 entries when the count is -1, else the count), the active docker, docker inside, crippled, open (1).
//   * loadDockPositions (RW 0x99187C): needs the drawable; the bones DockStart / DockAction / DockEnd (and DockWaiting for a positive count) are the
//     client's (S-209's pristine bones); RotWK's lumber mill model has none (checked in the W3D) and NumberApproachPositions -1, so every position stays
//     zero and the bone count is 0. INFERENCE (stop S-5201): the port loads no bones: a dock template with a positive NumberApproachPositions or with
//     Dock* bones would need them (no retail RotWK dock has either); such a dock is reported once.
//   * the interface (RW vtable 0xC684B8, 20 slots): isClearToApproach RW 0x99175A, reserveApproachPosition RW 0x99221D (computeApproachPosition RW
//     0x991BFE), advanceApproachPosition RW 0x991DBF, isClearToEnter RW 0x991715, isClearToAdvance RW 0x9919A0, getEnterPosition RW 0x9919F3,
//     getDockPosition RW 0x991A6C, getExitPosition RW 0x991ACF, onApproachReached RW 0x991B32, onEnterReached RW 0x991E5E, onDockReached RW 0x9917A3,
//     onExitReached RW 0x991800, action (SupplyCenterDockUpdate RW 0x8A4574), cancelDock RW 0x991EF8, isDockOpen / setDockOpen RW 0x8A44C4 / 0x8A44C8,
//     an unknown slot that answers false (RW 0x7FEAC1), setDockCrippled RW 0x991727, isAllowPassthroughType RW 0x991753, isRallyPointAfterDockType
//     (false, RW 0x9188EB). RotWK's queue removal (RW 0x991B80, onEnterReached and cancelDock) shifts the later owners down and always clears the last
//     slot (ZH clears the one slot).
//   * update RW 0x991F8A: no active docker and not crippled: the first reached owner becomes the active docker; the SUPPLY_SOURCE / DOZER + HARVESTER
//     MOVING clear does not apply to a supply center (not a SUPPLY_SOURCE); never sleeps. SupplyCenterDockUpdate::update RW 0x8A47C4 adds the model
//     condition UPGRADE_ECONOMY_BONUS (332) once the owner has BonusScience.
//   * action RW 0x8A4574: see SupplyCenterDockUpdate::action.
//   * xfer: SupplyCenterDockUpdate RW 0x8A44F8 (version 1) -> DockUpdate RW 0x99248C: UpdateModule::xfer, then version 1, the three positions, the
//     approach count, loaded, the three vectors, the active docker, inside, crippled, open (the reached flags are written but RW does not store them back
//     on a load: the port does, S-4220's own layout keeps a loaded game going the same way).
// DONOR: ZH DockUpdate.cpp / SupplyCenterDockUpdate.cpp (the same shape; differences above).

#pragma once

#include "Common/INI.h"
#include "Common/Module.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class Object;
struct MultiIniFieldParse;

class DockUpdateInterface
{
public:
	virtual ~DockUpdateInterface() {}
	virtual bool isClearToApproach(const Object &docker) const = 0;                          // + 0x00
	virtual bool reserveApproachPosition(Object &docker, Coord3D *pos, int *index) = 0;     // + 0x04
	virtual bool advanceApproachPosition(Object &docker, Coord3D *pos, int *index) = 0;     // + 0x08
	virtual bool isClearToEnter(const Object &docker) const = 0;                            // + 0x0C
	virtual bool isClearToAdvance(const Object &docker, int index) const = 0;               // + 0x10
	virtual void getEnterPosition(Object &docker, Coord3D *pos) = 0;                        // + 0x14
	virtual void getDockPosition(Object &docker, Coord3D *pos) = 0;                         // + 0x18
	virtual void getExitPosition(Object &docker, Coord3D *pos) = 0;                         // + 0x1C
	virtual void onApproachReached(Object &docker) = 0;                                     // + 0x20
	virtual void onEnterReached(Object &docker) = 0;                                        // + 0x24
	virtual void onDockReached(Object &docker) = 0;                                         // + 0x28
	virtual void onExitReached(Object &docker) = 0;                                         // + 0x2C
	virtual bool action(Object &docker, Object *drone) = 0;                                 // + 0x30
	virtual void cancelDock(Object &docker) = 0;                                            // + 0x34
	virtual bool isDockOpen() const = 0;                                                    // + 0x38
	virtual void setDockOpen(bool open) = 0;                                                // + 0x3C
	virtual bool unknownSlot40() const { return false; }                                    // + 0x40, RW 0x7FEAC1 (no caller found)
	virtual void setDockCrippled(bool setting) = 0;                                         // + 0x44
	virtual bool isAllowPassthroughType() const = 0;                                        // + 0x48
	virtual bool isRallyPointAfterDockType() const { return false; }                        // + 0x4C, RW 0x9188EB
};

class DockUpdateModuleData : public ModuleData
{
public:
	int m_numberApproachPositions = 0; ///< + 0x08
	bool m_isAllowPassthrough = true;  ///< + 0x0C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DockUpdate : public UpdateModule, public DockUpdateInterface
{
public:
	enum
	{
		DYNAMIC_APPROACH_VECTOR_FLAG = -1,
		DEFAULT_APPROACH_VECTOR_SIZE = 10 ///< RW 0x99231E: the vectors' first size for a dynamic count
	};
	DockUpdate(Thing *thing, const DockUpdateModuleData *data);
	DockUpdateInterface *getDockUpdateInterface() override { return this; }
	UpdateSleepTime update() override; // RW 0x991F8A

	bool isClearToApproach(const Object &docker) const override;
	bool reserveApproachPosition(Object &docker, Coord3D *pos, int *index) override;
	bool advanceApproachPosition(Object &docker, Coord3D *pos, int *index) override;
	bool isClearToEnter(const Object &docker) const override;
	bool isClearToAdvance(const Object &docker, int index) const override;
	void getEnterPosition(Object &docker, Coord3D *pos) override;
	void getDockPosition(Object &docker, Coord3D *pos) override;
	void getExitPosition(Object &docker, Coord3D *pos) override;
	void onApproachReached(Object &docker) override;
	void onEnterReached(Object &docker) override;
	void onDockReached(Object &docker) override;
	void onExitReached(Object &docker) override;
	void cancelDock(Object &docker) override;
	bool isDockOpen() const override { return m_dockOpen; }
	void setDockOpen(bool open) override { m_dockOpen = open; }
	void setDockCrippled(bool setting) override { m_dockCrippled = setting; }
	bool isAllowPassthroughType() const override { return m_baseData->m_isAllowPassthrough; }

	ObjectID activeDocker() const { return m_activeDocker; }
	const std::vector<ObjectID> &approachOwners() const { return m_approachPositionOwners; }
	const std::vector<bool> &approachReached() const { return m_approachPositionReached; }
	bool dockerInside() const { return m_dockerInside; }

	void crc(StateHasher &hasher) const override;
	static std::string stopLine(); ///< [S-5201]

protected:
	void xferDockUpdate(Xfer &x); // RW 0x99248C

private:
	void loadDockPositions();                                           // RW 0x99187C
	Coord3D computeApproachPosition(int index, const Object &docker);   // RW 0x991BFE
	void removeOwner(int index);                                        // RW 0x991B80
	Coord3D boneToWorld(const Coord3D &local) const;                    // Object RW 0x70BCE7

	const DockUpdateModuleData *m_baseData;
	Coord3D m_enterPosition{ 0.0f, 0.0f, 0.0f }; ///< + 0x24
	Coord3D m_dockPosition{ 0.0f, 0.0f, 0.0f };  ///< + 0x30
	Coord3D m_exitPosition{ 0.0f, 0.0f, 0.0f };  ///< + 0x3C
	int m_numberApproachPositions = 0;         ///< + 0x48
	int m_numberApproachPositionBones = -1;    ///< + 0x4C
	bool m_positionsLoaded = false;            ///< + 0x50
	std::vector<Coord3D> m_approachPositions;  ///< + 0x54
	std::vector<ObjectID> m_approachPositionOwners; ///< + 0x60
	std::vector<bool> m_approachPositionReached;    ///< + 0x6C
	ObjectID m_activeDocker = INVALID_ID;      ///< + 0x80
	bool m_dockerInside = false;               ///< + 0x84
	bool m_dockCrippled = false;               ///< + 0x85
	bool m_dockOpen = true;                    ///< + 0x86
};

class SupplyCenterDockUpdateModuleData : public DockUpdateModuleData
{
public:
	float m_valueMultiplier = 1.0f;          ///< + 0x10 (RW 0xBD1908)
	int m_bonusScience = -1;                 ///< + 0x14 (SCIENCE_INVALID)
	float m_bonusScienceMultiplier = 1.0f;   ///< + 0x18
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SupplyCenterDockUpdate : public DockUpdate
{
public:
	OBFME_MODULE_XFER(SupplyCenterDockUpdate)
	SupplyCenterDockUpdate(Thing *thing, const SupplyCenterDockUpdateModuleData *data);
	static void registerClass(ModuleFactory &modules);
	UpdateSleepTime update() override;                 // RW 0x8A47C4
	bool action(Object &docker, Object *drone) override; // RW 0x8A4574

	// the money a delivery of `boxes` boxes pays the dock's owner (RW 0x8A45C6 .. 0x8A46C0), the deposit left out (the tests read it): `value` is the float
	// the experience gets, `amount` the money; nothing is paid (paid false) for a value of 0 or less or a dock without an owner
	struct Delivery
	{
		bool paid = false;
		float value = 0.0f;
		int amount = 0;
	};
	Delivery computeDelivery(Object &dock, int boxes) const;

private:
	const SupplyCenterDockUpdateModuleData *m_data;
};
