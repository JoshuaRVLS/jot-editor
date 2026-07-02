#include <catch2/catch_test_macros.hpp>
#include "imageviewer.h"

#include <string>

TEST_CASE("Image Viewer Backend Parsing", "[jot]") {
  REQUIRE(ImageViewer::parse_backend("auto") == ImageViewer::Backend::Auto);
  REQUIRE(ImageViewer::parse_backend("kitty") == ImageViewer::Backend::Kitty);
  REQUIRE(ImageViewer::parse_backend("sixel") == ImageViewer::Backend::Sixel);
  REQUIRE(ImageViewer::parse_backend("cell") == ImageViewer::Backend::Cell);
  REQUIRE(ImageViewer::parse_backend("off") == ImageViewer::Backend::Off);
  REQUIRE(ImageViewer::parse_backend("unknown") == ImageViewer::Backend::Auto);
}

TEST_CASE("Image Viewer Base64", "[jot]") {
  REQUIRE(ImageViewer::base64_encode("") == "");
  REQUIRE(ImageViewer::base64_encode("f") == "Zg==");
  REQUIRE(ImageViewer::base64_encode("fo") == "Zm8=");
  REQUIRE(ImageViewer::base64_encode("foo") == "Zm9v");
  REQUIRE(ImageViewer::base64_encode("/tmp/a.png") == "L3RtcC9hLnBuZw==");
}

TEST_CASE("Image Viewer Kitty Command", "[jot]") {
  std::string cmd =
      ImageViewer::build_kitty_file_command("/tmp/a.png", 2, 3, 40, 12);

  REQUIRE(cmd.find("\x1b[4;3H") != std::string::npos);
  REQUIRE(cmd.find("\x1b_G") != std::string::npos);
  REQUIRE(cmd.find("a=T") != std::string::npos);
  REQUIRE(cmd.find("f=100") != std::string::npos);
  REQUIRE(cmd.find("t=f") != std::string::npos);
  REQUIRE(cmd.find("c=40") != std::string::npos);
  REQUIRE(cmd.find("r=12") != std::string::npos);
  REQUIRE(cmd.find("L3RtcC9hLnBuZw==") != std::string::npos);
  REQUIRE(cmd.find("\x1b\\") != std::string::npos);
}

TEST_CASE("Image Viewer Sixel Command", "[jot]") {
  std::string cmd = ImageViewer::build_sixel_command("/tmp/a b.png", 10, 5);

  REQUIRE(cmd.find("img2sixel") != std::string::npos);
  REQUIRE(cmd.find("-w 80") != std::string::npos);
  REQUIRE(cmd.find("-h 80") != std::string::npos);
#ifdef _WIN32
  REQUIRE(cmd.find("\"/tmp/a b.png\"") != std::string::npos);
#else
  REQUIRE(cmd.find("'/tmp/a b.png'") != std::string::npos);
#endif
}
