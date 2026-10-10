// OpenBFME. GPL-3.0.
// Lane PLAY-1: the building placement ghost's look (PlacementGhost.h; RotWK RW 0x69C5E6, RW 0x4B4379, RW 0x4B4443).

#include "GameClient/PlacementGhost.h"

#include "Common/AsciiString.h"
#include "Common/ModelState.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

namespace PlacementGhost
{
Look lookOf(const ThingTemplate &tt)
{
	Look out;
	ModelConditionFlags flags;
	const int bit = ModelCondition::indexOf("BUILD_PLACEMENT_CURSOR");
	if (bit >= 0)
	{
		flags.set((size_t)bit, true);
	}
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->drawModules().nuggets())
	{
		const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(n.data.get());
		if (!data)
		{
			continue;
		}
		const ModelConditionInfo *info = data->findBestInfo(flags);
		std::string model;
		if (info)
		{
			for (const std::string &m : info->modelNames)
			{
				if (!m.empty() && AsciiStringUtil::compareNoCase(m, "None") != 0)
				{
					model = m;
					break;
				}
			}
		}
		if (model.empty())
		{
			continue;
		}
		out.model = model;
		if (const AnimationStateInfo *st = data->findBestAnimationState(flags))
		{
			out.state = st->stateName;
			for (const std::string &line : st->beginScriptLines)
			{
				const size_t open = line.find('('), q0 = line.find('"'), q1 = q0 == std::string::npos ? std::string::npos : line.find('"', q0 + 1);
				const std::string call = open == std::string::npos ? std::string() : line.substr(0, open);
				auto trimmed = [](std::string t) {
					const size_t b = t.find_first_not_of(" \t");
					const size_t e = t.find_last_not_of(" \t");
					return b == std::string::npos ? std::string() : t.substr(b, e - b + 1);
				};
				const std::string fn = trimmed(call);
				if (q1 != std::string::npos && AsciiStringUtil::compareNoCase(fn, "CurDrawableHideSubObject") == 0)
				{
					out.hidden.push_back(line.substr(q0 + 1, q1 - q0 - 1));
				}
				else if (q1 != std::string::npos && AsciiStringUtil::compareNoCase(fn, "CurDrawableShowSubObject") == 0)
				{
					out.shown.push_back(line.substr(q0 + 1, q1 - q0 - 1));
				}
				else if (!trimmed(line).empty())
				{
					out.unread.push_back(line);
				}
			}
		}
		break;
	}
	return out;
}

CastleLook castleLookOf(const ThingTemplate &tt, const std::string &side, CastleTemplateStore &store,
	const std::function<const ThingTemplate *(const std::string &)> &findTemplate)
{
	CastleLook out;
	const CastleBehaviorModuleData *castle = nullptr;
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		castle = dynamic_cast<const CastleBehaviorModuleData *>(n.data.get());
		if (castle)
		{
			break; // RW 0x73CD88: the first castle module
		}
	}
	if (!castle)
	{
		return out;
	}
	out.castle = true;
	for (const auto &pair : castle->m_castleToUnpackForFaction) // RW 0x798F70
	{
		if (AsciiStringUtil::compareNoCase(pair.first, side) == 0)
		{
			out.base = pair.second;
			break;
		}
	}
	if (out.base.empty())
	{
		out.error = "CastleToUnpackForFaction of " + tt.getName() + " names no base for the faction '" + side + "'";
		return out;
	}
	std::string error;
	const CastleTemplate *layout = store.find(out.base, &error);
	if (!layout)
	{
		out.error = error;
		return out;
	}
	for (const CastleTemplateEntry &e : layout->entries)
	{
		const ThingTemplate *piece = e.templateName.empty() ? nullptr : findTemplate(e.templateName);
		if (!piece)
		{
			continue; // RW 0x6A34E8: no template, no drawable
		}
		Piece p;
		p.templateName = piece->getName();
		p.look = lookOf(*piece);
		p.x = e.x;
		p.y = e.y;
		p.z = e.z;
		p.angle = e.angle;
		out.pieces.push_back(p);
	}
	return out;
}
} // namespace PlacementGhost
