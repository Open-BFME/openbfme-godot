// OpenBFME. GPL-3.0.
#pragma once

#include <string>
#include <vector>

// Process-wide, local-only diagnostics. Install/reassert/uninstall on the main thread,
// after host initialization and before starting match workers; not from DllMain.
namespace WindowsCrashDump
{
bool install(const std::string &directory, std::string &error);
// Returns true if another component replaced our unhandled filter or terminate handler.
bool reassertHandlers();
void uninstall();
// lane CRASHDIALOG-1: once a crash's dump is written, this program runs again as the crash dialog (--crash-report=<id>, game.gd goes to godot/scenes/crash_report.tscn): no crash path of this process
// builds anything, the command line is made here. "" = none. Install first. The dialog's own process does not arm it: a crash there starts nothing.
bool armCrashDialog(const std::vector<std::string> &extraArguments, std::string &error);
// the command line of the crash dialog of process `pid`: `executable` quoted, then extraArguments (quoted when they hold a space), then the fixed arguments
std::wstring crashDialogCommandLine(const std::wstring &executable, const std::vector<std::wstring> &extraArguments, unsigned long pid);
}
