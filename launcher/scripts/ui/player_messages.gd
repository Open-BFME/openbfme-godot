extends RefCounted

const SocialClient := preload("res://scripts/net/social_client.gd")

enum Operation { GENERAL, UPDATE_CHECK, DOWNLOAD, SIGN_IN, LOBBY }


static func error_text(detail: String, operation := Operation.GENERAL) -> String:
	var lower := detail.to_lower()
	if operation == Operation.SIGN_IN:
		return social_error(detail, operation)
	if operation == Operation.LOBBY:
		return social_error(detail, operation)
	if lower.contains("disk full") or lower.contains("writing ") and lower.contains("failed"):
		return "Not enough disk space, or the install folder cannot be written to. Free up space and check the folder's permissions, then try the download again. Nothing new was installed."
	if lower.contains("sha-256") or lower.contains("does not match the signed manifest"):
		return "The download could not be verified and was deleted. Nothing new was installed. Use Check for updates, then try the download again. If it keeps failing, use Copy details to report it."
	if operation == Operation.UPDATE_CHECK and lower.contains("rejected:") and not lower.contains("http "):
		return "The update could not be verified. Check for updates and try again. If it keeps failing, use Copy details to report it."
	# Launcher-authored guidance, not a transfer failure, passes through: the optional BFME1 download says why it does not start.
	if operation == Operation.DOWNLOAD and lower.contains("is offered only when battle for middle-earth i is the chosen game"):
		var at := lower.find("the optional battle for middle-earth i download")
		var text := detail.substr(at) if at >= 0 else detail
		return text.substr(0, 1).to_upper() + text.substr(1) + ("" if text.ends_with(".") else ".")
	match operation:
		Operation.UPDATE_CHECK:
			return "Could not check for updates. Check your internet connection, then use Check for updates to try again. If you already have an installed build, you can still play it offline."
		Operation.DOWNLOAD:
			return "The download did not finish. Check your internet connection and the space and permissions of the download folder, then retry the download. Use Copy details if it keeps failing."
	return "The action could not be completed. Try again. If it keeps failing, use Copy details to report it."


static func status_text(detail: String, operation := Operation.GENERAL) -> String:
	var player := error_text(detail, operation)
	if player.begins_with("Not enough disk"):
		return "Install failed. Free space or check folder permissions, then retry."
	if player.begins_with("The download could not be verified"):
		return "Download could not be verified. Check for updates and retry."
	if player.begins_with("The update"):
		return "Update could not be verified. Check for updates and retry."
	if operation == Operation.UPDATE_CHECK:
		return "Could not check for updates. Check your internet connection; retry later."
	return player


static func social_error(detail: String, operation := Operation.SIGN_IN) -> String:
	if detail == "The lobby could not be reached.":
		if operation == Operation.SIGN_IN:
			return detail + " Check your internet connection and try Sign in again. You can play an installed build offline."
	if detail == "Online accounts are not available on this server yet.":
		return detail + " You can play an installed build offline. Try Sign in again later."
	# Only launcher-authored guidance may pass through; never echo a server diagnostic.
	for message in SocialClient.ERROR_MESSAGES.values() + SocialClient.HTTP_ERROR_MESSAGES.values():
		if detail == message:
			return message
	if detail in ["Choose a valid player name.", "The recovery code is invalid.", "The recovery code is 16 letters and digits.",
			"Finish signing in before joining.", "Wait for the current game installation before joining.",
			"This invitation is not addressed to you.", "This invitation expired.", "Install the game before joining an invitation."]:
		return detail
	if operation == Operation.SIGN_IN:
		return "Could not sign in. Check your internet connection and try Sign in again. You can play an installed build offline. Use Copy details if it keeps failing."
	return "The lobby request did not finish. Check your internet connection and try the lobby action again. You can play an installed build offline. Use Copy details if it keeps failing."
