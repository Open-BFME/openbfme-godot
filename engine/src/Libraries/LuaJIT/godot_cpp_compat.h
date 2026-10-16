// OpenBFME. GPL-3.0. Lane LUAGD-1: forced into every lua-gdextension translation unit (cmake/LuaGDExtension.cmake). Upstream 0.8.2 is
// written against godot-cpp's 4.6 branch, whose ref_counted.hpp defines Ref<T>; godot-cpp 10 (this build) moved it to ref.hpp.
#pragma once
#ifdef __cplusplus
#include <godot_cpp/classes/ref.hpp>
#endif
