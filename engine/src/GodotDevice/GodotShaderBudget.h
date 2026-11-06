// OpenBFME. GPL-3.0.
//
// The runtime half of the shader varying budget (lane SHADER-BUDGET-1): every shader Godot compiles from a code
// string passes through SetShaderCode / ShaderBudget.check, which counts it with Common/ShaderVaryings.h first
// and pushes a Godot ERROR naming the shader, its count and the limit when it cannot fit Intel's 29 locations.
// The code handed to Godot is never changed: the error must show up in the session's error counter and logs on
// every GPU, not only where the driver rejects the shader.

#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot
{

class Shader;

class ShaderBudget : public Object
{
	GDCLASS(ShaderBudget, Object)

public:
	// GDScript entry point: bool ShaderBudget.check(code, name). False (and an ERROR) when the code is over budget
	// or cannot be counted; the caller still assigns its code unchanged.
	static bool check(const String &code, const String &name);

protected:
	static void _bind_methods();
};

// The one set_code path: counts, reports, then assigns `code` unchanged.
void SetShaderCode(Shader *shader, const String &code, const char *name);
void SetShaderCode(Shader *shader, const String &code, const String &name);

} // namespace godot
