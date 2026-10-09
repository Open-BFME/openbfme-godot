// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ObjectCreationList (ZH Include/GameLogic/ObjectCreationList.h, Source/GameLogic/Object/ObjectCreationList.cpp): the `ObjectCreationList <Name>`
// INI block and the CreateObject nugget the summon special powers need. Lane SPELL-1.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly):
//   * TheObjectCreationListStore is RW 0xDE370C (a map name key -> list at store + 0xC, the retired lists at store + 0x18). The block parser
//     RW 0x5F0462: name = getNextToken, key = nameToKey; an existing entry is retired (flag + 0x10 = 1, pushed to + 0x18) only under load type 5;
//     in every case a NEW list (0x18 bytes, RW 0x5EFF5F) replaces the map entry (the LAST definition wins) and the nugget table RW 0xBF6970 parses
//     the block: CreateObject (RW 0x5F296A), CreateDebris (RW 0x5F2C59), ApplyRandomForce (RW 0x5F063E), FireWeapon (RW 0x5F05B8), Attack
//     (RW 0x5F05ED). Each nugget is its own sub-block ending with End and is appended to the list's nugget vector (+ 0x4, RW 0x5F042E).
//   * the generic nugget (constructor RW 0x5F0689, 0x12C bytes) has the 40-row table RW 0xBF6E50; CreateObject adds RW 0xBF74F8 (17 rows) and sets
//     + 0xA8 (isCreateObject) = 1; CreateDebris adds RW 0xBF7640. Defaults: Count 1 (+ 0x28), Disposition ON_GROUND_ALIGNED (+ 0x40 = 2),
//     VelocityScale 1.0, MinHealth / MaxHealth 1.0, JustBuiltDuration 1, PreserveLayer TRUE, IssueMoveAfterCreation TRUE, the rest 0.
//     Disposition is a bit string over RW 0xD9E3A8 (19 names).
//   * OCL::create (RW 0x5F00CA, primary object, secondary position): every nugget's vslot 3 in order; CreateObject's (RW 0x5F275D) calls the
//     creation core RW 0x5F0EE6 and then IssueMoveAfterCreation / OrientInSecondaryDirection on what it made.
//   * the creation core RW 0x5F0EE6, ported part: for each of Count: idx = GameLogicRandomValue(0, names - 1) (RW 0x5F11CD, ObjectCreationList.cpp
//     line 0x686, drawn even for one name); the template by name (RW 0x6D1305; unknown: nothing for this count); unless IgnoreCommandPointLimit
//     the source's player must afford the template's command points (RW 0x5F1238 -> 0x6A7F79), else the nugget stops; the object is made on the
//     source's team (RW 0x5F1391 -> ThingFactory::newObject) at the position + Offset and gets onBuildComplete (RW 0x5F1623 -> 0x68D252).
// NOT PORTED (stop S-530): the other nugget kinds' effects (CreateDebris, ApplyRandomForce, FireWeapon, Attack), the RequiresLivePlayer /
// MaxSimultaneousOfType / WaypointSpawnPoints / container / formation / disposition physics / fade / invulnerable / busy / veterancy /
// health / just-built / orientation paths of RW 0x5F0EE6, and every logic random draw those paths make besides the name pick.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/NameKeyGenerator.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class GameLogic;
class Object;

struct OCLNugget
{
	enum Kind
	{
		CREATE_OBJECT,
		CREATE_DEBRIS,
		APPLY_RANDOM_FORCE,
		FIRE_WEAPON,
		ATTACK
	};
	Kind kind = CREATE_OBJECT;
	// the generic table RW 0xBF6E50 (the values the CreateObject port reads; the others are kept as their text)
	std::string putInContainer, particleSystem;
	int count = 1;                     // + 0x28
	Coord3D offset{};                  // + 0x34
	unsigned disposition = 2;          // + 0x40 ON_GROUND_ALIGNED
	bool ignoreCommandPointLimit = false; // + 0x91 (CreateObject table)
	bool requiresLivePlayer = false;      // + 0x90
	std::vector<std::string> objectNames; // + 0x04 (CreateObject ObjectNames; CreateDebris ModelNames)
	std::map<std::string, std::string> otherFields; // every other row, its text (S-530)
};

class ObjectCreationList
{
public:
	const std::string &getName() const { return m_name; }
	const std::vector<OCLNugget> &nuggets() const { return m_nuggets; }
	// RW 0x5F00CA: the objects the list made (the CreateObject part, see the file comment). `source` may be null (no team: nothing is made).
	std::vector<Object *> create(GameLogic &logic, const Object *source, const Coord3D &where) const;
	// the unported nugget kinds / fields met by create (S-530)
	mutable unsigned long long m_unportedUses = 0;

	std::string m_name;
	std::vector<OCLNugget> m_nuggets;
	bool m_retired = false;
};

class ObjectCreationListStore
{
public:
	explicit ObjectCreationListStore(NameKeyGenerator &keys) : m_keys(keys) {}
	~ObjectCreationListStore();
	ObjectCreationListStore(const ObjectCreationListStore &) = delete;
	ObjectCreationListStore &operator=(const ObjectCreationListStore &) = delete;

	void parseObjectCreationListDefinition(INI *ini);              // RW 0x5F0462
	static void parseObjectCreationListDefinitionGlobal(INI *ini); // throws INIException(3, "TheObjectCreationListStore==NULL") without a store
	const ObjectCreationList *findObjectCreationList(const std::string &name) const;
	size_t size() const { return m_lists.size(); }

private:
	NameKeyGenerator &m_keys;
	std::map<NameKeyType, std::shared_ptr<ObjectCreationList>> m_lists; ///< store + 0xC
	std::vector<std::shared_ptr<ObjectCreationList>> m_retired;         ///< store + 0x18
};

extern thread_local ObjectCreationListStore *TheObjectCreationListStore;
