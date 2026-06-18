#include "cpp_assist.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("C++ Assist Generates Missing Methods", "[jot]") {
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

  REQUIRE(result.generated_count == 2);
  REQUIRE(result.source_text.find("Widget::Widget()") != std::string::npos);
  REQUIRE(result.source_text.find("int Widget::count() const") !=
              std::string::npos);
  REQUIRE(result.source_text.find("Widget::skip(") == std::string::npos);
}

TEST_CASE("C++ Assist Preserves Noexcept Suffix", "[jot]") {
  std::string header =
      "class Socket {\n"
      "public:\n"
      "  void close() noexcept;\n"
      "};\n";

  auto result = CppAssist::generate_missing_implementations(
      header, "", "socket.hpp", "socket.cpp", false);

  REQUIRE(result.generated_count == 1);
  REQUIRE(result.source_text.find("void Socket::close() noexcept") !=
              std::string::npos);
}

TEST_CASE("C++ Assist Namespace Generation", "[jot]") {
  std::string header =
      "namespace app {\n"
      "class Service {\n"
      "public:\n"
      "  std::string name() const;\n"
      "};\n"
      "}\n";

  auto result = CppAssist::generate_missing_implementations(
      header, "", "service.hpp", "service.cpp", false);

  REQUIRE(result.generated_count == 1);
  REQUIRE(result.created_source);
  REQUIRE(result.source_text.find("#include \"service.hpp\"") !=
              std::string::npos);
  REQUIRE(result.source_text.find("namespace app") != std::string::npos);
  REQUIRE(result.source_text.find("std::string Service::name(") !=
              std::string::npos);
  REQUIRE(result.source_text.find("return {};") != std::string::npos);
}

TEST_CASE("C++ Assist Counterpart And Header Skeleton", "[jot]") {
  REQUIRE(CppAssist::is_header_path("thing.hpp"));
  REQUIRE(CppAssist::is_source_path("thing.cpp"));
  REQUIRE(CppAssist::counterpart_path_for("thing.hpp").string() == "thing.cpp");
  REQUIRE(CppAssist::counterpart_path_for("thing.cpp").string() == "thing.hpp");
  REQUIRE(CppAssist::header_skeleton("thing.hpp").find("#ifndef THING_HPP_") !=
              std::string::npos);
}
