#include "test_framework.h"
#include "imageviewer.h"

#include <string>

TEST(TestImageViewerBackendParsing) {
  ASSERT_TRUE(ImageViewer::parse_backend("auto") == ImageViewer::Backend::Auto);
  ASSERT_TRUE(ImageViewer::parse_backend("kitty") == ImageViewer::Backend::Kitty);
  ASSERT_TRUE(ImageViewer::parse_backend("sixel") == ImageViewer::Backend::Sixel);
  ASSERT_TRUE(ImageViewer::parse_backend("cell") == ImageViewer::Backend::Cell);
  ASSERT_TRUE(ImageViewer::parse_backend("off") == ImageViewer::Backend::Off);
  ASSERT_TRUE(ImageViewer::parse_backend("unknown") == ImageViewer::Backend::Auto);
}

TEST(TestImageViewerBase64) {
  ASSERT_EQ(ImageViewer::base64_encode(""), "");
  ASSERT_EQ(ImageViewer::base64_encode("f"), "Zg==");
  ASSERT_EQ(ImageViewer::base64_encode("fo"), "Zm8=");
  ASSERT_EQ(ImageViewer::base64_encode("foo"), "Zm9v");
  ASSERT_EQ(ImageViewer::base64_encode("/tmp/a.png"), "L3RtcC9hLnBuZw==");
}

TEST(TestImageViewerKittyCommand) {
  std::string cmd =
      ImageViewer::build_kitty_file_command("/tmp/a.png", 2, 3, 40, 12);

  ASSERT_TRUE(cmd.find("\x1b[4;3H") != std::string::npos);
  ASSERT_TRUE(cmd.find("\x1b_G") != std::string::npos);
  ASSERT_TRUE(cmd.find("a=T") != std::string::npos);
  ASSERT_TRUE(cmd.find("f=100") != std::string::npos);
  ASSERT_TRUE(cmd.find("t=f") != std::string::npos);
  ASSERT_TRUE(cmd.find("c=40") != std::string::npos);
  ASSERT_TRUE(cmd.find("r=12") != std::string::npos);
  ASSERT_TRUE(cmd.find("L3RtcC9hLnBuZw==") != std::string::npos);
  ASSERT_TRUE(cmd.find("\x1b\\") != std::string::npos);
}

TEST(TestImageViewerSixelCommand) {
  std::string cmd = ImageViewer::build_sixel_command("/tmp/a b.png", 10, 5);

  ASSERT_TRUE(cmd.find("img2sixel") != std::string::npos);
  ASSERT_TRUE(cmd.find("-w 80") != std::string::npos);
  ASSERT_TRUE(cmd.find("-h 80") != std::string::npos);
  ASSERT_TRUE(cmd.find("'/tmp/a b.png'") != std::string::npos);
}
