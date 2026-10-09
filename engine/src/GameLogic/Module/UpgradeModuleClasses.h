// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (StatusBitsUpgrade.h, ModelConditionUpgrade.h,
// ArmorUpgrade.h, WeaponSetUpgrade.h, CommandSetUpgrade.h, SubObjectsUpgrade.h are the donors; every field and step below is RotWK's).
//
// The upgrade module classes of lane UPGRADE-1, on the UpgradeModule base (GameLogic/Module/UpgradeModule.h). TARGET FACTS (RotWK game.dat, caveat S-001): the
// data tables (extra 8 after RW 0xC76AD8) and the mux implementation (slot 10) / removal (slot 8) / postUpgradeCheck (slot 6) of every class:
//   * StatusBitsUpgrade (create RW 0x64FDD8, table RW 0xC6E574): StatusToSet (+0x138), StatusToClear (+0x148) (RW 0x7B1E5C object status masks).
//     Implementation RW 0x8B8B3B: Object::setStatus(StatusToSet, true), setStatus(StatusToClear, false) (RW 0x68D440), then the custom anim (RW 0x8D28F1).
//     Removal RW 0x8B8B70: the reverse (no custom anim).
//   * ModelConditionUpgrade (create RW 0x6502A8, table RW 0xC6F3A0): AddConditionFlags (+0x138), RemoveConditionFlags (+0x184) (RW 0x4B8C21),
//     RemoveConditionFlagsInRange (RW 0x8BA71A: not ported, an INIException naming stop S-485; no retail data uses it), AddTempConditionFlag (+0x1D0,
//     RW 0x869F22 `ModelConditionState:<name>`, -1 when not given), TempConditionTime (+0x1D4 seconds). Implementation RW 0x8BA668: a non-empty remove set
//     clears its flags (RW 0x5E3B79), a non-empty add set sets its flags (RW 0x5E3BA5), and a temp flag with a time above 0 is set for
//     _ftol2(fild(LOGICFRAMES_PER_SECOND = 5) * time) frames through the SMCHelper (RW 0x68B581). Removal RW 0x8BA6DB: the removed flags are set again,
//     the added flags cleared.
//   * ArmorUpgrade (create RW 0x64FB61, table RW 0xD9F8B0): KillArmorUpgrade (+0x138), IgnoreArmorUpgrade (+0x139), ArmorSetFlag (+0x13C, an index into
//     the armor set names RW 0xD9FA80). Implementation RW 0x8B761E: nothing when IgnoreArmorUpgrade; the custom anim; with a body (+0x25C): Kill clears the
//     body's armor set flag (body slot 0x38) and the model condition of the table RW 0xDB1AB0, otherwise sets both (slot 0x34); the drawable refresh
//     (RW 0x68B53C). Removal RW 0x8B76DE: the reverse.
//   * WeaponSetUpgrade (create RW 0x64FFB4, table RW 0xC6EC90): WeaponCondition (+0x138, the 128-bit weapon set flags, RW 0x6C9951; the data constructor
//     RW 0x8B9937 starts it at PLAYER_UPGRADE, `or [edi], 8`). Implementation
//     RW 0x8B98F2: the custom anim, then Object::setWeaponSetFlags (RW 0x68DECA: OR the mask, one weapon set update RW 0x6C99E2). Removal RW 0x8B9910:
//     clear the flags (RW 0x6911B7), clear the custom anim, setUpgradeExecuted(false).
//   * CommandSetUpgrade (create RW 0x64FC25, table RW 0xC6DF70): CommandSet (+0x138). Implementation RW 0x8B7C68: the custom anim, the object's command set
//     override (RW 0x693B94, Object + 0x43C), the control bar refresh (client). Removal RW 0x8B7CFA: when executed, the custom anim clears, the override is
//     emptied when it is still this module's set, setUpgradeExecuted(false). postUpgradeCheck RW 0x8B7CAB: the removal, then attemptUpgrade(the controlling
//     player's completed mask | the object's mask) (no castle mask here).
//   * SubObjectsUpgrade (create RW 0x64FE80, table RW 0xC6E928): ShowSubObjects (+0x138), HideSubObjects (+0x144), ExcludeSubobjects (+0x150)
//     (RW 0x42E59E appends), UpgradeTexture (+0x15C, RW 0x8B9420: `<old> <ignored> <new>` appended), FadeTimeInSeconds (+0x168), WaitBeforeFadeInSeconds
//     (+0x16C), RecolorHouse (+0x170), SkipFadeOnCreate (+0x171), HideSubObjectsOnRemove (+0x172), UnHideSubObjectsOnRemove (+0x173).
//     Implementation RW 0x8B91AD: only when neither the object's nor the controlling player's completed mask holds a ConflictsWith bit (RW 0x8B8FD7); the
//     module's +0x20 flag is set; with a drawable: every HideSubObjects name goes to Drawable::showModule(name, hide) (RW 0x6789B4) and, when no draw module
//     takes it, to Drawable::showSubObject(name, hide, permanent) (RW 0x672823); every ShowSubObjects name goes to both; the UpgradeTexture swap
//     (RW 0x8B911E) and, with RecolorHouse, the house recolour of the excluded names (RW 0x67273A); the custom anim. Removal RW 0x8B928E: with
//     HideSubObjectsOnRemove the shown names hide again (and the texture swap), with UnHideSubObjectsOnRemove the hidden names show; the custom anim clears.
//     NOT PORTED (stop S-484, reported per object): the fade (the times of RW 0x8B8F40), the texture swap, the house recolour.

#pragma once

#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class ModuleFactory;

class StatusBitsUpgradeModuleData : public UpgradeModuleData
{
public:
	ObjectStatusMaskType m_statusToSet{};
	ObjectStatusMaskType m_statusToClear{};
	static void buildFieldParse(MultiIniFieldParse &p);
};

class StatusBitsUpgrade : public UpgradeModule
{
public:
	StatusBitsUpgrade(Thing *thing, const StatusBitsUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}

protected:
	void upgradeImplementation() override;
	void processUpgradeRemoval() override;

private:
	const StatusBitsUpgradeModuleData *m_data;
};

class ModelConditionUpgradeModuleData : public UpgradeModuleData
{
public:
	std::array<std::uint32_t, 19> m_addFlags{};
	std::array<std::uint32_t, 19> m_removeFlags{};
	int m_tempFlag = -1;
	float m_tempTime = 0.0f;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ModelConditionUpgrade : public UpgradeModule
{
public:
	ModelConditionUpgrade(Thing *thing, const ModelConditionUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}

protected:
	void upgradeImplementation() override;
	void processUpgradeRemoval() override;

private:
	const ModelConditionUpgradeModuleData *m_data;
};

class ArmorUpgradeModuleData : public UpgradeModuleData
{
public:
	bool m_killArmorUpgrade = false;
	bool m_ignoreArmorUpgrade = false;
	int m_armorSetFlag = 0;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ArmorUpgrade : public UpgradeModule
{
public:
	ArmorUpgrade(Thing *thing, const ArmorUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}
	// RW 0xDB1AB0: the model condition an armor set flag shows
	static int modelConditionOfArmorSetFlag(int flag);

protected:
	void upgradeImplementation() override;
	void processUpgradeRemoval() override;

private:
	void apply(bool set);
	const ArmorUpgradeModuleData *m_data;
};

class WeaponSetUpgradeModuleData : public UpgradeModuleData
{
public:
	std::array<std::uint32_t, 4> m_weaponCondition{ { 8u, 0u, 0u, 0u } }; ///< RW 0x8B9953: the data constructor sets bit 3 (PLAYER_UPGRADE)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class WeaponSetUpgrade : public UpgradeModule
{
public:
	WeaponSetUpgrade(Thing *thing, const WeaponSetUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}

protected:
	void upgradeImplementation() override;
	void processUpgradeRemoval() override;

private:
	const WeaponSetUpgradeModuleData *m_data;
};

class CommandSetUpgradeModuleData : public UpgradeModuleData
{
public:
	std::string m_commandSet;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CommandSetUpgrade : public UpgradeModule
{
public:
	CommandSetUpgrade(Thing *thing, const CommandSetUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}
	void postUpgradeCheck() override;

protected:
	void upgradeImplementation() override;
	void processUpgradeRemoval() override;

private:
	const CommandSetUpgradeModuleData *m_data;
};

class SubObjectsUpgradeModuleData : public UpgradeModuleData
{
public:
	struct TextureSwap
	{
		std::string from, to;
	};
	std::vector<std::string> m_showSubObjects;
	std::vector<std::string> m_hideSubObjects;
	std::vector<std::string> m_excludeSubobjects;
	std::vector<TextureSwap> m_upgradeTexture;
	float m_fadeTimeInSeconds = 0.0f;
	float m_waitBeforeFadeInSeconds = 0.0f;
	bool m_recolorHouse = false;
	bool m_skipFadeOnCreate = false;
	bool m_hideSubObjectsOnRemove = false;
	bool m_unHideSubObjectsOnRemove = false;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SubObjectsUpgrade : public UpgradeModule
{
public:
	SubObjectsUpgrade(Thing *thing, const SubObjectsUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}
	bool hasShown() const { return m_shown; }
	void crc(StateHasher &hasher) const override;

protected:
	void upgradeImplementation() override;
	void processUpgradeRemoval() override;

private:
	const SubObjectsUpgradeModuleData *m_data;
	bool m_shown = false; ///< RW module + 0x20
};

// RemoveUpgradeUpgrade (create RW 0x65058C, table RW 0xC6FB00): UpgradeToRemove (+0x138), UpgradeGroupsToRemove (+0x144) (RW 0x42E59E appends),
// SuppressEvaEventForRemoval (+0x150), RemoveFromAllPlayerObjects (+0x151). Implementation RW 0x8BC1B3 (when not executed): every UpgradeToRemove name
// (an unknown name is only a debug message) is taken away: a PLAYER upgrade from the controlling player (Player::removeUpgrade, silent with
// SuppressEvaEventForRemoval), an OBJECT upgrade from the object (RW 0x691438) and, with RemoveFromAllPlayerObjects, through Player::removeUpgrade(u, false)
// from all of the player's objects; then every upgrade of the object's mask whose GroupName is one of UpgradeGroupsToRemove and that is not one of this
// module's TriggeredBy upgrades is removed from the object; then executed and the custom anim. Removal RW 0x8BC045: the custom anim clears, not executed.
class RemoveUpgradeUpgradeModuleData : public UpgradeModuleData
{
public:
	std::vector<std::string> m_upgradeToRemove;
	std::vector<std::string> m_upgradeGroupsToRemove;
	bool m_suppressEvaEventForRemoval = false;
	bool m_removeFromAllPlayerObjects = false;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RemoveUpgradeUpgrade : public UpgradeModule
{
public:
	RemoveUpgradeUpgrade(Thing *thing, const RemoveUpgradeUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}

protected:
	void upgradeImplementation() override;
	void processUpgradeRemoval() override;

private:
	const RemoveUpgradeUpgradeModuleData *m_data;
};

// GrantUpgradeCreate (create RW 0x6508A3, table RW 0xC70158): UpgradeToGrant (+8, a name looked up when it is granted: RW 0x66F5E5, an unknown name grants
// nothing), ExemptStatus (+0xC, RW 0x7B1E5C, empty by default), GiveOnBuildComplete (+0x1C). onCreate RW 0x8BD15E: without GiveOnBuildComplete, when
// ExemptStatus holds UNDER_CONSTRUCTION and the object is not under construction, the upgrade is granted; onBuildComplete RW 0x8BD1D4: once (the create
// module's flag, RW 0x4986C4), the upgrade is granted. Granting: a PLAYER upgrade Player::addUpgrade(u, COMPLETE, false) of the controlling player, an
// OBJECT upgrade Object::giveUpgrade (RW 0x69388B).
class GrantUpgradeCreateModuleData : public ModuleData
{
public:
	std::string m_upgradeToGrant;
	ObjectStatusMaskType m_exemptStatus{};
	bool m_giveOnBuildComplete = false;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class GrantUpgradeCreate : public BehaviorModule, public CreateModuleInterface
{
public:
	GrantUpgradeCreate(Thing *thing, const GrantUpgradeCreateModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	CreateModuleInterface *getCreate() override { return this; }
	void onCreate() override;
	void onBuildComplete() override;
	void crc(StateHasher &hasher) const override;

private:
	void grant();
	const GrantUpgradeCreateModuleData *m_data;
	bool m_needToRunOnBuildComplete = true; ///< the create module's flag (RW module + 0x14)
};

namespace UpgradeModuleClasses
{
// binds the typed data and the runtime class of every class above (the economy's two are bound by EconomyModules)
void registerAll(ModuleFactory &modules);
// the counts of what the runtime could not apply (stop S-484: fades, texture swaps, house recolours), reset by resetStats
struct Stats
{
	unsigned subObjectFadesNotPorted = 0;
	unsigned textureSwapsNotPorted = 0;
	unsigned houseRecolorsNotPorted = 0;
};
Stats &stats();
// the unknown names the upgrade modules met at run time (RW keeps them as debug messages): RemoveUpgradeUpgrade's UpgradeToRemove, GrantUpgradeCreate's UpgradeToGrant
std::vector<std::string> &unknownUpgradeNames();
// the lane's acceptance stops (S-480 .. S-485) with the run-time counts, appended to GameLogic::report().stops
std::vector<std::string> stopLines();
} // namespace UpgradeModuleClasses
