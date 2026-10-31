# disposable-domains.txt: source

ONLINEMENU-1 (owner identity decisions 2026-10-20). The lobby refuses e-mail sign-in from these domains and their subdomains
(`src/email.mjs`: `loadDisposableDomains`, `isDisposable`), plus any listed in the `EMAIL_BLOCKLIST` setting.

- Upstream: https://github.com/disposable-email-domains/disposable-email-domains
- File: https://raw.githubusercontent.com/disposable-email-domains/disposable-email-domains/main/disposable_email_blocklist.conf
- Fetched: 2026-10-20, upstream commit `b63df8b431f37ef0b22524f9cae2e86c7114a9de` (committed 2026-10-19T06:40:16Z, the file's latest change)
- Size: 9913 domains, one per line, no comments; sha256 `db1f4fcc116e32b2eabc6cd72f196387d0068482d9cb84e44a5de57933ea0f7b`
- Licence: CC0 1.0 Universal (public domain dedication), per the upstream `LICENSE.txt`

The file is kept exactly as downloaded.

## Refreshing

From `services/online/lobby`:

```
curl -fsSL -o data/disposable-domains.txt \
  https://raw.githubusercontent.com/disposable-email-domains/disposable-email-domains/main/disposable_email_blocklist.conf
curl -fsSL "https://api.github.com/repos/disposable-email-domains/disposable-email-domains/commits?path=disposable_email_blocklist.conf&per_page=1"
sha256sum data/disposable-domains.txt
node --test test/email.test.mjs
```

Then update the commit, date, count and sha256 above. Look over the diff before committing: an entry also blocks every subdomain of it, so
a large provider appearing in the list would lock its users out (the test checks a few big providers are not listed).
