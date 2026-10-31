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
	return "[S-5910] ambient wandering is ported from RotWK AnimalAIUpdate::update (RW 0x882058) and WanderAIUpdate::update (RW 0x8ACAFA): the idle gate, the RNG draws and their order, the SALT_LICK scan, the return-to-origin test (Coord3D::GetLengthEstimate, RW 0x4035AC) and ConditionForEntry. Not ported: the enemy flee scan every UpdateTimer frames, the special-layer check (RW 0x881FCC, with the returning state) and WanderAIUpdate WildBeast acquisition, which reports this stop and issues no wander order; the ordinary pathfinder validates wander destinations.";
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
	if (m_animal ? state != AI_IDLE : !isIdle()) return UPDATE_SLEEP_NONE;
	if (!m_animal && m_condition >= 0 && !obj.testModelCondition(m_condition)) return UPDATE_SLEEP_NONE; // RW 0x8ACAFA: bit test RW 0x46E918
	GameLogic &logic = obj.logic();
	Coord3D dest = *obj.getPosition();
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
