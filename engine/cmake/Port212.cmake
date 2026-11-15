# PORT-212 (fork issue 502, P-133): a build plot's construction path applies no maximum. RW 0x942733's construction arms (1 for
# FOUNDATION_CONSTRUCT, 0x35 for DOZER_CONSTRUCT) ask Buildable / Player::canBuild (RW 0x6AAA2E) and money and never call
# BuildAssistant::canMakeUnit, and the plot's message case RW 0x77A91B asks Player::canBuild too, so MaxSimultaneousOfType (count
# RW 0x794054, arm 3 only) does not stop a second structure of the same template. Synthetic INI, no game files.
set(OPENBFME_PORT212_TESTS tests/test_port_212.cpp)
