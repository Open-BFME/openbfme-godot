// OpenBFME. GPL-3.0.
// See GameLogic/Module/AttachUpdate.h (lane CAMP-1).

#include "GameLogic/Module/AttachUpdate.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Audio/AudioRequests.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameClient/FXList.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "Common/PlayerTemplate.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kStop =
	"[S-1361] AttachUpdate (RW 0x8953BB / 0x89515C): the Eva event names are not validated at parse time (RW 0x5DE588, as S-190); a HORDE parent's anchor "
	"is its members' mean position without the horde's second id list (HordeContain + 0x54, not kept); the horde-member test RW 0x6939DF takes the contained object's container (its status 0x26 branch not ported); AlwaysTeleport's teleport "
	"form of setPosition (RW 0x696E63, the drawable's interpolation reset, S-813) moves the object like setPosition";

#define AU_OFF(field) (int)offsetof(AttachUpdateModuleData, field)

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<ObjectFilter *>(store) = std::move(f);
}

// RW 0x73A302: "None" -> none; any other name must be an FXList (TheFXListStore)
void parseFX(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		static_cast<std::string *>(store)->clear();
		return;
	}
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	TheFXListStore->parseFXListRef(name);
	*static_cast<std::string *>(store) = name;
}

// RW 0x5DE588: an Eva event name ("None" -> none); kept by name (S-1361)
void parseEva(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	*static_cast<std::string *>(store) = AsciiStringUtil::compareNoCase(name, "None") == 0 ? std::string() : name;
}

// RW 0xC643B8
const FieldParse kAttachParse[] = {
	{ "ObjectFilter", parseFilter, nullptr, AU_OFF(m_objectFilter) },
	{ "ScanRange", INI::parseReal, nullptr, AU_OFF(m_scanRange) },
	{ "ParentStatus", ParseObjectStatusMask, nullptr, AU_OFF(m_parentStatus) },
	{ "AlwaysTeleport", INI::parseBool, nullptr, AU_OFF(m_alwaysTeleport) },
	{ "AnchorToTopOfGeometry", INI::parseBool, nullptr, AU_OFF(m_anchorToTopOfGeometry) },
	{ "ParentOwnerAttachmentEvaEvent", parseEva, nullptr, AU_OFF(m_ownerAttachEva) },
	{ "ParentAllyAttachmentEvaEvent", parseEva, nullptr, AU_OFF(m_allyAttachEva) },
	{ "ParentEnemyAttachmentEvaEvent", parseEva, nullptr, AU_OFF(m_enemyAttachEva) },
	{ "AttachFX", parseFX, nullptr, AU_OFF(m_attachFX) },
	{ "ParentOwnerDiedEvaEvent", parseEva, nullptr, AU_OFF(m_ownerDiedEva) },
	{ "ParentAllyDiedEvaEvent", parseEva, nullptr, AU_OFF(m_allyDiedEva) },
	{ "ParentEnemyDiedEvaEvent", parseEva, nullptr, AU_OFF(m_enemyDiedEva) },
	{ nullptr, nullptr, nullptr, 0 }
};

bool maskEmpty(const ObjectStatusMaskType &m)
{
	for (std::uint32_t w : m)
	{
		if (w)
		{
			return false;
		}
	}
	return true;
}

int kindBit(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

const unsigned kStatusAttached = 0x62; // RW 0x8951E5 (ATTACHED)
} // namespace

void AttachUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kAttachParse);
}

AttachUpdate::AttachUpdate(Thing *thing, const AttachUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE); // RW 0x8950DE: RW 0x850C32(1)
}

Coord3D AttachUpdate::anchorOf(const Object &parent) const
{
	Coord3D p = *parent.getPosition();
	// RW 0x895310: a HORDE parent (template + 0x115 bit 5) with a contain (+ 0x258) asks its horde interface (RW 0x68C866) slot 0x230 (HordeContain RW
	// 0x8713F6, lane CAMP-1H): the members' mean position - the contained members' positions summed (x87, each sum stored as float), then each component
	// times 1 / count; no member: false and the horde's own position stays. INFERENCE (S-1361): the slot also sums the members of the id list at
	// HordeContain + 0x54, which the port's horde does not keep (as HordeBanner.cpp / AreaScanModules.cpp)
	static const int kHorde = kindBit("HORDE");
	if (kHorde >= 0 && parent.isKindOf((unsigned)kHorde))
	{
		ContainModuleInterface *c = parent.getContain();
		if (c && c->getHordeContainInterface())
		{
			if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
			{
				Coord3D sum{ 0.0f, 0.0f, 0.0f };
				float count = 0.0f;
				for (const Object *m : *items)
				{
					if (!m)
					{
						continue;
					}
					sum.x = SimMath::pc24Add(sum.x, m->getPosition()->x);
					sum.y = SimMath::pc24Add(m->getPosition()->y, sum.y);
					sum.z = SimMath::pc24Add(m->getPosition()->z, sum.z);
					count = SimMath::pc24Add(count, 1.0f);
				}
				if (count != 0.0f)
				{
					const float inv = SimMath::pc24Div(1.0f, count);
					p.x = SimMath::pc24Mul(inv, sum.x);
					p.y = SimMath::pc24Mul(inv, sum.y);
					p.z = SimMath::pc24Mul(sum.z, inv);
				}
			}
		}
	}
	if (m_data->m_anchorToTopOfGeometry)
	{
		// RW 0xAD1920: the parent's geometry height (x87 sum, stored as float)
		p.z = SimMath::pc24Add(p.z, ObjectGeometry::maxHeightAbovePosition(ObjectGeometry::shapesOf(*parent.getTemplate())));
	}
	return p;
}

void AttachUpdate::tryAttach()
{
	if (m_parentID != 0)
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer();
	PartitionFilterFn filter([&](Object &o) {
		// RW 0xBE4CC8 (the ObjectFilter for the owner), RW 0xC10E20 (not effectively dead), RW 0xC1D660 (not the object itself)
		return &o != obj && !o.isEffectivelyDead() && ObjectFilterMatch::allows(logic, m_data->m_objectFilter, o, owner);
	});
	Object *parent = logic.partition().getClosestObject(*obj->getPosition(), m_data->m_scanRange, FROM_CENTER_3D, { &filter });
	if (!parent)
	{
		return;
	}
	static const int kHorde = kindBit("HORDE"), kCreep = kindBit("CREEP");
	// RW 0x6939DF / + 0x27C: a horde member is carried by its horde
	if (Object *container = parent->getContainedBy())
	{
		if (kHorde >= 0 && container->isKindOf((unsigned)kHorde))
		{
			parent = container;
		}
	}
	Player *parentPlayer = parent->getControllingPlayer();
	// lane CAMP-1H: RW 0x89523E .. 0x895251: a CREEP parent is taken only when its controlling player's template (Player + 0x34) is a PlayableSide (+ 0x151,
	// RW 0x6AAC66; a player without a template answers false). Retail calls it on the controlling player unchecked; a parent without one is refused here
	if (kCreep >= 0 && parent->isKindOf((unsigned)kCreep))
	{
		if (!parentPlayer || !parentPlayer->getPlayerTemplate() || !parentPlayer->getPlayerTemplate()->m_playableSide)
		{
			return;
		}
	}
	m_parentID = parent->getID();
	if (!maskEmpty(m_data->m_parentStatus)) // RW 0x776243
	{
		for (unsigned bit = 0; bit < 128; ++bit)
		{
			if (m_data->m_parentStatus[bit / 32] & (1u << (bit % 32)))
			{
				parent->setStatus(bit, true); // RW 0x68D440(mask, 1)
			}
		}
	}
	obj->setStatus(kStatusAttached, true); // RW 0x62684D(0x62, 1)
	if (!m_data->m_attachFX.empty()) // RW 0x4B1B5A(AttachFX, object, parent)
	{
		logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "AttachUpdate", logic.getFrame(), m_data->m_attachFX, *obj));
	}
	const Player *local = logic.players().getLocalPlayer();
	if (local && parentPlayer)
	{
		// RW 0x895291 .. 0x8952FE: the owner, ally (relationship 2, RW 0x6ACEAF) or enemy attachment event to TheEva now (RW 0x5DD9EE: at the parent's
		// position, + 0x38) and its died event kept for later (+ 0x24). lane CAMP-1H: reported through AudioApi::reportEva (queued for the audio owner from
		// the logic worker, S-814); the choice depends on the local player, so it is presentation, not hashed
		const std::string *attach = nullptr;
		if (parentPlayer == local)
		{
			attach = &m_data->m_ownerAttachEva;
			m_diedEva = m_data->m_ownerDiedEva;
		}
		else if (local->getRelationship(parentPlayer) == ALLIES)
		{
			attach = &m_data->m_allyAttachEva;
			m_diedEva = m_data->m_allyDiedEva;
		}
		else
		{
			attach = &m_data->m_enemyAttachEva;
			m_diedEva = m_data->m_enemyDiedEva;
		}
		if (!attach->empty())
		{
			AudioApi::reportEva(*attach, parent->getPosition());
		}
	}
}

UpdateSleepTime AttachUpdate::update()
{
	tryAttach();
	if (m_parentID == 0)
	{
		return UPDATE_SLEEP_NONE;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Object *parent = logic.findObjectByID(m_parentID);
	if (!parent || parent->isEffectivelyDead())
	{
		// RW 0x895435 ..: detach, the died Eva event at the object's own position (RW 0x5DD9EE; lane CAMP-1H: AudioApi::reportEva), the object kills itself
		// (RW 0x698EC3(8, 0))
		m_parentID = 0;
		if (!m_diedEva.empty())
		{
			AudioApi::reportEva(m_diedEva, obj->getPosition());
		}
		// RW 0x698EC3(8, 0) (lane CAMP-1H r2, Sol): the body's attemptDamage with source 0 (not the object itself: that would set its player's attacked-by-self
		// state), DAMAGE_UNRESISTABLE, DEATH_NORMAL, the maximum health as the amount and the kill flag
		if (BodyModuleInterface *body = obj->getBodyModule())
		{
			if (!obj->isEffectivelyDead())
			{
				DamageInfo damage;
				damage.m_input.m_sourceID = 0;
				damage.m_input.m_damageType = DAMAGE_UNRESISTABLE;
				damage.m_input.m_deathType = DEATH_NORMAL;
				damage.m_input.m_amount = body->getMaxHealth();
				damage.m_input.m_kill = true;
				body->attemptDamage(damage);
			}
		}
		return UPDATE_SLEEP_FOREVER;
	}
	const Coord3D at = anchorOf(*parent);
	const Coord3D *pos = obj->getPosition();
	if (!(at.x == pos->x && at.y == pos->y && at.z == pos->z))
	{
		obj->setPosition(&at); // RW 0x70C201 (AlwaysTeleport: RW 0x696E63, S-1361)
	}
	return UPDATE_SLEEP_NONE;
}

void AttachUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_parentID);
	// lane CAMP-1H: m_diedEva is not hashed: it is chosen by the local player's relationship (RW 0x8952A7) and only reaches TheEva (presentation)
}

void AttachUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<AttachUpdateModuleData>("AttachUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("AttachUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const AttachUpdateModuleData *typed = dynamic_cast<const AttachUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("AttachUpdate: the module data is not typed");
		}
		return std::make_unique<AttachUpdate>(thing, typed);
	});
}

std::vector<std::string> AttachUpdate::stopLines()
{
	return { kStop };
}
