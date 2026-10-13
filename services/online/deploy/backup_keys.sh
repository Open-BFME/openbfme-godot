#!/bin/bash
# Keeps copies of the online service's keys on the home-lab hosts and checks them (lane INFRA-2; services/online/DEPLOY.md "Backups and disaster
# recovery").
#   backup_keys.sh push    [host...]          copy the keys to each host's ~/openbfme-keys (dir 0700, files 0600), then verify
#   backup_keys.sh verify  [host...]          compare every file's SHA-256 and mode with the Deck's copy (hashes are compared, never printed)
#   backup_keys.sh restore <host> <dir>       copy a host's backup into <dir> (0700 / 0600): a new Deck, or a drill
# Hosts default to bfme-nas and bfme-pve (ssh aliases). What is copied: the four files of ~/.openbfme/online-secrets (ticket.keys, session.key,
# upload.key, admin.key: make_keys.sh) as online-secrets/, and the relay's ssh key pair ~/.ssh/openbfme_relay_ed25519{,.pub} as ssh/.
# Protection: file modes (0600 in a 0700 directory, for the hosts' single ssh user), not encryption: an encryption key would have to be kept
# somewhere that survives losing the Deck, which is the very case the backup is for; DEPLOY.md explains the choice. Nothing is printed but names.
# SECRETS_DIR and RELAY_SSH_KEY override the Deck-side sources (the tests use them).
set -euo pipefail
SECRETS_DIR="${SECRETS_DIR:-$HOME/.openbfme/online-secrets}"
RELAY_SSH_KEY="${RELAY_SSH_KEY:-$HOME/.ssh/openbfme_relay_ed25519}"
SSH="${BACKUP_SSH:-ssh}"
FILES=(online-secrets/ticket.keys online-secrets/session.key online-secrets/upload.key online-secrets/admin.key ssh/openbfme_relay_ed25519 ssh/openbfme_relay_ed25519.pub)
CMD="${1:-}"; shift || true

stage() {  # the Deck's files under the backup's names, in a private temp dir
  local d; d="$(mktemp -d)"; chmod 700 "$d"
  mkdir -m 700 "$d/online-secrets" "$d/ssh"
  for f in ticket.keys session.key upload.key admin.key; do
    [ -f "$SECRETS_DIR/$f" ] || { echo "backup_keys: $SECRETS_DIR/$f is missing" >&2; rm -rf "$d"; return 1; }
    install -m 600 "$SECRETS_DIR/$f" "$d/online-secrets/$f"
  done
  for x in "" .pub; do
    [ -f "$RELAY_SSH_KEY$x" ] || { echo "backup_keys: $RELAY_SSH_KEY$x is missing" >&2; rm -rf "$d"; return 1; }
    install -m 600 "$RELAY_SSH_KEY$x" "$d/ssh/openbfme_relay_ed25519$x"
  done
  echo "$d"
}

# "<mode> <sha256> <name>" lines of a backup directory (the remote side runs the same commands)
LIST='cd "$0" && for f in '"${FILES[*]}"'; do if [ -f "$f" ]; then printf "%s %s %s\n" "$(stat -c %a "$f")" "$(sha256sum < "$f" | cut -d" " -f1)" "$f"; else echo "- - $f"; fi; done; printf "%s - .\n" "$(stat -c %a .)"; for d in online-secrets ssh; do printf "%s - %s\n" "$(stat -c %a "$d" 2>/dev/null || echo -)" "$d"; done'

verify_host() {  # $1 host, $2 staged dir
  local want got bad=0
  want="$(bash -c "$LIST" "$2")"
  got="$($SSH "$1" "bash -c '$LIST' \$HOME/openbfme-keys" 2>/dev/null)" || { echo "backup_keys: $1: cannot read ~/openbfme-keys" >&2; return 1; }
  while read -r mode sum name; do
    local gmode gsum
    gmode="$(awk -v n="$name" '$3 == n { print $1 }' <<< "$got")"; gsum="$(awk -v n="$name" '$3 == n { print $2 }' <<< "$got")"
    if [ "$gsum" != "$sum" ]; then echo "backup_keys: $1: $name MISSING or DIFFERENT" >&2; bad=1
    elif [ "$sum" = - ] && [ "$gmode" != 700 ]; then echo "backup_keys: $1: $name is mode $gmode, not 700" >&2; bad=1
    elif [ "$sum" != - ] && [ "$gmode" != 600 ]; then echo "backup_keys: $1: $name is mode $gmode, not 600" >&2; bad=1
    fi
  done <<< "$want"
  [ "$bad" = 0 ] && echo "backup_keys: $1: ${#FILES[@]} files match the Deck's (SHA-256), modes 600 in 700 directories"
  return "$bad"
}

case "$CMD" in
  push|verify)
    HOSTS=("$@"); [ ${#HOSTS[@]} -gt 0 ] || HOSTS=(bfme-nas bfme-pve)
    S="$(stage)"; trap 'rm -rf "$S"' EXIT
    rc=0
    for h in "${HOSTS[@]}"; do
      if [ "$CMD" = push ]; then
        # into a new directory, then swapped in: a broken copy never replaces a good one
        tar -C "$S" -cf - online-secrets ssh | $SSH "$h" 'set -e; umask 077; rm -rf ~/openbfme-keys.new; mkdir ~/openbfme-keys.new; tar -C ~/openbfme-keys.new -xf -; chmod 700 ~/openbfme-keys.new ~/openbfme-keys.new/*; chmod 600 ~/openbfme-keys.new/*/*; rm -rf ~/openbfme-keys.old; if [ -d ~/openbfme-keys ]; then mv ~/openbfme-keys ~/openbfme-keys.old; fi; mv ~/openbfme-keys.new ~/openbfme-keys; rm -rf ~/openbfme-keys.old' \
          || { echo "backup_keys: $h: copy failed" >&2; rc=1; continue; }
      fi
      verify_host "$h" "$S" || rc=1
    done
    exit "$rc"
    ;;
  restore)
    [ $# -eq 2 ] || { echo "usage: backup_keys.sh restore <host> <dir>" >&2; exit 2; }
    H="$1"; D="$2"
    [ ! -e "$D" ] || [ -z "$(ls -A "$D")" ] || { echo "backup_keys: $D is not empty: restore into a new directory" >&2; exit 1; }
    mkdir -p "$D"; chmod 700 "$D"
    $SSH "$H" 'tar -C ~/openbfme-keys -cf - online-secrets ssh' | (umask 077; tar -C "$D" -xf -)
    chmod 700 "$D/online-secrets" "$D/ssh"; chmod 600 "$D"/online-secrets/* "$D"/ssh/*
    for f in "${FILES[@]}"; do [ -f "$D/$f" ] || { echo "backup_keys: $H's backup lacks $f" >&2; exit 1; }; done
    echo "backup_keys: restored ${#FILES[@]} files from $H into $D (online-secrets/ for make_keys.sh's place, ssh/ for ~/.ssh)"
    ;;
  *) echo "usage: backup_keys.sh push|verify [host...] | restore <host> <dir>" >&2; exit 2 ;;
esac
