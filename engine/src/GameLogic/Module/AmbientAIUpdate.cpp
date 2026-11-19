// OpenBFME. GPL-3.0.
#include "GameLogic/Module/AmbientAIUpdate.h"
#include "Common/ModelState.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "Common/Thing/RawModuleData.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include <sstream>
#include <cctype>
#include <stdexcept>

namespace
{
// TARGET: Coord3D::GetLengthEstimate, RW 0x4035AC: |the larger of x, y| + 0.25 * |the smaller|, then the same with that result and |z|
// (0.25 is the float at RW 0xBD1904; every step is stored as a float32, the x87 runs at 24-bit precision).
float lengthEstimate3D(float x, float y, float z)
{
	const auto absf = [](float a) { return a < 0.0f ? SimMath::subf32(0.0f, a) : a; };
	const auto estimate = [](float a, float b) { return a > b ? SimMath::addf32(a, SimMath::mulf32(b, 0.25f)) : SimMath::addf32(b, SimMath::mulf32(a, 0.25f)); };
	return estimate(estimate(absf(x), absf(y)), absf(z));
}
} // namespace

const char *AmbientAIUpdate::stopLine()
{
	return "[S-5910] ambient wandering is ported from RotWK AnimalAIUpdate::update (RW 0x882058) and WanderAIUpdate::update (RW 0x8ACAFA): the idle gate, the RNG draws and their order, the SALT_LICK scan, the return-to-origin test (Coord3D::GetLengthEstimate, RW 0x4035AC), ConditionForEntry, and the enemy scan of every UpdateTimer frame with its not-an-ally, alive and KindOf filters. Emulated, not ported: the flee itself, aiFleeFromObject (RW 0x881FF3) becoming a step of FleeDistance away with LOCOMOTORSET_PANIC, and the re-issue suppression by AI state 19 or 20 (S-221). Not ported: the special-layer check (RW 0x881FCC, with the returning state) and WanderAIUpdate WildBeast acquisition, which reports this stop and issues no wander order; the ordinary pathfinder validates wander destinations.";
}

AmbientAIUpdate::AmbientAIUpdate(Thing *thing, const ModuleData *data, bool animal)
	: AIUpdateInterface(thing, data), m_animal(animal)
{
	// DONOR: BFME2 AnimalAIUpdateModuleDataCtor (0x64BE43), WanderAIUpdateModuleDataCtor
	// (0x64F1CD), their field tables, and their update bodies. Defaults are not guessed.
	if (!animal) m_distance = 30;
	if (const auto *raw = dynamic_cast<const RawModuleData *>(data))
	{
		for (const auto &line : raw->lines())
		{
			std::string text = line.text.substr(0, line.text.find(';'));
			const auto eq = text.find('=');
			if (eq == std::string::npos) continue;
			std::istringstream keyIn(text.substr(0, eq)), valueIn(text.substr(eq + 1));
			std::string key, value;
			keyIn >> key; valueIn >> value;
			for (char &c : key) c = (char)std::tolower((unsigned char)c);
			int *field = nullptr;
			if (key == "wanderpercentage") field = &m_percentage;
			else if (key == "maxwanderdistance" || key == "wanderdistance") field = &m_distance;
			else if (key == "maxwanderradius") field = &m_radius;
			else if (key == "fleerange") field = &m_range;
			// TARGET: RW AnimalAIUpdate's field table: FleeDistance is the int at data + 0x68 (default 100 at RW 0x64BB4D)
			// and UpdateTimer the unsigned int at data + 0x78 (row RW 0xC04F38, parser RW 0x42EC5E, default 10 at
			// RW 0x64BB3D), read by the enemy scan of update (RW 0x882288 mov ecx, [eax+0x78]).
			else if (key == "fleedistance") field = &m_fleeDistance;
			else if (key == "updatetimer") field = &m_updateTimer;
			if (field)
			{
				std::istringstream number(value);
				if (!(number >> *field)) throw std::logic_error("AmbientAIUpdate: invalid " + key + " = " + value);
			}
			else if (key == "conditionforentry")
			{
				// TARGET: RW WanderAIUpdate field table 0xC05828 -> parser RW 0x869F22 (shared with TransportContain): the first token (separators
				// space, tab, colon) must be "ModelConditionState", the next one is looked up in the model condition names (RW 0x4B3B5B, -1 when unknown:
				// no gate). RW 0x8ACAFA tests the object's model condition bit (RW 0x46E918), not a KindOf.
				std::istringstream tokens(text.substr(eq + 1));
				std::string tag, name;
				const auto next = [&tokens](std::string &out) {
					out.clear();
					int c;
					while ((c = tokens.peek()) != EOF && (c == ' ' || c == '\t' || c == ':')) tokens.get();
					while ((c = tokens.peek()) != EOF && c != ' ' && c != '\t' && c != ':') out += (char)tokens.get();
				};
				next(tag); next(name);
				if (tag != "ModelConditionState") throw std::logic_error("AmbientAIUpdate: ConditionForEntry expects 'ModelConditionState <name>', got " + tag); // RW 0xC5A644
				m_condition = ModelCondition::indexOf(name);
			}
			else if (key == "wildbeast" || key == "selectable")
			{
				for (char &c : value) c = (char)std::tolower((unsigned char)c);
				if (value != "yes" && value != "no" && value != "true" && value != "false" && value != "1" && value != "0") throw std::logic_error("AmbientAIUpdate: invalid boolean " + value);
				(key == "wildbeast" ? m_wildBeast : m_selectable) = value == "yes" || value == "true" || value == "1";
			}
		}
	}
	getObject()->logic().noteStop(stopLine());
}

UpdateSleepTime AmbientAIUpdate::update()
{
	// TARGET: RW AnimalAIUpdate::update 0x882058 (2916 bytes), checked against
	// BFME2 donor 0x87EDC9: same instruction sequence after image addresses,
	// source paths and the +0xC4 runtime-layout shift are normalized. Capture the state BEFORE the base
	// update; first processed position AFTER it; one [0,100] draw per idle update,
	// then angle [-15,15] and distance [0,MaxWanderDistance] only on success.
	// RW WanderAIUpdate::update 0x8ACAFA (499 bytes) matches BFME2 0x8A9424
	// except image addresses and source paths. See docs/AMBIENT_MOVEMENT.md.
	// S-001: the available target image has disputed provenance.
	const unsigned state = currentStateId();
	AIUpdateInterface::update();
	Object &obj = *getObject();
	if (!m_processed) { m_origin = *obj.getPosition(); m_processed = true; }
	GameLogic &logic = obj.logic();
	Coord3D dest = *obj.getPosition();
	// S-5910 / lane PORT-7: the enemy scan of RW AnimalAIUpdate::update, and it runs BEFORE the idle test (both skip
	// branches of its gate land on RW 0x882508, the idle test, so a moving animal scans too). The gate is the logic frame
	// counter divided by UpdateTimer: RW 0x882288 mov ecx,[data+0x78] / RW 0x88228B test ecx,ecx / RW 0x882292 jbe
	// RW 0x882508 (0: never scans) / RW 0x882298 mov eax,[RW 0xDE412C] (TheGameLogic) / RW 0x88229D mov eax,[eax+0x40]
	// (the frame) / RW 0x8822A2 div ecx / RW 0x8822A4 test edx,edx / RW 0x8822A6 jne RW 0x882508 (remainder: not this
	// frame). The gate covers the scan alone, so the wander dice below still run on every idle frame whatever the timer.
	if (m_animal && m_updateTimer > 0 && (logic.getFrame() % (unsigned)m_updateTimer) == 0)
	{
		// The scan: RW 0x882398 call RW 0xA39090 (ThePartitionManager getClosestObject) with the object's position
		// (+ 0x38, RW 0x88238F), (float)FleeRange (+ 0x64, RW 0x882381), 0 at RW 0x882386 = FROM_CENTER_2D, and a chain of
		// three filters linked at RW 0x88236D / RW 0x882378 (RW 0xA394C0 appends): the relationship filter
		// (Rva00260EB1Filter, vftable RW 0xC11DC0) with flags 3 (RW 0x88235B) and the direction byte 0 (RW 0x882362) -
		// relationship(animal, other) is one of the flags' bits, bit (1 << Relationship), so 3 is ENEMIES (0) and NEUTRAL
		// (1), everything that is not an ally; the alive filter (vftable RW 0xC10E20, built at RW 0x88233A); and the KindOf
		// mask { 8, 9, 10, 11 } (RW 0x882324 .. RW 0x88232C) = INFANTRY, CAVALRY, MONSTER, MACHINE in the retail name table
		// (RW 0xDA0E68): a structure or a tree is no threat.
		static const int kThreatKinds[] = { ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY"), ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY"),
			ObjectTemplateInfoBuilder::kindOfIndex("MONSTER"), ObjectTemplateInfoBuilder::kindOfIndex("MACHINE") };
		PartitionFilterFn notAlly([&obj](Object &other) {
			const Relationship rel = obj.getRelationship(other);
			return rel == ENEMIES || rel == NEUTRAL;
		});
		PartitionFilterFn alive([](Object &other) { return !other.isEffectivelyDead(); });
		PartitionFilterFn threatKind([](Object &other) {
			for (const int kind : kThreatKinds)
			{
				if (kind >= 0 && other.isKindOf((unsigned)kind)) return true;
			}
			return false;
		});
		if (Object *enemy = logic.partition().getClosestObject(dest, (float)m_range, FROM_CENTER_2D, { &notAlly, &alive, &threatKind }))
		{
			// S-5910: the flee itself is emulated, not ported. Retail writes the module's FleeDistance into the object's own
			// flee distance (RW 0x88249B cvtsi2ss [data+0x68] / RW 0x8824A0 movss [object+0x1AC]) and calls its own
			// aiFleeFromObject (RW 0x8824BC call RW 0x881FF3, with the enemy from GameLogic::findObjectByID RW 0x8824B3 and
			// command source 1 = CMD_FROM_SCRIPT at RW 0x8824AE), then remembers it (RW 0x8824C4, object + 0x49C). The port
			// has no flee order and no panic state, so it steps FleeDistance away from the enemy with LOCOMOTORSET_PANIC and
			// aiMoveToPosition(CMD_FROM_AI). The same enemy is not suppressed: retail skips the re-issue while the animal is
			// in state 19 or 20 (RW 0x8823C8 / RW 0x8823D5 call RW 0x660AC1), states this port does not have (S-221).
			const Coord3D *from = enemy->getPosition();
			float dx = SimMath::subf32(dest.x, from->x), dy = SimMath::subf32(dest.y, from->y);
			const float len = SimMath::length2d(dx, dy);
			if (len > 0.0f) { dx = SimMath::divf32(dx, len); dy = SimMath::divf32(dy, len); }
			else { dx = 1.0f; dy = 0.0f; } // the emulation's own guard: an enemy exactly below an animal flees east
			dest.x = SimMath::addf32(dest.x, SimMath::mulf32(dx, (float)m_fleeDistance));
			dest.y = SimMath::addf32(dest.y, SimMath::mulf32(dy, (float)m_fleeDistance));
			dest.z = 0.0f;
			chooseLocomotorSet(LOCOMOTORSET_PANIC);
			aiMoveToPosition(dest, CMD_FROM_AI);
			return UPDATE_SLEEP_NONE;
		}
	}
	if (m_animal ? state != AI_IDLE : !isIdle()) return UPDATE_SLEEP_NONE;
	if (!m_animal && m_condition >= 0 && !obj.testModelCondition(m_condition)) return UPDATE_SLEEP_NONE; // RW 0x8ACAFA: bit test RW 0x46E918
	if (!m_animal)
	{
		if (m_wildBeast) { logic.noteStop(stopLine()); return UPDATE_SLEEP_NONE; }
		if (!m_selectable) obj.setStatus(3, true); // donor WanderAIUpdate: NOT_SELECTABLE
		dest.x = SimMath::pc24Add(dest.x, (float)logic.random().getValue(-m_distance, m_distance, "WanderAIUpdate.cpp", 0x79));
		dest.y = SimMath::pc24Add(dest.y, (float)logic.random().getValue(-m_distance, m_distance, "WanderAIUpdate.cpp", 0x7a));
	}
	else
	{
		if (logic.random().getValue(0, 100, "AnimalAIUpdate.cpp", 0x9a) >= m_percentage) return UPDATE_SLEEP_NONE;
		static const int emitterKind = ObjectTemplateInfoBuilder::kindOfIndex("SALT_LICK");
		PartitionFilterFn emitterFilter([](Object &o) { return emitterKind >= 0 && o.isKindOf((unsigned)emitterKind); });
		Object *emitter = logic.partition().getClosestObject(dest, (float)m_range, FROM_CENTER_3D, { &emitterFilter });
		const Coord3D center = emitter ? *emitter->getPosition() : m_origin;
		const float dx = SimMath::subf32(center.x, dest.x), dy = SimMath::subf32(center.y, dest.y), dz = SimMath::subf32(center.z, dest.z);
		const float radius = (float)m_radius;
		const bool returnToCenter = emitter ? SimMath::sumSquares2(dx, dy) > SimMath::mulf32(radius, radius) : lengthEstimate3D(dx, dy, dz) > radius;
		chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		if (returnToCenter) dest = center;
		else
		{
			const int angleLine = emitter ? 0xbc : 0xf2, distanceLine = emitter ? 0xbf : 0xf5;
			const float angle = (float)logic.random().getValue(-15, 15, "AnimalAIUpdate.cpp", angleLine);
			const float distance = (float)logic.random().getValue(0, m_distance, "AnimalAIUpdate.cpp", distanceLine);
			// The donor adds radians directly (no degrees conversion) to the direction vector.
			const Coord2D dir = locomotorHost().getUnitDirectionVector2D();
			dest.x = SimMath::pc24Add(SimMath::pc24Mul(SimMath::pc24Add(dir.x, SimMath::cosDet(angle)), distance), dest.x);
			dest.y = SimMath::pc24Add(SimMath::pc24Mul(SimMath::pc24Add(dir.y, SimMath::sinDet(angle)), distance), dest.y);
			dest.z = 0.0f;
		}
	}
	aiMoveToPosition(dest, CMD_FROM_AI);
	return UPDATE_SLEEP_NONE;
}

void AmbientAIUpdate::crc(StateHasher &h) const
{
	AIUpdateInterface::crc(h);
	h.addBool(m_processed);
	h.addFloat(m_origin.x); h.addFloat(m_origin.y); h.addFloat(m_origin.z);
}
void AmbientAIUpdate::xferAIStateBody(Xfer &x)
{
	AIUpdateInterface::xferAIStateBody(x);
	XferVersion v{ 1, 1 }; x.xferVersion(v);
	x.xferBool(&m_processed);
	AIXfer::coord(x, m_origin);
}

void AmbientAIUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 }; x.xferVersion(v);
	AIUpdateInterface::xfer(x); // runtime fields travel in obfme:AIWorld via xferAIStateBody
}
