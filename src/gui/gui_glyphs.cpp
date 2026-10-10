// Glyph atlas: renders codepoints (and shaped glyph ids, for ligature runs)
// with FreeType into the shared R8 atlas texture on demand, caching UV rects
// + pen metrics per (codepoint, style).
// Each glyph is uploaded from FreeType's own bitmap, so there is no CPU-side
// copy of the atlas to carry. The atlas is one 2048-wide row band at a time;
// when a glyph would overflow it, ensure_glyph returns false and the caller
// (paint path) clears the whole atlas once and retries, rebuilding the cache
// lazily.
#include "gui/gui.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace
{
  // Shaped glyphs get their own slice of the key space: a font's glyph id is
  // not a Unicode codepoint, so without a fence gid 65 would answer for 'A'.
  // Any value above 0x10FFFF works; this one leaves room for the ids a large
  // symbol font hands out.
  constexpr uint64_t kGlyphIdBase = 0x200000ull;
} // namespace

bool UIGui::ensure_glyph(uint32_t codepoint, int style)
{
  return ensure_glyph_key(0, codepoint, style);
}

bool UIGui::ensure_glyph_id(uint32_t glyph_id, int style)
{
  return ensure_glyph_key(kGlyphIdBase, glyph_id, style);
}

bool UIGui::ensure_glyph_key(uint64_t base, uint32_t id, int style)
{
  const uint64_t key = ((uint64_t)(base + id) << 2) | (uint64_t)(style & 3);
  if (glyphs_.find(key) != glyphs_.end())
  {
    return true;
  }
  FT_Face face = faces_[style] ? faces_[style] : faces_[kStyleRegular];
  if (!face)
  {
    return false;
  }
  // A codepoint goes through the cmap, a shaped id does not; FT_Load_Char is
  // exactly FT_Get_Char_Index + FT_Load_Glyph, so both paths rasterize the
  // same way and cache under the same key shape.
  const FT_ULong gid = base ? (FT_ULong)id : (FT_ULong)FT_Get_Char_Index(face, (FT_ULong)id);
  if (FT_Load_Glyph(face, gid, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
  {
    return false;
  }
  FT_GlyphSlot slot = face->glyph;
  const int gw = slot->bitmap.width;
  const int gh = slot->bitmap.rows;
  if (std::getenv("JOT_GUI_DEBUG") && glyphs_.size() < 12)
  {
    std::fprintf(stderr,
                 "jot-gui: glyph cp=%u gid=%lu style=%d bmp=%dx%d pitch=%d advance=%.1f\n",
                 id,
                 (unsigned long)gid,
                 style,
                 gw,
                 gh,
                 slot->bitmap.pitch,
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

  {
    // Uploaded straight from FreeType's bitmap, which is the only copy of the
    // glyph: rows are its own pitch apart, and the pixel-store is global GL
    // state, so the guard restores it afterwards (see AtlasPixelStoreGuard).
    AtlasPixelStoreGuard pixel_store(slot->bitmap.pitch);
    glBindTexture(GL_TEXTURE_2D, atlas_tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, atlas_x_, atlas_y_, gw, gh, GL_RED, GL_UNSIGNED_BYTE,
                    slot->bitmap.buffer);
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

const GuiGlyph *UIGui::ensure_shaped_glyph(uint32_t glyph_id, int style)
{
  if (!ensure_glyph_id(glyph_id, style))
  {
    clear_atlas();
    if (!ensure_glyph_id(glyph_id, style))
    {
      return nullptr;
    }
  }
  const uint64_t key = ((uint64_t)(kGlyphIdBase + glyph_id) << 2) | (uint64_t)(style & 3);
  auto it = glyphs_.find(key);
  return it == glyphs_.end() ? nullptr : &it->second;
}