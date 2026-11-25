// OpenBFME. GPL-3.0.
//
// OilSpillUpdate: drops oil breadcrumb objects and emits oil spill FX along movement path (lane MODGAP-2, stop S-6025).
//
// Both editions:
//   RotWK 2.01:
//     - friend_newModuleInstance: RW 0x0064CC80
//     - friend_newModuleData:     RW 0x0064CCB8
//     - buildFieldParse:          RW 0x0088F7BD, table RW 0x00C62AD8
//     - ModuleData ctor:          RW 0x0088F7D8, vtable RW 0x00C62B28
//     - OilSpillUpdate ctor:      RW 0x0088F863, vtable RW 0x00C62BB0
//     - getModuleNameKey:         RW 0x0088F8CA
//     - ~OilSpillUpdate:          RW 0x0088F90F
//     - update():                 RW 0x0088F97A
//     - xfer():                   RW 0x0088FA81
//   BFME2 1.06:
//     - friend_newModuleInstance: BFME2 0x0024CFA7
//     - friend_newModuleData:     BFME2 0x0024CFDF
//     - buildFieldParse:          BFME2 0x0048C151, table BFME2 0x00C4C2A8
//     - ModuleData ctor:          BFME2 0x0048C16C, vtable BFME2 0x00C4C2F8
//     - OilSpillUpdate ctor:      BFME2 0x0048C1F7, vtable BFME2 0x00C4C380
//     - getModuleNameKey:         BFME2 0x0048C25E
//     - ~OilSpillUpdate:          BFME2 0x0048C2A3
//     - update():                 BFME2 0x0048C30E
//     - xfer():                   BFME2 0x0048C415

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/StateHash.h"
#include "GameLogic/Module/SpellEffectModules.h"
#include "GameLogic/SnapshotXfer.h"

#include <string>
#include <vector>

class ModuleFactory;

// Local 173: RW 0x88F7FD (~OilSpillUpdateModuleData)
class OilSpillUpdateModuleData : public FireWeaponUpdateModuleData
{
public:
	OilSpillUpdateModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p);

	std::string m_breadcrumbName;         ///< +0x10 BreadcrumbName (parseAsciiString)
	std::string m_ignitionWeaponName;     ///< +0x14 IgnitionWeaponName (parseAsciiString)
	float m_ignitionWeaponSpacing = 0.0f; ///< +0x18 IgnitionWeaponSpacing (parseReal, default 0.0f)
	std::string m_oilSpillFX;             ///< +0x1C OilSpillFX (parseFXList)
};

// RW 0x0088F8C4 (getModuleName)
class OilSpillUpdate : public FireWeaponUpdate
{
public:
	OBFME_MODULE_XFER(OilSpillUpdate)
	OilSpillUpdate(Thing *thing, const OilSpillUpdateModuleData *data);
	~OilSpillUpdate() override;

	UpdateSleepTime update() override; ///< RW 0x0088F97A / BFME2 0x0048C30E
	void crc(StateHasher &hasher) const override;

	const std::vector<Coord3D> &breadcrumbs() const { return m_breadcrumbs; }
	const OilSpillUpdateModuleData *getData() const { return m_oilData; }

	static const char *stopLine();
	static void registerClass(ModuleFactory &modules);

private:
	const OilSpillUpdateModuleData *m_oilData = nullptr;
	std::vector<Coord3D> m_breadcrumbs; ///< +0x2C
};
