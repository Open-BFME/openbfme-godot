// OpenBFME. GPL-3.0.
//
// DrawableAudioLoop: the drawable's list of ModelConditionAudioLoopClientBehavior modules (RW 0x67449C: the list + 0x158 the flush walks) and the flush's call
// into it (RW 0x8D04CA). CLIENT ONLY: it reads the drawable's model condition flags and plays on the audio manager through the module; it never reaches the
// simulation (tools/sim/sim_policy.json excludes it).

#include "GameClient/Drawable.h"
#include "GameClient/ModelConditionAudioLoopClientBehavior.h"

void Drawable::trackAudioLoop(DrawableModule *module)
{
	if (auto *loop = dynamic_cast<ModelConditionAudioLoopClientBehavior *>(module))
	{
		m_audioLoops.push_back(loop);
	}
}

void Drawable::notifyAudioLoops(const ModelConditionFlags &flags)
{
	for (ModelConditionAudioLoopClientBehavior *loop : m_audioLoops)
	{
		loop->modelConditionFlagsChanged(flags);
	}
}
