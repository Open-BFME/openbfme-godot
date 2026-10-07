# OpenBFME Community Services Privacy Policy

Last updated: October 7, 2026

This policy covers the community tools operated for the OpenBFME Godot Port, including the YouTube uploader and the Discord feedback bot. The operator is the owner of the OpenBFME Godot Port YouTube channel.

## YouTube data

The uploader requests the Google OAuth scopes `youtube.upload` and `youtube.readonly`. It uses them to:

- confirm that authorization belongs to the expected OpenBFME Godot Port channel (`UCbdtgor-Fw4a4g06uEKkmlg`);
- read that channel's basic metadata and video visibility; and
- upload videos and metadata selected by the channel owner.

The uploader checks the authorized channel before uploading and records the visibility returned by YouTube. Google OAuth credentials and client configuration are stored outside this repository on an owner-controlled machine with restrictive file permissions. They are not published with the project.

## Discord data

In the designated public feedback channel, the bot processes the message content, author ID, message ID, channel ID, timestamp, and its reply. This data is stored in a local SQLite database. A local export containing the 100 most recent feedback items is available to authorized project developers.

Feedback text is sent to Anthropic's Claude service to draft a reply. Claude's tools are disabled for this task. The bot reads and responds only in the designated feedback channel and posts video notices only in the designated updates channel; it is denied access elsewhere by its configuration and Discord permissions.

## Sharing and use

The services use data only to publish owner-selected videos, verify the target YouTube channel and visibility, answer community feedback, and provide that feedback to authorized project developers. Data is processed by Google, Discord, and Anthropic where required to provide these functions. The operator does not sell community data or use it for advertising.

## Retention and deletion

Discord feedback records and replies remain in local storage until the operator deletes them. The service does not promise automatic deletion.

You can revoke the uploader's Google access from your Google Account permissions. The operator can also remove the private credential files or remove the Discord bot from the server. To request deletion of stored Discord feedback associated with your account, open an issue in the [OpenBFME Godot repository](https://github.com/Open-BFME/openbfme-godot/issues).

## Changes

This policy may be updated when the community services change. The date above identifies the latest revision.
