// GUI frontend lifecycle: window + GL 3.3 core context creation, FreeType
// face loading, shader program / VBO / texture setup, and atlas clearing.
// Purely one-time setup -- per-frame work lives in gui_render.cpp and the
// animation state in gui_anim.cpp.
#include "gui/gui.h"
#include "gui/gui_fit.h"

#include <SDL2/SDL.h>
// Mesa's gl.h only declares core 2.0+ entry points under this macro.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
const char *kVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform vec2 u_scale; // 2/viewport_w, -2/viewport_h (y-down -> GL y-up)
out vec2 v_uv;
out vec4 v_color;
void main()
{
  gl_Position = vec4(a_pos.x * u_scale.x - 1.0, a_pos.y * u_scale.y + 1.0, 0.0, 1.0);
  v_uv = a_uv;
  v_color = a_color;
}
)";

const char *kFragmentShader = R"(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
out vec4 frag;
void main()
{
  float a = texture(u_tex, v_uv).r;
  frag = vec4(v_color.rgb, v_color.a * a);
}
)";

bool compile_shader(unsigned int type, const char *src, unsigned int &out)
{
  unsigned int sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  int ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
    std::fprintf(stderr, "jot-gui: shader compile failed: %s\n", log);
    glDeleteShader(sh);
    return false;
  }
  out = sh;
  return true;
}
} // namespace

UIGui::UIGui(int cols, int rows, int default_fg, int default_bg, int font_px)
    : UI(nullptr), font_px_(std::clamp(font_px, 8, 40))
{
  init_sdl_and_gl();
  init_freetype();

  // Window sized for the requested cell grid. Cell size comes from the
  // regular face's metrics at font_px_ (see init_freetype).
  window_w_ = std::max(1, (int)(cols * cell_w_));
  window_h_ = std::max(1, (int)(rows * cell_h_));

  window_ = SDL_CreateWindow("jot",
                             SDL_WINDOWPOS_UNDEFINED,
                             SDL_WINDOWPOS_UNDEFINED,
                             window_w_,
                             window_h_,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
                                 | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window_)
  {
    throw std::runtime_error(std::string("jot-gui: SDL_CreateWindow failed: ") + SDL_GetError());
  }
  gl_context_ = SDL_GL_CreateContext(window_);
  if (!gl_context_)
  {
    throw std::runtime_error(std::string("jot-gui: SDL_GL_CreateContext failed: ") + SDL_GetError());
  }
  // Vsync: swap is paced by the monitor refresh (60/120/144Hz...).
  if (SDL_GL_SetSwapInterval(1) != 0)
  {
    // Non-fatal: without vsync the render timer still paces painting.
    std::fprintf(stderr, "jot-gui: vsync unavailable (%s)\n", SDL_GetError());
  }
  SDL_GL_GetDrawableSize(window_, &pixel_w_, &pixel_h_);
  scale_ = jot_gui::display_scale(window_w_, window_h_, pixel_w_, pixel_h_);

  if (!compile_shaders() || !create_textures())
  {
    throw std::runtime_error("jot-gui: GL setup failed");
  }
  glViewport(0, 0, pixel_w_, pixel_h_);
  SDL_StartTextInput();

  // Grid bookkeeping from the base class (no terminal to clear).
  resize(cols, rows);
  set_default_colors(default_fg, default_bg);

  if (std::getenv("JOT_GUI_DEBUG"))
  {
    std::fprintf(stderr,
                 "jot-gui: font_px=%d cell=%.1fx%.1f ascent=%.1f window=%dx%d grid=%dx%d\n",
                 font_px_, cell_w_, cell_h_, ascent_, window_w_, window_h_, cols, rows);
  }
}

UIGui::~UIGui()
{
  if (gl_context_)
  {
    SDL_GL_DeleteContext(static_cast<SDL_GLContext>(gl_context_));
  }
  if (window_)
  {
    SDL_DestroyWindow(window_);
  }
  for (FT_Face face : faces_)
  {
    if (face)
    {
      FT_Done_Face(face);
    }
  }
  if (ft_lib_)
  {
    FT_Done_FreeType(ft_lib_);
  }
  SDL_Quit();
}

bool UIGui::init_sdl_and_gl()
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
  {
    throw std::runtime_error(std::string("jot-gui: SDL_Init failed: ") + SDL_GetError());
  }
  // OpenGL 3.3 core profile.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  return true;
}

bool UIGui::init_freetype()
{
  if (FT_Init_FreeType(&ft_lib_) != 0)
  {
    throw std::runtime_error("jot-gui: FreeType init failed");
  }

  const char *home = std::getenv("HOME");
  const std::string base = home ? std::string(home) + "/.local/share/fonts" : "";

  // Search order: user fonts, system TTF, DejaVu fallback. The Nerd Font
  // Mono variant carries the icon glyphs jot's UI uses (kind icons, tree
  // guides, toast badges...).
  const std::vector<std::string> regular_paths = {
      base + "/JetBrainsMonoNerdFontMono-Regular.ttf",
      "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Regular.ttf",
      "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
  };
  const std::vector<std::string> bold_paths = {
      base + "/JetBrainsMonoNerdFontMono-Bold.ttf",
      "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Bold.ttf",
      "/usr/share/fonts/TTF/JetBrainsMono-Bold.ttf",
  };
  const std::vector<std::string> italic_paths = {
      base + "/JetBrainsMonoNerdFontMono-Italic.ttf",
      "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Italic.ttf",
      "/usr/share/fonts/TTF/JetBrainsMono-Italic.ttf",
  };
  const std::vector<std::string> bold_italic_paths = {
      base + "/JetBrainsMonoNerdFontMono-BoldItalic.ttf",
      "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-BoldItalic.ttf",
      "/usr/share/fonts/TTF/JetBrainsMono-BoldItalic.ttf",
  };

  // JOT_GUI_FONT overrides the regular face (styles fall back to it).
  const char *override_font = std::getenv("JOT_GUI_FONT");
  if (override_font && override_font[0])
  {
    load_face(kStyleRegular, {override_font});
  }
  else
  {
    load_face(kStyleRegular, regular_paths);
  }
  load_face(kStyleBold, bold_paths);
  load_face(kStyleItalic, italic_paths);
  load_face(kStyleBoldItalic, bold_italic_paths);

  FT_Face regular = faces_[kStyleRegular];
  if (!regular)
  {
    throw std::runtime_error("jot-gui: no usable font found (set JOT_GUI_FONT to a .ttf)");
  }

  FT_Set_Pixel_Sizes(regular, 0, font_px_);
  refresh_cell_metrics();
  return true;
}

void UIGui::refresh_scale()
{
  SDL_GetWindowSize(window_, &window_w_, &window_h_);
  SDL_GL_GetDrawableSize(window_, &pixel_w_, &pixel_h_);
  scale_ = jot_gui::display_scale(window_w_, window_h_, pixel_w_, pixel_h_);
}

void UIGui::refresh_cell_metrics()
{
  // Monospace cell: width from the max advance, height from the full
  // ascender->descender span plus a pixel of padding.
  FT_Face regular = faces_[kStyleRegular];
  if (!regular)
  {
    return;
  }
  cell_w_ = (float)regular->size->metrics.max_advance / 64.0f;
  ascent_ = (float)regular->size->metrics.ascender / 64.0f;
  float descender = (float)regular->size->metrics.descender / 64.0f;
  cell_h_ = std::max(1.0f, ascent_ - descender + 1.0f);
}

void UIGui::apply_font_zoom(int step)
{
  apply_font_size(font_px_ + step);
}

void UIGui::apply_font_size(int px)
{
  const int new_px = std::clamp(px, 8, 40);
  if (new_px == font_px_)
  {
    return;
  }
  font_px_ = new_px;
  // The same pixel size must apply to every style face: ensure_glyph picks
  // faces by style, and metrics (cell size) come from the regular face.
  for (FT_Face face : faces_)
  {
    if (face)
    {
      FT_Set_Pixel_Sizes(face, 0, font_px_);
    }
  }
  // The atlas holds glyphs rendered at the old size; drop it wholesale and
  // let the next paint rebuild it lazily.
  clear_atlas();
  refresh_cell_metrics();

  // The window keeps its size; the grid re-fits around the new cell size
  // (same math as the SDL resize handler). The editor is told via a
  // synthesized EVENT_RESIZE by the pump, which relayouts the panes.
  refresh_scale();
  const jot_gui::GridFit fit = jot_gui::fit_grid(pixel_w_, pixel_h_, cell_w_, cell_h_);
  resize(fit.cols, fit.rows);
  if (std::getenv("JOT_GUI_DEBUG"))
  {
    std::fprintf(stderr,
                 "jot-gui: zoom font_px=%d cell=%.1fx%.1f grid=%dx%d scale=%.2f\n",
                 font_px_,
                 cell_w_,
                 cell_h_,
                 fit.cols,
                 fit.rows,
                 (double)scale_);
  }
}

bool UIGui::load_face(int style, const std::vector<std::string> &paths)
{
  for (const std::string &path : paths)
  {
    if (path.empty())
    {
      continue;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(ft_lib_, path.c_str(), 0, &face) == 0)
    {
      FT_Set_Pixel_Sizes(face, 0, font_px_);
      faces_[style] = face;
      return true;
    }
  }
  return false;
}

bool UIGui::compile_shaders()
{
  unsigned int vs = 0, fs = 0;
  if (!compile_shader(GL_VERTEX_SHADER, kVertexShader, vs))
  {
    return false;
  }
  if (!compile_shader(GL_FRAGMENT_SHADER, kFragmentShader, fs))
  {
    glDeleteShader(vs);
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);
  int ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
    std::fprintf(stderr, "jot-gui: program link failed: %s\n", log);
    return false;
  }

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, kMaxBatchVertices * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(2 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(4 * sizeof(float)));
  glBindVertexArray(0);

  vertex_.reserve(kMaxBatchVertices);
  return true;
}

bool UIGui::create_textures()
{
  // 1x1 white texture: the bg pass draws through the same shader with
  // alpha 1, so a white texel yields a fully opaque quad.
  glGenTextures(1, &white_tex_);
  glBindTexture(GL_TEXTURE_2D, white_tex_);
  const unsigned char white = 255;
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Glyph atlas: one R8 texture, filled lazily by ensure_glyph. Glyph
  // uploads are sub-rectangles of the CPU-side 2048-wide atlas buffer, so
  // the pixel-store must tell GL the source rows are kAtlasW bytes apart
  // (GL_UNPACK_ROW_LENGTH) and not 4-byte aligned (GL_UNPACK_ALIGNMENT 1)
  // or every glyph is read from a diagonal slice of mostly-empty memory
  // and renders as a few stray pixels. The pixel-store is global GL state,
  // so it is guarded here and restored after the upload -- otherwise every
  // later texture upload reads rows with the wrong stride and can crash the
  // driver.
  AtlasPixelStoreGuard pixel_store(kAtlasW);
  atlas_pixels_.assign((size_t)kAtlasW * kAtlasH, 0);
  glGenTextures(1, &atlas_tex_);
  glBindTexture(GL_TEXTURE_2D, atlas_tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasW, kAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE,
               atlas_pixels_.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return true;
}

void UIGui::clear_atlas()
{
  AtlasPixelStoreGuard pixel_store(kAtlasW);
  std::fill(atlas_pixels_.begin(), atlas_pixels_.end(), 0);
  glBindTexture(GL_TEXTURE_2D, atlas_tex_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlasW, kAtlasH, GL_RED, GL_UNSIGNED_BYTE,
                  atlas_pixels_.data());
  atlas_x_ = 0;
  atlas_y_ = 0;
  atlas_row_h_ = 0;
  glyphs_.clear();
}