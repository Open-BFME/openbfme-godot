// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/MapUtil.h.

#include "GameClient/MapUtil.h"

#include "Common/DataChunk.h"
#include "Libraries/Compression/CompressionManager.h"

#include <cstring>
#include <new>
#include <stdexcept>

namespace MapReader
{

bool load(const std::vector<std::uint8_t> &bytes, const std::string &sourceName, const MapReadOptions &options,
	LoadedMap &out, std::string *error)
{
	out = LoadedMap();
	out.sourceName = sourceName;
	out.storedSize = bytes.size();

	auto fail = [&](const std::string &msg) {
		if (error)
		{
			*error = sourceName + ": " + msg;
		}
		return false;
	};

	std::vector<std::uint8_t> decoded;
	const std::uint8_t *body = bytes.data();
	size_t bodySize = bytes.size();
	if (CompressionManager::isDataCompressed(bytes.data(), bytes.size()))
	{
		std::string e;
		bool decompressed = false;
		try
		{
			decompressed = CompressionManager::decompressData(bytes.data(), bytes.size(), decoded, &e);
		}
		catch (const std::bad_alloc &)
		{
			return fail("envelope: out of memory decompressing the RefPack stream");
		}
		if (!decompressed)
		{
			return fail("envelope: " + e);
		}
		body = decoded.data();
		bodySize = decoded.size();
		out.envelope = "EAR";
	}
	else
	{
		out.envelope = "raw"; // .scb libraries are stored raw ("CkMp" first)
	}
	out.decodedSize = bodySize;

	try
	{
		ChunkInputStream stream(body, bodySize);
		DataChunkInput file(&stream);
		if (!file.isValidFileType())
		{
			return fail("not a chunk file (no 'CkMp' table of contents)");
		}
		out.tocEntries = file.contents().size();

		file.registerParser("HeightMapData", "", WorldHeightMap::ParseHeightMapDataChunk, &out.heightMap);
		file.registerParser("BlendTileData", "", WorldHeightMap::ParseBlendTileDataChunk, &out.heightMap);
		MapChunkParse::registerParsers(file, &out.chunks);
		SidesListParse::registerParsers(file, &out.sides);
		ScriptParseContext scriptCtx;
		scriptCtx.playerScripts = &out.playerScripts;
		ScriptsParse::registerParsers(file, &scriptCtx);

		if (!file.parse(nullptr))
		{
			return fail("a chunk parser rejected the data");
		}

		for (const DataChunkRecord &r : file.chunkLog())
		{
			ChunkStat s;
			s.label = file.contents().getName(r.id);
			s.parentLabel = r.parentId ? file.contents().getName(r.parentId) : std::string();
			s.version = r.version;
			s.size = r.size;
			s.depth = r.depth;
			s.parsed = r.parsed;
			s.leftover = r.leftover;
			out.versions[s.parentLabel + "/" + s.label].insert(s.version);
			if (r.depth == 0)
			{
				out.topLevelOrder.push_back(s.label);
			}
			out.chunkLog.push_back(std::move(s));
		}
		for (const DataChunkInput::Issue &i : file.issues())
		{
			MapChunkIssue m;
			m.unknownChunk = i.kind == DataChunkInput::Issue::UnknownChunk;
			m.label = i.label;
			m.parentLabel = i.parentLabel;
			m.version = i.version;
			m.bytes = i.bytes;
			m.offset = i.offset;
			out.issues.push_back(std::move(m));
		}
	}
	catch (const MapParseError &e)
	{
		return fail(e.what());
	}
	catch (const std::bad_alloc &)
	{
		// the counts are validated against the payload first (WorldHeightMap.cpp requireBytes); this is the
		// backstop so that no allocation failure escapes the error path
		return fail("out of memory while parsing");
	}
	catch (const std::length_error &e)
	{
		return fail(std::string("size limit exceeded while parsing: ") + e.what());
	}

	out.stops = { "S-037", "S-038" };
	out.hasHeightMap = !out.heightMap.m_data.empty();
	out.hasPlayerScripts = out.playerScripts.version != 0;
	if (out.heightMap.m_hasBlendTileData)
	{
		out.heightMap.patchBadIndices();
	}

	for (const MapObject &o : out.chunks.objects)
	{
		if (o.m_objectName.empty())
		{
			out.warnings.push_back("object with an empty template name at (" + std::to_string(o.m_location.x) + ", "
				+ std::to_string(o.m_location.y) + "): retail finds no template and skips it");
		}
	}

	if (options.strict && !out.issues.empty())
	{
		const MapChunkIssue &i = out.issues.front();
		return fail(std::string(i.unknownChunk ? "unknown chunk '" : "leftover bytes in chunk '") + i.label + "' (parent '"
			+ i.parentLabel + "', v" + std::to_string(i.version) + ", " + std::to_string(i.bytes) + " bytes at offset "
			+ std::to_string(i.offset) + "); " + std::to_string(out.issues.size()) + " issue(s) in total");
	}
	return true;
}

} // namespace MapReader
