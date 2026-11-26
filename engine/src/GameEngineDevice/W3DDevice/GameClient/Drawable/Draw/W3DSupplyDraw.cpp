// OpenBFME. GPL-3.0.
// W3DSupplyDraw::SupplyBonePrefix status update (RotWK RW 0x004CA784).
// TARGET RW 0x004B6588 getModelName, RW 0x004CA654 getModuleNameKey, RW 0x004CA699 GetSnapshotName,
// RW 0x004CA6B8 xfer, RW 0x004CA6F0 loadPostProcess, RW 0x004CA74C scalar deleting dtor,
// RW 0x004CA768 ModuleData scalar deleting dtor, RW 0x0066F121 module accessor.

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DSupplyDraw.h"

#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

W3DSupplyDraw::W3DSupplyDraw(const ThingTemplate &thingTemplate, const W3DSupplyDrawModuleData &data, W3DDrawAssets &assets)
	: m_template(thingTemplate)
	, m_data(data)
	, m_boneQuery(assets, {})
{
}

int W3DSupplyDraw::visibleBoneCount(int currentBoxes, int startingBoxes, int totalBones)
{
	if (totalBones <= 0 || currentBoxes <= 0 || startingBoxes <= 0)
	{
		return 0;
	}
	const double scaled = (static_cast<double>(currentBoxes) / static_cast<double>(startingBoxes)) * static_cast<double>(totalBones);
	if (scaled >= static_cast<double>(totalBones))
	{
		return totalBones;
	}
	if (scaled <= 0.0)
	{
		return 0;
	}
	return std::min(totalBones, static_cast<int>(std::ceil(scaled)));
}

void W3DSupplyDraw::findTotalBones(const ModelConditionFlags &flags, float scale)
{
	// RW 0x672A73 asks every model draw module and sums the results. Each module query at RW 0x4C3731 caps its
	// result at 64; reserve that many transforms per model-draw module in template order.
	size_t modelDrawModules = 0;
	for (const ThingTemplate::Nugget &n : m_template.drawModules().nuggets())
	{
		if (dynamic_cast<const W3DModelDrawModuleData *>(n.data.get()))
		{
			++modelDrawModules;
		}
	}
	if (modelDrawModules > static_cast<size_t>(std::numeric_limits<int>::max() / 64))
	{
		throw std::length_error("W3DSupplyDraw: too many model draw modules for the RW pristine-bone query");
	}
	const int maxBones = static_cast<int>(modelDrawModules * 64);
	if (maxBones == 0)
	{
		m_totalBones = 0;
		return;
	}
	std::vector<Matrix3D> transforms(static_cast<size_t>(maxBones));
	m_totalBones = m_boneQuery.getPristineBonePositions(m_template, flags, scale, m_data.m_supplyBonePrefix, 1, transforms.data(), maxBones);
}

bool W3DSupplyDraw::setSupplyStatus(W3DScriptedModelDraw &draw, const ModelConditionFlags &flags, float scale, int currentBoxes, int startingBoxes)
{
	m_currentBoxes = currentBoxes;
	m_startingBoxes = startingBoxes;
	m_hasSupplyStatus = true;
	if (m_totalBones < 0)
	{
		findTotalBones(flags, scale);
		// RW's first reader call seeds its old visible count from the full prefix count (RW 0x4CA7B7..0x4CA7DB).
		m_visibleBones = m_totalBones;
	}
	if (m_lastRenderObjectsCreated != draw.renderObjectsCreated())
	{
		// W3DScriptedModelDraw recreates its render object on a model change and clears plain hide/show records.
		// Rebase to the model's default (all supply parts shown); the next status call reapplies the current range.
		m_visibleBones = m_totalBones;
		m_lastRenderObjectsCreated = draw.renderObjectsCreated();
	}

	const int nextVisible = visibleBoneCount(currentBoxes, startingBoxes, m_totalBones);
	if (nextVisible == m_visibleBones)
	{
		return false;
	}

	const bool show = nextVisible >= m_visibleBones; // RW 0x4CA83D..0x4CA83F passes new >= old to the +0x88 vcall.
	const int first = std::min(m_visibleBones, nextVisible) + 1;
	const int last = std::max(m_visibleBones, nextVisible);
	bool changed = false;
	for (int index = first; index <= last; ++index)
	{
		char name[300];
		std::snprintf(name, sizeof(name), "%s%02d", m_data.m_supplyBonePrefix.c_str(), index); // RW 0xBDFC18 (%s%02d)
		const RenderObjPrototype *model = draw.currentModel();
		if (!model || W3DFindSubObjectByName(*model, name) < 0)
		{
			continue; // RW's apply of a hide/show record is a no-op when this model has no such sub-object.
		}
		if (show)
		{
			draw.showSubObject(name); // local equivalent of RW 0x4C3B25 with visible=true, permanent=false
		}
		else
		{
			draw.hideSubObject(name); // local equivalent of RW 0x4C3B25 with visible=false, permanent=false
		}
		changed = true;
	}
	m_visibleBones = nextVisible;
	return changed;
}

bool W3DSupplyDraw::refresh(W3DScriptedModelDraw &draw, const ModelConditionFlags &flags, float scale)
{
	return m_hasSupplyStatus ? setSupplyStatus(draw, flags, scale, m_currentBoxes, m_startingBoxes) : false;
}

void W3DSupplyDraw::xfer(Xfer &x)
{
	// RW 0x004CA6B8 (DONOR BFME2 0x000CAEDF): version(1, 1), xfers m_totalBones (+0x2E8) and m_lastNumberShown (+0x2EC)
	SnapshotXfer::version(x, 1, 1);
	x.xferInt(&m_totalBones);
	x.xferInt(&m_visibleBones);
}
