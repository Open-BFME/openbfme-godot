// OpenBFME. GPL-3.0.
// Godot exposes pan and magnification but not AppKit's rotateWithEvent. Use an app-local monitor,
// scoped to the HUD's native window. No global event tap or Accessibility permission is needed.
#include "GodotDevice/MacOSTrackpad.h"
#include <AppKit/AppKit.h>
#include <algorithm>
#include <vector>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
using namespace godot;
namespace { std::vector<void *> monitors; }

void *MacOSTrackpad::install(std::uint64_t receiver, int window)
{
	DisplayServer *display = DisplayServer::get_singleton();
	if (!display || display->get_name() != String("macOS")) return nullptr; // headless tests have no native window
	NSWindow *native = (__bridge NSWindow *)(void *)display->window_get_native_handle(DisplayServer::WINDOW_HANDLE, window);
	if (!native) return nullptr;
	id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskRotate handler:^NSEvent *(NSEvent *event) {
		if (event.window != native || NSApp.keyWindow != native) return event;
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(receiver)));
		if (!node || !node->is_inside_tree()) return event;
		const Vector2 position = node->get_viewport()->get_mouse_position();
		const bool handled = node->call("trackpad_rotate", (double)event.rotation, position);
		return handled ? nil : event;
	}];
	if (!monitor) UtilityFunctions::push_error("macOS trackpad: failed to install the local rotation monitor");
	void *token = (__bridge_retained void *)monitor;
	if (token) monitors.push_back(token);
	return token;
}

void MacOSTrackpad::uninstall(void *monitor)
{
	const auto found = std::find(monitors.begin(), monitors.end(), monitor);
	if (found == monitors.end()) return; // includes tokens removed by module shutdown
	monitors.erase(found);
	id token = (__bridge_transfer id)monitor;
	[NSEvent removeMonitor:token];
}

void MacOSTrackpad::shutdown()
{
	while (!monitors.empty()) uninstall(monitors.back());
}
