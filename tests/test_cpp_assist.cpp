#include "cpp_assist.h"
#include "test_framework.h"

TEST(TestCppAssistGeneratesMissingMethods) {
  std::string header =
      "class Widget {\n"
      "public:\n"
      "  Widget();\n"
      "  void reset();\n"
      "  int count() const;\n"
      "  void inline_only() {}\n"
      "  virtual void skip() = 0;\n"
      "};\n";

  auto result = CppAssist::generate_missing_implementations(
      header, "#include \"widget.hpp\"\n\nvoid Widget::reset() {}\n",
      "widget.hpp", "widget.cpp", true);

  ASSERT_EQ(result.generated_count, 2);
  ASSERT_TRUE(result.source_text.find("Widget::Widget()") != std::string::npos);
  ASSERT_TRUE(result.source_text.find("int Widget::count() const") !=
              std::string::npos);
  ASSERT_TRUE(result.source_text.find("Widget::skip(") == std::string::npos);
}

TEST(TestCppAssistPreservesNoexceptSuffix) {
  std::string header =
      "class Socket {\n"
      "public:\n"
      "  void close() noexcept;\n"
      "};\n";

  auto result = CppAssist::generate_missing_implementations(
      header, "", "socket.hpp", "socket.cpp", false);

  ASSERT_EQ(result.generated_count, 1);
  ASSERT_TRUE(result.source_text.find("void Socket::close() noexcept") !=
              std::string::npos);
}

TEST(TestCppAssistNamespaceGeneration) {
  std::string header =
      "namespace app {\n"
      "class Service {\n"
      "public:\n"
      "  std::string name() const;\n"
      "};\n"
      "}\n";

  auto result = CppAssist::generate_missing_implementations(
      header, "", "service.hpp", "service.cpp", false);

  ASSERT_EQ(result.generated_count, 1);
  ASSERT_TRUE(result.created_source);
  ASSERT_TRUE(result.source_text.find("#include \"service.hpp\"") !=
              std::string::npos);
  ASSERT_TRUE(result.source_text.find("namespace app") != std::string::npos);
  ASSERT_TRUE(result.source_text.find("std::string Service::name(") !=
              std::string::npos);
  ASSERT_TRUE(result.source_text.find("return {};") != std::string::npos);
}

TEST(TestCppAssistCounterpartAndHeaderSkeleton) {
  ASSERT_TRUE(CppAssist::is_header_path("thing.hpp"));
  ASSERT_TRUE(CppAssist::is_source_path("thing.cpp"));
  ASSERT_EQ(CppAssist::counterpart_path_for("thing.hpp").string(), "thing.cpp");
  ASSERT_EQ(CppAssist::counterpart_path_for("thing.cpp").string(), "thing.hpp");
  ASSERT_TRUE(CppAssist::header_skeleton("thing.hpp").find("#ifndef THING_HPP_") !=
              std::string::npos);
}
