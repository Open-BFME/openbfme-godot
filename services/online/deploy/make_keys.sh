#!/bin/bash
# Makes the online service's secrets in DIR (default ./openbfme-secrets, mode 700): ticket.keys (the relay ticket key ring, "1:<64 hex>"; the lobby and
# every relay need the SAME file), session.key (the lobby's session token key), upload.key (the relay -> lobby log upload key), admin.key (INFRA-2: the lobby's backup export; only the lobby and the backup host).
# Run it once (again later: it only adds the files that are missing), on a trusted
# machine; copy the files to the hosts with scp; never commit them (services/online/.gitignore refuses *.keys / *.key in this folder anyway).
# A new ticket key: add a line "2:<64 hex>" to ticket.keys on the relays first, then on the lobby (it signs with the highest id); drop the old line later.
set -euo pipefail
DIR="${1:-./openbfme-secrets}"
mkdir -p "$DIR"; chmod 700 "$DIR"
hex() { head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n'; }
[ -e "$DIR/ticket.keys" ] || { printf '# OpenBFME relay ticket keys (keyId:hex)\n1:%s\n' "$(hex)" > "$DIR/ticket.keys"; }
[ -e "$DIR/session.key" ] || hex > "$DIR/session.key"
[ -e "$DIR/upload.key" ] || hex > "$DIR/upload.key"
[ -e "$DIR/admin.key" ] || hex > "$DIR/admin.key"
chmod 600 "$DIR"/*
echo "make_keys: $DIR/ticket.keys session.key upload.key admin.key"
