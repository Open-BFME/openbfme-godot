# Lane RENDERWAIT-1 (the render never waits on the logic thread: the wait log and the client window of LiveGame): tests of the test executable.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. No new sources (LiveGame's files are already listed).
set(OPENBFME_RENDERWAIT1_TESTS
    tests/test_renderwait1.cpp
)
