// OpenBFME. GPL-3.0.
//
// Module types, interface masks and ModuleData, ported from ZH Include/Common/Module.h with the
// RotWK registry facts (RW = RotWK game.dat, caveat S-001).
//
// TARGET FACTS:
//   * Interface bits UPDATE 0x1, DIE 0x2, DAMAGE 0x4, CREATE 0x8, COLLIDE 0x10, BODY 0x20, CONTAIN 0x40,
//     UPGRADE 0x80, SPECIAL_POWER 0x100, DESTROY 0x200, DRAW 0x400, CLIENT_UPDATE 0x800 are the values
//     the registry stores (B1 Include/Common/Module.h:83-97; the registry golden shows 0x5F, 0x84,
//     0x8C ... as unions of them). RotWK adds CLIENT_BEHAVIOR 0x1000 (all six type-3 classes) and one
//     class, PillageModule, registers 0x2000, which has no name here (stop S-073).
//   * Module type 3, CLIENT_BEHAVIOR, is RotWK's addition (RW table userData of the ClientBehavior row).
//   * ModuleData vtable (RW 0x73F496, 0x73F4A9): slot 4 is isAiModuleData (true for 13 classes) and
//     slot 7 is a second predicate that parseModuleName consults when a ChildObject declares a module
//     (true for 11 body classes: every BODY-mask class except InactiveBody). Its NAME is unverified
//     (stop S-073); the behaviour is.

#pragma once

#include "Common/NameKeyGenerator.h"

#include <cstdint>
#include <string>

enum ModuleType
{
	MODULETYPE_BEHAVIOR = 0,
	MODULETYPE_DRAW = 1,
	MODULETYPE_CLIENT_UPDATE = 2,
	MODULETYPE_CLIENT_BEHAVIOR = 3, ///< RotWK addition
	NUM_MODULE_TYPES = 4
};

enum ModuleInterfaceType
{
	MODULEINTERFACE_UPDATE = 0x00000001,
	MODULEINTERFACE_DIE = 0x00000002,
	MODULEINTERFACE_DAMAGE = 0x00000004,
	MODULEINTERFACE_CREATE = 0x00000008,
	MODULEINTERFACE_COLLIDE = 0x00000010,
	MODULEINTERFACE_BODY = 0x00000020,
	MODULEINTERFACE_CONTAIN = 0x00000040,
	MODULEINTERFACE_UPGRADE = 0x00000080,
	MODULEINTERFACE_SPECIAL_POWER = 0x00000100,
	MODULEINTERFACE_DESTROY = 0x00000200,
	MODULEINTERFACE_DRAW = 0x00000400,
	MODULEINTERFACE_CLIENT_UPDATE = 0x00000800,
	MODULEINTERFACE_CLIENT_BEHAVIOR = 0x00001000 ///< RotWK addition (registry golden)
};

// ZH Include/Common/Module.h ModuleData. The two predicates are set by the ModuleFactory from the
// registry, so a typed data class does not have to implement them (it may still override them).
class ModuleData
{
public:
	ModuleData() = default;
	virtual ~ModuleData() = default;

	void setModuleTagNameKey(NameKeyType key) { m_moduleTagNameKey = key; }
	NameKeyType getModuleTagNameKey() const { return m_moduleTagNameKey; }

	// RW vtable slot 4
	virtual bool isAiModuleData() const { return m_aiModuleData; }
	// RW vtable slot 7 (name unverified, S-073): see the file comment
	virtual bool bfmeSlot7Predicate() const { return m_slot7Predicate; }

	// ZH ModuleData::buildFieldParse: a base class adds no table.
	// static void buildFieldParse(MultiIniFieldParse &p) {}

	// the factory's registry knowledge, copied onto every data object it makes
	void bfmeSetPredicates(bool ai, bool slot7)
	{
		m_aiModuleData = ai;
		m_slot7Predicate = slot7;
	}

private:
	NameKeyType m_moduleTagNameKey = NAMEKEY_INVALID;
	bool m_aiModuleData = false;
	bool m_slot7Predicate = false;
};

// ---------------------------------------------------------------------------------------------------------------------
// Runtime module classes (lane LOGIC-1). Port of ZH Include/Common/Module.h Module / ObjectModule / DrawableModule (spec
// ini-and-object-model.md 5.1).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; RW 0x6D16F5 onwards, the Object constructor's module loop and newObject):
//   * the object's behavior modules are made in template list order by ModuleFactory::newModule (RW 0x656480) and kept in a null
//     terminated array; every module's vtable has the Module callbacks in the ZH order (slot 5 onObjectCreated is called for each
//     behavior after the whole list exists, RW 0x69A637 `call [eax + 0x14]`).
//   * the BehaviorModuleInterface sub-object (module + 0xC) answers getBody (slot 0), getContain (slot 2), getCreate (slot 3, RW
//     0x6D16FA `call [eax + 0xC]`), getSpecialPower (slot 8, RW 0x693D9F `call [eax + 0x20]`), getUpdate (slot 9, RW 0x62BD82 `call
//     [eax + 0x24]`) and, RotWK only, the AI update interface (slot 19, RW 0x69A3E3 `call [eax + 0x4C]`).
//
// Not ported: Snapshot (xfer / loadPostProcess; saves belong to a later lane), preloadAssets (assets are the draw runtime's).
// `crc` is OpenBFME's own state hash hook (Common/StateHash.h), not retail's xfer CRC.
// ---------------------------------------------------------------------------------------------------------------------

class Thing;
class Object;
class Player;
class Drawable;
class StateHasher;
struct Coord3D;

class Module
{
public:
	explicit Module(const ModuleData *moduleData)
		: m_moduleData(moduleData)
	{
	}
	virtual ~Module() = default;
	Module(const Module &) = delete;
	Module &operator=(const Module &) = delete;

	// the module's CLASS name as the INI declared it (set by the ModuleFactory that made it; helper modules set their own)
	const std::string &getModuleClassName() const { return m_className; }
	NameKeyType getModuleNameKey() const { return m_classNameKey; }
	NameKeyType getModuleTagNameKey() const { return m_moduleData ? m_moduleData->getModuleTagNameKey() : NAMEKEY_INVALID; }
	const ModuleData *getModuleData() const { return m_moduleData; }
	// a content hash of the class name (the retail name hash, NameKeyGenerator::hash: a pure function), for the state hash: unlike a name KEY it does
	// not depend on the order names were first seen in
	std::uint32_t getModuleClassHash() const { return m_classHash; }
	void friend_setModuleClass(const std::string &name, NameKeyType key)
	{
		m_className = name;
		m_classNameKey = key;
		m_classHash = NameKeyGenerator::hash(name.c_str());
	}

	// ZH Module::onObjectCreated: called once all the modules of a Thing exist
	virtual void onObjectCreated() {}
	// ZH Module::onDrawableBoundToObject
	virtual void onDrawableBoundToObject() {}
	// ZH Module::onDelete: called on every module before the modules are deleted
	virtual void onDelete() {}
	// OpenBFME state hash hook: add the module's logic state (in a defined order, floats by bits)
	virtual void crc(StateHasher &hasher) const { (void)hasher; }
	// true for the instance ModuleFactory makes of a class whose behaviour is not ported (stop S-140): counted and reported
	virtual bool isUnported() const { return false; }
	// true for the Object constructor's helper modules (GameLogic/Module/ObjectHelper.h): they are not registry classes
	virtual bool isHelper() const { return false; }

private:
	const ModuleData *m_moduleData;
	std::string m_className;
	NameKeyType m_classNameKey = NAMEKEY_INVALID;
	std::uint32_t m_classHash = 0;
};

// ZH ObjectModule: a module of an Object
class ObjectModule : public Module
{
public:
	ObjectModule(Thing *thing, const ModuleData *moduleData);
	Object *getObject() const { return m_object; }

	// ZH ObjectModule::onCapture: the owner changed (RW 0x696F61: vtable slot 0x24 called with the old and the new PLAYER; the first declaration took Objects, ECON-1)
	virtual void onCapture(Player * /*oldOwner*/, Player * /*newOwner*/) {}

private:
	Object *m_object;
};

// ZH DrawableModule: a module of a Drawable (draw, client update, client behavior)
class DrawableModule : public Module
{
public:
	DrawableModule(Thing *thing, const ModuleData *moduleData);
	Drawable *getDrawable() const { return m_drawable; }
	// lane AUDIO-4: the drawable's transform changed (ZH DrawModule::reactToTransformChange; RotWK's ClientBehavior slot 11, read as this)
	virtual void reactToTransformChange(const Coord3D * /*oldPos*/, float /*oldAngle*/) {}

private:
	Drawable *m_drawable;
};
