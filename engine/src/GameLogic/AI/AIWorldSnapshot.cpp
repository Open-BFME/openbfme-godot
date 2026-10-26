// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The obfme:AIWorld block (lane SAVE-4c, docs/SAVES.md section 8): TheAI's movement world and the AI update module family. OpenBFME native layout (stop
// S-4220). Retail has no such block: TheAI's own state is CHUNK_AiOrdersManager / the AIGroup xfers (AIGroup::xfer RW 0x7706EC, (1, 1)), the pathfinder is
// not saved by ZH (rebuilt from the map and the objects), and the AI modules travel inside Object::xfer (RW 0x697F28). This port keeps state retail rebuilds
// (the occupant chains' order, AIPathfindSnapshot.cpp) and keeps its AI modules' state together here, so obfme:Objects (SAVE-4b) carries the AI update
// family as made by its object and this block fills them, in object id order, each in a BLOK so a module that reads short or long is named.
// In order: the map flag, the AI modules' object ids (checked before anything is loaded), the pathfinder (Pathfinder::xfer), every pathfinder view's layer (ObjectPathfindAdapter + its layer: the unit's PathfindLayerEnum,
// hashed through its AI), every AI module (AIUpdateInterface::xferAIState), the movement failures, the noted stops of the report and the next formation id. Rebuilt, not saved:
// the per-template movement infos (parsed from the templates on use), the collision pass's scratch arrays, the queue diagnostics (peaks).

#include "GameLogic/AI/AIWorld.h"

#include "GameLogic/AI/AIXfer.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"

#include <set>
#include <utility>

void AIWorld::xfer(Xfer &x)
{
	AIXfer::IssuedIdScope ids(m_logic.getNextObjectID()); // the object ids the modules name: ids the game has issued
	AIXfer::transaction(x, [this](Xfer &t) { xferBody(t); }); // the whole block, or nothing (a refused load leaves the world as it was)
}

void AIWorld::xferBody(Xfer &x)
{
	// version 2 (lane FLANK-1): the next formation id (AI + 0x20, lane BACKLOG-8) is saved: it is hashed once a formation was made, and a fresh game's 1 made a
	// loaded snapshot's hash differ from the saved one (save2 retail T1); a version 1 snapshot keeps 1
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	const bool loading = x.isLoading();
	AIXfer::StringTableScope strings; // the units' stop lines once each (AIXfer::reportString)
	bool ready = m_mapReady;
	x.xferBool(&ready);
	if (ready != m_mapReady)
	{
		throw XferException(std::string("ai world: the snapshot's map grid is ") + (ready ? "made" : "not made") + ", this game's " + (m_mapReady ? "is" : "is not"));
	}

	// the AI modules by object id first: a snapshot of another set is refused before anything changes
	std::uint32_t ais = (std::uint32_t)m_ais.size();
	x.xferUnsignedInt(&ais);
	if (ais != (std::uint32_t)m_ais.size())
	{
		throw XferException("ai world: the snapshot holds " + std::to_string(ais) + " AI modules, the loaded game " + std::to_string(m_ais.size()));
	}
	for (auto &kv : m_ais)
	{
		PathfindObjectID id = kv.first;
		x.xferUnsignedInt(&id);
		if (id != kv.first)
		{
			throw XferException("ai world: the snapshot's AI of object " + std::to_string(id) + " where the loaded game has object " + std::to_string(kv.first) + "'s");
		}
	}
	m_pathfinder->xfer(x);
	// the pathfinder views' layers (a view exists from the object's first pathfinder use; a view the snapshot does not name is on the ground): read and
	// checked (an object of the game, named once, a layer 0 .. LAYER_LAST: the wall layers reach 0x40, lane WALL-1) before any layer changes
	std::vector<std::pair<ObjectID, PathfindLayerEnum>> views;
	if (!loading)
	{
		for (size_t id = 0; id < m_adapters.size(); ++id)
		{
			if (m_adapters[id])
			{
				views.emplace_back((ObjectID)id, m_adapters[id]->getLayer());
			}
		}
	}
	x.xferVectorSize(views, 8);
	std::set<ObjectID> seenViews;
	std::vector<Object *> viewObjects;
	for (auto &view : views)
	{
		x.xferObjectID(&view.first);
		AIXfer::enumIn(x, view.second, LAYER_INVALID, LAYER_LAST, "ai world: a pathfinder view's layer");
		if (!loading)
		{
			continue;
		}
		AIXfer::inRange(view.second, LAYER_INVALID, LAYER_LAST, "ai world: a pathfinder view's layer");
		Object *obj = m_logic.findObjectByID(view.first);
		if (!obj)
		{
			throw XferException("ai world: the pathfinder view of object " + std::to_string(view.first) + ", which the loaded game lacks");
		}
		if (!seenViews.insert(view.first).second)
		{
			throw XferException("ai world: the pathfinder view of object " + std::to_string(view.first) + " is saved twice");
		}
		viewObjects.push_back(obj);
	}
	if (loading)
	{
		// the views become exactly the saved ones (review r3: a view's presence is state, PathfindWorld's object lookup and the next save see it): every
		// AI module keeps its own view (made by its constructor), so each must be named; a view the snapshot does not name goes
		for (const auto &kv : m_ais)
		{
			if (!seenViews.count((ObjectID)kv.first))
			{
				throw XferException("ai world: the AI of object " + std::to_string(kv.first) + " has no pathfinder view in the snapshot");
			}
		}
		for (size_t id = 0; id < m_adapters.size(); ++id)
		{
			if (m_adapters[id] && !seenViews.count((ObjectID)id))
			{
				m_adapters[id].reset();
			}
		}
		for (size_t i = 0; i < views.size(); ++i)
		{
			adapterFor(*viewObjects[i]).setLayer(views[i].second);
		}
	}
	// every AI module's state, in that order
	for (auto &kv : m_ais)
	{
		const PathfindObjectID id = kv.first;
		x.beginBlock();
		try
		{
			kv.second->xferAIStateBody(x); // the block is one transaction already (AIWorld::xfer)
		}
		catch (const XferException &e)
		{
			throw XferException("the AI of object " + std::to_string(id) + " (" + kv.second->getObject()->getTemplate()->getName() + "): " + e.what());
		}
		x.endBlock();
	}
	AIXfer::map(x, m_failures, 5, [&](std::string &k, unsigned &n) {
		AIXfer::reportString(x, k);
		AIXfer::uns(x, n);
	});
	std::vector<std::string> noted(m_noted.begin(), m_noted.end());
	AIXfer::reportStrings(x, noted);
	std::int32_t nextFormation = m_nextFormationId; // version 2: last, so the earlier layout keeps its offsets
	if (v.current >= 2)
	{
		x.xferInt(&nextFormation);
		if (loading && nextFormation < 1)
		{
			throw XferException("ai world: the next formation id " + std::to_string(nextFormation) + " (ids start at 1)");
		}
		if (loading)
		{
			// review of #74 (r2): every AI's formation id (loaded above) is 0 or one issued below the next id
			for (auto &kv : m_ais)
			{
				if (kv.second->formationId() >= nextFormation)
				{
					throw XferException("ai world: the AI of object " + std::to_string(kv.first) + " has formation id " + std::to_string(kv.second->formationId()) +
										", not one issued below the next formation id " + std::to_string(nextFormation));
				}
			}
		}
	}
	else if (loading)
	{
		nextFormation = 1;
	}
	AIXfer::blockReadToEnd(x, "ai world"); // inside the transaction: a block with bytes after its state is refused before anything stays changed
	if (loading)
	{
		m_noted = decltype(m_noted)(noted.begin(), noted.end());
		m_nextFormationId = nextFormation; // lane FLANK-1 (version 2)
	}
}
