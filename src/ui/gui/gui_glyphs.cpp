// Glyph atlas: renders codepoints with FreeType into the shared R8 atlas
// texture on demand, caching UV rects + pen metrics per (codepoint, style).
// The atlas is one 2048-wide row band at a time; when a glyph would overflow
// it, ensure_glyph returns false and the caller (paint path) clears the
// whole atlas once and retries, rebuilding the cache lazily.
#include "gui/gui.h"

#include <GL/gl.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool UIGui::ensure_glyph(uint32_t codepoint, int style)
{
  const uint64_t key = ((uint64_t)codepoint << 2) | (uint64_t)(style & 3);
  if (glyphs_.find(key) != glyphs_.end())
  {
    return true;
  }
  FT_Face face = faces_[style] ? faces_[style] : faces_[kStyleRegular];
  if (!face)
  {
    return false;
  }
  if (FT_Load_Char(face, (FT_ULong)codepoint, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
  {
    return false;
  }
  FT_GlyphSlot slot = face->glyph;
  const int gw = slot->bitmap.width;
  const int gh = slot->bitmap.rows;
  if (std::getenv("JOT_GUI_DEBUG") && glyphs_.size() < 12)
  {
    std::fprintf(stderr, "jot-gui: glyph cp=%u style=%d bmp=%dx%d pitch=%d advance=%.1f\n",
                 codepoint, style, gw, gh, slot->bitmap.pitch,
                 (float)slot->advance.x / 64.0f);
  }
  if (gw <= 0 || gh <= 0)
  {
    // Zero-size glyph (space etc.): still cache an empty entry so the
    // caller doesn't retry every frame.
    glyphs_[key] = {0, 0, 0, 0, 0, 0, (float)slot->advance.x / 64.0f};
    return true;
  }

  // Move to the next atlas row when this glyph doesn't fit.
  if (atlas_x_ + gw > kAtlasW)
  {
    atlas_x_ = 0;
    atlas_y_ += atlas_row_h_;
    atlas_row_h_ = 0;
  }
  if (atlas_y_ + gh > kAtlasH)
  {
    return false; // atlas full; caller clears and retries
  }

  for (int y = 0; y < gh; y++)
  {
    const unsigned char *src = slot->bitmap.buffer + (size_t)y * slot->bitmap.pitch;
    unsigned char *dst = atlas_pixels_.data() + (size_t)(atlas_y_ + y) * kAtlasW + atlas_x_;
    std::memcpy(dst, src, (size_t)gw);
  }
  {
    // Rows come from the 2048-wide CPU atlas buffer; restore the global
    // pixel-store state afterwards (see AtlasPixelStoreGuard).
    AtlasPixelStoreGuard pixel_store(kAtlasW);
    glBindTexture(GL_TEXTURE_2D, atlas_tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, atlas_x_, atlas_y_, gw, gh, GL_RED, GL_UNSIGNED_BYTE,
                    atlas_pixels_.data() + (size_t)atlas_y_ * kAtlasW + atlas_x_);
  }

  GuiGlyph g;
  g.u0 = (float)atlas_x_ / (float)kAtlasW;
  g.v0 = (float)atlas_y_ / (float)kAtlasH;
  g.u1 = (float)(atlas_x_ + gw) / (float)kAtlasW;
  g.v1 = (float)(atlas_y_ + gh) / (float)kAtlasH;
  g.bearing_x = (float)slot->bitmap_left;
  g.bearing_top = (float)slot->bitmap_top;
  g.advance = (float)slot->advance.x / 64.0f;
  glyphs_[key] = g;

  atlas_x_ += gw + 1;
  atlas_row_h_ = std::max(atlas_row_h_, gh);
  return true;
}