#include "test_framework.h"

std::vector<TestCase> tests;

void register_test(const std::string &name, std::function<void()> func) {
  tests.push_back({name, func});
}

int main() {
  int passed = 0;
  int failed = 0;

  std::cout << "Running " << tests.size() << " tests...\n";

  for (const auto &test : tests) {
    std::cout << "[ RUN      ] " << test.name << "\n";
    try {
      test.func();
      std::cout << "[       OK ] " << test.name << "\n";
      passed++;
    } catch (const std::exception &e) {
      std::cout << "[  FAILED  ] " << test.name << "\n";
      failed++;
    } catch (...) {
      std::cout << "[  FAILED  ] " << test.name << " (Unknown exception)\n";
      failed++;
    }
  }

  std::cout << "\nResults: " << passed << " passed, " << failed << " failed.\n";
  return failed > 0 ? 1 : 0;
}
