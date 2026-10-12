#include "imageviewer.h"
#include "ui/terminal.h"
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
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

  void set_env_var(const char *name, const char *value)
  {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
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

TEST_CASE("Image Viewer Leaves The Box To The Picture", "[jot]")
{
  // A picture with no readable pixel size (the viewer has not been opened on
  // anything yet) asks for the pane's own box: natural_box is the arithmetic
  // behind the fit, with the file's size and the cell size already in hand.
  ImageViewer viewer;
  int w = 0;
  int h = 0;
  viewer.set_cell_pixel_size(0, 0);
  viewer.natural_box(64, 30, w, h);
  REQUIRE(w == 64);
  REQUIRE(h == 30);
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

TEST_CASE("Image Viewer Places Graphics At A Cell", "[jot]")
{
  // Both terminal-side protocols paint at the cursor, and the frame writes its
  // graphics after the cells, so each placement carries the move that lands it
  // in the pane instead of wherever the last painted row left the cursor.
  REQUIRE(ImageViewer::cursor_move(0, 0) == "\x1b[1;1H");
  REQUIRE(ImageViewer::cursor_move(2, 3) == "\x1b[4;3H");
}

TEST_CASE("Image Viewer Sixel Cache Keys", "[jot]")
{
  // A redraw re-sends the placement, and the converted bytes are only reusable
  // at the raster size they were made for, so the key separates both -- without
  // letting a path read as part of a size.
  REQUIRE(ImageViewer::sixel_cache_key("/a.png", 40, 20)
          == ImageViewer::sixel_cache_key("/a.png", 40, 20));
  REQUIRE(ImageViewer::sixel_cache_key("/a.png", 40, 20)
          != ImageViewer::sixel_cache_key("/a.png", 41, 20));
  REQUIRE(ImageViewer::sixel_cache_key("/a.png", 40, 20)
          != ImageViewer::sixel_cache_key("/b.png", 40, 20));
  REQUIRE(ImageViewer::sixel_cache_key("/a2.png", 4, 0)
          != ImageViewer::sixel_cache_key("/a", 24, 0));
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

TEST_CASE("Image Viewer Places A Picture At Its Own Resolution", "[jot]")
{
  int w = 0;
  int h = 0;

  // One image pixel per screen pixel: 300x200 through a 10x20 cell is 30x10
  // cells, and it stays that size in a pane with room to spare. A picture is
  // not enlarged to fill the pane -- that is what "stretched" was.
  ImageViewer::fit_image_to_cells(300, 200, 10, 20, 200, 50, w, h);
  REQUIRE(w == 30);
  REQUIRE(h == 10);

  // A picture too big for the pane is scaled down once, with whichever side
  // runs out of room deciding the scale for both, so the shape survives: the
  // 400x100 cell box a 4000x2000 file asks for becomes 100x25 in a 100x50
  // pane, which is still 2:1 in pixels.
  ImageViewer::fit_image_to_cells(4000, 2000, 10, 20, 100, 50, w, h);
  REQUIRE(w == 100);
  REQUIRE(h == 25);

  // A tiny picture stays tiny: 16x16 pixels is 2x1 cells, not the pane.
  ImageViewer::fit_image_to_cells(16, 16, 10, 20, 100, 50, w, h);
  REQUIRE(w == 2);
  REQUIRE(h == 1);

  // A terminal that reports no cell size is measured with the 8x16 the sixel
  // conversion has always assumed.
  ImageViewer::fit_image_to_cells(80, 32, 0, 0, 200, 50, w, h);
  REQUIRE(w == 10);
  REQUIRE(h == 2);

  // No pixel size at all (no identify, an unreadable header) leaves the pane's
  // own box, which is the only thing the caller has to fall back on.
  ImageViewer::fit_image_to_cells(0, 0, 8, 16, 42, 17, w, h);
  REQUIRE(w == 42);
  REQUIRE(h == 17);
}

TEST_CASE("Image Viewer Sixel Command", "[jot]")
{
  // The helper's raster is in pixels, and both sides come from the picture's
  // own shape: asking for two sides that disagree with it is what squeezed the
  // placement into a pane-shaped box.
  std::string cmd = ImageViewer::build_sixel_command("/tmp/a b.png", 80, 40);

  REQUIRE(cmd.find("img2sixel") != std::string::npos);
  REQUIRE(cmd.find("-w 80") != std::string::npos);
  REQUIRE(cmd.find("-h 40") != std::string::npos);
  // The command is read back through a pipe, so only stderr may be silenced:
  // the sixel payload arrives on stdout, and swallowing it leaves the viewer
  // with nothing to place.
#ifdef _WIN32
  REQUIRE(cmd.find("\"/tmp/a b.png\"") != std::string::npos);
  REQUIRE(cmd.find(" 2>NUL") != std::string::npos);
  REQUIRE(cmd.find(" >NUL") == std::string::npos);
#else
  REQUIRE(cmd.find("'/tmp/a b.png'") != std::string::npos);
  REQUIRE(cmd.find(" 2>/dev/null") != std::string::npos);
  REQUIRE(cmd.find(" >/dev/null") == std::string::npos);
#endif
}

TEST_CASE("Image Viewer Requires The Kitty Protocol", "[jot]")
{
  // An image is drawn with the kitty graphics protocol and nothing else, so
  // the session is gated on the protocol's handshake. Only a real terminal can
  // answer it, so the tests fix the answer the way JOT_TEXT_SCALE_FORCE fixes
  // the text-scale probe: any value but "0" claims the protocol.
  set_env_var("JOT_KITTY_PROTOCOL", "1");
  REQUIRE(Terminal::kitty_graphics_probe());
  set_env_var("JOT_KITTY_PROTOCOL", "0");
  REQUIRE_FALSE(Terminal::kitty_graphics_probe());
  set_env_var("JOT_KITTY_PROTOCOL", "");

  // What main() records after the probe is what the picture path reads, so the
  // two can never disagree about a terminal.
  Terminal::remember_kitty_graphics(true);
  REQUIRE(Terminal::kitty_graphics_available());
  Terminal::remember_kitty_graphics(false);
  REQUIRE_FALSE(Terminal::kitty_graphics_available());
  // Left refusing: that is the state an unprobed process (a test that builds
  // an Editor with no main() in front of it) starts in.
  Terminal::remember_kitty_graphics(false);
}
