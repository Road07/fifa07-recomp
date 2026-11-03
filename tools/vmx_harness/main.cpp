#include "catch2/catch_test_macros.hpp"
int main() {
  unsigned failures = 0;
  for (const auto& test : vmx_harness::tests()) {
    try { test.run(); }
    catch (const std::exception& error) {
      std::printf("FAIL %s: %s\n", test.name, error.what());
      ++failures;
    }
  }
  std::printf("RESULT %zu vector instruction cases, %u failures\n", vmx_harness::tests().size(), failures);
  return failures ? 1 : 0;
}
