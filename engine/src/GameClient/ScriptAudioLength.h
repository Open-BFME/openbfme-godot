// OpenBFME. GPL-3.0.
//
// Lane SCRIPT-3: HAS_FINISHED_AUDIO's length model (stop S-1187): RW 0x4541FF (TheAudio getAudioLengthMS) as the logic needs it. A copy of the event
// generates its file names (RW 0x6DBA01 / 0x6DB821) and the attack, main and decay files' lengths are summed, each RW 0x454186's whole ms
// (AIL_stream_ms_position of the opened stream; 0 when it does not open). A list of one file is retail's pick (no draw); a longer list draws the
// audio generator in retail (RW 0x6DA80E) and is its first file here, so every peer agrees (`picked`). INFERENCE: the Miles stream length is the
// decoded frames * 1000 / rate, truncated. Integer arithmetic only: the result feeds the logic (ScriptEngine's timer list). Client-side (the
// files, the audio INI), kept out of the simulation sources.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

class AudioIniState;
class AudioAssetCache;
class ArchiveFileSystem;

namespace ScriptAudioLength
{
// false when the event has no info (TheAudio vslot 0x12C null)
bool lengthMs(const AudioIniState &audio, AudioAssetCache &files, const std::string &name, std::int32_t &ms, bool &picked);
// a probe-only file cache over the mounted archives (no decoded budget)
std::shared_ptr<AudioAssetCache> makeProbeCache(ArchiveFileSystem &fs);
} // namespace ScriptAudioLength
