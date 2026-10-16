// OpenBFME. GPL-3.0. Lane LUAGD-1: lua-gdextension's classes in the openbfme extension (LuaGDExtensionRegister.cpp).
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>

namespace LuaGDExtension
{
// at MODULE_INITIALIZATION_LEVEL_SCENE (register_types.cpp)
void initialize();
void uninitialize();
// A new LuaState for the bfme.* API (BfmeLuaRuntime runs on its lua_State, returned in *luaState): Godot's Variant types (Vector3,
// Color, String methods ...) and global enums are opened; no Lua library (the runtime opens its own sandboxed set) and none of the
// addon's ways out of the sandbox: Godot classes, singletons, res:// / user:// file loading, and the utility functions (they include
// bytes_to_var_with_objects, which makes arbitrary objects). The Ref keeps the state alive.
godot::Ref<godot::RefCounted> newBfmeState(void **luaState);
} // namespace LuaGDExtension
