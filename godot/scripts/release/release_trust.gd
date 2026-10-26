## Lane ONEZIP-1 r2: what a packaged game trusts: the OpenBFME release key (Ed25519, 64 hex digits) and the GitHub repository its
## releases come from. tools/release/package.sh writes this file into the staged project from launcher/release_key.pub and --repo,
## the same values it builds the launcher with. In the source tree both are empty: a development run never asks a launcher.
## Lane SIGNKMS-1: RELEASE_KEYS, the trusted keys of launcher/release_keys.pub (comma-separated 64-hex keys, in file order; the first is
## RELEASE_KEY), written by package.sh next to RELEASE_KEY. The game checks signatures under the whole list
## (scripts/crypto/release_signature.gd); a list that does not begin with RELEASE_KEY is no list (launcher_check.gd trusted_keys).
extends RefCounted

const RELEASE_KEY := ""
const RELEASE_KEYS := ""
const REPO := ""
