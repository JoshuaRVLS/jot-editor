#ifndef FEATURES_TAB_ORDER_H
#define FEATURES_TAB_ORDER_H

// The workspace's buffer order -- what the tab strip shows, in the order it
// shows it.
//
// Buffers live in a vector that is erased from the middle (`close_buffer_at`),
// so a buffer's index is not its identity and every index goes stale the moment
// a tab closes to its left. The order therefore refers to buffers by *uid*: a
// `FileBuffer::tab_uid`, handed out on first sight and carried by the buffer for
// as long as it lives (barbar.nvim's "buffer number" role, minus the renaming).
//
// The model is fed the live buffer vector and reconciles: uids it has never
// seen are placed according to the insert policy, uids whose buffer is gone are
// dropped, and the order stays a list of live buffers only. Reconciling rather
// than being told about every open/close keeps the one call site honest
// (`Editor::sync_tab_order`) instead of trusting eight different `push_back`s.
//
// Pure logic, no editor state: see test/test_tab_order.cpp.
#include "core/buffer.h"
#include <string>
#include <unordered_map>
#include <vector>

// The strip's geometry, kept here with the order because both answer the same
// question -- what the strip shows -- and both are pure (test/test_tab_order.cpp).
namespace TabStrip
{
  // Natural widths shrunk to fit `available`: a short tab keeps its width, the
  // long ones give up the difference, and nothing goes below `min_cells` (the
  // label is ellipsized to it). Returns false when even that does not fit, in
  // which case the caller shows a window of the tabs instead of all of them.
  bool allocate_widths(const std::vector<int> &natural,
                       int available,
                       int min_cells,
                       std::vector<int> &out);

  // Labels for tabs whose file names collide: duplicated names grow the
  // shortest parent prefix that tells them apart ("frame.cpp <render>"), and a
  // name that is unique anywhere on the strip is left alone. `paths` are the
  // buffers' file paths; empty paths mean unnamed buffers ([No Name]).
  std::vector<std::string> unique_names(const std::vector<std::string> &paths);
} // namespace TabStrip

// Where a buffer that the model has never seen goes. `after_current` is what a
// file picker does in most editors (the new tab opens next to the one you were
// in); `start`/`end` are barbar's insert_at_start / insert_at_end.
enum class TabInsert
{
  TAB_INSERT_AFTER_CURRENT,
  TAB_INSERT_START,
  TAB_INSERT_END
};

// barbar's :BufferOrderBy* family.
enum class TabSort
{
  TAB_SORT_NONE,
  TAB_SORT_NAME,
  TAB_SORT_PATH,
  TAB_SORT_LANGUAGE,
  TAB_SORT_BUFFER,
  TAB_SORT_WINDOW
};

class TabOrder
{
public:
  // Reconciles the order against `buffers`: assigns uids to buffers that have
  // none, drops uids whose buffer is gone, and places new buffers by `insert`
  // relative to `current_uid` (the buffer the active pane is showing).
  void sync(std::vector<FileBuffer> &buffers, long long current_uid, TabInsert insert);

  // The live buffers in strip order, as buffer indices into `buffers`.
  std::vector<int> indices(const std::vector<FileBuffer> &buffers) const;

  // Position of a buffer in the strip, or -1 when it is not in the order.
  int position_of(const std::vector<FileBuffer> &buffers, int buffer_index) const;

  // Where the strip's cursor should land for a position, clamped into range.
  int index_at(const std::vector<FileBuffer> &buffers, int position) const;

  bool pinned(int buffer_index, const std::vector<FileBuffer> &buffers) const;
  // Pins/unpins explicitly: the context menu offers Pin and Unpin as separate
  // entries, so each one has to land on its own state rather than flip.
  void set_pinned(int buffer_index, const std::vector<FileBuffer> &buffers, bool pinned);
  void toggle_pin(int buffer_index, const std::vector<FileBuffer> &buffers);
  bool any_pinned() const
  {
    return !pinned_.empty();
  }

  // Moves a buffer one place earlier (-1) or later (+1). Pinned buffers stay in
  // the pinned block: a move that would cross the pin boundary is refused, so
  // the command never silently unpins. Returns true when the order changed.
  bool move(const std::vector<FileBuffer> &buffers, int buffer_index, int delta);

  void sort(const std::vector<FileBuffer> &buffers, TabSort by);

  // The letter this buffer answers to in jump-to-buffer mode, assigned on first
  // ask and kept for the buffer's life (dropbar/barbar's "target letter"). The
  // hints are tried in order -- the file's initials, then the home row -- and a
  // taken letter is skipped rather than reassigned.
  char letter_for(int buffer_index, const std::vector<FileBuffer> &buffers);

  // The buffer a jump letter names, or -1 when no live buffer owns it: the
  // picker's lookup, where `letter_for` is the assignment.
  int buffer_for_letter(const std::vector<FileBuffer> &buffers, char letter) const;

  // Whether a letter is currently taken (for the strip's jump-mode display).
  bool has_letters() const
  {
    return !letters_.empty();
  }

  const std::unordered_map<long long, char> &letters() const
  {
    return letters_;
  }

  // The uid the model would give a buffer: its own when set, else 0.
  static long long uid_of(const FileBuffer &buffer)
  {
    return buffer.tab_uid;
  }

private:
  static bool is_real_tab(const FileBuffer &buffer);
  // Re-establishes the invariant "pinned block first, each block in order_
  // order" after a membership or placement change.
  void normalize();
  // The key a sort uses for one buffer.
  static std::string sort_key(const FileBuffer &buffer, TabSort by);
  long long ensure_uid(FileBuffer &buffer);
  void place_new(std::vector<long long> &order, size_t at, long long uid) const;
  bool is_pinned_uid(long long uid) const
  {
    for (long long p : pinned_)
    {
      if (p == uid)
      {
        return true;
      }
    }
    return false;
  }
  long long uid_at(const std::vector<FileBuffer> &buffers, int buffer_index) const;
  int live_index(const std::vector<FileBuffer> &buffers, long long uid) const;

  // The order: uids of live buffers, pinned block first.
  std::vector<long long> order_;
  // Membership only -- the block's order is read from order_.
  std::vector<long long> pinned_;
  std::unordered_map<long long, char> letters_;
  long long next_uid_ = 1;
  // Where `after_current` places the next newcomer, when the newcomer has no
  // current to sit next to.
  TabInsert last_insert_ = TabInsert::TAB_INSERT_AFTER_CURRENT;
};

#endif
