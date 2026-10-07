// Ligature shaping for the GUI glyph painter (jot --gui).
//
// The cell grid holds one grapheme per cell and the atlas resolves it with
// FreeType's cmap lookup, which never applies the font's own GSUB lookups.
// Programming fonts ink their joins through those lookups -- "->" is drawn as
// the font's connected arrow, not as a hyphen next to a greater-than -- so a
// cell-by-cell raster paints two separate marks where a terminal (which shapes
// with HarfBuzz) paints one. This shapes a run of adjacent cells instead and
// hands the painter the glyph ids the tables produced, so the GUI matches what
// the same font looks like everywhere else.
//
// Only runs the rules can plausibly reach are shaped: adjacent single-byte
// ASCII punctuation cells that share one ink (style, colours, dim/reverse).
// Everything else -- identifiers, CJK, a colour seam between two spans -- keeps
// the per-cell path, so the common case costs a hash lookup on a run that is
// already cached after its first frame.
//
// HarfBuzz is optional: without it (or when the setting turns ligatures off)
// shape() answers with an empty result and the painter draws every cell as it
// did before. See src/CMakeLists.txt for the pkg-config probe.
#ifndef UI_GUI_LIGATURES_H
#define UI_GUI_LIGATURES_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// The FreeType face handle, spelled the way gui.h declares it (repeating a
// typedef for the same type is legal, and keeping this header free of
// ft2build.h is what lets it compile standalone, see tools/check_headers.py).
typedef struct FT_FaceRec_ *FT_Face;

// Forward declaration of HarfBuzz's opaque font, so no hb header reaches the
// translation units that only paint cells.
struct hb_font_t;

namespace jot_gui
{
  // One cluster of a shaped run: `cells` cells starting at `cell` are inked by
  // `glyph`. `cells > 1` is a ligature (the font merged the cells into one
  // glyph); `cells == 1` with a glyph that differs from the cell's own cmap
  // entry is a contextual alternate, which is how faces like Fira Code and
  // JetBrains Mono join "->" -- two glyphs that touch, not one glyph.
  struct ShapedCluster
  {
    int cell = 0;
    int cells = 1;
    unsigned int glyph = 0;
  };

  class LigatureShaper
  {
  public:
    // Same four style slots as UIGui::FontStyle; asserted against it where the
    // shaper is used.
    static constexpr int kStyleCount = 4;
    // Punctuation runs are short and repeat endlessly ("->", "=>", "//"), so
    // the cache is keyed on the run text and never needs to be large; past
    // this many entries it is dropped wholesale rather than evicted one by one.
    static constexpr std::size_t kMaxRuns = 4096;

    LigatureShaper() = default;
    ~LigatureShaper();

    // Shapes `text` (one byte per cell, ASCII punctuation) against `face` at
    // `font_px` for `style`, and returns the cached clusters for it. A null
    // answer means shaping is unavailable for this call (no HarfBuzz in the
    // build, no face); a non-null, empty answer means the font left the run
    // alone and the caller should draw it cell by cell. Results are cached by
    // (style, text) until clear().
    const std::vector<ShapedCluster> *
    shape(int style, const std::string &text, FT_Face face, int font_px);

    // Drops every cached run and every HarfBuzz font. Faces are only ever
    // replaced after this runs, so no shaper can outlive the face it wraps.
    void clear();

  private:
    // The HarfBuzz view of `face`, built on first use and rebuilt when the face
    // or the pixel size changes (hb-ft reads the face's size, so a zoom has to
    // be picked up by a fresh font). Returns null when HarfBuzz is absent.
    hb_font_t *font_for(int style, FT_Face face, int font_px);

    hb_font_t *fonts_[kStyleCount] = {nullptr, nullptr, nullptr, nullptr};
    FT_Face faces_[kStyleCount] = {nullptr, nullptr, nullptr, nullptr};
    int font_px_[kStyleCount] = {-1, -1, -1, -1};
    // Key: one byte for the style, then the run's text.
    std::unordered_map<std::string, std::vector<ShapedCluster>> runs_;
  };
} // namespace jot_gui

#endif // UI_GUI_LIGATURES_H
