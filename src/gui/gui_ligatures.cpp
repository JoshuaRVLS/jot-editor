// Ligature shaping: the HarfBuzz side of the GUI's glyph painter, see
// gui_ligatures.h for why runs are shaped at all. The whole implementation
// lives under JOT_GUI_LIGATURES (set by src/CMakeLists.txt when pkg-config
// finds harfbuzz); without it the shaper answers every call with "nothing to
// draw here" and the painter keeps its per-cell path.
#include "gui/gui_ligatures.h"

#ifdef JOT_GUI_LIGATURES

#include <hb-ft.h>
#include <hb.h>

#include <algorithm>

namespace jot_gui
{
  LigatureShaper::~LigatureShaper()
  {
    clear();
  }

  void LigatureShaper::clear()
  {
    runs_.clear();
    for (int i = 0; i < kStyleCount; i++)
    {
      // hb-ft takes its own reference on the face, so destroying the font
      // here is what releases it; the caller may FT_Done_Face right after.
      if (fonts_[i])
      {
        hb_font_destroy(fonts_[i]);
        fonts_[i] = nullptr;
      }
      faces_[i] = nullptr;
      font_px_[i] = -1;
    }
  }

  hb_font_t *LigatureShaper::font_for(int style, FT_Face face, int font_px)
  {
    if (style < 0 || style >= kStyleCount || !face)
    {
      return nullptr;
    }
    if (fonts_[style] && faces_[style] == face && font_px_[style] == font_px)
    {
      return fonts_[style];
    }
    if (fonts_[style])
    {
      hb_font_destroy(fonts_[style]);
      fonts_[style] = nullptr;
    }
    // Referenced, not borrowed: the font keeps the face alive across the
    // FT_Done_Face that replaces it on a family switch.
    fonts_[style] = hb_ft_font_create_referenced(face);
    faces_[style] = fonts_[style] ? face : nullptr;
    // A new face object already carries the size it was created with, and a
    // zoom reuses the face with a new size, so keying the cache on both keeps
    // hb-ft's metrics in step with what FreeType rasterizes.
    font_px_[style] = fonts_[style] ? font_px : -1;
    return fonts_[style];
  }

  const std::vector<ShapedCluster> *
  LigatureShaper::shape(int style, const std::string &text, FT_Face face, int font_px)
  {
    // A run shorter than two cells cannot ligate, and shaping it would put an
    // entry in the cache for every punctuation cell on screen.
    if (text.size() < 2)
    {
      return nullptr;
    }
    if (style < 0 || style >= kStyleCount || !face)
    {
      return nullptr;
    }
    std::string key;
    key.reserve(text.size() + 1);
    key.push_back((char)('0' + style));
    key += text;
    auto it = runs_.find(key);
    if (it != runs_.end())
    {
      return &it->second;
    }

    hb_font_t *font = font_for(style, face, font_px);
    if (!font)
    {
      return nullptr;
    }
    hb_buffer_t *buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text.data(), (int)text.size(), 0, -1);
    // The runs are ASCII, so the guess is "Latin, left to right" whatever the
    // locale is; it still matters for the shaping direction.
    hb_buffer_guess_segment_properties(buffer);
    // Default features: liga/calt/clig are on, which is exactly what a
    // terminal applies to the same font -- that equivalence is the point.
    hb_shape(font, buffer, nullptr, 0);

    unsigned int count = 0;
    const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &count);
    // Cluster ids are byte offsets into the run. The painter only ever hands
    // over one byte per cell, so an offset is also the cell index.
    std::vector<ShapedCluster> clusters;
    clusters.reserve(count);
    bool ok = info != nullptr;
    for (unsigned int i = 0; ok && i < count; i++)
    {
      const unsigned int start = info[i].cluster;
      const unsigned int end = (i + 1 < count) ? info[i + 1].cluster : (unsigned int)text.size();
      if (start >= end || end > text.size())
      {
        // Not a left-to-right run of one-byte cells (a font can reorder
        // punctuation): bail out rather than guess which cell a glyph belongs
        // to, and let the per-cell path paint it.
        ok = false;
        break;
      }
      ShapedCluster cluster;
      cluster.cell = (int)start;
      cluster.cells = (int)(end - start);
      cluster.glyph = info[i].codepoint;
      clusters.push_back(cluster);
    }
    hb_buffer_destroy(buffer);
    if (!ok)
    {
      return nullptr;
    }

    if (runs_.size() >= kMaxRuns)
    {
      runs_.clear();
    }
    // Cached even when nothing was substituted: that is what stops the painter
    // from re-shaping a run that does not ligate on every frame.
    auto placed = runs_.emplace(std::move(key), std::move(clusters));
    return &placed.first->second;
  }
} // namespace jot_gui

#else // !JOT_GUI_LIGATURES

namespace jot_gui
{
  LigatureShaper::~LigatureShaper()
  {
  }

  void LigatureShaper::clear()
  {
  }

  const std::vector<ShapedCluster> *LigatureShaper::shape(int, const std::string &, FT_Face, int)
  {
    return nullptr;
  }
} // namespace jot_gui

#endif // JOT_GUI_LIGATURES
