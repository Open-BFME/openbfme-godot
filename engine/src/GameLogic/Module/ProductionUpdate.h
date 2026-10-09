// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ProductionUpdate (ZH Include/GameLogic/Module/ProductionUpdate.h, Source/GameLogic/Object/Update/ProductionUpdate.cpp; B1 .../ProductionUpdate_*.cpp),
// lane PROD-1: the production queue of a building. The full port notes (layouts, the update() flow, the arithmetic) are in
// workspace/rebuild/specs/production.md; this header keeps the load-bearing facts.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * module data: createData RW 0x64E577 (0x50 bytes), data constructor RW 0x8A2FD2, table RW 0xC68128 (17 rows); the ProductionModifier block RW 0x8A160B
//     (table RW 0xC67CB8) and QuantityModifier RW 0x8A3633 append to lists; MaxQueueEntries defaults to 0x14 (20), DisabledTypesToProcess to
//     DISABLED_HELD.
//   * module: create RW 0x64E53C (0x140 bytes), constructor RW 0x8A17D8, update RW 0x8A1B9F, ProductionUpdateInterface vtable RW 0xC67DB0 (31 slots at
//     module + 0x20, named in ProductionUpdateInterface below with their addresses).
//   * a production entry (RW 0x8A08D3, 0x54 bytes) is a float progress counter: each update adds 1.0f (through the attribute modifier of type 0xD,
//     unported) to +0x1C, percent = progress / (float)totalFrames * 100.0f, the unit is made when percent >= 100.0f. totalFrames is
//     calcTimeToBuild (BuildAssistant.h): five times whole seconds.
//   * the money is withdrawn per queued unit and the cost is stored in the entry: a cancel refunds exactly that (RW 0x8A13EE), not a recomputed cost.
// DONOR: B1 ProductionUpdate_*.cpp, ZH ProductionUpdate.cpp (the door state machine and the construction complete state are ZH's with RW's order).
//
// Lane UPGRADE-1: upgrade entries (type 2): queueUpgrade RW 0x8A0FDA, cancelUpgrade RW 0x8A1140, the progress with UpgradeTemplate::calcTimeToBuild
// (RW 0x8A04DA -> 0x66F1A8) and the completion (RW 0x8A2015: Player::addUpgrade COMPLETE or Object::giveUpgrade RW 0x69388B).
// Lane HERO-1: hero / build-index entries (type 3): queueCreateUnit's index branch (RW 0x8A123D .. 0x8A1312: the player's hero list record, its cost and start,
// RW 0x780AD3 / 0x7812B2), the pause pass (RW 0x8A0669), the ready entry of currentEntry (RW 0x8A078A), the record's progress as the completion gate with the
// hero countdown (RW 0x8A1F12), the object from the record (RW 0x78142F) and the revive hand-off (RW 0x8A2C98: RespawnUpdate::onRevived, the level cap,
// countdown 4 * LOGICFRAMES_PER_SECOND, NO_COLLISIONS RW 0x6907D7), cancel by index (RW 0x8A047B / 0x8A03EC) and the record's cancel (RW 0x8A1460).
// NOT PORTED (stop S-203): the experience tracker (XP to the producer, VeteranUnitsFromVeteranFactory), audio (speed bonus loop, voices), the EVA messages,
// the attribute modifiers of the producer (the progress rate is 1.0f), the player upgrades applied to a new unit (RW 0x6936FE), the entry name /
// script naming (RW +0x40), the horde-join of entry + 0x3C, the BuildFadeInOnCreateList extra objects' drawables, SecondaryQueue selection of the
// dispatcher is ported but hero queues are not.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/UpdateModule.h"

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

struct ObjectFilter;
class Object;
class UpgradeTemplate;
class Player;
class ThingTemplate;
class ModuleFactory;

enum ProductionType
{
	PRODUCTION_INVALID = 0,
	PRODUCTION_UNIT = 1,
	PRODUCTION_UPGRADE = 2,
	PRODUCTION_BUILD_INDEX = 3 ///< RotWK's hero / purchase list entry (HERO-1)
};

typedef std::uint32_t ProductionID;

// RW 0x8A08D3
struct ProductionEntry
{
	ProductionType type = PRODUCTION_INVALID;
	const ThingTemplate *objectToProduce = nullptr; ///< +8
	const UpgradeTemplate *upgradeToResearch = nullptr; ///< +0xC (lane UPGRADE-1)
	ProductionID productionID = 1;                  ///< +0x10
	float percentComplete = 0.0f;                   ///< +0x14
	float percentPerFrame = 0.0f;                   ///< +0x18
	float framesUnderConstruction = 0.0f;           ///< +0x1C (a float counter)
	int quantityTotal = 0;                          ///< +0x20
	int quantityProduced = 0;                       ///< +0x24
	int cost = 0;                                   ///< +0x28
	int exitDoor = DOOR_NONE_AVAILABLE;             ///< +0x2C
	int value30 = -1;                               ///< +0x30
	bool flag34 = false;                            ///< +0x34 (set when a horde entry became its member entry)
	ObjectID hordeTarget = INVALID_ID;              ///< +0x3C
	std::string name;                               ///< +0x40
	bool flag44 = false;                            ///< +0x44
	int quantityRemaining() const { return quantityTotal - quantityProduced; }
};

// RW 0x8A160B block entry (0x14 bytes)
struct ProductionModifier
{
	std::string requiredUpgrade;   ///< +0
	std::shared_ptr<ObjectFilter> filter; ///< +4 ModifierFilter; null = the parser's default (ALL)
	float costMultiplier = 1.0f;   ///< +8
	float timeMultiplier = 1.0f;   ///< +0xC
	bool heroPurchase = false;     ///< +0x10
	bool heroRevive = false;       ///< +0x11
};

struct QuantityModifier
{
	std::string templateName;
	int quantity = 1;
};

class ProductionUpdateModuleData : public ModuleData
{
public:
	int m_numDoorAnimations = 0;                 // +8
	unsigned m_doorOpeningTime = 0;              // +0xC (frames)
	unsigned m_doorWaitOpenTime = 0;             // +0x10
	unsigned m_doorClosingTime = 0;              // +0x14 (DoorCloseTime)
	unsigned m_constructionCompleteDuration = 0; // +0x18
	std::vector<QuantityModifier> m_quantityModifiers; // +0x1C
	int m_maxQueueEntries = 0x14;                // +0x28
	std::uint32_t m_disabledTypesToProcess = 1u << 3; // +0x2C (DISABLED_HELD = index 3 of the name table RW 0xDAD904: DEFAULT USER_PARALYZED EMP HELD PARALYZED UNMANNED UNDERPOWERED FREEFALL TEMPORARILY_BUSY SCRIPT_DISABLED SCRIPT_UNDERPOWERED USER_FROZEN)
	bool m_giveNoXP = false;                     // +0x30
	unsigned m_unitInvulnerableTime = 0;         // +0x34
	unsigned m_specialPrepModelConditionTime = 0; // +0x38
	std::list<ProductionModifier> m_productionModifiers; // +0x3C
	bool m_veteranUnitsFromVeteranFactory = false; // +0x40
	bool m_setBonusModelConditionOnSpeedBonus = false; // +0x41
	std::string m_bonusForType;                  // +0x44
	std::string m_speedBonusAudioLoop;           // +0x48 (an audio event name)
	bool m_secondaryQueue = false;               // +0x4C

	static void buildFieldParse(MultiIniFieldParse &p);
};

// the part of ProductionUpdate the command dispatcher and the AI use (RW vtable 0xC67DB0 at module + 0x20)
class ProductionUpdateInterface
{
public:
	virtual ~ProductionUpdateInterface() = default;
	virtual int canQueueCreateUnit() const = 0;                          // slot 0, RW 0x8A0711: 3 factory disabled, 4 queue full, else 0
	virtual ProductionID requestUniqueUnitID() = 0;                      // slot 2, RW 0x8A18FA
	// slot 8, RW 0x8A11D2
	virtual bool queueCreateUnit(const ThingTemplate *unitType, int buildIndex, ProductionID productionID, int value30, bool batch, const std::string &name, bool flag44) = 0;
	virtual void cancelUnitCreate(ProductionID productionID) = 0;        // slot 10, RW 0x8A13EE
	virtual void cancelUnitCreateByType(const ThingTemplate *type, bool all) = 0; // slot 5, RW 0x8A0428
	virtual int countUnitTypeInQueue(const ThingTemplate *type) const = 0; // slot 7, RW 0x8A056C (type 1 entries only)
	virtual bool isSecondaryQueue() const = 0;                           // slot 30, RW 0x8A06E2
	virtual int getProductionCount() const = 0;                          // slot 17
	virtual const ProductionEntry *firstProduction() const = 0;          // slot 21
	virtual const ProductionEntry *nextProduction(const ProductionEntry *e) const = 0; // slot 22
	virtual void setHoldDoorOpen(int door, bool hold) = 0;               // slot 23, RW 0x8A0D8A
	virtual void setFactoryDisabled(bool disabled) = 0;                  // slot 24
	virtual bool isFactoryDisabled() const = 0;                          // slot 25
	virtual float productionCostMultiplier(const ThingTemplate *type) const = 0; // slot 26, RW 0x8A093D
	virtual float productionTimeMultiplier(const ThingTemplate *type) const = 0; // slot 27, RW 0x8A09B7
	virtual void cancelAllProduction() = 0;                              // slot 15, RW 0x8A05B2
	virtual int maxQueueEntries() const = 0;
	// ---- lane UPGRADE-1: upgrade research (the entries of type 2) ----
	virtual int canQueueUpgrade(const UpgradeTemplate *upgrade) const = 0; // slot 1, RW 0x8A06ED: 4 queue full, 3 TEMPORARILY_DEFECTED, else 0
	virtual bool queueUpgrade(const UpgradeTemplate *upgrade) = 0;          // slot 3, RW 0x8A0FDA
	virtual void cancelUpgrade(const UpgradeTemplate *upgrade) = 0;         // slot 4, RW 0x8A1140
	virtual bool isUpgradeInQueue(const UpgradeTemplate *upgrade) const = 0; // slot 6, RW 0x8A053D
	// ---- lane HERO-1: hero (build-index, type 3) entries ----
	virtual float heroCostMultiplier(bool revive) const = 0;                // slot 28, RW 0x8A0A31
	virtual float heroTimeMultiplier(bool revive) const = 0;                // slot 29, RW 0x8A0AB0
	virtual void cancelUnitCreateByBuildIndex(int buildIndex) = 0;          // slot 12, RW 0x8A047B
};

class ProductionUpdate : public UpdateModule, public ProductionUpdateInterface
{
public:
	ProductionUpdate(Thing *thing, const ProductionUpdateModuleData *data);

	static void registerClass(ModuleFactory &modules);

	ProductionUpdateInterface *getProductionUpdateInterface() override { return this; }
	UpdateSleepTime update() override;
	DisabledMaskType getDisabledTypesToProcess() const override { return m_data->m_disabledTypesToProcess; }
	void crc(StateHasher &hasher) const override;

	// ProductionUpdateInterface
	int canQueueCreateUnit() const override;
	ProductionID requestUniqueUnitID() override { return m_uniqueID++; }
	bool queueCreateUnit(const ThingTemplate *unitType, int buildIndex, ProductionID productionID, int value30, bool batch, const std::string &name, bool flag44) override;
	void cancelUnitCreate(ProductionID productionID) override;
	void cancelUnitCreateByType(const ThingTemplate *type, bool all) override;
	int countUnitTypeInQueue(const ThingTemplate *type) const override;
	bool isSecondaryQueue() const override { return m_data->m_secondaryQueue; }
	int getProductionCount() const override { return (int)m_queue.size(); }
	const ProductionEntry *firstProduction() const override { return m_queue.empty() ? nullptr : m_queue.front().get(); }
	const ProductionEntry *nextProduction(const ProductionEntry *e) const override;
	void setHoldDoorOpen(int door, bool hold) override;
	void setFactoryDisabled(bool disabled) override { m_factoryDisabled = disabled; }
	bool isFactoryDisabled() const override { return m_factoryDisabled; }
	float productionCostMultiplier(const ThingTemplate *type) const override;
	float productionTimeMultiplier(const ThingTemplate *type) const override;
	void cancelAllProduction() override;
	int maxQueueEntries() const override { return m_data->m_maxQueueEntries; }
	int canQueueUpgrade(const UpgradeTemplate *upgrade) const override;
	bool queueUpgrade(const UpgradeTemplate *upgrade) override;
	void cancelUpgrade(const UpgradeTemplate *upgrade) override;
	bool isUpgradeInQueue(const UpgradeTemplate *upgrade) const override;
	float heroCostMultiplier(bool revive) const override;
	float heroTimeMultiplier(bool revive) const override;
	void cancelUnitCreateByBuildIndex(int buildIndex) override;
	unsigned heroCountdown() const { return m_heroCountdown; } ///< RW + 0x128 (lane HERO-1)
	unsigned postExitDelay() const { return m_postExitDelay; } ///< RW + 0x118 (lane HERO-1 review: tests)

	const ProductionUpdateModuleData *data() const { return m_data; }
	ObjectID exitingObjectID() const { return m_exitingObjectID; }
	UnsignedInt constructionCompleteFrame() const { return m_constructionCompleteFrame; }

	struct DoorInfo
	{
		UnsignedInt openedFrame = 0;   // +0
		UnsignedInt waitOpenFrame = 0; // +4
		UnsignedInt closedFrame = 0;   // +8
		bool holdOpen = false;         // +0xC
	};
	const DoorInfo &door(int i) const { return m_doors[(size_t)i]; }

private:
	typedef std::array<std::uint32_t, 19> Flags;
	static void setBit(Flags &f, int bit, bool on);

	void updateDoors();
	ProductionEntry *currentEntry();
	void removeFromProductionQueue(ProductionEntry *e);
	void completeUpgrade(ProductionEntry *entry, Player *player); // lane UPGRADE-1
	void completeUnit(ProductionEntry *entry, Player *player, UnsignedInt now);
	int totalProductionFrames(const ProductionEntry &e, Player *player) const;
	void flushModelConditions();
	void pauseHeroEntries();                                   ///< RW 0x8A0669 (lane HERO-1)
	void cancelFirstUnitOfType(const ThingTemplate *type);     ///< slot 11, RW 0x8A03EC

	const ProductionUpdateModuleData *m_data;
	std::vector<std::unique_ptr<ProductionEntry>> m_queue; ///< RW +0x28 head .. +0x2C tail
	ProductionID m_uniqueID = 1;                           ///< RW +0x30
	UnsignedInt m_constructionCompleteFrame = 0;           ///< RW +0x38
	DoorInfo m_doors[DOOR_COUNT_MAX];                      ///< RW +0x3C
	Flags m_clearFlags{}, m_setFlags{};                    ///< RW +0x7C, +0xC8
	bool m_flagsDirty = false;                             ///< RW +0x114
	unsigned m_postExitDelay = 0;                          ///< RW +0x118
	bool m_extrasProcessed = false;                        ///< RW +0x11C
	ObjectID m_exitingObjectID = INVALID_ID;               ///< RW +0x120
	std::vector<ObjectID> m_extraObjects;                  ///< RW +0x124
	bool m_cancellingAll = false;                          ///< RW +0x11D
	bool m_factoryDisabled = false;                        ///< RW +0x13C
	unsigned m_heroCountdown = 0;                          ///< RW +0x128 (lane HERO-1: frames between two revives, RW 0x8A2C1E)
};
