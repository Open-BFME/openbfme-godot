// OpenBFME. GPL-3.0. GiveUpgradeUpdate, RotWK 2.01. Every branch below is the instruction named in its comment; the
// BFME2 1.06 decomp (Open-BFME-2, GiveUpgradeUpdate.cpp) is the donor for the shape only.
//
// WHAT ROTWK DOES (2.01 game.dat, addresses are virtual addresses):
//   * the search (RW 0x89FEBD): with no target yet (+ 0x40) and DeliverUpgrade set (module data + 0xE8), every object
//     in range that is an ally, not dead, not WALL_UPGRADE (KindOf 150) and affectedByUpgrade is asked in near-to-far
//     order. A candidate that is still open reserves itself and, when it is contained or its producer is not a
//     structure, that container/producer too (status 0x42 on both, RW 0x8A0029 / 0x8A0035) before the ability starts.
//   * the give test (RW 0x89FE64): a contained target is answered by its provider — the contained target's producer
//     (Object + 0x78), the producer's contain module (Object + 0x258) and that contain's horde interface (slot 0x7C,
//     RW 0x89FD77); the provider answers for its members (slot 0xAC, RW 0x86ECAB). Any other target answers itself.
//   * the delivery (RW 0x8A01B2): the upgrade goes to the target's horde — the target's own contain when the target is
//     a HORDE (KindOf 109), otherwise the producer's horde (RW 0x89FD77) — and that horde gives it to every member
//     that is affectedByUpgrade and flashes them (slot 0xB8, RW 0x871A90). A target without a horde takes it itself
//     (RW 0x69388B) and flashes (RW 0x8A02A3). The porter then leaves every player's selection (RW 0x625759 with the
//     mask 0xFFFFF) and the closing part runs (RW 0x89FE01): the reservation clears, searching stops, SpawnOutFX
//     plays at the porter, the porter turns UNSELECTABLE and fades out.
//   * update (RW 0x8A00C2): an AI command that no longer comes from the AI ends the ability; while searching the porter
//     asks for a target every frame (one frame of sleep); while fading it lowers its drawable's opacity by
//     FadeOutSpeed a frame and destroys itself below zero.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#include "GameLogic/Module/GiveUpgradeUpdate.h"
#include "Common/AsciiString.h"
#include "Common/INIDataTypes.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Upgrade.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include <cstddef>
#include <stdexcept>

namespace {
using Data = GiveUpgradeUpdateModuleData;
void parseName(INI *ini, void *, void *store, const void *)
{
	const std::string value = ini->getNextToken();
	*static_cast<std::string *>(store) = AsciiStringUtil::compareNoCase(value, "None") == 0 ? std::string() : value;
}
#define GIVE_OFF(m) (int)offsetof(Data, m)
// RW 0xC67AE8: RotWK 2.01's table has SpawnOutFX (+ 0xE0, parser RW 0x73A302), FadeOutSpeed (+ 0xE4, parser
// RW 0x42ED00) and DeliverUpgrade (+ 0xE8, parser RW 0x42E558) only. GiveUpgradeEffect is a BFME1/BFME2 field (the
// editions' table); the game parses it nowhere, so the value a mod writes is dead there — here it is parsed only so the
// loader accepts the blocks, and nothing reads m_giveFX.
const FieldParse kFields[] = {
	{ "GiveUpgradeEffect", parseName, nullptr, GIVE_OFF(m_giveFX) }, // no reader: not a RotWK 2.01 field
	{ "SpawnOutFX", parseName, nullptr, GIVE_OFF(m_spawnFX) },
	{ "FadeOutSpeed", INI::parseReal, nullptr, GIVE_OFF(m_fadeSpeed) },
	{ "DeliverUpgrade", INI::parseBool, nullptr, GIVE_OFF(m_deliver) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef GIVE_OFF
const RGBColor kGiveUpgradeFlash{ 0.99f, 0.99f, 0.99f }; // RW 0xBE53BC, the same float in all three components
}

// RW 0x89FDC3 (~GiveUpgradeUpdateModuleData), RW 0x89FDA7 (deleting dtor)
void Data::buildFieldParse(MultiIniFieldParse &p) { SpecialAbilityUpdateModuleData::buildFieldParse(p); p.add(kFields); }
GiveUpgradeUpdate::GiveUpgradeUpdate(Thing *thing, const Data *data) : SpecialAbilityUpdate(thing, data), m_giveData(data) {}

const char *GiveUpgradeUpdate::stopLine()
{
	return "[S-6011] GiveUpgradeUpdate: the search's filter chain (RW 0x89FF34's three partition functors RW 0xC0F374 / "
		   "RW 0xC67A88 / RW 0xC10E20) is INFERENCE; GiveUpgradeEffect is not a field of this module in RotWK 2.01 "
		   "(field table RW 0xC67AE8) and has no reader";
}

const UpgradeTemplate *GiveUpgradeUpdate::upgrade() const
{
	if (!TheUpgradeCenter) return nullptr;
	const UpgradeMaskType mask = getObject()->getUpgradeMask();
	for (const UpgradeTemplate *t : TheUpgradeCenter->templates())
	{
		if (t && mask.test(t->getMaskBit())) return t; // RW 0x66F468
	}
	return nullptr;
}

// RW 0x89FD77, its only two callers being the give test (RW 0x89FE92) and the delivery (RW 0x8A0243): the contained
// target's producer id (Object + 0x78, RW 0x89FD7F) looked up in the logic (RW 0x449681), that producer's contain
// module (Object + 0x258, RW 0x89FD91) and the contain's horde interface (vslot 0x7C, RW 0x89FDA1). Null when the
// target has no producer, the producer is gone or the producer has no contain (RW 0x89FD9B).
HordeContainInterface *GiveUpgradeUpdate::hordeProviderOf(const Object &target) const
{
	const ObjectID producerID = target.getProducerID();
	if (producerID == INVALID_ID) return nullptr; // RW 0x89FD7B
	Object *producer = getObject()->logic().findObjectByID(producerID); // RW 0x89FD82 -> RW 0x449681
	if (!producer) return nullptr;                                      // RW 0x89FD8F
	ContainModuleInterface *contain = producer->getContain();           // RW 0x89FD91: Object + 0x258
	if (!contain) return nullptr;                                       // RW 0x89FD99
	return contain->getHordeContainInterface();                         // RW 0x89FDA1: contain vslot 0x7C
}

bool GiveUpgradeUpdate::canGiveUpgradeTo(Object &target) const
{
	const UpgradeTemplate *u = upgrade(); // RW 0x89FE67 -> RW 0x66F468
	if (!u) return false;                 // RW 0x89FE80
	if (target.getContainedBy())          // RW 0x89FE86: Object + 0x27C
	{
		HordeContainInterface *provider = hordeProviderOf(target); // RW 0x89FE92
		if (!provider) return false;                               // RW 0x89FE99
		return provider->affectedByUpgrade(u);                     // RW 0x89FEA0: horde slot 0xAC, RW 0x86ECAB
	}
	return target.affectedByUpgrade(u); // RW 0x89FEA8 -> RW 0x694914
}

void GiveUpgradeUpdate::onObjectCreated()
{
	// DeliverUpgrade (module data + 0xE8) is the only gate on the automatic search: the search itself asks it again
	// (RW 0x89FEDE), so a porter without it never wakes.
	if (m_giveData->m_deliver)
	{
		m_searching = true; // RW 0x89FEF4: module + 0x89
		setWakeFrame(getObject(), UPDATE_SLEEP(1));
	}
}

// RW 0x0089FC9D (SP interface slot 0, RW 0xC67B28; DONOR BFME2 0x0049C51A): if searching (+0x89) with a live target
// (+0x40), do not re-initiate (RW 0x89FCA3 .. 0x89FCB9); otherwise forward to SpecialAbilityUpdate::initiateIntentToDoSpecialPower (RW 0x852E8F).
void GiveUpgradeUpdate::initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *targetObj, const Coord3D *loc, unsigned options, int extra)
{
	if (target() && m_searching) // RW 0x89FCA3 .. 0x89FCB9: findObjectByID(+0x40) != nullptr && m_searching != 0
	{
		return;
	}
	// RW 0x8A006C is the approachTarget override (vtable slot 0x38, RW 0xC67B90): the search first (RW 0x8A006F ->
	// RW 0x89FEBD), then the base approach (RW 0x85393B). An ordered target is already in + 0x40 by then (the base
	// initiate writes it), and the search leaves at once (RW 0x89FECE); with no ordered target the search picks one —
	// only while DeliverUpgrade is set (RW 0x89FEDE). The port's state machine starts the ability here.
	SpecialAbilityUpdate::initiateIntentToDoSpecialPower(t, targetObj, loc, options, extra);
	findDeliveryTarget(true);
}

// RW 0x0089FCD6 (SP interface slot 8 / +0x20, RW 0xC67B28; DONOR BFME2 0x0049C553 GiveUpgradeUpdateSlot8.cpp):
// base RW 0x851B6E first; then true if no target (+0x40 == 0) or the target is missing / effectively dead (+0x458 bit 0).
bool GiveUpgradeUpdate::allowsSpecialPowerUse() const
{
	if (!SpecialAbilityUpdate::allowsSpecialPowerUse()) // RW 0x89FCDD -> RW 0x851B6E
	{
		return false;
	}
	if (m_targetID == INVALID_ID) // RW 0x89FCE6
	{
		return true;
	}
	Object *obj = target(); // RW 0x89FCF1 -> RW 0x449681
	return !obj || obj->isEffectivelyDead(); // RW 0x89FCFD .. 0x89FD10: Object + 0x458 bit 0
}

// RW 0x89FEBD: one pass of the automatic search.
void GiveUpgradeUpdate::findDeliveryTarget(bool ordered)
{
	Object *owner = getObject();
	if (target() || m_targetID != INVALID_ID) return; // RW 0x89FECE: module + 0x40 already holds a target
	// RW 0x89FEDE: DeliverUpgrade (module data + 0xE8) is the *automatic* search's switch, and the search the module
	// starts by itself honours it. INFERENCE (S-6011): an ability that a player or a script ordered (test `LOCAL-18`, a
	// porter with `DeliverUpgrade = No` whose SpecialPowerModule picks the power) still searches — with the gate applied
	// to both call sites the ordered delivery never happens, and RW 0x8A006C reaches this code from the ordered side too.
	if (!m_giveData->m_deliver && !ordered) return;
	m_searching = true;                               // RW 0x89FEF4: module + 0x89
	const UpgradeTemplate *u = upgrade();             // RW 0x89FEFB -> RW 0x66F468
	if (!u || !m_giveData->m_specialPowerTemplate) return;
	GameLogic &logic = owner->logic();
	// The search's own three functors: the owner's relationship and the target's upgrade (RW 0xC0F374 /
	// RW 0xC67A88), the near-to-far sort of RW 0xC10E20 / RW 0xC1676C around the porter's position (RW 0x68B678).
	logic.noteStop(stopLine()); // the filter chain itself is INFERENCE (see stopLine)
	PartitionFilterFn eligible([&](Object &o) {
		return &o != owner && owner->getRelationship(o) == ALLIES && !o.isEffectivelyDead()
			&& !o.isKindOfName("WALL_UPGRADE") && o.affectedByUpgrade(u); // RW 0x89FFAF: Thing + 0x11A bit 0x40 (KindOf 150)
	});
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(*owner->getPosition(), 100000.0f, FROM_CENTER_2D, { &eligible }, ITER_SORTED_NEAR_TO_FAR))
	{
		Object *candidate = hit.object;
		if (candidate->testStatus(0x42)) continue; // RW 0x89FFBB -> RW 0x44DDEC
		// RW 0x89FFC9 -> RW 0x693A1A(0): the container of a contained candidate is the second object the delivery
		// reserves; otherwise a producer that is not a structure (Thing + 0x108 bit 0x80) is (RW 0x89FFEE).
		Object *linked = candidate->getContainedBy();
		if (!linked)
		{
			const ObjectID producerID = candidate->getProducerID(); // RW 0x89FFD7: Object + 0x78
			if (producerID != INVALID_ID)                           // RW 0x89FFDC
			{
				Object *producer = logic.findObjectByID(producerID); // RW 0x89FFE5
				if (producer && !producer->isKindOfName("STRUCTURE")) // RW 0x89FFF1
				{
					linked = producer; // RW 0x89FFFA
				}
			}
		}
		if (linked)
		{
			if (linked->testStatus(0x42)) continue;     // RW 0x8A0002
			if (linked->testStatus(2)) continue;        // RW 0x8A0010: UNDER_CONSTRUCTION
			linked->setStatus(0x42, true);              // RW 0x8A002C
		}
		candidate->setStatus(0x42, true);               // RW 0x8A0037
		// RW 0x8A004A: the module data's special power template (+ 0x38) and the candidate, through the ability's own
		// vslot 0 at this + 0x20 (the base initiate, no location, no options, no extra).
		SpecialAbilityUpdate::initiateIntentToDoSpecialPower(m_giveData->m_specialPowerTemplate, candidate, nullptr, 0, 0);
		return;
	}
}

// RW 0x89FD15: the reservation clears on the target and on its container, then the target id is forgotten.
void GiveUpgradeUpdate::releaseTarget()
{
	if (m_targetID == INVALID_ID) return;  // RW 0x89FD1B
	Object *t = target();                  // RW 0x89FD27 -> RW 0x449681
	if (!t)
	{
		m_targetID = INVALID_ID;           // RW 0x89FD55
		return;
	}
	t->setStatus(0x42, false);             // RW 0x89FD38
	if (Object *container = t->getContainedBy()) // RW 0x89FD41 -> RW 0x693A1A(0)
	{
		container->setStatus(0x42, false); // RW 0x89FD50
	}
	m_targetID = INVALID_ID;               // RW 0x89FD55
}

// RW 0x89FE01: the delivery's closing part, called from RW 0x8A02BB.
void GiveUpgradeUpdate::finishDelivery()
{
	Object *owner = getObject();
	if (!owner) return;        // RW 0x89FE0B
	releaseTarget();           // RW 0x89FE0D -> RW 0x89FD15
	m_searching = false;       // RW 0x89FE15
	// RW 0x89FE1C: module data + 0xE0 (SpawnOutFX); RW 0x89FE26..RW 0x89FE35 push the fx, the porter's position
	// (Object + 0x38), a null matrix and 0.0f and call RW 0x494615 (FXList::doFXPos at the porter).
	if (FXEventLog::isFXName(m_giveData->m_spawnFX))
	{
		FXEvent fx = FXEventLog::objectEvent(FXEvent::POSITION_FX, "GiveUpgradeUpdate SpawnOutFX", owner->logic().getFrame(), m_giveData->m_spawnFX, *owner);
		fx.hasTransform = false; // RW 0x89FE2E: the matrix argument is 0
		owner->logic().fxEvents().emit(fx);
	}
	owner->setStatus(3, true); // RW 0x89FE43: UNSELECTABLE on the porter
	m_fading = true;           // RW 0x89FE50: module + 0x88
	m_opacity = 1.0f;          // RW 0x89FE57: RW 0xBD1908
}

void GiveUpgradeUpdate::triggerAbilityEffect()
{
	if (m_fading) return;                         // RW 0x8A01BB
	SpecialAbilityUpdate::triggerAbilityEffect();  // RW 0x8A01CA -> RW 0x853EDF
	Object *owner = getObject();
	Object *recipient = target();                 // RW 0x8A01CF
	// RW 0x8A01E2 .. RW 0x8A0201: no target, a destroyed one (Object + 0x458 bit 0) or one the give test refuses:
	// the porter's AI idles (RW 0x8A02D0 -> RW 0x5E821A with CMD_FROM_AI) and nothing is delivered.
	if (!recipient || recipient->isEffectivelyDead() || !canGiveUpgradeTo(*recipient))
	{
		aiIdleFromAI(); // RW 0x8A02D0: RW 0x5E821A(2)
		return;
	}
	const UpgradeTemplate *u = upgrade(); // RW 0x8A021B -> RW 0x66F468
	m_searching = false;                  // RW 0x8A020D: module + 0x89
	if (!u)
	{
		finishDelivery(); // RW 0x8A0225 -> RW 0x8A02BB
		return;
	}
	HordeContainInterface *horde = nullptr;
	if (recipient->isKindOfName("HORDE")) // RW 0x8A022E: Thing + 0x115 bit 0x20 (KindOf 109)
	{
		if (ContainModuleInterface *contain = recipient->getContain())
		{
			horde = contain->getHordeContainInterface(); // RW 0x8A0239 -> RW 0x68C866
		}
	}
	else
	{
		horde = hordeProviderOf(*recipient); // RW 0x8A0243 -> RW 0x89FD77
	}
	if (horde)
	{
		// RW 0x8A024C .. 0x8A0255: horde slot 0xB8 (RW 0x871A90) with the flag 0: the horde object takes the
		// upgrade and every member that is affectedByUpgrade takes it and flashes.
		horde->giveUpgrade(u, false);
	}
	else if (recipient->affectedByUpgrade(u)) // RW 0x8A0262 -> RW 0x694914
	{
		recipient->giveUpgrade(u); // RW 0x8A0287 -> RW 0x69388B
		// RW 0x8A028E -> RW 0x70E013: the recipient's drawable; the port reaches it through the logic's client hooks
		// (the same bridge SlavedUpdate and AODHordeContain use), which is where the client's drawable flash hangs.
		(void)kGiveUpgradeFlash; // client-side drawable flash (RW 0x8A02A3 -> RW 0x675749)
	}
	// RW 0x8A02A8 -> RW 0x625759: the porter leaves every player's selection (mask 0xFFFFF, the client hears of it).
	// client-side selection clear (RW 0x8A02A8 -> RW 0x625759) is a client UI concern.
	finishDelivery(); // RW 0x8A02BB
}

UpdateSleepTime GiveUpgradeUpdate::update()
{
	const UpdateSleepTime result = SpecialAbilityUpdate::update(); // RW 0x8A00DF -> RW 0x854DF7
	// RW 0x8A00C9 asks the porter's AI (Object + 0x260, AI vslot 0x23C) about the command it is running and lets the
	// ability end (RW 0x8A00F2 -> RW 0x8513E4, finishAbility(0, 1)) when the answer is not a special power. The port cannot
	// name that query with evidence, and the one candidate it has (`AIUpdateInterface::lastCommandSource() == CMD_FROM_AI`)
	// cancels deliveries the game makes: a porter ordered by a player (LOCAL-18) or by a mission script answers
	// CMD_FROM_PLAYER / CMD_FROM_SCRIPT there, and the delivery this lane is about would never happen. INFERENCE (S-6011):
	// until RW 0x8A00C9's slot 0x23C is read, a started ability is not cancelled here; an idle one (no target, not
	// searching, not fading) ends instead, which is what RW 0x8A013A does for a target it can no longer use.
	if (m_fading)
	{
		m_opacity = SimMath::subf32(m_opacity, m_giveData->m_fadeSpeed); // RW 0x8A0159 .. 0x8A016F: module + 0x8C less module data + 0xE4
		if (m_opacity < 0.0f)                 // RW 0x8A0169: 0 > opacity
		{
			getObject()->logic().destroyObject(getObject()); // RW 0x8A0176 -> RW 0x62BBAB
			m_fading = false;                                // RW 0x8A0185
			m_opacity = 0.0f;                                // RW 0x8A0188
		}
		else if (ObjectClientHooks *client = getObject()->logic().clientHooks()) // RW 0x8A018F -> RW 0x70E013
		{
			client->setDrawableOpacity(*getObject(), m_opacity);                // RW 0x8A019B: drawable + 0xB0
		}
		// RW 0x8A01A1: a searching porter wakes every frame; the fading one wakes too. INFERENCE (S-6011): RW's base keeps
		// the ability active while the porter fades (its idle answer is RW 0x854EA4's 0x3FFFFFFF, sleep forever), the port's
		// SpecialAbilityUpdate ends it the frame after the delivery and would leave the porter half faded.
		return UPDATE_SLEEP(1);
	}
	if (m_searching)
	{
		findDeliveryTarget(); // RW 0x8A0100 .. 0x8A0108
		return UPDATE_SLEEP(1); // RW 0x8A01A1: a searching porter wakes every frame
	}
	Object *current = target(); // RW 0x8A0112 -> RW 0x449681
	if (!current || current->isEffectivelyDead() // RW 0x8A0124: Object + 0x458 bit 0
		|| !canGiveUpgradeTo(*current))          // RW 0x8A0131 -> RW 0x89FE64
	{
		// RW 0x8A013A .. 0x8A014A: the search flag clears, the ability ends and the AI idles.
		m_searching = false;
		finishAbility(false, true); // RW 0x8513E4
		aiIdleFromAI();             // RW 0x8A014A -> RW 0x5E821A(2)
		return result;
	}
	return result;
}

void GiveUpgradeUpdate::finishAbility(bool ownerDying, bool aborted)
{
	releaseTarget(); // RW 0x89FD5F -> RW 0x89FD15
	SpecialAbilityUpdate::finishAbility(ownerDying, aborted); // RW 0x89FD6E -> RW 0x8530BA
}

void GiveUpgradeUpdate::crc(StateHasher &h) const { SpecialAbilityUpdate::crc(h); h.addBool(m_fading); h.addFloat(m_opacity); h.addBool(m_searching); }
// RW 0x0089FB9F: base SpecialAbilityUpdate::xfer (0x852782), isLightCRC early-out, v1, m_fading (+0x88), m_opacity (+0x8C), m_searching (+0x89)
void GiveUpgradeUpdate::xfer(Xfer &x) { SpecialAbilityUpdate::xfer(x); SnapshotXfer::version(x, 1, 1); x.xferBool(&m_fading); x.xferReal(&m_opacity); x.xferBool(&m_searching); }
// RW 0x0089FB79 (getModuleName), RW 0x0089FBF4 (getModuleNameKey), RW 0x0089FC39 / RW 0x0089FB7F (dtor)
void GiveUpgradeUpdate::registerClass(ModuleFactory &factory)
{
	// RW 0x89FB14 (GiveUpgradeUpdate::buildFieldParse)
	factory.bindTypedData<Data>("GiveUpgradeUpdate", MODULETYPE_BEHAVIOR);
	factory.bindModuleProc("GiveUpgradeUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const Data *>(data);
		if (!typed) throw std::logic_error("GiveUpgradeUpdate: the module data is not typed");
		return std::make_unique<GiveUpgradeUpdate>(thing, typed);
	});
}
