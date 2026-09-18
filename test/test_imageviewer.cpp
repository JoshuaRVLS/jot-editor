#include "imageviewer.h"
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
  // A valid 1x1 PPM, so a present ImageMagick can decode it and an absent one
  // still leaves the fallback preview to scroll through. Both leave a preview
  // with content, which is all these cases need.
  std::string write_temp_image(const std::string &name)
  {
    std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path);
    out << "P3\n1 1\n255\n255 0 0\n";
    out.close();
    return path.string();
  }
} // namespace

TEST_CASE("Image Viewer Backend Parsing", "[jot]")
{
  REQUIRE(ImageViewer::parse_backend("auto") == ImageViewer::Backend::Auto);
  REQUIRE(ImageViewer::parse_backend("kitty") == ImageViewer::Backend::Kitty);
  REQUIRE(ImageViewer::parse_backend("sixel") == ImageViewer::Backend::Sixel);
  REQUIRE(ImageViewer::parse_backend("cell") == ImageViewer::Backend::Cell);
  REQUIRE(ImageViewer::parse_backend("off") == ImageViewer::Backend::Off);
  REQUIRE(ImageViewer::parse_backend("unknown") == ImageViewer::Backend::Auto);
}

TEST_CASE("Image Viewer Base64", "[jot]")
{
  REQUIRE(ImageViewer::base64_encode("") == "");
  REQUIRE(ImageViewer::base64_encode("f") == "Zg==");
  REQUIRE(ImageViewer::base64_encode("fo") == "Zm8=");
  REQUIRE(ImageViewer::base64_encode("foo") == "Zm9v");
  REQUIRE(ImageViewer::base64_encode("/tmp/a.png") == "L3RtcC9hLnBuZw==");
}

TEST_CASE("Image Viewer Recognises Image Paths", "[jot]")
{
  ImageViewer viewer;

  REQUIRE(viewer.is_image_file("photo.PNG"));
  REQUIRE(viewer.is_image_file("/tmp/a.jpeg"));
  REQUIRE(viewer.is_image_file("icon.svg"));
  REQUIRE(viewer.is_image_file("scan.tiff"));
  REQUIRE_FALSE(viewer.is_image_file("/tmp/notes.txt"));
  REQUIRE_FALSE(viewer.is_image_file("/tmp/Makefile"));
}

TEST_CASE("Image Viewer Previews Scroll", "[jot]")
{
  const std::string path = write_temp_image("jot-imageviewer-scroll.ppm");
  ImageViewer viewer;
  viewer.open(path);

  REQUIRE(viewer.is_active());
  REQUIRE(viewer.get_current() == path);
  REQUIRE(viewer.preview_content_rows() > 0);

  // The preview is not a buffer, so the scroll offset is clamped to its own
  // content: the wheel and the navigation keys pan these rows.
  viewer.set_preview_scroll(-5);
  REQUIRE(viewer.get_preview_scroll() == 0);

  // Nothing has rendered yet, so the viewport is taken as a single row and the
  // bottom of the preview is the last row.
  const int max_scroll = viewer.preview_content_rows() - 1;
  viewer.set_preview_scroll(100000);
  REQUIRE(viewer.get_preview_scroll() == max_scroll);

  if (max_scroll >= 1)
  {
    viewer.scroll_preview(-1);
    REQUIRE(viewer.get_preview_scroll() == max_scroll - 1);
  }

  // Re-opening a picture already generated is served from the cache rather
  // than copied through ImageMagick again.
  viewer.close();
  REQUIRE(viewer.get_preview_scroll() == 0);
  viewer.open(path);
  REQUIRE(viewer.preview_content_rows() > 0);

  viewer.close();
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_CASE("Image Viewer Kitty Command", "[jot]")
{
  std::string cmd = ImageViewer::build_kitty_file_command("/tmp/a.png", 2, 3, 40, 12);

  REQUIRE(cmd.find("\x1b[4;3H") != std::string::npos);
  REQUIRE(cmd.find("\x1b_G") != std::string::npos);
  REQUIRE(cmd.find("a=T") != std::string::npos);
  REQUIRE(cmd.find("f=100") != std::string::npos);
  // The transmission carries the viewer's image id, and the delete names that
  // id: "delete all" would take out placements that were never ours.
  REQUIRE(cmd.find("i=1001") != std::string::npos);
  const std::string del = ImageViewer::build_kitty_delete_command();
  REQUIRE(del.find("d=i,i=1001") != std::string::npos);
  REQUIRE(del.find("d=A") == std::string::npos);
  REQUIRE(cmd.find("t=f") != std::string::npos);
  REQUIRE(cmd.find("c=40") != std::string::npos);
  REQUIRE(cmd.find("r=12") != std::string::npos);
  REQUIRE(cmd.find("L3RtcC9hLnBuZw==") != std::string::npos);
  REQUIRE(cmd.find("\x1b\\") != std::string::npos);
}

TEST_CASE("Image Viewer Sixel Command", "[jot]")
{
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
