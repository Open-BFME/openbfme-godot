# Crash report client contract

The crash service (the platform API's `POST /v1/reports`, its crash index) receives the crash reports the game and the launcher send
(lane CRASHACT-1; client: `godot/scripts/crash/crash_client.gd`, byte for byte `launcher/scripts/net/crash_client.gd`). This folder
holds the client's copy of the service's report schema, `report.schema.json` (from lane crashclient2, Devin), which
`tools/release/test_crash_upload.py` checks every report against.

## Wire

- `POST https://api.openbfme.com/v1/reports`, `Content-Type: application/octet-stream`, `Content-Encoding: gzip`; the body is the
  report's JSON, gzip-compressed without a file name, comment or extra field and with mtime 0. At most 262,144 bytes compressed and
  1,048,576 bytes of JSON.
- Headers: `X-Crash-Install-Id` (a random lowercase UUID4 per install, `user://crash-install-id`), `X-Crash-Nonce` (a new UUID4 per
  request), `X-Crash-Time` (Unix seconds; the service accepts 300 s of skew) and `X-Crash-Signature`: lowercase hex HMAC-SHA256, keyed
  with the service's client key, of

  ```text
  crash-v1
  POST
  /v1/reports
  <x-crash-time>
  <x-crash-install-id>
  <x-crash-nonce>
  <lowercase sha256 of the exact compressed body>
  ```

- Success: `201 {"v":1,"report_id":"<24 lowercase hex>"}`. Errors: `{"v":1,"error":"crash.<code>"}`; 400 / 413 mean the service will
  never take that report (the client drops it by name), anything else (401, 429, 5xx, no answer) keeps it queued for the next start.
- The service refuses a whole report when any string still looks like an IP address, a home folder (a Windows user's folder under
  Users, `/home/...`, `/Users/...`) or a credential label; the client withholds such a line before sending (`CrashClient.service_would_refuse`).
- Limits on the service side: 2 requests per source, then one every 10 minutes; 3 per install a day; 100 a day in all. The client
  sends at most 2 a start.

## Report fields

`schema_version` (1), `build_kind` ("release"), `version` (the crashed build's `git describe`), `client` {`platform`: windows / linux /
macos, `version`}, `os_name`, `os_version`, `cpu_name`, `gpu_name`, `backtrace` (the crash handler's frames and the error lines; at least
one entry, else `unknown: no backtrace was recorded`), `log_tail` (a header line naming the program, build, sim profile id and Uber /
classic mode, then the last lines of that run's log: 200 in all), `context` {`map`, `mode`, `factions`}, `settings` {window
width / height, vsync; texture quality and shadows "unknown"}. The service stores only these fields.

## Not yet (follow-ups for the crash service)

- The client key is never in the source. `tools/release/crash_key.py` writes `res://scripts/crash_client_key.gd` (git-ignored) into each
  package's freshly staged game and launcher project from `CRASH_CLIENT_KEY`: the operator's release.env for the packages built from
  the Deck (and its `package.sh --verify` rebuilds), the GitHub Actions secret `CRASH_CLIENT_KEY` for the Actions builds (the package
  step and the macOS call alone: tools/release/public_workflows.py pins the lines). A build without it sends nothing: every upload
  stops with `crash.no_client_key` and the reports stay queued (at most 8, 30 days).
- `OPENBFME_CRASH_UPLOAD=0` sends nothing at all (`crash.upload_off`): every CI host's env.sh and the package smoke tests
  (tools/release/test_export.sh, the nightly's Windows starts) set it.
- Minidumps: the service takes no attachments. Windows minidumps (`user://logs/minidumps`) stay on the player's PC; an upload needs a
  private, admin-only attachment store behind `/v1/reports` first.
