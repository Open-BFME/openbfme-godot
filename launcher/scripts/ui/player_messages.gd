extends RefCounted

const SocialClient := preload("res://scripts/net/social_client.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

const ID_DISK := "launcher.player_messages.disk"
const ID_VERIFY := "launcher.player_messages.verify"
const ID_UPDATE_VERIFY := "launcher.player_messages.update_verify"

enum Operation { GENERAL, UPDATE_CHECK, DOWNLOAD, SIGN_IN, LOBBY }


static func error_text(detail: String, operation := Operation.GENERAL) -> String:
	var lower := detail.to_lower()
	if operation == Operation.SIGN_IN:
		return social_error(detail, operation)
	if operation == Operation.LOBBY:
		return social_error(detail, operation)
	if lower.contains("disk full") or lower.contains("writing ") and lower.contains("failed"):
		return L10n.t(ID_DISK)
	if lower.contains("sha-256") or lower.contains("does not match the signed manifest"):
		return L10n.t(ID_VERIFY)
	if operation == Operation.UPDATE_CHECK and lower.contains("rejected:") and not lower.contains("http "):
		return L10n.t(ID_UPDATE_VERIFY)
	# Launcher-authored guidance, not a transfer failure, passes through: the optional BFME1 download says why it does not start.
	if operation == Operation.DOWNLOAD and lower.contains("is offered only when battle for middle-earth i is the chosen game"):
		var at := lower.find("the optional battle for middle-earth i download")
		var text := detail.substr(at) if at >= 0 else detail
		return text.substr(0, 1).to_upper() + text.substr(1) + ("" if text.ends_with(".") else ".")
	match operation:
		Operation.UPDATE_CHECK:
			return L10n.t("launcher.player_messages.update_check")
		Operation.DOWNLOAD:
			return L10n.t("launcher.player_messages.download")
	return L10n.t("launcher.player_messages.general")


static func status_text(detail: String, operation := Operation.GENERAL) -> String:
	var player := error_text(detail, operation)
	if player == L10n.t(ID_DISK):
		return L10n.t("launcher.player_messages.status_disk")
	if player == L10n.t(ID_VERIFY):
		return L10n.t("launcher.player_messages.status_verify")
	if player == L10n.t(ID_UPDATE_VERIFY):
		return L10n.t("launcher.player_messages.status_update_verify")
	if operation == Operation.UPDATE_CHECK:
		return L10n.t("launcher.player_messages.status_update_check")
	return player


static func social_error(detail: String, operation := Operation.SIGN_IN) -> String:
	# lane L10N-1: the client's messages arrive in the player's language: compared as the catalog's text, never as English
	if detail == L10n.t("launcher.social_client.the_lobby_could_not_be_reached"):
		if operation == Operation.SIGN_IN:
			return L10n.t("launcher.player_messages.unreachable_sign_in", {"detail": detail})
	if detail == L10n.t("launcher.social_client.online_accounts_are_not_available"):
		return L10n.t("launcher.player_messages.no_accounts", {"detail": detail})
	# Only launcher-authored guidance may pass through; never echo a server diagnostic.
	for message in SocialClient.ERROR_MESSAGES.values() + SocialClient.HTTP_ERROR_MESSAGES.values():
		if detail == message:
			return message
	for id in ["launcher.social_client.choose_a_valid_player_name", "launcher.social_client.the_recovery_code_is_invalid",
			"launcher.social_client.the_recovery_code_is_16_letters", "launcher.oauth_account.the_recovery_code_is_16_letters",
			"launcher.main.invite_finish_signin", "launcher.main.invite_wait_install", "launcher.main.invite_not_yours",
			"launcher.main.invite_expired", "launcher.main.invite_install_game"]:
		if detail == L10n.t(id):
			return detail
	if operation == Operation.SIGN_IN:
		return L10n.t("launcher.player_messages.sign_in")
	return L10n.t("launcher.player_messages.lobby")
