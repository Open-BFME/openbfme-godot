// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// BehaviorModule and the module interfaces (ZH Include/GameLogic/Module/BehaviorModule.h and the *Module.h headers; spec
// ini-and-object-model.md 5.1). A BehaviorModule answers the typed accessors (getBody, getContain, getCreate, ...); a module returns
// `this` for the interfaces it implements and null for the rest, and the Object caches the ones it needs when it builds its module
// list (RW 0x69A3B3: body, contain and the AI interface after each module is made).
//
// Only the interface members the live object layer needs are declared; the lanes that port a module class add the rest of the
// interface they need (damage, die, special power, collide, ...). The forward declared interfaces (Collide, Damage, Die, Upgrade,
// SpecialPower) are placeholders so the accessor set matches the binary's slot list; a module that implements one derives from the
// class its lane defines.

#pragma once

#include "Common/Module.h"
#include "GameLogic/ObjectTypes.h"

#include <list>
#include <vector>

class Object;
struct Coord3D;
struct DamageInfo;
struct DamageInfoInput;
class AIUpdateInterface;
class CollideModuleInterface;
class DamageModuleInterface;
class DieModuleInterface;
class SpecialPowerModuleInterface;
class UpdateModuleInterface;
class UpdateModule;
class HordeContainInterface;
class ExitInterface;                 // GameLogic/Module/ExitInterface.h (lane PROD-1)
class ProductionUpdateInterface;     // GameLogic/Module/ProductionUpdate.h (lane PROD-1)
class FoundationAIUpdate;            // GameLogic/Module/ConstructionModules.h (lane BUILD-1)

// ZH Include/GameLogic/Module/BodyModule.h BodyDamageType
enum BodyDamageType
{
	BODY_PRISTINE = 0,
	BODY_DAMAGED,
	BODY_REALLYDAMAGED,
	BODY_RUBBLE,
	BODYDAMAGETYPE_COUNT
};

// ZH BodyModuleInterface (the health part; damage handling belongs to the combat lane)
class BodyModuleInterface
{
public:
	virtual ~BodyModuleInterface() = default;
	virtual float getHealth() const = 0;
	virtual float getMaxHealth() const = 0;
	virtual float getInitialHealth() const = 0;
	virtual BodyDamageType getDamageState() const = 0;
	// ZH BodyModuleInterface::setInitialHealth(Int initialPercent): the map's objectInitialHealth (and the production of a damaged unit)
	virtual void setInitialHealth(int initialPercent) = 0;
	// lane COMBAT-2: the object's modules are all built (Object::buildModules ends): the body applies its initial damage state (RW 0x8C3841's constructor ends with setCorrectDamageState, RW
	// 0x8C39D0; here it needs the finished object: model conditions, damage modules, structure effects)
	virtual void applyInitialDamageState() {}
	// ---- lane COMBAT-1 (RW ActiveBody vtable 0xC720F0 slots; ZH BodyModuleInterface) ----
	// RW 0x8C3FA3 ActiveBody::attemptDamage: armour, the body scalar, the health change, the death
	virtual void attemptDamage(DamageInfo &info) { (void)info; }
	// RW 0x8C2FC1 ActiveBody::attemptHealing
	virtual void attemptHealing(DamageInfo &info) { (void)info; }
	// RW 0x8C1C51 (thunk) estimateDamage: what the armour leaves of the input, without the flank terms; used by the AI to decide whether a weapon can hurt
	virtual float estimateDamage(const DamageInfoInput &input) const { (void)input; return 0.0f; }
	// lane SCRIPT-2: RW vt[0x88] (ActiveBody RW 0x8C3326) setIndestructible: the body's + 0xC7 byte (attemptDamage returns at once while it is set,
	// RW 0x8C3FDA); a BRIDGE passes it to its four towers (RW 0x8C3337 .. 0x8C337C, not ported here). A body class without one ignores it
	virtual void setIndestructible(bool on) { (void)on; }
	virtual bool isIndestructible() const { return false; }
	// RW vt[0x90]: the body's DodgePercent (a fraction)
	virtual float getDodgePercent() const { return 0.0f; }
	// lane XP-1: RW vt[0x5C] (ActiveBody RW 0x8C1CD5) setMaxHealth(newMax, changeType): 1 keeps the health ratio, 2 adds the difference to the health
	// (the AttributeModifier HEALTH / HEALTH_MULT bonuses use 1). A body class without the override counts the call (bodyMaxHealthCallsIgnored)
	virtual void setMaxHealth(float newMax, int changeType)
	{
		(void)newMax;
		(void)changeType;
		++bodyMaxHealthCallsIgnored();
	}
	static unsigned long long &bodyMaxHealthCallsIgnored()
	{
		static unsigned long long n = 0;
		return n;
	}
};

// ZH ContainModuleInterface (the membership part). The members are Objects; the list keeps insertion order.
class ContainModuleInterface
{
public:
	virtual ~ContainModuleInterface() = default;
	typedef std::list<Object *> ContainedItemsList;
	virtual const ContainedItemsList *getContainedItemsList() const = 0;
	virtual unsigned getContainCount() const = 0;
	// true when the object was added
	virtual bool addToContain(Object *obj) = 0;
	virtual void removeFromContain(Object *obj) = 0;
	// ZH ContainModuleInterface::containReactToTransformChange: the container moved or turned (Object::reactToTransformChange)
	virtual void containReactToTransformChange() {}
	// RotWK ContainModuleInterface::getHordeContainInterface (B1 HordeContainCreatePayload.cpp)
	virtual HordeContainInterface *getHordeContainInterface() { return nullptr; }
	// lane HERO-2: the contain interface's vslot 0x108 (Object::defect RW 0x699368: the contained follow the container's defection; HordeContain RW 0x86ED25)
	// and vslot 0x10C (Object::endDefection RW 0x69ABA7; HordeContain RW 0x86EDB0). The other contain classes' slots are not read: a contain with
	// passengers reports the stop (S-1222)
	virtual void onDefect(Object *newOwner, bool permanent);
	virtual void onDefectionEnded();
	// ---- lane GARRISON-1 (the contain interface at module + 0x20; RW slots of the OpenContain table RW 0xC59AF0, GarrisonContain RW 0xC5C698, HordeGarrisonContain
	// RW 0xC5C9F0). The defaults are OpenContain's answers; a contain class that is not ported answers through UnportedBehaviorModule (no contain at all) ----
	// slot 0x10 isGarrisonable: GarrisonContain RW 0x8BD372 true, OpenContain RW 0x9188EB false (TunnelContain RW 0xC5DCE0 + 0x10: RW 0x9188EB false, lane UI-1)
	virtual bool isGarrisonable() const { return false; }
	// lane UI-1: slot 0xC8 isDisplayedOnControlBar (the control bar's transport inventory RW 0x943D6F -> RW 0x94251F asks it): OpenContain RW 0x9188EB false;
	// GarrisonContain / HordeGarrisonContain, TransportContain / HordeTransportContain, SiegeEngineContain / HordeSiegeEngineContain and TunnelContain RW 0x8BD372
	// true (their contain tables RW 0xC5C698, 0xC5C9F0, 0xC5A690, 0xC5C370, 0xC5D668, 0xC5DA10, 0xC5DCE0). HordeContain's table was not read: false (INFERENCE:
	// a horde's command sets hold no EXIT_CONTAINER button, so the answer shows nowhere)
	virtual bool isDisplayedOnControlBar() const { return false; }
	// slot 0x98 isValidContainerFor(obj, checkCapacity, checkPath) (OpenContain RW 0x86603B, GarrisonContain RW 0x87B8C5, HordeGarrisonContain RW 0x87D0E0)
	virtual bool isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const
	{
		(void)obj;
		(void)checkCapacity;
		(void)checkPath;
		return false;
	}
	// slot 0xB0 (RW 0x865C15): the module data's ObjectStatusOfContained mask (OpenContain data + 0x58); null for a contain without one
	virtual const ObjectStatusMaskType *getObjectStatusOfContained() const { return nullptr; }
	// slots 0x158 / 0x15C / 0x160: the entry position, the entry offset and the exit offset in the world (HordeGarrisonContain RW 0x87D1A7 / 0x87D1E0 / 0x87D219:
	// the data's EntryPosition / EntryOffset / ExitOffset through the container's transform, RW 0x87CEB3); false: the contain has none (the container's position)
	virtual bool getEntryPosition(Coord3D &out) const { (void)out; return false; }
	virtual bool getEntryOffset(Coord3D &out) const { (void)out; return false; }
	virtual bool getExitOffset(Coord3D &out) const { (void)out; return false; }
	// slot 0x80 orderAllPassengersToExit (HordeGarrisonContain RW 0x87CE8F -> 0x991027); `source` is a CommandSourceType
	virtual void orderAllPassengersToExit(int source) { (void)source; }
	// slot 0x44 onObjectWantsToEnterOrExit (ZH OpenContain::onObjectWantsToEnterOrExit; AIEnterState RW 0x751699 / 0x752B67, AIExitState RW 0x74352D): 0 enter,
	// 1 exit, 2 neither
	virtual void onObjectWantsToEnterOrExit(Object *obj, int wants) { (void)obj; (void)wants; }
	// the exit interface of the container (slot 0x74, RW 0x867A94: module + 0x30): AIExitState asks it whether the exit is busy (slot 0), reserves a door (slot 4) and
	// exits through it (slot 8)
	// slot 0x124 (RW 0x670313: interface + 0x48, the contained STEALTH_GARRISON objects) and slot 0x38 (RW 0x865A55: the data's AllowAlliesInside)
	virtual unsigned getStealthUnitsContained() const { return 0; }
	virtual bool allowAlliesInside() const { return true; }
	virtual bool isExitBusy() const { return false; }
	virtual int reserveDoorForExit(const Object &obj) { (void)obj; return -1; }
	virtual void exitObjectViaDoor(Object *obj, int door) { (void)obj; (void)door; }
	// lane GARRISON-2: slot 0xD4, read by RW 0x68BF11 for the locomotor's speed (OpenContain RW 0x867A75: 1.0; SiegeEngineContain RW 0x87ED45: the crew count x
	// SpeedPercentPerCrew, x87)
	virtual float getCrewPowerMultiplier() const { return 1.0f; }
	// lane IDLE-1: contain vslot 0xB8, asked by the idle mood scan (RW 0x66844A) of the object that owns this contain when that object is itself contained:
	// false for OpenContain, its plain heirs and HordeContain (RW 0x9188EB), SiegeEngineContain's CrewAllowedToFire (RW 0x87EDA9: data + 0x1A0)
	virtual bool moodScanWhileContained() const { return false; }
};

// ZH CreateModuleInterface: onCreate runs for every create module, in list order, after the object's constructor (spec 5.3 step 3;
// RW 0x6D16E6 loop calls vslot 0 of getCreate()). onBuildComplete belongs to the construction lane.
class CreateModuleInterface
{
public:
	virtual ~CreateModuleInterface() = default;
	virtual void onCreate() = 0;
	virtual void onBuildComplete() = 0;
};

// ZH DestroyModuleInterface: onDestroy runs inside GameLogic::destroyObject before the DESTROYED status is set (spec 5.5 step 2)
class DestroyModuleInterface
{
public:
	virtual ~DestroyModuleInterface() = default;
	virtual void onDestroy() = 0;
};

class UpgradeMux;
class ProjectileUpdateInterface;

class BehaviorModule : public ObjectModule
{
public:
	BehaviorModule(Thing *thing, const ModuleData *moduleData)
		: ObjectModule(thing, moduleData)
	{
	}

	virtual BodyModuleInterface *getBody() { return nullptr; }
	virtual CollideModuleInterface *getCollide() { return nullptr; }
	virtual ContainModuleInterface *getContain() { return nullptr; }
	virtual CreateModuleInterface *getCreate() { return nullptr; }
	virtual DamageModuleInterface *getDamage() { return nullptr; }
	virtual DestroyModuleInterface *getDestroy() { return nullptr; }
	virtual DieModuleInterface *getDie() { return nullptr; }
	virtual SpecialPowerModuleInterface *getSpecialPower() { return nullptr; }
	virtual UpdateModuleInterface *getUpdate() { return nullptr; }
	// RW slot 0x50 / 4 = 20 of the +0xC interface: the exit interface of a production exit module (Object::getObjectExitInterface, RW 0x68BB14)
	virtual ExitInterface *getExitInterface() { return nullptr; }
	// the production queue of a ProductionUpdate (RW 0x68C327 asks for it)
	virtual ProductionUpdateInterface *getProductionUpdateInterface() { return nullptr; }
	// lane BUILD-1: the FoundationAIUpdate interface (RW 0x68C3C3 asks every module through slot 38 of its +0xC interface)
	virtual FoundationAIUpdate *getFoundationAIUpdate() { return nullptr; }
	// RotWK adds the AI update interface to the interface list (RW slot 19)
	virtual AIUpdateInterface *getAIUpdateInterface() { return nullptr; }
	// the scheduler works on UpdateModule objects (RW reads the interface at module + 0x10, the module itself): this is the same module
	virtual UpdateModule *asUpdateModule() { return nullptr; }
	// lane UPGRADE-1: the UpgradeMux of an upgrade module (RW slot 0x28 / 4 = 10 of the +0xC interface; Object::updateUpgradeModules RW 0x6936FE asks every module)
	virtual UpgradeMux *getUpgrade() { return nullptr; }
	// ZH BehaviorModuleInterface::getProjectileUpdateInterface (lane PROJ-2: the client snapshot asks the projectile's look-ahead point and its launch)
	virtual ProjectileUpdateInterface *getProjectileUpdateInterface() { return nullptr; }
};
