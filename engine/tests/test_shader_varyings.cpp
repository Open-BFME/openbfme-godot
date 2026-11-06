// OpenBFME unit tests: the shader varying counter and the runtime guard (Common/ShaderVaryings.h), the same rule
// as tools/check_shader_varyings.py: Godot 4.7.2's base index plus one location per user varying, Intel's limit 29.

#include "doctest.h"

#include "Common/ShaderVaryings.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{

std::string spatial(int user)
{
	std::string code = "shader_type spatial;\n";
	for (int i = 0; i < user; ++i)
	{
		code += "varying vec4 v" + std::to_string(i) + ";\n";
	}
	return code + "void fragment() {}\n";
}

ShaderVaryings::Loader mapLoader(const std::map<std::string, std::string> &files)
{
	return [files](const std::string &path, std::string &out) {
		const auto it = files.find(path);
		if (it == files.end())
		{
			return false;
		}
		out = it->second;
		return true;
	};
}

std::string readFile(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE_MESSAGE(file.good(), path);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string projectDir() { return std::string(OPENBFME_DOCS_DIR) + "/../godot"; }

bool loadProject(const std::string &rel, std::string &out)
{
	std::ifstream file(projectDir() + "/" + rel, std::ios::binary);
	if (!file.good())
	{
		return false;
	}
	out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return true;
}

} // namespace

TEST_CASE("shader varyings: the spatial budget is exactly fourteen user varyings")
{
	const ShaderVaryings::Result fits = ShaderVaryings::Count(spatial(14), "a.gdshader", mapLoader({}));
	REQUIRE(fits.ok);
	CHECK(fits.shaderType == "spatial");
	CHECK(fits.base == 15);
	CHECK(fits.used == 29);
	CHECK(ShaderVaryings::Problem(fits, "a.gdshader").empty());

	const ShaderVaryings::Result over = ShaderVaryings::Count(spatial(15), "a.gdshader", mapLoader({}));
	REQUIRE(over.ok);
	CHECK(over.used == 30);
	const std::string problem = ShaderVaryings::Problem(over, "a.gdshader");
	CHECK(problem.find("30 varyings") != std::string::npos);
	CHECK(problem.find("the limit is 29") != std::string::npos);
}

TEST_CASE("shader varyings: matrices and arrays take their Godot slots")
{
	const std::string code = "shader_type spatial;\nvarying mat3 m;\nvarying flat vec2 a[4];\nvarying highp float f, g;\n";
	const ShaderVaryings::Result result = ShaderVaryings::Count(code, "a.gdshader", mapLoader({}));
	REQUIRE(result.ok);
	CHECK(result.used == 15 + 3 + 4 + 2);
	REQUIRE(result.varyings.size() == 4);
	CHECK(result.varyings[0].name == "m");
	CHECK(result.varyings[0].slots == 3);
	CHECK(result.varyings[1].name == "a");
	CHECK(result.varyings[1].slots == 4);
	CHECK(result.varyings[2].name == "f");
	CHECK(result.varyings[3].name == "g");
}

TEST_CASE("shader varyings: includes count once and a res:// path is not a comment")
{
	const std::map<std::string, std::string> files = { { "shaders/inc.gdshaderinc", "varying vec3 v_inc;\nvarying vec2 v_two;\n" } };
	const std::string code = spatial(13) + "#include \"res://shaders/inc.gdshaderinc\"\n#include \"inc.gdshaderinc\"\n";
	const ShaderVaryings::Result result = ShaderVaryings::Count(code, "shaders/a.gdshader", mapLoader(files));
	REQUIRE(result.ok);
	CHECK(result.used == 30); // 15 + 13 root + 2 included once
	CHECK(result.varyings.size() == 15);
}

TEST_CASE("shader varyings: commented out varyings do not count")
{
	const std::string code = spatial(14) + "// varying vec4 gone;\n/* varying vec4 also_gone;\n*/\n";
	const ShaderVaryings::Result result = ShaderVaryings::Count(code, "a.gdshader", mapLoader({}));
	REQUIRE(result.ok);
	CHECK(result.used == 29);
}

TEST_CASE("shader varyings: the canvas_item base is nine")
{
	std::string code = "shader_type canvas_item;\n";
	for (int i = 0; i < 21; ++i)
	{
		code += "varying vec2 v" + std::to_string(i) + ";\n";
	}
	const ShaderVaryings::Result result = ShaderVaryings::Count(code, "c.gdshader", mapLoader({}));
	REQUIRE(result.ok);
	CHECK(result.used == 30);
	CHECK(ShaderVaryings::Problem(result, "c.gdshader").find("30 varyings") != std::string::npos);
}

TEST_CASE("shader varyings: a missing include is a problem, never a pass")
{
	const ShaderVaryings::Result result = ShaderVaryings::Count(spatial(1) + "#include \"res://nope.gdshaderinc\"\n", "a.gdshader", mapLoader({}));
	CHECK_FALSE(result.ok);
	const std::string problem = ShaderVaryings::Problem(result, "a.gdshader");
	CHECK(problem.find("cannot count varyings") != std::string::npos);
	CHECK(problem.find("nope.gdshaderinc") != std::string::npos);
}

TEST_CASE("shader varyings: the pre-fix terrain counts 31")
{
	// main's terrain_common.gdshaderinc declares twelve varyings; with terrain.gdshader's own three and
	// object_shadow.gdshaderinc's one, the spatial base of 15 takes the terrain to 31, over Intel's 29. The fix
	// packs six smooth terrain varyings into three vec4s (28). Fixture: that pre-fix godot/shaders/terrain_common.gdshaderinc.
	const std::string preFix = readFile(std::string(OPENBFME_TESTDATA_DIR) + "/terrain_common_pre_fix.gdshaderinc");
	std::string terrain;
	REQUIRE(loadProject("shaders/terrain.gdshader", terrain));
	const ShaderVaryings::Result result = ShaderVaryings::Count(terrain, "shaders/terrain.gdshader",
		[&preFix](const std::string &path, std::string &out) {
			if (path == "shaders/terrain_common.gdshaderinc")
			{
				out = preFix;
				return true;
			}
			return loadProject(path, out);
		});
	REQUIRE(result.ok);
	CHECK(result.used == 31);
	CHECK(ShaderVaryings::Problem(result, "shaders/terrain.gdshader").find("31 varyings") != std::string::npos);
}

TEST_CASE("shader varyings: the shipped .gdshader files fit")
{
	namespace fs = std::filesystem;
	int checked = 0;
	for (const fs::directory_entry &entry : fs::recursive_directory_iterator(projectDir()))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".gdshader")
		{
			continue;
		}
		const std::string rel = fs::relative(entry.path(), projectDir()).generic_string();
		if (rel.find(".godot/") != std::string::npos)
		{
			continue;
		}
		CAPTURE(rel);
		std::string text;
		REQUIRE(loadProject(rel, text));
		const ShaderVaryings::Result result = ShaderVaryings::Count(text, rel, loadProject);
		REQUIRE(result.ok);
		CHECK(result.used <= ShaderVaryings::kMaxVaryings); // tolerance: the budget is an upper bound; a shipped shader may use fewer.
		++checked;
	}
	CHECK(checked >= 6); // tolerance: at least the six shipped .gdshader files; any new one is checked too
}

TEST_CASE("shader varyings: the runtime guard reports an over-budget shader and never edits the code")
{
	const std::string code = spatial(15);
	const std::string counted = code;
	std::vector<std::string> reported;
	const bool ok = ShaderVaryings::Guard("test shader", code, "a.gdshader", mapLoader({}),
		[&reported](const std::string &message) { reported.push_back(message); });
	CHECK_FALSE(ok);
	REQUIRE(reported.size() == 1);
	CHECK(reported[0].find("test shader") != std::string::npos);
	CHECK(reported[0].find("30 varyings") != std::string::npos);
	CHECK(reported[0].find("the limit is 29") != std::string::npos);
	CHECK(code == counted); // the guard counts only: the text handed to Godot is the caller's

	reported.clear();
	CHECK(ShaderVaryings::Guard("test shader", spatial(14), "a.gdshader", mapLoader({}), [&reported](const std::string &message) { reported.push_back(message); }));
	CHECK(reported.empty());
}
