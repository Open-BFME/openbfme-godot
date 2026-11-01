// OpenBFME. GPL-3.0.
// See GameLogic/Module/FireWeaponWhenDamagedBehavior.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/FireWeaponWhenDamagedBehavior.h"

#include "Common/AsciiString.h"
#include "Common/ModEngineFeatures.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <cstddef>
#include <stdexcept>

namespace
{
typedef FireWeaponWhenDamagedBehaviorModuleData Data;

// RW 0x73AE79: a weapon template name; "None" (stricmp) stores none (the template is looked up by the module's constructor)
void parseWeaponName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	*static_cast<std::string *>(store) = AsciiStringUtil::compareNoCase(name, "None") == 0 ? std::string() : name;
}

const INITypeFlagListSpec kDamageTypeFlags = { TheDamageNames, 1 }; // RW 0x73A5C9: 1 << (type - 1)

#define FWWD_ROWS                                                                                                                                        \
	{ "StartsActive", INI::parseBool, nullptr, (int)offsetof(Data, m_startsActive) },                                                                     \
	{ "ReactionWeaponPristine", parseWeaponName, nullptr, (int)offsetof(Data, m_reactionWeapon[Data::PRISTINE]) },                                       \
	{ "ReactionWeaponDamaged", parseWeaponName, nullptr, (int)offsetof(Data, m_reactionWeapon[Data::DAMAGED]) },                                         \
	{ "ReactionWeaponReallyDamaged", parseWeaponName, nullptr, (int)offsetof(Data, m_reactionWeapon[Data::REALLY_DAMAGED]) },                            \
	{ "ReactionWeaponRubble", parseWeaponName, nullptr, (int)offsetof(Data, m_reactionWeapon[Data::RUBBLE]) },                                           \
	{ "ContinuousWeaponPristine", parseWeaponName, nullptr, (int)offsetof(Data, m_continuousWeapon[Data::PRISTINE]) },                                   \
	{ "ContinuousWeaponDamaged", parseWeaponName, nullptr, (int)offsetof(Data, m_continuousWeapon[Data::DAMAGED]) },                                     \
	{ "ContinuousWeaponReallyDamaged", parseWeaponName, nullptr, (int)offsetof(Data, m_continuousWeapon[Data::REALLY_DAMAGED]) },                        \
	{ "ContinuousWeaponRubble", parseWeaponName, nullptr, (int)offsetof(Data, m_continuousWeapon[Data::RUBBLE]) },                                       \
	{ "DamageTypes", INI::parseTypeFlagList, &kDamageTypeFlags, (int)offsetof(Data, m_damageTypes) },                                                    \
	{ "DamageAmount", INI::parseReal, nullptr, (int)offsetof(Data, m_damageAmount) }

// RW 0xC06698
const FieldParse kFields[] = { FWWD_ROWS, { nullptr, nullptr, nullptr, 0 } };
// Edain Unchained's 0xEE6010: RW 0xC06698 plus FireAtAttacker (+0x164, parseBool RW 0x42E558)
const FieldParse kFieldsFireAtAttacker[] = {
	FWWD_ROWS,
	{ "FireAtAttacker", INI::parseBool, nullptr, (int)offsetof(Data, m_fireAtAttacker) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef FWWD_ROWS
} // namespace

// RW 0x653444 (Edain Unchained: its 0x65344B pushes 0xEE6010)
void FireWeaponWhenDamagedBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(ModEngineFeatures::has(ModEngineFeature::EdainUnchainedFireAtAttacker) ? kFieldsFireAtAttacker : kFields);
	buildBaseFieldParse(p); // RW 0x8D26E0: RW 0xC76AD8 at offset 8
}

void FireWeaponWhenDamagedBehavior::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "FireWeaponWhenDamagedBehavior";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<FireWeaponWhenDamagedBehavior>(thing, typed);
	});
}

// RW 0x885A6F
FireWeaponWhenDamagedBehavior::FireWeaponWhenDamagedBehavior(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, UpgradeMux(thing ? thing->asObject() : nullptr, data)
	, m_data(data)
{
	Object *obj = getObject();
	const std::uint32_t now = obj->logic().getFrame();
	auto make = [&](const std::string &name, std::unique_ptr<Weapon> &out) {
		const WeaponTemplate *tmpl = (!name.empty() && TheWeaponStore) ? TheWeaponStore->findWeaponTemplate(name) : nullptr; // RW 0x73AE79 (NULL when unknown)
		if (!tmpl)
		{
			return;
		}
		out = std::make_unique<Weapon>(tmpl, PRIMARY_WEAPON, now); // RW 0x68B150(template, 0)
		weaponHost().reloadExtraWeapon(*out);                      // RW 0x6CEE4C(object)
		out->setOwnerID(obj->getID());                             // weapon + 8 = object + 0x74
	};
	for (int i = 0; i < Data::STATE_COUNT; ++i) // + 0x144 .. + 0x150
	{
		make(data->m_reactionWeapon[i], m_reaction[i]);
	}
	for (int i = 0; i < Data::STATE_COUNT; ++i) // + 0x154 .. + 0x160
	{
		make(data->m_continuousWeapon[i], m_continuous[i]);
	}
	if (data->m_startsActive)
	{
		giveSelfUpgrade(); // RW 0x855388
	}
	bool continuous = false;
	for (int i = 0; i < Data::STATE_COUNT; ++i)
	{
		continuous = continuous || m_continuous[i] != nullptr; // RW tests the data's template pointers (+ 0x154 .. + 0x160): a weapon exists for each
	}
	setWakeFrame(obj, isAlreadyUpgraded() && continuous ? UPDATE_SLEEP(1) : UPDATE_SLEEP_FOREVER); // RW 0x885C65 .. 0x885CA1
}

FireWeaponWhenDamagedBehavior::~FireWeaponWhenDamagedBehavior() = default;

ObjectWeapons &FireWeaponWhenDamagedBehavior::weaponHost()
{
	if (ObjectWeapons *own = getObject()->getWeapons())
	{
		return *own;
	}
	if (!m_ownHost)
	{
		m_ownHost = std::make_unique<ObjectWeapons>(*getObject()); // S-924 (inference, as FireWeaponUpdate)
	}
	return *m_ownHost;
}

// RW 0x8554D6: 0x850C32(obj, 1)
void FireWeaponWhenDamagedBehavior::upgradeImplementation()
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1));
}

int FireWeaponWhenDamagedBehavior::stateIndex() const
{
	const BodyModuleInterface *body = getObject()->getBodyModule(); // Object + 0x25C (RW dereferences it unchecked)
	if (!body)
	{
		throw std::logic_error("FireWeaponWhenDamagedBehavior of " + getObject()->getTemplate()->getName() + ": the object has no body (RW 0x885D15 reads it)");
	}
	switch (body->getDamageState()) // vslot 0x24
	{
	case BODY_RUBBLE:
		return Data::RUBBLE;
	case BODY_REALLYDAMAGED:
		return Data::REALLY_DAMAGED;
	case BODY_DAMAGED:
		return Data::DAMAGED;
	default:
		return Data::PRISTINE;
	}
}

// RW 0x885CD5 (Edain Unchained: the fire at RW 0x885D81 goes through 0xEE60F0)
void FireWeaponWhenDamagedBehavior::onDamage(const DamageInfo &info)
{
	if (!isAlreadyUpgraded()) // mux slot 0
	{
		return;
	}
	const unsigned bit = 1u << ((unsigned)(info.m_input.m_damageType - 1) & 31u); // DamageInfo + 0x10: dec ecx; shl edi, cl
	if ((m_data->m_damageTypes & bit) == 0)
	{
		return;
	}
	if (m_data->m_damageAmount > info.m_output.m_actualDamageDealt) // comiss data + 0x140, DamageInfo + 0x70; ja (a NaN goes on)
	{
		return;
	}
	Weapon *w = m_reaction[stateIndex()].get();
	if (!w || weaponHost().extraWeaponStatus(*w) != WEAPON_READY_TO_FIRE) // RW 0x6CDCE7
	{
		return;
	}
	Object *obj = getObject();
	if (m_data->m_fireAtAttacker) // parsed only with ModEngineFeature::EdainUnchainedFireAtAttacker
	{
		// Edain Unchained 0xEE60F0: the source (DamageInfo + 8) by TheGameLogic->findObjectByID (RW 0x449681), fired at by RW 0x6CF3AE
		if (Object *source = obj->logic().findObjectByID(info.m_input.m_sourceID))
		{
			weaponHost().fireExtraWeaponDetonationAt(*w, *source);
			++m_attackerShots;
			return;
		}
	}
	weaponHost().fireExtraWeaponAt(*w, *obj->getPosition()); // RW 0x6CF3D2(object, &object + 0x38)
	++m_reactionShots;
}

// RW 0x885D90
UpdateSleepTime FireWeaponWhenDamagedBehavior::update()
{
	if (!isAlreadyUpgraded())
	{
		return UPDATE_SLEEP_FOREVER; // 0x3FFFFFFF
	}
	Weapon *w = m_continuous[stateIndex()].get();
	if (w && weaponHost().extraWeaponStatus(*w) == WEAPON_READY_TO_FIRE) // RW 0x6CDCE7
	{
		weaponHost().fireExtraWeaponAt(*w, *getObject()->getPosition()); // RW 0x6CF3D2
		++m_continuousShots;
	}
	return UPDATE_SLEEP_NONE; // RW returns 1
}

void FireWeaponWhenDamagedBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	crcMux(hasher);
	for (const std::unique_ptr<Weapon> *set : { m_reaction, m_continuous })
	{
		for (int i = 0; i < Data::STATE_COUNT; ++i)
		{
			hasher.addBool(set[i] != nullptr);
			if (set[i])
			{
				set[i]->crc(hasher);
			}
		}
	}
	hasher.addBool(m_ownHost != nullptr);
	if (m_ownHost)
	{
		m_ownHost->crc(hasher);
	}
	hasher.addU64(m_reactionShots);
	hasher.addU64(m_attackerShots);
	hasher.addU64(m_continuousShots);
}

// RW 0x88588B's fields (OpenBFME native snapshot, S-4190): the weapons are made by the constructor from the data (the same list for the object a load
// makes); each present flag must match and each weapon loads its state. The weapon host of an object without a WeaponSet (S-924) is saved whole.
void FireWeaponWhenDamagedBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	xferMux(x);          // RW 0x8D27E8
	for (std::unique_ptr<Weapon> *set : { m_reaction, m_continuous })
	{
		for (int i = 0; i < Data::STATE_COUNT; ++i)
		{
			bool present = set[i] != nullptr;
			x.xferBool(&present);
			if (present != (set[i] != nullptr))
			{
				throw XferException(std::string("the snapshot's FireWeaponWhenDamagedBehavior of ") + getObject()->getTemplate()->getName() + (present ? " has" : " lacks") +
					" weapon " + std::to_string(set == m_reaction ? i : Data::STATE_COUNT + i) + ", this one " + (present ? "lacks" : "has") + " it");
			}
			if (present)
			{
				set[i]->xfer(x);
			}
		}
	}
	bool ownHost = !x.isLoading() && m_ownHost != nullptr;
	x.xferBool(&ownHost);
	if (x.isLoading() && ownHost != (m_ownHost != nullptr))
	{
		m_ownHost.reset();
		if (ownHost)
		{
			m_ownHost = std::make_unique<ObjectWeapons>(*getObject());
		}
	}
	if (m_ownHost)
	{
		m_ownHost->xfer(x);
	}
	SnapshotXfer::u64(x, m_reactionShots);
	SnapshotXfer::u64(x, m_attackerShots);
	SnapshotXfer::u64(x, m_continuousShots);
}
