extends SceneTree

const SocialLaunch := preload("res://scripts/core/social_launch.gd")

var _failures := 0


class GameLauncher extends RefCounted:
	var seen_token := ""
	var seen_args := PackedStringArray()
	var calls := 0
	var refuse := false
	var spawn_failure := false

	func play(_version: String, args: PackedStringArray, before_start: Callable) -> Dictionary:
		calls += 1
		seen_args = args
		if refuse:
			return {"ok": false, "error": "Game verification failed."}
		if not before_start.call():
			return {"ok": false, "error": "Prelaunch failed."}
		seen_token = OS.get_environment("OPENBFME_LOBBY_TOKEN")
		return {"ok": not spawn_failure, "error": "" if not spawn_failure else "Process did not start."}


class BundledBuild extends "res://scripts/core/build_info.gd":
	func os_name() -> String:
		return "Synthetic"


class BundledGate extends "res://scripts/core/updater.gd":
	var verified := true

	func _init() -> void:
		super(BundledBuild.new())

	func plays_bundled(_version: String) -> bool:
		return true

	func bundled() -> Dictionary:
		return {"path": "user://missing-social-test-game", "version": "v0.1.0"}

	func check_bundled() -> Dictionary:
		return {"ok": verified, "error": "Synthetic package mismatch."}


func _check(ok: bool, name: String) -> void:
	if not ok:
		_failures += 1
		print("SOCIAL LAUNCH FAIL ", name)


func _init() -> void:
	var existed := OS.has_environment("OPENBFME_LOBBY_TOKEN")
	var original := OS.get_environment("OPENBFME_LOBBY_TOKEN")
	var launcher := GameLauncher.new()
	var invite := "1234567890abcdef"
	var lobby := "https://lobby.example.org"
	var synthetic := "synthetic-session-token"
	OS.unset_environment("OPENBFME_LOBBY_TOKEN")
	var result := SocialLaunch.play(launcher, "v0.1.0", invite, lobby, synthetic)
	_check(result.ok and launcher.seen_token == synthetic, "same token inherited by verified game")
	_check(launcher.seen_args == PackedStringArray(["--", "--online-invite=" + invite, "--online-lobby=" + lobby]), "pending invitation uses game-owned acceptance seam")
	_check(not " ".join(launcher.seen_args).contains(synthetic), "no token on command line")
	_check(not OS.has_environment("OPENBFME_LOBBY_TOKEN"), "temporary token removed after successful spawn")
	OS.set_environment("OPENBFME_LOBBY_TOKEN", "previous-synthetic-session")
	launcher.spawn_failure = true
	result = SocialLaunch.play(launcher, "v0.1.0", invite, lobby, synthetic)
	_check(not result.ok and OS.get_environment("OPENBFME_LOBBY_TOKEN") == "previous-synthetic-session", "previous environment restored after spawn failure")
	launcher.refuse = true
	launcher.seen_token = ""
	result = SocialLaunch.play(launcher, "v0.1.0", invite, lobby, synthetic)
	_check(not result.ok and launcher.seen_token == "", "failed game verification never injects token")
	var calls := launcher.calls
	for invalid in ["1234", "ABCDEF1234567890", invite + " --arbitrary", invite + "\n"]:
		_check(not SocialLaunch.play(launcher, "v0.1.0", invalid, lobby, synthetic).ok, "invalid invitation refused")
	_check(not SocialLaunch.play(launcher, "v0.1.0", invite, lobby, "").ok, "empty session refused")
	_check(launcher.calls == calls, "refused handoffs never start game")
	var bundled := BundledGate.new()
	var callbacks: Array = []
	result = bundled.play("v0.1.0", PackedStringArray(), func() -> bool:
		callbacks.append(true)
		return false)
	_check(result.get("stopped", false) and callbacks.size() == 1, "bundled path forwards prelaunch callback")
	bundled.verified = false
	callbacks.clear()
	result = bundled.play("v0.1.0", PackedStringArray(), func() -> bool:
		callbacks.append(true)
		return false)
	_check(result.get("modified", false) and callbacks.is_empty(), "bundled verification precedes prelaunch callback")
	if existed:
		OS.set_environment("OPENBFME_LOBBY_TOKEN", original)
	else:
		OS.unset_environment("OPENBFME_LOBBY_TOKEN")
	print("SOCIAL LAUNCH OK" if _failures == 0 else "SOCIAL LAUNCH FAILED")
	quit(0 if _failures == 0 else 1)
