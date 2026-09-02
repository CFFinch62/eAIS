#include <cstdio>

#include "test_support.h"

// Host-side tests for the AIS decoding layer. No hardware, no PlatformIO:
//
//   test/run_tests.sh
//
// Every expected value here was derived from the AIS specification with an
// independent decoder, not read back out of the code under test. Several of the
// sentences are synthesised so the expected contents are known exactly rather
// than assumed from a sample found online.
int main() {
  std::printf("eAIS decoder tests\n==================\n");

  runSentenceTests();
  runDecoderTests();
  runTargetTableTests();

  std::printf("\n==================\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
