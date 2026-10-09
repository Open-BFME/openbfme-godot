// OpenBFME unit tests. GPL-3.0.
// Fixture for the economy tests (lane ECON-1): the live object world of LogicTestUtil with the economy modules registered, GameData's economy values
// (EconomySettings from a gamedata.ini excerpt of the retail values), and a synthetic terrain for the TerrainResourceManager.

#pragma once

#include "EconGameData.h"
#include "LogicTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/System/TerrainResourceManager.h"

#include <set>
#include <string>
#include <vector>

namespace econtest
{
// a flat world of `cellsWide` x `cellsHigh` cells of 20 units, optionally with blocked (water) cells
class FlatTerrain : public TerrainResourceTerrain
{
public:
	FlatTerrain(float width, float height)
		: m_w(width)
		, m_h(height)
	{
	}
	void getExtent(float &loX, float &loY, float &hiX, float &hiY) const override
	{
		loX = loY = 0.0f;
		hiX = m_w;
		hiY = m_h;
	}
	bool cellTypeAt(float x, float y, int &type) const override
	{
		type = 0;
		for (const Block &b : m_blocks)
		{
			if (x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1)
			{
				type = b.type;
			}
		}
		return x >= 0 && y >= 0 && x < m_w && y < m_h;
	}
	void block(float x0, float y0, float x1, float y1, int type = 1) { m_blocks.push_back({ x0, y0, x1, y1, type }); }

private:
	struct Block
	{
		float x0, y0, x1, y1;
		int type;
	};
	float m_w, m_h;
	std::vector<Block> m_blocks;
};

struct Fx : logictest::LogicWorld
{
	explicit Fx(const char *objects, const char *templates = nullptr)
		: LogicWorld(RandomAlgorithm::ZH_CarryChain, templates)
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		LogicModules::registerAll(w.modules);
		const std::string err = w.load(objects);
		REQUIRE_MESSAGE(err.empty(), err);
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
		std::string e;
		REQUIRE_MESSAGE(EconomySettings::scan(kGameData, logic->economy().settings(), &e), e);
		logic->economy().initAllCommandPoints();
	}
	Economy &economy() { return logic->economy(); }
	Player *player(const char *name) { return players.findPlayerWithName(name); }
	Object *makeAt(const char *templateName, const char *owner, float x, float y)
	{
		Object *o = make(templateName, teamOf(owner));
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		return o;
	}
	std::uint32_t hash() const { return logic->computeStateHash(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			logic->runLogicFrame();
		}
	}
};
} // namespace econtest
