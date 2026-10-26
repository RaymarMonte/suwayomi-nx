// doctest's main() for the integration tests (TESTING.md layer 3): they talk to the Suwayomi test server, so ctest doesn't run
// them. tools/build.sh integration starts the server in each auth mode and runs the suites "all" + that mode's suite.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
