// OpenBFME parser fuzz targets. GPL-3.0. Each executable reaches the same in-memory
// parser used by the engine, with an independent corpus and coverage map.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#if OPENBFME_FUZZ_TARGET == 1
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Common/ArchiveFile.h"
#elif OPENBFME_FUZZ_TARGET == 2 || OPENBFME_FUZZ_TARGET == 3
#include "GameClient/GameText.h"
#elif OPENBFME_FUZZ_TARGET == 4
#include "Common/INI.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#elif OPENBFME_FUZZ_TARGET == 5
#include "GameClient/MapUtil.h"
#elif OPENBFME_FUZZ_TARGET == 6
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#elif OPENBFME_FUZZ_TARGET >= 7 && OPENBFME_FUZZ_TARGET <= 10
#include "Libraries/Source/Apt/AptFile.h"
#elif OPENBFME_FUZZ_TARGET == 11
#include "GameClient/TGAFile.h"
#elif OPENBFME_FUZZ_TARGET == 12
#include "Common/Audio/AudioDecode.h"
#elif OPENBFME_FUZZ_TARGET == 13
#include "Common/Recorder.h"
#elif OPENBFME_FUZZ_TARGET == 14
#include "Common/System/SaveGame/SaveGameFile.h"
#elif OPENBFME_FUZZ_TARGET == 15
#include "GameClient/CursorFile.h"
#elif OPENBFME_FUZZ_TARGET == 16
#include "GameClient/VP6Decoder.h"
#elif OPENBFME_FUZZ_TARGET == 17
#include "Common/MiniJson.h"
#elif OPENBFME_FUZZ_TARGET == 18
#include "Libraries/Compression/EAC/refdecode.h"
#elif OPENBFME_FUZZ_TARGET == 19
#include "GameClient/FontSubstitution.h"
#elif OPENBFME_FUZZ_TARGET == 20
#include "GameClient/MapCache.h"
#elif OPENBFME_FUZZ_TARGET == 21
#include "Libraries/file/TextFile.h"
#else
#error Unknown parser fuzz target
#endif

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    std::string error;
#if OPENBFME_FUZZ_TARGET == 1
    Win32BIGFileSystem fs;
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(data, data + size);
    auto archive = fs.openArchiveFromMemory("fuzz.big", bytes, &error);
    // Also exercise the archive's member reader, not just its directory.
    if (archive)
    {
        FilenameList names;
        archive->getFileListInDirectory("", "", "*", names, true);
        for (const auto &name : names)
        {
            std::vector<std::uint8_t> contents;
            archive->readFile(name, contents, &error);
        }
    }
#elif OPENBFME_FUZZ_TARGET == 2
    GameTextTable table;
    table.parseCSF(data, size, &error);
#elif OPENBFME_FUZZ_TARGET == 3
    GameTextTable table;
    table.parse(data, size, &error);
#elif OPENBFME_FUZZ_TARGET == 4
    Win32BIGFileSystem fs;
    RetailObjectWorld world(fs);
    auto context = world.enterContext();
    INI ini(world.iniEnvironment());
    try
    {
        ini.loadMemory("fuzz.ini", std::vector<std::uint8_t>(data, data + size), INI_LOAD_OVERWRITE);
    }
    catch (const INIException &)
    {
        // Syntax errors are the documented INI error channel.
    }
#elif OPENBFME_FUZZ_TARGET == 5
    LoadedMap map;
    MapReader::load(std::vector<std::uint8_t>(data, data + size), "fuzz.map", MapReadOptions{}, map, &error);
#elif OPENBFME_FUZZ_TARGET == 6
    W3DFileContents file;
    Load_W3D_File(data, size, file, &error);
#elif OPENBFME_FUZZ_TARGET == 7
    AptConstFile file;
    AptConstFile::parse(std::vector<std::uint8_t>(data, data + size), file, &error);
#elif OPENBFME_FUZZ_TARGET == 8
    // One fuzz unit contains both files: little-endian .const byte count, then .const, then .apt.
    if (size < 4) return 0;
    const std::size_t constSize = std::size_t(data[0]) | (std::size_t(data[1]) << 8) |
        (std::size_t(data[2]) << 16) | (std::size_t(data[3]) << 24);
    if (constSize > size - 4) return 0;
    AptConstFile consts;
    if (!AptConstFile::parse(std::vector<std::uint8_t>(data + 4, data + 4 + constSize), consts, &error)) return 0;
    AptFile file;
    if (AptFile::parse("fuzz", std::make_shared<const std::vector<std::uint8_t>>(data + 4 + constSize, data + size), consts, file, &error))
    {
        for (const auto offset : file.programOffsets())
        {
            file.codeAt(offset, &error);
        }
    }
#elif OPENBFME_FUZZ_TARGET == 9
    AptImageMap map;
    AptImageMap::parse(std::vector<std::uint8_t>(data, data + size), map, &error);
#elif OPENBFME_FUZZ_TARGET == 10
    AptGeometry geometry;
    AptGeometry::parse(std::vector<std::uint8_t>(data, data + size), geometry, &error);
#elif OPENBFME_FUZZ_TARGET == 11
    TGAImage image;
    TGAFile::decode(data, size, image, &error);
#elif OPENBFME_FUZZ_TARGET == 12
    try
    {
        const auto info = AudioDecode::probe(data, size);
        if (info.frames <= 262144 && size <= 65536)
        {
            AudioDecode::decode(data, size);
        }
    }
    catch (const AudioDecode::AudioDecodeError &)
    {
        // Bad audio reports an offset-bearing parse error.
    }
#elif OPENBFME_FUZZ_TARGET == 13
    ReplayFile file;
    ReplayFile::parse(std::vector<std::uint8_t>(data, data + size), file, &error);
#elif OPENBFME_FUZZ_TARGET == 14
    SaveGameFile file;
    SaveGameFile::parse(std::vector<std::uint8_t>(data, data + size), file, &error);
#elif OPENBFME_FUZZ_TARGET == 15
    CursorImage image;
    DecodeCursorFile(std::vector<std::uint8_t>(data, data + size), image, &error);
#elif OPENBFME_FUZZ_TARGET == 16
    EAVP6Movie movie;
    if (EAVP6Movie::parse(data, size, movie, &error))
    {
        VP6Decoder video;
        for (const auto &frame : movie.video)
        {
            if (frame.offset > size || frame.size > size - frame.offset || !video.decodeFrame(data + frame.offset, frame.size, &error))
            {
                break;
            }
        }
    }
#elif OPENBFME_FUZZ_TARGET == 17
    JsonValue root;
    JsonValue::parse(std::string(reinterpret_cast<const char *>(data), size), root, &error);
#elif OPENBFME_FUZZ_TARGET == 18
    std::vector<std::uint8_t> result;
    REF_decode(data, size, result, nullptr, &error, 4u * 1024u * 1024u);
#elif OPENBFME_FUZZ_TARGET == 19
    FontSubstitution fonts;
    fonts.parse(std::string(reinterpret_cast<const char *>(data), size), &error);
#elif OPENBFME_FUZZ_TARGET == 20
    std::vector<MapCacheEntry> maps;
    MapCache::parse(std::string(reinterpret_cast<const char *>(data), size), maps, &error);
#elif OPENBFME_FUZZ_TARGET == 21
    TextFile text;
    text.parseBytes("fuzz.ini", std::vector<std::uint8_t>(data, data + size),
        [](const std::string &, std::vector<std::uint8_t> &, std::string *error) {
            if (error) *error = "missing included file";
            return false;
        }, &error);
#endif
    return 0;
}
