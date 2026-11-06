// OpenBFME. GPL-3.0. See Common/ShaderVaryings.h.

#include "Common/ShaderVaryings.h"

#include <cctype>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace ShaderVaryings
{
namespace
{

bool IsIdentStart(char c) { return std::isalpha((unsigned char)c) != 0 || c == '_'; }
bool IsIdentChar(char c) { return std::isalnum((unsigned char)c) != 0 || c == '_'; }

// Copies the text with // and /* */ comments blanked (newlines kept), but keeps string literals intact: a
// "res://..." include path is not a comment.
std::string StripComments(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size();)
	{
		const char c = text[i];
		if (c == '"')
		{
			out.push_back(text[i++]);
			while (i < text.size())
			{
				const char s = text[i++];
				out.push_back(s);
				if (s == '\\' && i < text.size())
				{
					out.push_back(text[i++]);
				}
				else if (s == '"')
				{
					break;
				}
			}
		}
		else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
		{
			while (i < text.size() && text[i] != '\n')
			{
				++i;
			}
		}
		else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
		{
			i += 2;
			while (i < text.size())
			{
				if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/')
				{
					i += 2;
					break;
				}
				if (text[i] == '\n')
				{
					out.push_back('\n');
				}
				++i;
			}
		}
		else
		{
			out.push_back(c);
			++i;
		}
	}
	return out;
}

struct Token
{
	enum Kind
	{
		Identifier,
		Number,
		String,
		Punct
	};
	Kind kind = Punct;
	std::string text;
};

std::vector<Token> Tokenize(const std::string &text)
{
	std::vector<Token> tokens;
	for (size_t i = 0; i < text.size();)
	{
		const char c = text[i];
		if (std::isspace((unsigned char)c))
		{
			++i;
		}
		else if (IsIdentStart(c))
		{
			const size_t start = i;
			while (i < text.size() && IsIdentChar(text[i]))
			{
				++i;
			}
			tokens.push_back({ Token::Identifier, text.substr(start, i - start) });
		}
		else if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < text.size() && std::isdigit((unsigned char)text[i + 1])))
		{
			const size_t start = i;
			while (i < text.size() && (std::isalnum((unsigned char)text[i]) || text[i] == '.'))
			{
				++i;
			}
			tokens.push_back({ Token::Number, text.substr(start, i - start) });
		}
		else if (c == '"')
		{
			const size_t start = i++;
			while (i < text.size())
			{
				const char s = text[i++];
				if (s == '\\' && i < text.size())
				{
					++i;
				}
				else if (s == '"')
				{
					break;
				}
			}
			tokens.push_back({ Token::String, text.substr(start, i - start) });
		}
		else
		{
			tokens.push_back({ Token::Punct, std::string(1, c) });
			++i;
		}
	}
	return tokens;
}

int MatrixSlots(const std::string &type)
{
	if (type == "mat2") return 2;
	if (type == "mat3") return 3;
	if (type == "mat4") return 4;
	return 1;
}

bool IsVaryingType(const std::string &type)
{
	static const char *const kTypes[] = { "float", "vec2", "vec3", "vec4", "int", "ivec2", "ivec3", "ivec4", "uint", "uvec2", "uvec3", "uvec4",
		"bool", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4" };
	for (const char *t : kTypes)
	{
		if (type == t)
		{
			return true;
		}
	}
	return false;
}

bool IsQualifier(const std::string &word) { return word == "flat" || word == "smooth" || word == "lowp" || word == "mediump" || word == "highp"; }

std::string JoinPath(const std::string &parent, const std::string &include)
{
	if (include.rfind("res://", 0) == 0)
	{
		return include.substr(6);
	}
	std::vector<std::string> parts;
	size_t start = 0;
	while (start <= parent.size() && !parent.empty())
	{
		const size_t slash = parent.find('/', start);
		const std::string part = parent.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (!part.empty())
		{
			parts.push_back(part);
		}
		if (slash == std::string::npos)
		{
			break;
		}
		start = slash + 1;
	}
	if (!parts.empty())
	{
		parts.pop_back(); // the including file itself
	}
	start = 0;
	while (start <= include.size())
	{
		const size_t slash = include.find('/', start);
		const std::string part = include.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (part == "..")
		{
			if (!parts.empty())
			{
				parts.pop_back();
			}
		}
		else if (!part.empty() && part != ".")
		{
			parts.push_back(part);
		}
		if (slash == std::string::npos)
		{
			break;
		}
		start = slash + 1;
	}
	std::string joined;
	for (const std::string &part : parts)
	{
		joined += joined.empty() ? part : "/" + part;
	}
	return joined;
}

std::vector<std::string> Includes(const std::string &text)
{
	std::vector<std::string> includes;
	size_t lineStart = 0;
	while (lineStart <= text.size())
	{
		size_t lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string::npos)
		{
			lineEnd = text.size();
		}
		size_t i = lineStart;
		while (i < lineEnd && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r'))
		{
			++i;
		}
		if (i < lineEnd && text[i] == '#')
		{
			++i;
			while (i < lineEnd && (text[i] == ' ' || text[i] == '\t'))
			{
				++i;
			}
			const size_t wordStart = i;
			while (i < lineEnd && IsIdentChar(text[i]))
			{
				++i;
			}
			if (text.substr(wordStart, i - wordStart) == "include")
			{
				while (i < lineEnd && (text[i] == ' ' || text[i] == '\t'))
				{
					++i;
				}
				if (i < lineEnd && text[i] == '"')
				{
					const size_t begin = ++i;
					while (i < lineEnd && text[i] != '"')
					{
						++i;
					}
					if (i < lineEnd)
					{
						includes.push_back(text.substr(begin, i - begin));
					}
				}
			}
		}
		if (lineEnd == text.size())
		{
			break;
		}
		lineStart = lineEnd + 1;
	}
	return includes;
}

// Appends `text`'s varying declarations; false + error on a declaration this counter cannot read.
bool ParseVaryings(const std::string &file, const std::string &text, std::vector<Varying> &out, std::string &error)
{
	const std::vector<Token> tokens = Tokenize(text);
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (tokens[i].kind != Token::Identifier || tokens[i].text != "varying")
		{
			continue;
		}
		size_t j = i + 1;
		while (j < tokens.size() && tokens[j].kind == Token::Identifier && IsQualifier(tokens[j].text))
		{
			++j;
		}
		if (j >= tokens.size() || tokens[j].kind != Token::Identifier || !IsVaryingType(tokens[j].text))
		{
			error = file + ": cannot read the varying declaration after `varying`";
			return false;
		}
		const int typeSlots = MatrixSlots(tokens[j].text);
		++j;
		while (true)
		{
			if (j >= tokens.size() || tokens[j].kind != Token::Identifier)
			{
				error = file + ": cannot read a varying name";
				return false;
			}
			const std::string name = tokens[j++].text;
			int count = 1;
			if (j < tokens.size() && tokens[j].text == "[")
			{
				++j;
				if (j >= tokens.size() || tokens[j].kind != Token::Number)
				{
					error = file + ": cannot read the array length of varying `" + name + "`";
					return false;
				}
				count = std::atoi(tokens[j++].text.c_str());
				if (count <= 0 || j >= tokens.size() || tokens[j].text != "]")
				{
					error = file + ": cannot read the array length of varying `" + name + "`";
					return false;
				}
				++j;
			}
			out.push_back({ file, name, count * typeSlots });
			if (j >= tokens.size())
			{
				error = file + ": the varying declaration `" + name + "` has no `;`";
				return false;
			}
			if (tokens[j].text == ",")
			{
				++j;
				continue;
			}
			if (tokens[j].text == ";")
			{
				++j;
				break;
			}
			error = file + ": cannot read the varying declaration `" + name + "`";
			return false;
		}
		i = j - 1;
	}
	return true;
}

int BaseIndex(const std::string &type)
{
	if (type == "spatial") return kSpatialBase;
	if (type == "canvas_item") return kCanvasItemBase;
	if (type == "sky") return kSkyBase;
	if (type == "particles") return kParticlesBase;
	if (type == "fog") return kFogBase;
	return -1;
}

} // namespace

Result Count(const std::string &code, const std::string &path, const Loader &load)
{
	Result result;
	const std::string root = StripComments(code);
	const std::vector<Token> rootTokens = Tokenize(root);
	for (size_t i = 0; i + 2 < rootTokens.size(); ++i)
	{
		if (rootTokens[i].kind == Token::Identifier && rootTokens[i].text == "shader_type" && rootTokens[i + 1].kind == Token::Identifier &&
			rootTokens[i + 2].text == ";")
		{
			result.shaderType = rootTokens[i + 1].text;
			break;
		}
	}
	if (result.shaderType.empty())
	{
		result.error = path + ": no shader_type";
		return result;
	}
	result.base = BaseIndex(result.shaderType);
	if (result.base < 0)
	{
		result.error = path + ": unknown shader_type " + result.shaderType;
		return result;
	}

	std::set<std::string> seen;
	std::vector<std::pair<std::string, std::string>> pending;
	pending.push_back({ path, code });
	while (!pending.empty())
	{
		const std::string file = pending.back().first;
		const std::string text = pending.back().second;
		pending.pop_back();
		if (!seen.insert(file).second)
		{
			continue;
		}
		const std::string stripped = StripComments(text);
		if (!ParseVaryings(file, stripped, result.varyings, result.error))
		{
			return result;
		}
		const std::vector<std::string> includes = Includes(stripped);
		for (auto it = includes.rbegin(); it != includes.rend(); ++it)
		{
			const std::string resolved = JoinPath(file, *it);
			std::string included;
			if (!load(resolved, included))
			{
				result.error = file + ": cannot read include `" + *it + "` (" + resolved + ")";
				return result;
			}
			pending.push_back({ resolved, included });
		}
	}
	int used = result.base;
	for (const Varying &varying : result.varyings)
	{
		used += varying.slots;
	}
	result.used = used;
	result.ok = true;
	return result;
}

std::string Problem(const Result &result, const std::string &name)
{
	if (!result.ok)
	{
		return name + ": cannot count varyings: " + result.error;
	}
	if (result.used <= kMaxVaryings)
	{
		return std::string();
	}
	std::string listed;
	for (const Varying &varying : result.varyings)
	{
		if (!listed.empty())
		{
			listed += ", ";
		}
		listed += varying.name + " (" + std::to_string(varying.slots) + ", " + varying.file + ")";
	}
	return name + ": " + std::to_string(result.used) + " varyings (" + result.shaderType + " base " + std::to_string(result.base) +
		" + yours), the limit is " + std::to_string(kMaxVaryings) +
		" (Intel GPUs; NVIDIA/AMD allow 32, so it only fails there): pack them into vec4s. Yours: " + listed;
}

bool Guard(const std::string &name, const std::string &code, const std::string &path, const Loader &load, const Reporter &report)
{
	const Result result = Count(code, path, load);
	const std::string problem = Problem(result, name);
	if (!problem.empty())
	{
		report(problem);
		return false;
	}
	return true;
}

int Used(const std::string &code, const std::string &path, const Loader &load, std::string &error)
{
	const Result result = Count(code, path, load);
	if (!result.ok)
	{
		error = result.error;
		return -1;
	}
	return result.used;
}

} // namespace ShaderVaryings
