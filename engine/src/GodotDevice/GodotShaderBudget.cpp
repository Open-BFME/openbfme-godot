// OpenBFME. GPL-3.0. See GodotShaderBudget.h.

#include "GodotDevice/GodotShaderBudget.h"

#include "Common/ShaderVaryings.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <string>

namespace godot
{
namespace
{

std::string toNative(const String &s)
{
	const CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

String toGodot(const std::string &s) { return String::utf8(s.c_str(), (int64_t)s.size()); }

//  #include "res://..." files are read through Godot's file system (a .gdshaderinc inside the PCK as well).
bool loadProjectFile(const std::string &path, std::string &out)
{
	Ref<FileAccess> file = FileAccess::open(String("res://") + String(path.c_str()), FileAccess::READ);
	if (file.is_null())
	{
		return false;
	}
	out = toNative(file->get_as_text());
	return true;
}

void report(const std::string &message) { UtilityFunctions::push_error(toGodot(message)); }

} // namespace

bool ShaderBudget::check(const String &code, const String &name)
{
	return ShaderVaryings::Guard(name.is_empty() ? std::string("shader") : toNative(name), toNative(code), "inline.gdshader", loadProjectFile, report);
}

void SetShaderCode(Shader *shader, const String &code, const char *name) { SetShaderCode(shader, code, String(name)); }

void SetShaderCode(Shader *shader, const String &code, const String &name)
{
	ShaderBudget::check(code, name);
	shader->set_code(code); // always the caller's code, counted or not
}

void ShaderBudget::_bind_methods()
{
	ClassDB::bind_static_method("ShaderBudget", D_METHOD("check", "code", "name"), &ShaderBudget::check);
}

} // namespace godot
