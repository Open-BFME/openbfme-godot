// OpenBFME. GPL-3.0.
// ProductionQueueHordeContain: units entering a port are handed to its production queue (EvilPort, issue #178).
// RotWK 2.01 game.dat:
//   * Registration: RW 0x00658518 (mask 95), instance factory RW 0x0064BE3D, data factory RW 0x0064BE78
//   * Class size: module 0x9E4 (ctor RW 0x0088459B), data 0xE0 (ctor RW 0x008849CE)
//   * DestinationTemplate field table RW 0x00C5F964, parser RW 0x0088494C, store offset +0xD4
//   * Entry slot 29 (RW 0x00884729) and slot 30 (RW 0x00884691)
//   * CreateTemplate RW 0x0088460B: requests unit ID, tests filter, queues unit and sets hordeTarget (slot 14, RW 0x008A03A0)
// DONOR BFME2 1.06:
//   * Code/GameEngine/Source/GameLogic/Object/Contain/ProductionQueueHordeContain.cpp
//   * Code/GameEngine/Source/GameLogic/Object/Contain/ProductionQueueHordeContainSlots.cpp

#pragma once

#include "GameLogic/Module/GarrisonContain.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/ObjectFilter.h"

#include <string>
#include <vector>

struct DestinationTemplateEntry
{
	std::string templateName;
	ObjectFilter filter;
};

// Data class size 0xE0 (RW 0x0064BE78, ctor RW 0x008849CE): base HordeGarrisonContainBehaviorData (0xD4) + vector at +0xD4
class ProductionQueueHordeContainBehaviorData : public HordeGarrisonContainBehaviorData
{
public:
	std::vector<DestinationTemplateEntry> destinationTemplates; // offset +0xD4 (RW 0x00C5F964)

	ProductionQueueHordeContainBehaviorData() = default;

	// RW 0x008849B3 (DONOR BFME2 0x0048175B)
	static void buildFieldParse(MultiIniFieldParse &p);
	// RW 0x0088494C (DONOR BFME2 0x004816F4)
	static void iniParseTemplateContainer(INI *ini, void *instance, void *store, const void *userData);
};

// Module class size 0x9E4 (RW 0x0064BE3D, ctor RW 0x0088459B): derives from HordeGarrisonContain (GarrisonContain with isHordeGarrison() == true)
class ProductionQueueHordeContain : public GarrisonContain
{
public:
	OBFME_MODULE_XFER(ProductionQueueHordeContain)

	// RW 0x0088459B (DONOR BFME2 0x00481343)
	ProductionQueueHordeContain(Thing *thing, const ProductionQueueHordeContainBehaviorData *data);
	~ProductionQueueHordeContain() override = default;

	static void registerClass(ModuleFactory &modules);

	// Slot 29 (RW 0x00884729) & slot 30 (RW 0x00884691) entry logic
	bool addToContain(Object *obj) override;

	void crc(StateHasher &hasher) const override;

	const std::vector<DestinationTemplateEntry> &destinationTemplates() const { return m_queueData->destinationTemplates; }

private:
	// RW 0x0088460B (DONOR BFME2 0x004813B3): matches enteringObj against DestinationTemplate filters, queues unit and sets hordeTarget
	void createTemplate(ObjectID id, Object *enteringObj);

	const ProductionQueueHordeContainBehaviorData *m_queueData = nullptr;
};
