// OpenBFME. GPL-3.0.
// See GameClient/MouseoverTooltip.h for the target facts, the donor and what is inference (lane HOVER-2). Client code: the logic is read, never changed.

#include "GameClient/MouseoverTooltip.h"

#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/System/ShroudManager.h"

namespace
{
bool kindOf(const KindOfMaskType &mask, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return bit >= 0 && MaskTest(mask, (unsigned)bit);
}

// a player's display name is kept as UTF-8 (Player::getPlayerDisplayName); decoded as InGameHud's beacon message does
UnicodeString utf16(const std::string &utf8)
{
	UnicodeString out;
	for (std::size_t i = 0; i < utf8.size();)
	{
		const unsigned char c = (unsigned char)utf8[i];
		const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
		char32_t cp = extra == 3 ? (c & 0x07) : extra == 2 ? (c & 0x0F) : extra == 1 ? (c & 0x1F) : c;
		for (int k = 1; k <= extra && i + (std::size_t)k < utf8.size(); ++k)
		{
			cp = (cp << 6) | ((unsigned char)utf8[i + (std::size_t)k] & 0x3F);
		}
		i += (std::size_t)extra + 1;
		if (cp > 0xFFFF)
		{
			cp -= 0x10000;
			out += (char16_t)(0xD800 + (cp >> 10));
			out += (char16_t)(0xDC00 + (cp & 0x3FF));
		}
		else
		{
			out += (char16_t)cp;
		}
	}
	return out;
}

// the local player's cell status at (x, y) is CLEAR (RW 0xB4FB20 over the local player's index)
bool clearAt(GameLogic &logic, const Player &local, float x, float y)
{
	const ShroudManager *shroud = logic.shroud();
	return shroud && shroud->getStatusAt(local.getPlayerIndex(), x, y) == CELLSHROUD_CLEAR;
}
} // namespace

MouseoverTooltip::Result MouseoverTooltip::compose(const Object &obj, GameLogic &logic, const GameTextSource *text, const Inputs &in)
{
	Result r;
	const ThingTemplate *tt = obj.getTemplate() ? obj.getTemplate()->getFinalOverride() : nullptr;
	if (!tt)
	{
		return r;
	}
	const KindOfMaskType &kind = logic.templateInfo(tt).kindOf;
	// the player: the contain's apparent controlling player for the local player (decomp line 452 .. 453), else the object's controlling player
	if (obj.getContain())
	{
		r.unported.push_back("the contain's apparent controlling player (decomp InGameUICreateMouseoverHint.cpp:453): the controlling player is used (S-7187)");
	}
	const Player *player = obj.getControllingPlayer();
	// a DISGUISER's SpecialDisguiseUpdate (RW 0x69F7B9) and the neutral-relationship component (decomp rva0028F4BC) replace the template: not ported
	if (kindOf(kind, "DISGUISER"))
	{
		r.unported.push_back("a disguised object's template (SpecialDisguiseUpdate, RW 0x69F7B9, S-7187)");
	}
	// the name: the template's DisplayName text; the drawable's script caption and the object's own name override it (decomp lines 477 / 479: not
	// ported, as in the unit help, S-4612)
	UnicodeString name = CommandButtonHelp::templateText(*tt, "DisplayName", text);
	const UnicodeString displayName = name;
	if (name.empty())
	{
		name = fetchOrMissing(text, "ThingTemplate:" + tt->getName()); // RW 0x69F95C
	}
	// a SupplyWarehouseDockUpdate appends its boxes' value (RW 0x69F9C0 / 0x69FA09): not ported (RotWK's skirmish data has none)
	const bool warehouse = obj.findModule("SupplyWarehouseDockUpdate") != nullptr;
	if (warehouse)
	{
		r.unported.push_back("a supply warehouse's value (TOOLTIP:SupplyWarehouse, RW 0x69FA09, S-7187)");
	}
	if (!player || !in.localPlayer)
	{
		return r;
	}
	UnicodeString tooltip = name;
	const PlayerTemplate *pt = player->getPlayerTemplate();
	if (in.networkGame && pt && pt->m_playableSide)
	{
		tooltip += u'\n';
		tooltip += utf16(player->getPlayerDisplayName()); // "%s\n%s" (decomp line 498)
	}
	// the local player's shroud at the object, or for a TREE at the ground under the pointer (decomp lines 503 .. 504)
	const Coord3D *pos = obj.getPosition();
	bool visible = clearAt(logic, *in.localPlayer, pos->x, pos->y);
	if (!visible && kindOf(kind, "TREE") && in.groundUnderPointer)
	{
		Coord3D ground{};
		visible = in.groundUnderPointer(ground) && clearAt(logic, *in.localPlayer, ground.x, ground.y);
	}
	if (!visible)
	{
		return r;
	}
	// never for a prop (RW 0x69FB7C: the name's text equals "OBJECT:Prop"'s)
	if (displayName == fetchOrMissing(text, "OBJECT:Prop"))
	{
		return r;
	}
	// the local player's structures and units, and any LINKED_TO_FLAG object, go to the control bar (decomp lines 520 .. 529, RW 0x69FC31's DebugAI test)
	const bool own = player == in.localPlayer &&
	                 (kindOf(kind, "CAVALRY") || kindOf(kind, "INFANTRY") || kindOf(kind, "MACHINE") || kindOf(kind, "MONSTER") || kindOf(kind, "STRUCTURE"));
	if (own || kindOf(kind, "LINKED_TO_FLAG"))
	{
		r.controlBarRecord = true;
		if (!in.haveDebugAI)
		{
			r.unported.push_back("GameData has no DebugAI row (GlobalData + 0x9B8): read as No");
		}
		if (!in.debugAI && !warehouse)
		{
			return r;
		}
	}
	r.text = tooltip;
	return r;
}

std::vector<std::string> MouseoverTooltip::acceptanceStops()
{
	return { "[S-7187] mouse-over tool tip (HOVER-2): ported RW 0x69F637's tool tip (BFME2 decomp InGameUICreateMouseoverHint.cpp:443 .. 535): the "
	         "template's DisplayName or ThingTemplate:<name>, the player's name in a network game, the local player's shroud at the object (a TREE: at the "
	         "ground under the pointer), never a prop, the local player's units and structures and LINKED_TO_FLAG objects only with GameData's DebugAI; "
	         "not ported: the control bar's TooltipRecord (ControlBar rva00405C04), a contain's apparent controlling player, a disguise's template, the "
	         "drawable's script caption and the object's own name, a supply warehouse's value; the colour is not kept (RotWK's UseTooltipAltTextColor = No); "
	         "INFERENCE: the request is cleared every frame the pointer is over the HUD, scrolls, selects or rests on no object (ZH clears it on every mouse "
	         "event), TheRecorder's multiplayer test is the network game flag, the playable-side test the template's PlayableSide" };
}
