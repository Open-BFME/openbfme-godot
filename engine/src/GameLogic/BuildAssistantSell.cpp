// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/BuildAssistantSell.h (lane FACT-A).

#include "GameLogic/BuildAssistantSell.h"
#include "GameLogic/SnapshotXfer.h"

#include "Common/BuildAssistant.h"
#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <stdexcept>
#include <variant>

namespace
{
const char *const kLane = "FACT-A";

int statusBit(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("BuildAssistantSell: no object status ") + name);
	}
	return bit;
}

int kindOfBit(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("BuildAssistantSell: no KindOf ") + name);
	}
	return bit;
}

// RW 0x793D57: the object's command set (by name, from the control bar's store) offers a SELL button in one of its 33 slots
bool commandSetOffersSell(const Object &obj)
{
	if (!TheCommandStore)
	{
		return false; // RW 0x793D57: no TheControlBar answers false
	}
	const CommandSet *set = TheCommandStore->findCommandSet(obj.getCommandSetName());
	if (!set)
	{
		return false;
	}
	for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		if (b && b->m_command == GUI_COMMAND_SELL)
		{
			return true;
		}
	}
	return false;
}

// RW 0x8C1D75 (ActiveBody vslot 5, the body interface at object + 0x25C): health / max health under PC24 when the max is above 0.0, else 0.0
float healthFraction(const BodyModuleInterface &body)
{
	const float maxHealth = body.getMaxHealth();
	if (!(maxHealth > 0.0f)) // comiss xmm0, 0.0; jbe
	{
		return 0.0f;
	}
	return NumericState::fstpDword(NumericState::pc24DivW((double)body.getHealth(), (double)maxHealth)); // fld; fdiv; returned in st0, stored as a float
}
} // namespace

void BuildAssistantSell::registerHandlers(GameLogicDispatch &dispatch)
{
	dispatch.registerHandler(MSG_SELL, kLane, [this](GameLogic &, const GameMessage &m) { return onSellMessage(m); });
}

// RW 0x77B2C8 .. 0x77B3E9
bool BuildAssistantSell::onSellMessage(const GameMessage &m)
{
	Player *player = m_logic.players().getNthPlayer(m.getPlayerIndex());
	if (!player)
	{
		return false;
	}
	++m_stats.messages;
	if (m_logic.economy().context().livingWorld)
	{
		return true; // RW 0x77B2C8 .. 0x77B2D6: no selling in the Living World
	}
	const PlayerTemplate *pt = player->getPlayerTemplate();
	const ThingTemplate *beacon = pt && !pt->m_beaconName.empty() ? m_logic.things().findTemplate(pt->m_beaconName) : nullptr; // RW 0x77B2DC .. 0x77B2F3
	for (ObjectID id : AICommands::selection(m_logic, m.getPlayerIndex()))
	{
		Object *obj = m_logic.findObjectByID(id);
		if (!obj)
		{
			continue;
		}
		if (beacon && BuildAssistant::isEquivalentTo(obj->getTemplate()->getFinalOverride(), beacon->getFinalOverride())) // RW 0x77B32F
		{
			// lane CHAT-1: RW 0x77B33C .. 0x77B3D5: the sender's own beacon is destroyed (GameLogic::destroyObject RW 0x62BBAB; the control bar's dirty flag
			// is client state). Another player's beacon only hides it on the sender's screen in retail (unreachable: the group holds the sender's objects)
			++m_stats.beacons;
			if (obj->getControllingPlayer() == player) // RW 0x68B678
			{
				m_logic.destroyObject(obj);
			}
			continue;
		}
		sellObject(obj); // RW 0x77B3E0: TheBuildAssistant vslot 0x6C
	}
	return true;
}

// RW 0x7966BB
void BuildAssistantSell::sellObject(Object *obj)
{
	static const int structure = kindOfBit("STRUCTURE"), notSellable = kindOfBit("NOT_SELLABLE");
	static const int underConstruction = statusBit("UNDER_CONSTRUCTION"), sold = statusBit("SOLD"), unselectable = statusBit("UNSELECTABLE");
	if (!obj || !obj->isKindOf((unsigned)structure) || obj->isKindOf((unsigned)notSellable) || !commandSetOffersSell(*obj) // RW 0x7966C6 .. 0x7966F6
		|| obj->testStatus((unsigned)underConstruction))                                                                 // RW 0x7966FC .. 0x796707
	{
		++m_stats.refused;
		return;
	}
	for (const Entry &e : m_list)
	{
		if (e.id == obj->getID())
		{
			return; // RW 0x796719 .. 0x79672E
		}
	}
	obj->setConstructionPercent(99.9f); // RW 0x796734: the float RW 0xC30850 to object + 0x288
	m_list.push_front(Entry{ obj->getID(), m_logic.getFrame() }); // RW 0x79675B .. 0x79677D (the frame: TheGameLogic + 0x40)
	obj->setStatus((unsigned)sold, true);         // RW 0x796782 .. 0x796794
	obj->setStatus((unsigned)unselectable, true);
	// RW 0x7967A7: GameLogic::deselectObject(obj, 0xFFFFF, true): every player's selection loses it (BFME2 decomp GameLogicDeselectObject.cpp, tier A); the
	// client's deselectDrawable follows the UNSELECTABLE status
	for (int i = 0; i < m_logic.players().getPlayerCount(); ++i)
	{
		if (Player *p = m_logic.players().getNthPlayer(i))
		{
			std::vector<ObjectID> &sel = p->selection();
			sel.erase(std::remove(sel.begin(), sel.end(), obj->getID()), sel.end());
		}
	}
	if (ProductionUpdateInterface *pu = obj->getProductionUpdate())
	{
		pu->cancelAllProduction(); // RW 0x7967D6 .. 0x7967E3 (production vslot 0x3C)
	}
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		ai->aiIdle(CMD_FROM_AI); // RW 0x7967F5
	}
	if (ContainModuleInterface *c = obj->getContain())
	{
		c->orderAllPassengersToExit((int)CMD_FROM_AI); // RW 0x796806: contain vslot 0x64 (S-1980)
		m_logic.noteStop(acceptanceStops()[0]);
	}
	++m_stats.sold;
}

// RW 0x79468B
void BuildAssistantSell::update()
{
	static const int structure = kindOfBit("STRUCTURE");
	for (auto it = m_list.begin(); it != m_list.end();)
	{
		Object *obj = m_logic.findObjectByID(it->id);
		if (!obj)
		{
			it = m_list.erase(it); // RW 0x7946D6 .. 0x7946F0
			continue;
		}
		Player *player = obj->getControllingPlayer();
		if (player)
		{
			// RW 0x794707 .. 0x794736: RefundValue (u16, template + 0x5EC) or ftol(paid * SellPercentage) at PC24
			std::uint32_t refund = 0;
			const FieldValue *rv = obj->getTemplate()->getFinalOverride()->findField("RefundValue");
			const long long *rvi = rv ? std::get_if<long long>(rv) : nullptr;
			if (rvi && (*rvi & 0xFFFF) != 0)
			{
				refund = (std::uint32_t)(*rvi & 0xFFFF);
			}
			else
			{
				if (!m_logic.settings().sellPercentageLoaded)
				{
					m_logic.noteStop("[S-1981] sell: GameData SellPercentage (GlobalData + 0xB94) was not loaded: a structure without RefundValue is sold for nothing");
				}
				refund = NumericState::ftol2Low32(NumericState::pc24MulW((double)obj->getBuildCostPaid(), (double)m_logic.settings().sellPercentage));
			}
			// RW 0x794738 .. 0x79476A: times the body's health fraction (1.0 without a body: RW 0xBD1908), fild with the unsigned fix-up, fmul, ftol
			float fraction = 1.0f;
			if (const BodyModuleInterface *body = obj->getBodyModule())
			{
				fraction = healthFraction(*body);
			}
			const std::uint32_t value = NumericState::ftol2Low32(NumericState::pc24MulW(NumericState::fildU32(refund), (double)fraction));
			player->getMoney()->deposit(value, nullptr, true); // RW 0x79477E: Money::deposit(value, no score keeper, sound)
			m_stats.paid += value;
			// RW 0x794786 .. 0x794808: the "GUI:AddCash" text floats over the structure (client)
		}
		if (ProductionUpdateInterface *pu = obj->getProductionUpdate())
		{
			pu->cancelAllProduction(); // RW 0x79480E .. 0x79481B
		}
		// RW 0x79481E .. 0x79487A: a damaged structure credits its last attacker of the past 4 seconds with the kill, otherwise its owner counts it lost
		bool scored = false;
		const BodyModuleInterface *body = obj->getBodyModule();
		if (obj->isKindOf((unsigned)structure) && body && (int)body->getDamageState() > (int)BODY_DAMAGED)
		{
			ObjectID killerID = INVALID_ID;
			if (emotionAttackedWithin(*obj, 4, killerID))
			{
				if (Object *killer = m_logic.findObjectByID(killerID))
				{
					killer->scoreTheKill(*obj, 1); // RW 0x794865
					scored = true;
				}
			}
		}
		if (!scored && player)
		{
			player->getScoreKeeper().addObjectLost(m_logic, *obj); // RW 0x79487A
		}
		obj->kill(DEATH_LASERED); // RW 0x794885: Object::kill(8 = UNRESISTABLE, 8 = the death type named LASERED in RW 0xDA1630)
		it = m_list.erase(it);
	}
}

void BuildAssistantSell::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_list.size());
	for (const Entry &e : m_list)
	{
		h.addU32((std::uint32_t)e.id);
		h.addU32(e.frame);
	}
}

std::vector<std::string> BuildAssistantSell::acceptanceStops()
{
	return {
		"[S-1980] sell (FACT-A, RW 0x77B2C8 / 0x7966BB / 0x79468B): MSG_SELL sells the group's structures, the refund (RefundValue or paid * SellPercentage, times the health "
		"fraction) is paid in the next phase 5 and the structure dies (UNRESISTABLE, death type 8), a selected own beacon is destroyed (lane CHAT-1); not ported: the local "
		"player's BeaconName (the issuing player's is read: every retail faction names MultiplayerBeacon), the contain's onSelling (contain vslot 0x64: the riders are "
		"ordered out with CMD_FROM_AI as BFME2's OpenContain does; GarrisonContain's removeAllContained first is not ported), the client's animation loop and cash text",
		"[S-1981] sell: GameData SellPercentage (row RW 0xC00690, parsePercentToReal RW 0x42EEFA, GlobalData + 0xB94) must be loaded; without it a structure that has no "
		"RefundValue is sold for nothing and the stop is noted",
	};
}

// ---- lane SAVE-4b: obfme:SellList (OpenBFME native, S-4190; retail keeps the list in TheBuildAssistant + 0xC, which no retail block names) -------------
// The entries newest first, as held; the statistics are the report's, not state.
void BuildAssistantSell::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	SnapshotXfer::each(x, m_list, 8, [&](Entry &e) {
		x.xferObjectID(&e.id);
		x.xferUnsignedInt(&e.frame);
	});
}
