// OpenBFME. GPL-3.0.
// See GameLogic/ObjectSounds.h (stop S-707). Client presentation: reads the logic's state, changes none of it.

#include "GameLogic/ObjectSounds.h"

#include "Common/AsciiString.h"
#include "Common/Audio/AudioRequests.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/SoundSelectorClientBehavior.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

#include <variant>

namespace
{
class NoRoll : public SoundSelectors::RandomRoll
{
public:
	float roll() override { return 1.0f; } // never below a Chance: the random selectors do not answer (ObjectSounds.h)
};
} // namespace

std::string ObjectSounds::templateRow(const ThingTemplate &tt, const char *row)
{
	const FieldValue *f = tt.getFinalOverride()->findField(row);
	const RawTokens *raw = f ? std::get_if<RawTokens>(f) : nullptr;
	if (!raw || raw->tokens.empty() || AsciiStringUtil::compareNoCase(raw->tokens.front(), "NoSound") == 0)
	{
		return std::string();
	}
	const std::string &token = raw->tokens.front();
	if (token.size() >= 7 && AsciiStringUtil::compareNoCase(token.substr(0, 7), "+SOUND:") == 0)
	{
		return token.substr(7);
	}
	return token;
}

std::string ObjectSounds::objectRow(const Object &obj, const char *row)
{
	const ThingTemplate &fin = *static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride();
	if (SoundSelectors::any(fin))
	{
		SoundSelector::Query q;
		q.object = &obj;
		q.drawableFlags = &obj.getModelConditionBits();
		q.clientFrame = obj.logic().getFrame();
		SoundSelectors::RandomStates states;
		NoRoll roll;
		SoundSelector::Record r;
		if (SoundSelectors::row(fin, q, SoundSelector::rowIndex(row), states, roll, &r))
		{
			return r.sound;
		}
	}
	return templateRow(fin, row);
}

void ObjectSounds::playRow(const Object &obj, const char *row, const Player *owner)
{
	const std::string sound = objectRow(obj, row);
	if (!sound.empty())
	{
		AudioApi::playSoundForObject(sound, obj.getID(), owner ? owner->getPlayerIndex() : -1);
	}
}

void ObjectSounds::playRow(const Object &obj, const char *row)
{
	playRow(obj, row, obj.getControllingPlayer());
}
