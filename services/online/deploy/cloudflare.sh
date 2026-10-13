#!/bin/bash
# Deploys the lobby to Cloudflare (a Worker + one Durable Object + an R2 bucket for the replays; docs/ONLINE.md option (d)).
# Needs: node 18+ with npx (wrangler is fetched by npx), and in the environment
#   CLOUDFLARE_API_TOKEN   an API token with "Workers Scripts: Edit", "Workers R2 Storage: Edit" (and "Workers Routes: Edit" for a custom domain)
#   CLOUDFLARE_ACCOUNT_ID  the account id
#   RELAYS                 "us-east=a.b.c.d:7350[,eu=...]" (the relays' public addresses; the first one is used in phase O1)
#   PUBLIC_URL             the lobby's URL (https://lobby.<domain>, or the workers.dev URL wrangler prints)
#   SECRETS                the directory make_keys.sh wrote (ticket.keys, session.key, upload.key)
# Nothing secret is written into the repository: wrangler.toml is generated in a temporary folder.
set -euo pipefail
: "${CLOUDFLARE_API_TOKEN:?}" "${CLOUDFLARE_ACCOUNT_ID:?}" "${RELAYS:?}" "${PUBLIC_URL:?}" "${SECRETS:?}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
cp -r "$HERE/lobby/src" "$W/src"
sed -e "s|^# account_id = .*|account_id = \"$CLOUDFLARE_ACCOUNT_ID\"|" -e "s|^RELAYS = .*|RELAYS = \"$RELAYS\"|" -e "s|^PUBLIC_URL = .*|PUBLIC_URL = \"$PUBLIC_URL\"|" \
    "$HERE/lobby/wrangler.toml.example" > "$W/wrangler.toml"
cd "$W"
npx --yes wrangler@4 r2 bucket create openbfme-replays 2> /dev/null || true   # exists already: fine
npx --yes wrangler@4 deploy
npx --yes wrangler@4 secret put TICKET_KEYS < "$SECRETS/ticket.keys"
npx --yes wrangler@4 secret put SESSION_KEY < "$SECRETS/session.key"
npx --yes wrangler@4 secret put UPLOAD_KEY < "$SECRETS/upload.key"
curl -fsS "$PUBLIC_URL/v1/health" && echo
