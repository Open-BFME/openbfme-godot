# Launcher OAuth integration fixture

`test_launcher_oauth.py` runs the real launcher `OAuthAccount` and
`SocialClient` against a real Node lobby and temporary SQLite database. Only
the provider transport is replaced, through the fixture's `oauthFetch` seam.

For a later manual UI session, run:

```text
python3 tools/release/test_launcher_oauth.py --serve
```

The command prints the ephemeral lobby and control origins and keeps the
fixture alive until interrupted. The separate loopback control API is:

- `GET /health` — readiness probe.
- `GET /stats` — non-sensitive request counters.
- `GET /authorize?url=<encoded authorization URL>` — validates the exact
  Discord or Google authorization URL and returns a one-use callback URL.
- `POST /callback` — follows the test browser's callback URL to the fixed
  loopback OAuth listener, with redirects disabled and a four-second timeout.
- `POST /fault/next-exchange` — makes one provider token exchange fail, for
  UI error-state testing.

The control server is test-only and is never added to production lobby
routing. A synthetic one-use authorization code is returned only to the test
browser as part of the callback URL. The fixture uses only synthetic provider
credentials and subjects; codes, verifiers, raw subjects, tokens, and secrets
are not written to fixture logs, stats, or the production store.
