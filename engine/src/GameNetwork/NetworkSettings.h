// OpenBFME. GPL-3.0.
//
// NetworkSettings (lane MP-2): the GameData network timing values (ZH GlobalData m_networkFPSHistoryLength .. m_networkDisconnectScreenNotifyTime) the
// disconnect path and the run-ahead read.
//
// TARGET FACTS (RotWK game.dat GlobalData = RW 0xDE4364, caveat S-001): the GameData field table rows RW 0xC007D0 .. 0xC00850 name the nine fields, each
// parsed by RW 0x42EC5E (one token through the INI scanner RW 0x42E9D7 into a 32-bit store; read here as parseUnsignedInt), stored at
//   NetworkFPSHistoryLength +0xC08  NetworkLatencyHistoryLength +0xC0C  NetworkRunAheadMetricsTime +0xC10  NetworkCushionHistoryLength +0xC14
//   NetworkRunAheadSlack +0xC18     NetworkKeepAliveDelay +0xC1C        NetworkDisconnectTime +0xC20       NetworkPlayerTimeoutTime +0xC24
//   NetworkDisconnectScreenNotifyTime +0xC28
// GlobalData's constructor (RW 0x6432FF .. 0x643352) gives the defaults below; RotWK 2.01's GameData.ini sets 30 / 200 / 5000 / 10 / 10 / 20 / 15000 / 60000
// / 15000. Readers in the binary: +0xC20 by the connection silence test RW 0x8D335D / 0x8D33C7 (four times as long before logic frame 6, RW 0xBFD298 = 6),
// +0xC24 by DisconnectManager RW 0x8D7E52 / 0x8D8735; +0xC28 has no reader in RotWK (ZH's notify timer, DisconnectManager::updateDisconnectStatus, is gone).
// Not simulation state: these values time the network only (never hashed).

#pragma once

#include <cstdint>
#include <string>

class ArchiveFileSystem;

struct NetworkSettings
{
	std::uint32_t fpsHistoryLength = 30;        ///< +0xC08
	std::uint32_t latencyHistoryLength = 200;   ///< +0xC0C
	std::uint32_t runAheadMetricsTime = 500;    ///< +0xC10 (ms)
	std::uint32_t cushionHistoryLength = 10;    ///< +0xC14
	std::uint32_t runAheadSlack = 10;           ///< +0xC18 (percent)
	std::uint32_t keepAliveDelay = 20;          ///< +0xC1C (s)
	std::uint32_t disconnectTime = 5000;        ///< +0xC20 (ms): silence before the disconnect screen
	std::uint32_t playerTimeoutTime = 60000;    ///< +0xC24 (ms): silence on the screen before a player is dropped
	std::uint32_t disconnectScreenNotifyTime = 15000; ///< +0xC28 (ms; no RotWK reader)
	bool loaded = false;                        ///< read from GameData (false: the constructor defaults)

	// reads data\ini\gamedata.ini through the shared INI pipeline; a missing file is an error, a missing field keeps the constructor default (as retail)
	static bool load(ArchiveFileSystem &fs, NetworkSettings &out, std::string *error);
	static bool scan(const std::string &text, NetworkSettings &out, std::string *error);
};
