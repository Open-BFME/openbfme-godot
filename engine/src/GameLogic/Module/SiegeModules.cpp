// OpenBFME. GPL-3.0.
// See GameLogic/Module/SiegeModules.h for the target facts and what is inference (lane SIEGE-1).

#include "GameLogic/Module/SiegeModules.h"
#include "GameLogic/Module/DeployStyleAIUpdate.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace
{
const char *const kStopPortal =
	"[S-4940] siege deploy portal (lanes SIEGE-1 / WALL-2): SiegeDeploySpecialPower asks the ladder's / tower's DynamicPortalBehaviour through "
	"SiegePortalInterface (RW 0x863EB3 IsEndPortalAboveWall, the waypoints RW 0x86404D / 0x8639EF, RW 0x863D77 / 0x863D30 the top attack point); INFERENCE: "
	"RW 0x863D77's arithmetic order is lane SIEGE-1's reading; a DynamicPortalBehaviour that does not implement the interface (none in the port) would keep "
	"the deploy in phase 1";
const char *const kStopAdjust =
	"[S-4941] siege deploy (lane SIEGE-1): Pathfinder::adjustMeleeOffset (RW 0x6EA0EC) is not ported: a SiegeDeploySpecialPower with SkipAdjustPosition No (no "
	"retail object) keeps the unadjusted offset";
const char *const kStopSiege =
	"[S-4942] siege deploy (lane SIEGE-1): ported from RotWK: SiegeDockingBehavior (RW 0x85D216 / 0x85CE65 / 0x85CBC8 / 0x85CA8A / 0x85CD20 / 0x85CCB8), "
	"SiegeDeploySpecialPower (RW 0x8CA5C2 / 0x8CA30F / 0x8CA072 / 0x8C9D06 / 0x8CA2D0 over the base RW 0x991660), SiegeDeployHordeSpecialPower (RW 0x8CAA12 / "
	"0x8CA978), StopSpecialPower (RW 0x8CACC6 / 0x8CAC43), "
	"SiegeAIUpdate (RW 0x893C80) and the face state 0x25 (RW 0x7449D4 / 0x75638B, command 0x27 RW 0x667290). Not ported: the AI's + 0x16C / + 0x3B8 the face "
	"command clears, the InitiateSound and the selection voices (client), the dock points without a client (no bone provider: a wall without dock points)";

SiegePortalInterface *(*s_portalOverride)(Object &obj) = nullptr;

int kindOf(const char *name)
{
	const int k = CombatNames::kindOf(name);
	if (k < 0)
	{
		throw std::logic_error(std::string("SiegeModules: the KindOf ") + name + " is not in the binary's table");
	}
	return k;
}

int statusOf(const char *name)
{
	const int s = CombatNames::status(name);
	if (s < 0)
	{
		throw std::logic_error(std::string("SiegeModules: the object status ") + name + " is not in the binary's table");
	}
	return s;
}

int conditionOf(const char *name)
{
	const int c = CombatNames::modelCondition(name);
	if (c < 0)
	{
		throw std::logic_error(std::string("SiegeModules: the model condition ") + name + " is not in the binary's table");
	}
	return c;
}

void setCondition(Object &obj, int bit, bool on)
{
	if (obj.testModelCondition(bit) != on) // RW: test the bit, change it, notify (RW 0x68B53C)
	{
		obj.setModelConditionState(bit, on);
	}
}

const SpecialPowerTemplate *findPower(const std::string &name)
{
	return TheSpecialPowerStore && !name.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(name) : nullptr; // RW 0x73B22F: TheSpecialPowerStore's lookup
}

void parseBasePower(INI *ini, void *instance, void *, const void *) // RW 0x73B22F (the base data is the typed data's first subobject)
{
	SpecialPowerUpdateBaseModuleData *d = static_cast<SpecialPowerUpdateBaseModuleData *>(static_cast<ModuleData *>(instance));
	d->m_specialPowerTemplateName = ini->getNextToken();
	d->m_specialPowerTemplate = findPower(d->m_specialPowerTemplateName);
}

void parseStopPower(INI *ini, void *instance, void *, const void *) // RW 0x73B22F
{
	StopSpecialPowerModuleData *d = static_cast<StopSpecialPowerModuleData *>(instance);
	d->m_stopPowerTemplateName = ini->getNextToken();
	d->m_stopPowerTemplate = findPower(d->m_stopPowerTemplateName);
}

void parseSoundName(INI *ini, void *, void *store, const void *) // RW 0x73ACEF (a sound name, kept)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

#define SB_OFF(m) (int)offsetof(SpecialPowerUpdateBaseModuleData, m)
const FieldParse kSpecialPowerUpdateBase[] = { // RW 0x9911B3 -> the table RW 0xC87588
	{ "SpecialPowerTemplate", parseBasePower, nullptr, 0 },
	{ "InitiateSound", parseSoundName, nullptr, SB_OFF(m_initiateSound) },
	{ "StartsPaused", INI::parseBool, nullptr, SB_OFF(m_startsPaused) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SB_OFF
const FieldParse kSiegeDeployHorde[] = { // RW 0xC74958
	{ "HordeDeploy", INI::parseBool, nullptr, (int)offsetof(SiegeDeployHordeSpecialPowerModuleData, m_hordeDeploy) },
	{ nullptr, nullptr, nullptr, 0 },
};
#define SD_OFF(m) (int)offsetof(SiegeDeploySpecialPowerModuleData, m)
const FieldParse kSiegeDeploy[] = { // RW 0xC747E8
	{ "LowerDelay", INI::parseDurationUnsignedInt, nullptr, SD_OFF(m_lowerDelay) },
	{ "RaiseDelay", INI::parseDurationUnsignedInt, nullptr, SD_OFF(m_raiseDelay) },
	{ "EvacuatePassengersOnDeploy", INI::parseBool, nullptr, SD_OFF(m_evacuatePassengersOnDeploy) },
	{ "EvacuateCrewOnDeploy", INI::parseBool, nullptr, SD_OFF(m_evacuateCrewOnDeploy) },
	{ "SkipAdjustPosition", INI::parseBool, nullptr, SD_OFF(m_skipAdjustPosition) },
	{ "WallSearchDistance", INI::parseReal, nullptr, SD_OFF(m_wallSearchDistance) },
	{ "AwayFromWallWaitDist", INI::parseReal, nullptr, SD_OFF(m_awayFromWallWaitDist) },
	{ "ExtraWallDistance", INI::parseReal, nullptr, SD_OFF(m_extraWallDistance) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SD_OFF

const FieldParse kStopPower[] = { // RW 0xC74A78
	{ "StopPowerTemplate", parseStopPower, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

// a bone of the drawable (model space, row-major 3x4) in world space: the position (the object's basis applied, then its position) and the X axis
void boneToWorld(const Object &obj, const float bone[12], Coord3D &pos, Coord3D &xAxis)
{
	const float *b = obj.getBasis(); // b[r * 3 + c]: row r of the object's 3x3
	const Coord3D &p = *obj.getPosition();
	const float t[3] = { p.x, p.y, p.z };
	float w[3], x[3];
	for (int r = 0; r < 3; ++r)
	{
		const float v = SimMath::addf32(SimMath::mulf32(b[r * 3 + 2], bone[11]), SimMath::mulf32(b[r * 3 + 1], bone[7]));
		w[r] = SimMath::addf32(SimMath::addf32(v, SimMath::mulf32(b[r * 3 + 0], bone[3])), t[r]);
		const float u = SimMath::addf32(SimMath::mulf32(b[r * 3 + 2], bone[8]), SimMath::mulf32(b[r * 3 + 1], bone[4]));
		x[r] = SimMath::addf32(u, SimMath::mulf32(b[r * 3 + 0], bone[0]));
	}
	pos = Coord3D{ w[0], w[1], w[2] };
	xAxis = Coord3D{ x[0], x[1], x[2] };
}

float length3(const Coord3D &d)
{
	return (float)SimMath::length3d(d.x, d.y, d.z); // Coord3D::length (sqrt of the float sum of squares)
}

Coord3D normalized(const Coord3D &d)
{
	const float len = length3(d);
	if (len == 0.0f)
	{
		return d; // Coord3D::normalize leaves a zero vector
	}
	return Coord3D{ SimMath::divf32(d.x, len), SimMath::divf32(d.y, len), SimMath::divf32(d.z, len) };
}

void hashCoord(StateHasher &h, const Coord3D &c)
{
	h.addFloat(c.x);
	h.addFloat(c.y);
	h.addFloat(c.z);
}

template <class Runtime, class Data>
void bindRuntime(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}

// SiegeAIUpdate (RW 0x893BBA): the base AI with RotWK's isAllowedToRespondToAiCommands (RW 0x893C80)
class SiegeAIUpdate : public AIUpdateInterface
{
public:
	SiegeAIUpdate(Thing *thing, const ModuleData *data)
		: AIUpdateInterface(thing, data)
	{
	}
	// the save (SAVE-4e): its version and AIUpdateInterface's (no state of its own, as DozerAIUpdate's)
	OBFME_MODULE_XFER(SiegeAIUpdate)
	bool allowedToRespondToCommand(CommandSourceType source, int command) const override
	{
		if (!AIUpdateInterface::allowedToRespondToCommand(source, command)) // RW 0x667174
		{
			return false;
		}
		static const int kSiegeDeploy = 0x2D; // SPECIAL_SIEGEDEPLOY (RW 0xDA5C90 index 45)
		for (const auto &m : getObject()->modules()) // RW 0x691526: the first module whose special power's type is 0x2D
		{
			SpecialPowerModuleInterface *sp = m->getSpecialPower();
			const SpecialPowerTemplate *t = sp ? sp->getSpecialPowerTemplate() : nullptr;
			if (t && t->getSpecialPowerType() == kSiegeDeploy)
			{
				return sp->isReady() || command == 0x1B; // slot 1; AI command 0x1B (evacuate)
			}
		}
		return true;
	}
};
void SiegeAIUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIUpdateInterface::xfer(x);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// the WALL-2 integration point
// ---------------------------------------------------------------------------------------------------------------------------------
SiegePortalInterface *findSiegePortal(Object &obj)
{
	if (s_portalOverride)
	{
		return s_portalOverride(obj);
	}
	BehaviorModule *m = obj.findModule("DynamicPortalBehaviour"); // RW 0x863983
	if (!m)
	{
		return nullptr;
	}
	SiegePortalInterface *portal = dynamic_cast<SiegePortalInterface *>(m);
	if (!portal)
	{
		obj.logic().noteStop(kStopPortal); // WALL-2's module is not ported yet (S-4940)
	}
	return portal;
}

const char *SiegeModules::portalStopLine()
{
	return kStopPortal;
}

bool siegeTowerTopReach(Object &tower, const Coord3D &sourcePos, float sourceRadius)
{
	// RW 0x6EDB6F .. 0x6EDC74
	SiegePortalInterface *portal = findSiegePortal(tower); // RW 0x863983
	const SiegeDeploySpecialPower *deploy = SiegeDeploySpecialPower::of(tower);
	if (!portal || !deploy || !deploy->isDeployed()) // RW 0x8C9C01
	{
		return false;
	}
	const Coord3D top = portal->topAttackPosition(); // RW 0x863D77
	const float dz = SimMath::subf32(top.z, sourcePos.z);
	const float dx = SimMath::subf32(top.x, sourcePos.x);
	const float dy = SimMath::subf32(top.y, sourcePos.y);
	if (!(SimMath::absD((double)dz) < (double)20.0f)) // RW 0xBDBC6C
	{
		return false;
	}
	const float reach = SimMath::addf32(portal->topAttackRadius(), sourceRadius); // RW 0x863D30 + the attacker's Object + 0xB8 (x87, stored as a float)
	return SimMath::absD((double)dx) < (double)reach && SimMath::absD((double)dy) < (double)reach;
}

std::vector<std::string> SiegeModules::stopLines()
{
	return { kStopPortal, kStopAdjust, kStopSiege, DeployStyleAIUpdate::stopLine() };
}

// ---------------------------------------------------------------------------------------------------------------------------------
// SiegeDockingBehavior
// ---------------------------------------------------------------------------------------------------------------------------------
void SiegeDockingBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	(void)p; // RW 0x85C939: the base ModuleData table only
}

SiegeDockingBehavior::SiegeDockingBehavior(Thing *thing, const SiegeDockingBehaviorModuleData *data)
	: UpdateModule(thing, data)
{
	setWakeFrame(getObject(), (UpdateSleepTime)1); // RW 0x85CA22: RW 0x850C32(object, 1)
	getObject()->logic().noteStop(kStopSiege);
}

SiegeDockingBehavior *SiegeDockingBehavior::of(const Object &obj)
{
	return dynamic_cast<SiegeDockingBehavior *>(obj.findModule("SiegeDockingBehavior"));
}

UpdateSleepTime SiegeDockingBehavior::update()
{
	// RW 0x85D216
	if (!m_initialized)
	{
		initializeBones();
		m_initialized = true;
	}
	else
	{
		refresh();
	}
	return (UpdateSleepTime)LOGICFRAMES_PER_SECOND; // RW 0xD9F608
}

void SiegeDockingBehavior::initializeBones()
{
	// RW 0x85CE65: up to ten SIEGETOWER bones (type 0), then up to ten SIEGELADDER bones (type 1), in world space (RW 0x68C650 with convertToWorld)
	Object *obj = getObject();
	ProjectileLaunchOffsets *provider = obj->logic().combat().launchOffsets();
	int index = 0;
	const char *const prefixes[2] = { "SIEGETOWER", "SIEGELADDER" };
	for (int type = 0; type < 2; ++type)
	{
		float bones[10][12];
		const int count = provider ? provider->multiLogicalBones(*obj, prefixes[type], obj->getModelConditionBits(), 10, bones) : 0;
		for (int i = 0; i < count; ++i)
		{
			Entry e;
			e.index = index++;
			e.type = type;
			Coord3D xAxis;
			boneToWorld(*obj, bones[i], e.position, xAxis);
			e.direction = Coord3D{ SimMath::subf32(0.0f, xAxis.x), SimMath::subf32(0.0f, xAxis.y), SimMath::subf32(0.0f, xAxis.z) }; // RW: 0.0f - x
			e.reservedBy = INVALID_ID;
			m_entries.push_back(e);
		}
	}
}

void SiegeDockingBehavior::refresh()
{
	// RW 0x85CBC8
	GameLogic &logic = getObject()->logic();
	for (Entry &e : m_entries)
	{
		if (e.reservedBy == INVALID_ID)
		{
			continue;
		}
		Object *other = logic.findObjectByID(e.reservedBy);
		// the holder's dead bit (Object + 0x458 bit 0), as update() reads it (review of #64: a ladder playing its death animation frees its point at once)
		const SiegeDeploySpecialPower *power = other && !other->isEffectivelyDead() ? SiegeDeploySpecialPower::of(*other) : nullptr;
		if (!power || power->dockId() != getObject()->getID()) // + 0x24 slot 0x50 (RW 0x7B0F25)
		{
			e.reservedBy = INVALID_ID;
		}
	}
}

bool SiegeDockingBehavior::compatible(const Object &obj, const Entry &e) const
{
	static const int kSiegeLadder = kindOf("SIEGE_LADDER"); // template + 0x119 bit 3
	return e.reservedBy == INVALID_ID && (obj.isKindOf((unsigned)kSiegeLadder) || e.type == 0);
}

bool SiegeDockingBehavior::approachPointFor(ObjectID id, Coord3D &out)
{
	// RW 0x85CA8A
	Object *obj = getObject()->logic().findObjectByID(id);
	if (!obj || m_entries.empty())
	{
		return false;
	}
	int best = -1;
	float bestDist = 99999.0f; // RW 0xBDF350
	for (size_t i = 0; i < m_entries.size(); ++i)
	{
		const Entry &e = m_entries[i];
		if (!compatible(*obj, e))
		{
			continue;
		}
		const float scale = 30.0f;
		const Coord3D approach{ SimMath::subf32(e.position.x, SimMath::mulf32(e.direction.x, scale)), SimMath::subf32(e.position.y, SimMath::mulf32(e.direction.y, scale)),
			SimMath::subf32(e.position.z, SimMath::mulf32(e.direction.z, scale)) };
		const Coord3D &p = *obj->getPosition();
		const float dist = length3(Coord3D{ SimMath::subf32(approach.x, p.x), SimMath::subf32(approach.y, p.y), SimMath::subf32(approach.z, p.z) });
		if (dist < bestDist)
		{
			out = approach;
			best = (int)i;
			bestDist = dist;
		}
	}
	return best >= 0;
}

int SiegeDockingBehavior::reserveFor(ObjectID id)
{
	// RW 0x85CD20
	Object *obj = getObject()->logic().findObjectByID(id);
	if (!obj)
	{
		return -1;
	}
	refresh();
	if (m_entries.empty())
	{
		return -1;
	}
	int best = -1;
	float bestDist = 99999.0f;
	for (size_t i = 0; i < m_entries.size(); ++i)
	{
		const Entry &e = m_entries[i];
		if (!compatible(*obj, e))
		{
			continue;
		}
		const Coord3D &p = *obj->getPosition();
		const float dist = length3(Coord3D{ SimMath::subf32(e.position.x, p.x), SimMath::subf32(e.position.y, p.y), SimMath::subf32(e.position.z, p.z) });
		if (dist < bestDist)
		{
			best = (int)i;
			bestDist = dist;
		}
	}
	if (best >= 0)
	{
		m_entries[(size_t)best].reservedBy = id;
	}
	return best;
}

bool SiegeDockingBehavior::canDock(ObjectID id)
{
	// RW 0x85CCB8
	Object *obj = getObject()->logic().findObjectByID(id);
	if (!obj)
	{
		return false;
	}
	refresh();
	for (const Entry &e : m_entries)
	{
		if (compatible(*obj, e))
		{
			return true;
		}
	}
	return false;
}

Coord3D SiegeDockingBehavior::dockPosition(int index) const
{
	if (index >= 0 && (size_t)index < m_entries.size()) // RW 0x85C94A
	{
		return m_entries[(size_t)index].position;
	}
	return *getObject()->getPosition();
}

Coord3D SiegeDockingBehavior::dockDirection(int index) const
{
	if (index >= 0 && (size_t)index < m_entries.size()) // RW 0x85C987
	{
		return m_entries[(size_t)index].direction;
	}
	return *getObject()->getPosition();
}

void SiegeDockingBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferBool(&m_initialized);
	SnapshotXfer::each(x, m_entries, 36, [&](Entry &e) { // index, type, two coordinates, the reserving id
		x.xferInt(&e.index);
		SnapshotXfer::checkRange(e.index, 0, 19, "a siege dock's index"); // initializeBones: up to ten SIEGETOWER then ten SIEGELADDER bones, numbered in order
		SnapshotXfer::enumIn(x, e.type, 0, 1, "a siege dock's bone type"); // 0 a SIEGETOWER bone, 1 a SIEGELADDER bone
		SnapshotXfer::coord3D(x, e.position);
		SnapshotXfer::coord3D(x, e.direction);
		x.xferObjectID(&e.reservedBy);
	});
}

void SiegeDockingBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_initialized);
	h.addU32((std::uint32_t)m_entries.size());
	for (const Entry &e : m_entries)
	{
		h.addI32(e.index);
		h.addI32(e.type);
		hashCoord(h, e.position);
		hashCoord(h, e.direction);
		h.addU32(e.reservedBy);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the update + special power base RW 0x991660
// ---------------------------------------------------------------------------------------------------------------------------------
void SpecialPowerUpdateBaseModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kSpecialPowerUpdateBase); // RW 0x9911B3
}

SpecialPowerUpdateBase::SpecialPowerUpdateBase(Thing *thing, const SpecialPowerUpdateBaseModuleData *data)
	: UpdateModule(thing, data)
	, m_baseData(data)
{
	const SpecialPowerTemplate *t = data->m_specialPowerTemplate;
	if (t && !t->isSharedNSync())
	{
		startPowerRecharge(1.0f); // RW 0x991694
	}
	if (data->m_startsPaused)
	{
		pauseCountdown(true); // RW 0x9916A9
	}
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x9916B5: sleeps forever
}

unsigned SpecialPowerUpdateBase::now() const
{
	return getObject()->logic().getFrame();
}

bool SpecialPowerUpdateBase::readyImpl(bool insert) const
{
	// RW 0x99135D
	const SpecialPowerTemplate *t = m_baseData->m_specialPowerTemplate;
	Player *p = getObject()->getControllingPlayer();
	if (t && p && t->isSharedNSync())
	{
		const unsigned shared = insert ? p->science().getSharedReadyFrame(t->getID(), now()) : p->science().peekSharedReadyFrame(t->getID(), now());
		return shared <= now();
	}
	return m_pauseCount == 0 && m_readyFrame <= now();
}

float SpecialPowerUpdateBase::percentImpl(bool insert) const
{
	// RW 0x9913BC: isReady through the vtable (SiegeDeploySpecialPower's override RW 0x8C9BE0), then the paused percent, then the timer
	if (insert ? isReady() : isReadyForDisplay())
	{
		return 1.0f;
	}
	if (m_pauseCount > 0)
	{
		return m_pausedPercent;
	}
	const SpecialPowerTemplate *t = m_baseData->m_specialPowerTemplate;
	if (!t)
	{
		return 0.0f;
	}
	unsigned ready = m_readyFrame;
	Player *p = getObject()->getControllingPlayer();
	if (p && t->isSharedNSync())
	{
		ready = insert ? p->science().getSharedReadyFrame(t->getID(), now()) : p->science().peekSharedReadyFrame(t->getID(), now());
	}
	// RW 0x991434 .. 0x991469: fild (unsigned)(ready - now); fstp dword; fild (unsigned)ReloadTime; fdivr; fsubr 1.0
	const float left = NumericState::fstpDword(NumericState::fildU32(ready - now()));
	const double q = NumericState::pc24DivW((double)left, NumericState::fildU32(t->getReloadTime()));
	return NumericState::fstpDword(NumericState::pc24SubW(1.0, q));
}

unsigned SpecialPowerUpdateBase::getReadyFrame() const
{
	// RW 0x991473
	const SpecialPowerTemplate *t = m_baseData->m_specialPowerTemplate;
	Player *p = getObject()->getControllingPlayer();
	if (t && p && t->isSharedNSync())
	{
		return p->science().getSharedReadyFrame(t->getID(), now());
	}
	if (m_pauseCount < 1 && getObject()->getDisabledMask() == 0)
	{
		return m_readyFrame;
	}
	return now() - m_pauseFrame + m_readyFrame;
}

void SpecialPowerUpdateBase::pauseCountdown(bool pause)
{
	// RW 0x99116C
	if (pause)
	{
		if (m_pauseCount == 0)
		{
			m_pauseFrame = now();
			m_pausedPercent = getPercentReady();
		}
		++m_pauseCount;
		return;
	}
	if (m_pauseCount > 0 && --m_pauseCount == 0)
	{
		m_readyFrame += now() - m_pauseFrame;
	}
}

void SpecialPowerUpdateBase::startPowerRecharge(float percentOfCurrent)
{
	// RW 0x991500 (as WeaponModeSpecialPowerUpdate's, the same base)
	const SpecialPowerTemplate *t = m_baseData->m_specialPowerTemplate;
	Object *obj = getObject();
	Player *p = obj ? obj->getControllingPlayer() : nullptr;
	if (!t || !p)
	{
		return;
	}
	if (t->isSharedNSync())
	{
		p->science().resetOrStartSharedReadyFrame(t->getID(), t->getReloadTime(), now()); // RW 0x991549
		return;
	}
	float modifierScale = 1.0f;
	obj->attributeModifierProduct(ATTRIBUTE_RECHARGE_TIME, nullptr, true, modifierScale); // RW 0x991553 .. 0x991564
	const float playerScale = SimMath::pc24Add(p->science().rechargeDiscount(), 1.0f);   // RW 0x99156C .. 0x99157B
	const double wide = SimMath::pc24MulW(SimMath::pc24MulW(NumericState::fildU32(t->getReloadTime()), playerScale), modifierScale);
	const unsigned reload = SimMath::ftol2Low32(wide); // RW 0x9915A1
	if (1.0f > percentOfCurrent)
	{
		const float pct = getPercentReady();
		if (pct == 0.0f)
		{
			return; // RW 0x9915CD
		}
		float rest = SimMath::sseSub(pct, percentOfCurrent);
		if (0.0f > rest)
		{
			rest = 0.0f;
		}
		const double scaled = SimMath::pc24MulW(SimMath::pc24SubW(1.0, rest), NumericState::fildU32(reload));
		m_readyFrame = SimMath::ftol2Low32(scaled) + now();
		return;
	}
	m_readyFrame = now() + reload;
}

void SpecialPowerUpdateBase::doSpecialPower(unsigned options)
{
	// RW 0x9911FB: not paused, not disabled (RW 0x9325B4): the initiate (slot 0) without a target
	if (m_pauseCount < 1 && getObject()->getDisabledMask() == 0)
	{
		initiateIntentToDoSpecialPower(m_baseData->m_specialPowerTemplate, nullptr, nullptr, options, 0);
	}
}

void SpecialPowerUpdateBase::doSpecialPowerAtObject(Object *target, unsigned options)
{
	// RW 0x991231: the initiate with the target
	if (m_pauseCount < 1 && getObject()->getDisabledMask() == 0)
	{
		initiateIntentToDoSpecialPower(m_baseData->m_specialPowerTemplate, target, nullptr, options, 0);
	}
}

void SpecialPowerUpdateBase::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	// RW 0x991269: the initiate with the location
	if (m_pauseCount < 1 && getObject()->getDisabledMask() == 0)
	{
		initiateIntentToDoSpecialPower(m_baseData->m_specialPowerTemplate, nullptr, &loc, options, 0);
	}
}

void SpecialPowerUpdateBase::xferSpecialPowerUpdateBase(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_readyFrame);
	x.xferInt(&m_pauseCount);
	SnapshotXfer::checkRange(m_pauseCount, 0, 0x7FFFFFFF, "a special power update's pause count");
	x.xferUnsignedInt(&m_pauseFrame);
	x.xferReal(&m_pausedPercent);
}

void SpecialPowerUpdateBase::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_readyFrame);
	h.addI32(m_pauseCount);
	h.addU32(m_pauseFrame);
	h.addFloat(m_pausedPercent);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// SiegeDeployHordeSpecialPower
// ---------------------------------------------------------------------------------------------------------------------------------
void SiegeDeployHordeSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kSpecialPowerUpdateBase); // RW 0x9911B3
	p.add(kSiegeDeployHorde);       // RW 0xC74958
}

SiegeDeployHordeSpecialPower::SiegeDeployHordeSpecialPower(Thing *thing, const SiegeDeployHordeSpecialPowerModuleData *data)
	: SpecialPowerUpdateBase(thing, data)
	, m_data(data)
{
	getObject()->logic().noteStop(kStopSiege);
}

SiegeDeployHordeSpecialPower *SiegeDeployHordeSpecialPower::of(const Object &obj)
{
	return dynamic_cast<SiegeDeployHordeSpecialPower *>(obj.findModule("SiegeDeployHordeSpecialPower"));
}

void SiegeDeployHordeSpecialPower::initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *target, const Coord3D *loc, unsigned options, int extra)
{
	// RW 0x8CAA12
	(void)loc;
	(void)extra;
	Object *obj = getObject();
	if (!m_data->m_hordeDeploy)
	{
		setWakeFrame(obj, UPDATE_SLEEP_NONE); // RW 0x8CABDD: RW 0x850C32(object, 1)
		return;
	}
	ContainModuleInterface *contain = obj->getContain(); // RW 0x68C866
	if (!contain || !target)
	{
		return;
	}
	static const int kSiegeTower = kindOf("SIEGE_TOWER"); // template + 0x113 bit 5 (KindOf 93)
	Object *leader = nullptr;
	if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList()) // contain slot 66
	{
		for (Object *m : *items)
		{
			if (m->isKindOf((unsigned)kSiegeTower))
			{
				leader = m;
				break;
			}
		}
	}
	if (!leader)
	{
		return; // RotWK dereferences the null leader (BFME2 0x4C65D6): a crash there; no 2.01 object reaches it (HordeDeploy is never set)
	}
	contain->removeFromContain(leader); // contain slot 14
	// TheGameLogic->deselectObject(leader, 0xFFFFF, true): the selection is the client's
	if (AIUpdateInterface *lai = leader->getAIUpdateInterface())
	{
		lai->aiIdle(CMD_FROM_AI);
	}
	SpecialPowerModules::doSpecialPowerAtObject(*leader, t, target, options, false); // Object::doSpecialPowerAtObject(t, target, options, 0)
	std::vector<Object *> members; // contain slot 126 (a copy)
	if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
	{
		members.assign(items->begin(), items->end());
	}
	for (Object *m : members)
	{
		AIUpdateInterface *mai = m->getAIUpdateInterface();
		if (mai)
		{
			mai->aiIdle(CMD_FROM_AI);
		}
		SpecialPowerModules::doSpecialPowerAtObject(*m, t, target, options, false);
		if (SiegeDeployHordeSpecialPower *own = SiegeDeployHordeSpecialPower::of(*m))
		{
			own->setTargetObjectID(leader->getID());     // slot 13
			own->setTargetPosition(*target->getPosition()); // slot 14
		}
		if (mai)
		{
			mai->aiBusy(CMD_FROM_AI); // AI command 0x31 (RW 0x852E2A(0, CMD_FROM_AI))
		}
	}
}

UpdateSleepTime SiegeDeployHordeSpecialPower::update()
{
	// RW 0x8CA978
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Object *target = m_targetId != INVALID_ID ? logic.findObjectByID(m_targetId) : nullptr;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai || ai->lastCommandSource() == CMD_FROM_PLAYER) // AI vslot 0x23C
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (!target || target->isEffectivelyDead())
	{
		ai->aiIdle(CMD_FROM_AI); // RW 0x5E821A
		return UPDATE_SLEEP_FOREVER;
	}
	static const int kDeployed = statusOf("DEPLOYED"); // 0x3B
	if (!target->testStatus((unsigned)kDeployed))
	{
		ai->aiMoveToObject(target, CMD_FROM_AI); // RW 0x7549D7
		return UPDATE_SLEEP_NONE;
	}
	if (++m_counter < (unsigned)LOGICFRAMES_PER_SECOND) // RW 0xD9F608
	{
		return UPDATE_SLEEP_NONE;
	}
	m_counter = 0;
	ai->aiIdle(CMD_FROM_AI);
	ai->aiMoveToPosition(m_deployPosition, CMD_FROM_AI); // RW 0x66C4CA
	return UPDATE_SLEEP_FOREVER;
}

void SiegeDeployHordeSpecialPower::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferSpecialPowerUpdateBase(x);
	x.xferObjectID(&m_targetId);
	SnapshotXfer::coord3D(x, m_deployPosition);
	x.xferUnsignedInt(&m_counter);
}

void SiegeDeployHordeSpecialPower::crc(StateHasher &h) const
{
	SpecialPowerUpdateBase::crc(h);
	h.addU32(m_targetId);
	hashCoord(h, m_deployPosition);
	h.addU32(m_counter);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// SiegeDeploySpecialPower
// ---------------------------------------------------------------------------------------------------------------------------------
void SiegeDeploySpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kSpecialPowerUpdateBase); // RW 0x9911B3
	p.add(kSiegeDeploy);            // RW 0xC747E8
}

void SiegeDeploySpecialPower::setPortalOverrideForTests(SiegePortalInterface *(*find)(Object &obj))
{
	s_portalOverride = find;
}

SiegeDeploySpecialPower::SiegeDeploySpecialPower(Thing *thing, const SiegeDeploySpecialPowerModuleData *data)
	: SpecialPowerUpdateBase(thing, data)
	, m_data(data)
{
	// RW 0x8C9B49: after the base, + 0x38 .. + 0x70 cleared
	getObject()->logic().noteStop(kStopSiege);
}

SiegeDeploySpecialPower *SiegeDeploySpecialPower::of(const Object &obj)
{
	return dynamic_cast<SiegeDeploySpecialPower *>(obj.findModule("SiegeDeploySpecialPower"));
}
void SiegeDeploySpecialPower::stopSpecialPower()
{
	// RW 0x8CA2D0
	switch (m_phase)
	{
	case PHASE_IDLE:
	case PHASE_APPROACH:
		setPhase(PHASE_IDLE);
		break;
	case PHASE_LOWERING:
	case PHASE_DEPLOYED:
		m_stopRequested = true;
		break;
	default:
		break;
	}
}

void SiegeDeploySpecialPower::stopDeploy()
{
	// RW 0x8CA2F1
	static const int kDeploying = statusOf("DEPLOYING"); // 0x40
	getObject()->setStatus((unsigned)kDeploying, false);
	setPhase(PHASE_IDLE);
	m_dockId = INVALID_ID;
}

void SiegeDeploySpecialPower::clearPortal()
{
	// RW 0x8C9BCB
	if (SiegePortalInterface *portal = findSiegePortal(*getObject()))
	{
		portal->clearPortalWaypoints(); // RW 0x8639EF
	}
}

bool SiegeDeploySpecialPower::portalAboveWall(Object &obj)
{
	SiegePortalInterface *portal = findSiegePortal(obj); // RW 0x863983
	return portal && portal->isEndPortalAboveWall(nullptr); // RW 0x863EB3(0)
}

Coord3D SiegeDeploySpecialPower::computeApproachPoint(Object &dock, Coord3D &direction, bool &docked)
{
	// RW 0x8C9D06
	static const int kDeploying = statusOf("DEPLOYING");
	Object *obj = getObject();
	const Coord3D target = *dock.getPosition();
	Coord3D position = target;
	SiegeDockingBehavior *behavior = SiegeDockingBehavior::of(dock);
	if (behavior)
	{
		obj->setStatus((unsigned)kDeploying, false);
		const int index = behavior->reserveFor(obj->getID()); // dock interface slot 2
		if (index < 0)
		{
			docked = false;
		}
		else
		{
			obj->setStatus((unsigned)kDeploying, true);
			position = behavior->dockPosition(index);
			direction = behavior->dockDirection(index);
			docked = true;
		}
	}
	if (!docked)
	{
		return position;
	}
	Coord3D offset{ SimMath::subf32(position.x, target.x), SimMath::subf32(position.y, target.y), SimMath::subf32(position.z, target.z) };
	// RW 0x8C9DFE: z*z + y*y + x*x (x87, the products in that order) above 1.0 (RW 0xBD1908)
	const float sq = SimMath::addf32(SimMath::addf32(SimMath::mulf32(offset.z, offset.z), SimMath::mulf32(offset.y, offset.y)), SimMath::mulf32(offset.x, offset.x));
	if (!(1.0f < sq))
	{
		return position;
	}
	offset = Coord3D{ SimMath::mulf32(offset.x, 0.1f), SimMath::mulf32(offset.y, 0.1f), SimMath::mulf32(offset.z, 0.1f) }; // RW 0xBD83D4
	if (!m_data->m_skipAdjustPosition)
	{
		// RW 0x6EA0EC (Pathfinder::adjustMeleeOffset) is not ported (S-4941): the offset stays as it is; the position then restarts from the wall
		obj->logic().noteStop(kStopAdjust);
		position = Coord3D{ SimMath::addf32(offset.x, target.x), SimMath::addf32(offset.y, target.y), SimMath::addf32(offset.z, target.z) };
	}
	const Coord3D dir = normalized(offset); // Coord3D::Normalize
	const Coord3D step{ SimMath::mulf32(dir.x, 5.0f), SimMath::mulf32(dir.y, 5.0f), SimMath::mulf32(dir.z, 5.0f) }; // RW 0xBDAE58
	AIWorld *world = obj->logic().aiWorld();
	for (int i = 0; i < 10 && world; ++i)
	{
		if (!world->pathfinder().isPointOnWall(position, false)) // RW 0x6EA857
		{
			break;
		}
		position = Coord3D{ SimMath::addf32(step.x, position.x), SimMath::addf32(step.y, position.y), SimMath::addf32(step.z, position.z) };
	}
	const float extra = m_data->m_extraWallDistance; // + 0x2C
	position = Coord3D{ SimMath::addf32(SimMath::mulf32(extra, dir.x), position.x), SimMath::addf32(SimMath::mulf32(dir.y, extra), position.y),
		SimMath::addf32(SimMath::mulf32(dir.z, extra), position.z) };
	return position;
}

void SiegeDeploySpecialPower::initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *target, const Coord3D *loc, unsigned options, int extra)
{
	// RW 0x8CA5C2
	(void)t;
	(void)loc;
	(void)options;
	(void)extra;
	if (isActive() || !target) // slot 2 (RW 0x8C9BEE)
	{
		return;
	}
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}
	SiegeDockingBehavior *dock = SiegeDockingBehavior::of(*target);
	if (!dock)
	{
		return;
	}
	GameLogic &logic = obj->logic();
	Object *dockObject = nullptr;
	if (dock->canDock(obj->getID())) // dock interface slot 3
	{
		dockObject = logic.findObjectByID(target->getID());
	}
	else
	{
		// RW 0x8CA62E .. 0x8CA6EE: the objects near the target (WallSearchDistance, KindOf 0x3C WALK_ON_TOP_OF_WALL, from the centre in 2D, near to far): the first
		// whose dock can take this object
		static const int kWalkOnTopOfWall = kindOf("WALK_ON_TOP_OF_WALL");
		PartitionFilterFn walls([](Object &o) { return o.isKindOf((unsigned)kWalkOnTopOfWall); });
		const PartitionHits hits = logic.partition().iterateObjectsInRange(*target->getPosition(), m_data->m_wallSearchDistance, FROM_CENTER_2D, { &walls },
			ITER_SORTED_NEAR_TO_FAR);
		for (const PartitionHit &h : hits)
		{
			SiegeDockingBehavior *other = h.object ? SiegeDockingBehavior::of(*h.object) : nullptr;
			if (other && other->canDock(obj->getID()))
			{
				dockObject = h.object;
				break;
			}
		}
	}
	if (!dockObject)
	{
		// RW 0x8CA6F9 .. 0x8CA74B: no dock: toward the target, pushed AwayFromWallWaitDist along its X axis when that is positive
		const Coord3D &tp = *target->getPosition();
		if (m_data->m_awayFromWallWaitDist > 0.0f) // RW 0xC1B594 (0.0) < + 0x28
		{
			const float *b = target->getBasis(); // the target's X axis: + 0x08 / + 0x18 (the first column)
			const float d = m_data->m_awayFromWallWaitDist;
			const Coord3D p{ SimMath::addf32(tp.x, SimMath::mulf32(d, b[0])), SimMath::addf32(tp.y, SimMath::mulf32(d, b[3])), tp.z };
			ai->aiMoveToPosition(p, CMD_FROM_AI);
		}
		else
		{
			ai->aiMoveToPosition(tp, CMD_FROM_AI);
		}
	}
	else
	{
		bool docked = false;
		m_approach = computeApproachPoint(*dockObject, m_direction, docked);
		if (!docked)
		{
			m_dockId = INVALID_ID;
			stopDeploy();
		}
		else
		{
			ai->mover().ignoreObstacle(dockObject->getID()); // RW 0x66831A
			ai->aiIdle(CMD_FROM_AI);                        // RW 0x5E821A
			ai->aiMoveToPosition(m_approach, CMD_FROM_AI);  // RW 0x66C4CA
			m_toggle = true;
			setPhase(PHASE_APPROACH);
			m_dockId = dockObject->getID();
		}
	}
	setWakeFrame(obj, UPDATE_SLEEP_NONE); // RW 0x850C32(object, 1)
}

UpdateSleepTime SiegeDeploySpecialPower::update()
{
	// RW 0x8CA30F
	Object *obj = getObject();
	if (obj->isEffectivelyDead()) // + 0x458 bit 0
	{
		clearPortal();
		return UPDATE_SLEEP_FOREVER;
	}
	if (m_phase == PHASE_IDLE)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (m_stopRequested)
	{
		setPhase(PHASE_RAISING);
		m_stopRequested = false;
		return UPDATE_SLEEP_NONE;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		stopDeploy();
		return UPDATE_SLEEP_FOREVER;
	}
	// RW 0x8CA3B9: no drawable (RW 0x70E013): forever. The logic objects of this port have no drawable of their own: INFERENCE, every object with this module has one
	// in retail (its model is what the bones are read from)
	const bool idle = ai->isIdle(); // AI vslot 0x1B8
	const CommandSourceType last = ai->lastCommandSource(); // AI vslot 0x23C
	if ((!idle && last != CMD_FROM_AI) || (idle && last == CMD_FROM_PLAYER))
	{
		stopDeploy(); // an order of the player's ended the deploy
		return UPDATE_SLEEP_FOREVER;
	}
	GameLogic &logic = obj->logic();
	Object *dock = nullptr;
	if (m_dockId != INVALID_ID)
	{
		dock = logic.findObjectByID(m_dockId);
		if (!dock || dock->isEffectivelyDead())
		{
			dock = nullptr;
			m_dockId = INVALID_ID;
			if (m_phase == PHASE_DEPLOYED)
			{
				setPhase(PHASE_RAISING);
			}
		}
	}
	if (!dock)
	{
		setPhase(PHASE_RAISING);
	}
	switch (m_phase)
	{
	case PHASE_IDLE:
	{
		static const int kDeploying = statusOf("DEPLOYING");
		if (dock)
		{
			dock->setStatus((unsigned)kDeploying, false); // RW 0x8CA411: the dock object's (null in the retail flow: the phase is only 0 here after a stop)
		}
		return UPDATE_SLEEP_FOREVER;
	}
	case PHASE_APPROACH:
	{
		const Coord3D &p = *obj->getPosition();
		const Coord3D face{ SimMath::addf32(m_direction.x, p.x), SimMath::addf32(m_direction.y, p.y), SimMath::addf32(m_direction.z, p.z) };
		const float rel = emotionRelAngle(*obj, face); // RW 0x4B3D8D
		bool ok = SimMath::absD((double)rel) < (double)0.15f; // RW 0xBE53C8 (a float read as the double compare's operand)
		if (ok)
		{
			const ThingTemplate *ot = static_cast<const ThingTemplate *>(obj->getTemplate())->getFinalOverride();
			const ThingTemplate *dt = static_cast<const ThingTemplate *>(dock->getTemplate())->getFinalOverride();
			const bool touching = NotifyCrushModules::geometriesOverlap(ObjectGeometry::shapesOf(*ot), *obj->getPosition(), obj->getOrientation(),
				ObjectGeometry::shapesOf(*dt), *dock->getPosition(), dock->getOrientation()); // RW 0x68BA2A -> RW 0xAD2CE0
			ok = (touching || idle) && portalAboveWall(*obj);
		}
		if (ok)
		{
			ai->setLocomotorGoalNone(); // AI vslot 0x220
			ai->aiBusy(CMD_FROM_AI);    // AI command 0x31 (RW 0x852E2A(0, CMD_FROM_AI) -> RW 0x66498A: state 0x2A)
			setPhase(PHASE_LOWERING);
			break;
		}
		if (!idle)
		{
			return UPDATE_SLEEP_NONE;
		}
		ai->mover().ignoreObstacle(dock->getID()); // RW 0x66831A
		m_toggle = !m_toggle;
		if (m_toggle)
		{
			bool docked = false;
			const Coord3D approach = computeApproachPoint(*dock, m_direction, docked);
			m_approach = approach;
			if (docked)
			{
				ai->aiMoveToPosition(m_approach, CMD_FROM_AI); // RW 0x66C4CA
			}
		}
		else
		{
			const Coord3D at{ SimMath::addf32(m_direction.x, obj->getPosition()->x), SimMath::addf32(m_direction.y, obj->getPosition()->y),
				SimMath::addf32(m_direction.z, obj->getPosition()->z) };
			ai->aiFacePosition(at, CMD_FROM_AI); // AI command 0x27 (RW 0x7C821E)
		}
		return UPDATE_SLEEP_NONE;
	}
	case PHASE_LOWERING:
		if (++m_frame < m_data->m_lowerDelay) // + 0x18
		{
			return UPDATE_SLEEP_NONE;
		}
		setPhase(PHASE_DEPLOYED);
		return UPDATE_SLEEP_NONE;
	case PHASE_RAISING:
		setPhase(PHASE_IDLE);
		return UPDATE_SLEEP_NONE;
	default:
		return UPDATE_SLEEP_NONE;
	}
	return UPDATE_SLEEP_NONE;
}

void SiegeDeploySpecialPower::setPhase(int next)
{
	// RW 0x8CA072
	static const int kUnpacking = conditionOf("UNPACKING");     // 96 (+ 0x118 bit 0)
	static const int kDeployedCond = conditionOf("DEPLOYED");   // 100 (+ 0x118 bit 4)
	static const int kPacking = conditionOf("PACKING");         // 94 (+ 0x114 bit 30)
	static const int kMoving = conditionOf("MOVING");           // 61 (+ 0x110 bit 29)
	static const int kDeployedStatus = statusOf("DEPLOYED");    // 0x3B
	if (next == m_phase)
	{
		return;
	}
	Object *obj = getObject();
	switch (m_phase)
	{
	case PHASE_LOWERING:
		setCondition(*obj, kUnpacking, false);
		break;
	case PHASE_DEPLOYED:
		obj->setStatus((unsigned)kDeployedStatus, false);
		setCondition(*obj, kDeployedCond, false);
		clearPortal(); // RW 0x8C9BCB
		break;
	case PHASE_RAISING:
		m_dockId = INVALID_ID;
		setCondition(*obj, kPacking, false);
		break;
	default:
		break;
	}
	m_frame = 0;
	m_phase = next;
	switch (m_phase)
	{
	case PHASE_LOWERING:
	{
		setCondition(*obj, kUnpacking, true);
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (Locomotor *loco = ai ? ai->curLocomotor() : nullptr) // RW 0x68B31D: the AI's current locomotor
		{
			loco->setSpeedTowards(ai->locomotorHost(), 0.0f); // RW 0x5E4C40(0.0)
		}
		if (ContainModuleInterface *c = obj->getContain()) // contain slot 0x118: the riders
		{
			if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
			{
				for (Object *rider : *items)
				{
					setCondition(*rider, kUnpacking, true);
				}
			}
		}
		break;
	}
	case PHASE_DEPLOYED:
	{
		obj->setStatus((unsigned)kDeployedStatus, true);
		setCondition(*obj, kDeployedCond, true);
		setCondition(*obj, kMoving, false);
		if (SiegePortalInterface *portal = findSiegePortal(*obj)) // RW 0x863983
		{
			portal->createPortalWaypoints(); // RW 0x86404D
		}
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (m_data->m_evacuatePassengersOnDeploy && ai)
		{
			// AI command 0x1B (RW 0x6AF1AC(0, CMD_FROM_AI) -> RW 0x66E0DC): the contain's slot 0x80. The AI's own gate (SiegeAIUpdate RW 0x893C80) lets 0x1B through
			if (ai->acceptCommand(CMD_FROM_AI, 0x1B))
			{
				if (ContainModuleInterface *c = obj->getContain())
				{
					c->orderAllPassengersToExit((int)CMD_FROM_AI);
				}
			}
		}
		if (m_data->m_evacuateCrewOnDeploy)
		{
			// contain slot 0x11C: the crew list (a copy), each member with an AI takes the owner's attitude (RW 0x66E12A(RW 0x664CD6(owner's AI))) and exits
			OpenContain *open = dynamic_cast<OpenContain *>(obj->getContain());
			const ContainModuleInterface::ContainedItemsList *crew = open ? open->crewList() : nullptr;
			if (crew)
			{
				const ContainModuleInterface::ContainedItemsList copy = *crew;
				for (Object *member : copy)
				{
					if (AIUpdateInterface *mai = member->getAIUpdateInterface())
					{
						mai->setAttitude(ai ? ai->attitude() : 0);
						mai->aiExit(obj, CMD_FROM_AI); // RW 0x7716C1
					}
				}
			}
		}
		break;
	}
	case PHASE_RAISING:
		setCondition(*obj, kPacking, true);
		obj->setDisabled(DISABLED_TEMPORARILY_BUSY, m_data->m_raiseDelay + obj->logic().getFrame()); // RW 0x6907F1(8, RaiseDelay + now)
		break;
	default:
		break;
	}
}

void SiegeDeploySpecialPower::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferSpecialPowerUpdateBase(x);
	SnapshotXfer::enumIn(x, m_phase, PHASE_IDLE, PHASE_RAISING, "a siege deploy's phase");
	x.xferUnsignedInt(&m_frame);
	x.xferObjectID(&m_dockId);
	SnapshotXfer::coord3D(x, m_approach);
	x.xferBool(&m_toggle);
	SnapshotXfer::coord3D(x, m_direction);
	x.xferBool(&m_stopRequested);
}

void SiegeDeploySpecialPower::crc(StateHasher &h) const
{
	SpecialPowerUpdateBase::crc(h);
	h.addI32(m_phase);
	h.addU32(m_frame);
	h.addU32(m_dockId);
	hashCoord(h, m_approach);
	h.addBool(m_toggle);
	hashCoord(h, m_direction);
	h.addBool(m_stopRequested);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// StopSpecialPower
// ---------------------------------------------------------------------------------------------------------------------------------
void StopSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p); // RW 0x89699B
	p.add(kStopPower);                          // RW 0xC74A78
}

StopSpecialPower::StopSpecialPower(Thing *thing, const StopSpecialPowerModuleData *data)
	: SpecialPowerModule(thing, data)
	, m_stopData(data)
{
}

void StopSpecialPower::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	SpecialPowerModule::xfer(x);
	x.xferUnsignedInt(&m_stops);
}

BehaviorModule *StopSpecialPower::stopTarget() const
{
	// RW 0x8CACC6's search: the first module of the object (Object + 0x24C) whose special power (+ 0xC interface slot 0x20) answers the StopPowerTemplate (slot 0)
	if (m_found)
	{
		return m_found;
	}
	const SpecialPowerTemplate *t = m_stopData->m_stopPowerTemplate;
	if (!t)
	{
		return nullptr;
	}
	for (const auto &m : getObject()->modules())
	{
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		if (sp && sp->getSpecialPowerTemplate() == t)
		{
			m_found = m.get();
			break;
		}
	}
	return m_found;
}

bool StopSpecialPower::isReady() const
{
	// RW 0x8CACC6
	BehaviorModule *m = stopTarget();
	if (!m)
	{
		return false;
	}
	const SpecialPowerUpdateInterface *u = dynamic_cast<const SpecialPowerUpdateInterface *>(m); // + 0xC slot 0x64: the special power update interface
	return u ? u->isActive() : true;
}

void StopSpecialPower::doSpecialPower(unsigned options)
{
	// RW 0x8CAC43
	(void)options;
	if (!isReady())
	{
		return;
	}
	if (SpecialPowerStopInterface *s = dynamic_cast<SpecialPowerStopInterface *>(stopTarget())) // slot 17 of its special power interface
	{
		s->stopSpecialPower();
		++m_stops;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the face state 0x25 and its command
// ---------------------------------------------------------------------------------------------------------------------------------
AIFacePositionState::AIFacePositionState(AIStateMachine &m)
	: AIState(m, "AIFaceState")
{
}

StateReturnType AIFacePositionState::onEnter()
{
	// RW 0x74D68C (mode 0: no goal object needed, not the idle variant)
	AIUpdateInterface &a = ai();
	m_canTurn = a.curLocomotor() && a.curLocomotor()->getTemplate().m_minSpeed == 0.0f; // RW 0x5E3796
	return STATE_CONTINUE;
}

StateReturnType AIFacePositionState::update()
{
	// RW 0x75638B, mode 0: the machine's goal position (machine + 0x24)
	Object &obj = owner();
	AIUpdateInterface &a = ai();
	const Coord3D tp = machine().goalPosition();
	const float rel = emotionRelAngle(obj, tp);
	if (!(SimMath::absD((double)rel) < (double)0.035f)) // RW 0xC2A1BC
	{
		if (!m_canTurn)
		{
			a.setLocomotorGoalPositionExplicit(tp); // AI vslot 0x210
		}
		else
		{
			a.setLocomotorGoalOrientation(SimMath::addf32(obj.getOrientation(), rel)); // AI vslot 0x21C
		}
		return STATE_CONTINUE;
	}
	return STATE_SUCCESS;
}

void AIFacePositionState::onExit(StateExitType status)
{
	// RW 0x743DA9: the object's horde (RW 0x694BF8) drops its face point
	(void)status;
	if (ContainModuleInterface *c = owner().getContain())
	{
		if (HordeContainInterface *hc = c->getHordeContainInterface())
		{
			hc->clearFacePoint();
		}
	}
}

void AIFacePositionState::crc(StateHasher &h) const
{
	h.addBool(m_canTurn);
}

void AIFacePositionState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferBool(&m_canTurn);
}

void AIUpdateInterface::aiFacePosition(const Coord3D &pos, CommandSourceType source)
{
	// AI command 0x27 (RW 0x7C821E) -> the gate RW 0x667174 -> AI vslot 0x118 (RW 0x667290)
	if (!acceptCommand(source, 0x27))
	{
		return;
	}
	if (!GarrisonRules::isMobile(*getObject())) // RW 0x690E97
	{
		return;
	}
	m_machine->clear(); // machine slot 0x14
	Coord3D goal;
	goalPositionClipped(pos, source, goal); // RW 0x6654AD
	m_machine->setGoalPosition(goal);
	setLastCommandSource(source); // + 0x48 (the + 0x16C / + 0x3B8 the command clears are not identified: S-4942)
	m_machine->setState(AI_FACE_POSITION); // 0x25
	wakeUpNow();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------------------------------------------------------------
void SiegeModules::registerAll(ModuleFactory &modules)
{
	bindRuntime<SiegeDockingBehavior, SiegeDockingBehaviorModuleData>(modules, "SiegeDockingBehavior");
	bindRuntime<SiegeDeploySpecialPower, SiegeDeploySpecialPowerModuleData>(modules, "SiegeDeploySpecialPower");
	bindRuntime<SiegeDeployHordeSpecialPower, SiegeDeployHordeSpecialPowerModuleData>(modules, "SiegeDeployHordeSpecialPower");
	bindRuntime<StopSpecialPower, StopSpecialPowerModuleData>(modules, "StopSpecialPower");
	DeployStyleModules::registerAll(modules); // the Demolisher deploy (GameLogic/Module/DeployStyleAIUpdate.cpp)
	// after AIUpdateInterface::registerClasses: SiegeAIUpdate replaces the base AI's binding (as DozerAIUpdate does)
	modules.bindModuleProc("SiegeAIUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &info) -> std::unique_ptr<Module> {
		if (!thing->asObject() || !thing->asObject()->logic().aiWorld())
		{
			return makeUnportedModule(thing, data, info.name, info.type, info.interfaceMask);
		}
		return std::make_unique<SiegeAIUpdate>(thing, data);
	});
}
