extends RefCounted
const L10n := preload("res://scripts/l10n/l10n.gd")


static func invite_args(invite: String, lobby: String) -> Dictionary:
	if RegEx.create_from_string("^[0-9a-f]{16}\\z").search(invite) == null:
		return {"ok": false, "error": L10n.t("launcher.social_launch.invitation_invalid")}
	return {"ok": true, "args": PackedStringArray(["--", "--online-invite=" + invite, "--online-lobby=" + lobby])}


static func play(updater: RefCounted, version: String, invite: String, lobby: String, token: String) -> Dictionary:
	var parsed := invite_args(invite, lobby)
	if not parsed.ok:
		return parsed
	if token.is_empty() or token.length() > 8192 or token.contains("\r") or token.contains("\n"):
		return {"ok": false, "error": L10n.t("launcher.social_launch.sign_in_again")}
	var existed := OS.has_environment("OPENBFME_LOBBY_TOKEN")
	var previous := OS.get_environment("OPENBFME_LOBBY_TOKEN")
	var result: Dictionary = updater.play(version, parsed.args, func() -> bool:
		OS.set_environment("OPENBFME_LOBBY_TOKEN", token)
		return OS.get_environment("OPENBFME_LOBBY_TOKEN") == token)
	if existed:
		OS.set_environment("OPENBFME_LOBBY_TOKEN", previous)
	else:
		OS.unset_environment("OPENBFME_LOBBY_TOKEN")
	return result
