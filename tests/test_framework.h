#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal Test Runner
struct TestCase {
  std::string name;
  std::function<void()> func;
};

// Global tests vector (defined in test_main.cpp)
extern std::vector<TestCase> tests;

void register_test(const std::string &name, std::function<void()> func);

#define TEST(name)                                                             \
  void name();                                                                 \
  struct Register_##name {                                                     \
    Register_##name() { register_test(#name, name); }                          \
  } register_##name;                                                           \
  void name()

// Assertion macros
#define ASSERT_EQ(a, b)                                                        \
  if ((a) != (b)) {                                                            \
    std::cerr << "FAILED: " << #a << " == " << #b << "\n";                     \
    std::cerr << "  Values: " << (a) << " != " << (b) << "\n";                 \
    std::cerr << "  File: " << __FILE__ << ":" << __LINE__ << "\n";            \
    throw std::runtime_error("Assertion failed");                              \
  }

#define ASSERT_TRUE(a)                                                         \
  if (!(a)) {                                                                  \
    std::cerr << "FAILED: " << #a << " is true\n";                             \
    std::cerr << "  File: " << __FILE__ << ":" << __LINE__ << "\n";            \
    throw std::runtime_error("Assertion failed");                              \
  }

#endif
