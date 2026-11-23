# PORT-97: TransportContain / HordeTransportContain FadePassengerOnEnter / FadePassengerOnExit, the passengers' fade into and out of a
# transport (issue fork issue 245; RW 0x86AAC2 .. 0x86ABCB and RW 0x86ADBD .. 0x86AEA1). The engine change is in files other lists already carry
# (TransportContainRuntime.cpp / .h), so this lane adds tests only.
set(OPENBFME_PORT97_TESTS
    tests/test_port_97.cpp
)