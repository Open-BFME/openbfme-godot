// OpenBFME. GPL-3.0.
//
// The ClientBehavior module classes the port runs (lane AUDIO-4), registered with the game's module factory next to the logic's classes (LogicModules).
// The declaration only: the classes live in GameClient/ (client presentation).
#pragma once

class ModuleFactory;

namespace ClientBehaviorModules
{
// AnimationSoundClientBehavior (GameClient/AnimationSoundClientBehavior.h)
void registerAll(ModuleFactory &modules);
} // namespace ClientBehaviorModules
