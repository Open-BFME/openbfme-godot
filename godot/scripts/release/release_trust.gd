## Lane ONEZIP-1 r2: what a packaged game trusts: the OpenBFME release key (Ed25519, 64 hex digits) and the GitHub repository its
## releases come from. tools/release/package.sh writes this file into the staged project from launcher/release_key.pub and --repo,
## the same values it builds the launcher with. In the source tree both are empty: a development run never asks a launcher.
extends RefCounted

const RELEASE_KEY := ""
const REPO := ""
