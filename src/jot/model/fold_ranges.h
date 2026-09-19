#ifndef JOT_MODEL_FOLD_RANGES_H
#define JOT_MODEL_FOLD_RANGES_H

#include <cstdint>
#include <memory>
#include <vector>
struct FoldRange
{
  int start_line = 0;
  int end_line = 0;
  bool collapsed = false;
};

namespace Folding
{
  // The prepared fold index (folding.h). Held through a shared_ptr so this
  // header does not have to know its definition -- folding.h is the one that
  // includes this one.
  class FoldView;
} // namespace Folding

// One buffer's fold ranges with the index prepared from them.
//
// The ranges used to be a bare vector and every caller rebuilt the index that
// answers the fold questions -- once per visible row of every frame before the
// per-row queries moved onto the index, and once per mouse event, wheel notch
// and cursor reveal after that. Keeping the index next to the ranges it
// describes lets it be built once per *change* instead, and keeps the two from
// being able to drift apart:
//
//   - The ranges are private. Reading goes through ranges() (or the implicit
//     conversion every existing reader already used); every writer bumps
//     revision(), which is what the index is validated against. There is no way
//     to write the ranges without invalidating the index -- the compiler
//     enforces it, rather than a comment asking nicely.
//   - The index records the checksum() of the ranges it was built from, so a
//     change the revision cannot see -- an alias held across a mutation, a
//     writer that bypasses this class -- is still detectable. view_of()
//     re-checks that checksum on a slow cadence and re-indexes on a mismatch,
//     and index_needs_rebuild() is the same verification on demand.
class FoldRanges
{
public:
  using value_type = FoldRange;
  using const_iterator = std::vector<FoldRange>::const_iterator;

  // Reads. A FoldRange may not be written through any of these.
  const std::vector<FoldRange> &ranges() const
  {
    return ranges_;
  }
  operator const std::vector<FoldRange> &() const
  {
    return ranges_;
  }
  const FoldRange &operator[](std::size_t index) const
  {
    return ranges_[index];
  }
  const FoldRange &at(std::size_t index) const
  {
    return ranges_.at(index);
  }
  std::size_t size() const
  {
    return ranges_.size();
  }
  bool empty() const
  {
    return ranges_.empty();
  }
  const_iterator begin() const
  {
    return ranges_.begin();
  }
  const_iterator end() const
  {
    return ranges_.end();
  }
  // The state of the ranges as a counter: equal revisions mean no writer has
  // run since that revision, and nothing else needs to be checked.
  std::uint64_t revision() const
  {
    return revision_;
  }

  // Writers. Each one bumps the revision, which is what retires the prepared
  // index; nothing else is required of a caller.
  void clear();
  void assign(std::vector<FoldRange> ranges);
  FoldRanges &operator=(std::vector<FoldRange> ranges)
  {
    assign(std::move(ranges));
    return *this;
  }
  void set_collapsed(std::size_t index, bool collapsed);
  void set_all_collapsed(bool collapsed);

  // A hash of what the ranges currently say, including each range's position
  // (the index remembers the position of every folded header, so an insertion
  // or deletion in front of one matters as much as its span does). A mismatch
  // with the checksum the index was built from means the index no longer
  // describes these ranges.
  std::uint64_t checksum() const;

  // --- What Folding::view_of() uses to serve and refresh the index ---
  const std::shared_ptr<const Folding::FoldView> &prepared_index() const
  {
    return prepared_index_;
  }
  std::uint64_t prepared_revision() const
  {
    return prepared_revision_;
  }
  std::uint64_t prepared_checksum() const
  {
    return prepared_checksum_;
  }
  // Publishes an index for the ranges as they are right now. const because it
  // only writes the cache: the ranges themselves are untouched, and a query
  // path holding a const buffer is exactly who prepares the index.
  void set_prepared_index(std::shared_ptr<const Folding::FoldView> index) const;
  // The prepared index is absent, stale by revision, or built from different
  // contents than the ranges hold now. Recomputes the checksum, so it verifies
  // -- it is not the hot-path check.
  bool index_needs_rebuild() const;
  // True when a verification is due on the hot path: view_of() counts accesses
  // and asks for a checksum comparison every kVerifyEveryAccesses of them, so
  // a change the revision cannot see is caught (and healed) within that many
  // index lookups instead of surviving until the next real write.
  static constexpr unsigned kVerifyEveryAccesses = 1024;
  bool verification_due() const;

private:
  std::vector<FoldRange> ranges_;
  std::uint64_t revision_ = 1;
  mutable std::shared_ptr<const Folding::FoldView> prepared_index_;
  mutable std::uint64_t prepared_revision_ = 0;
  mutable std::uint64_t prepared_checksum_ = 0;
  mutable unsigned accesses_since_verify_ = 0;
};

inline void FoldRanges::clear()
{
  ranges_.clear();
  revision_++;
}

inline void FoldRanges::assign(std::vector<FoldRange> ranges)
{
  ranges_ = std::move(ranges);
  revision_++;
}

inline void FoldRanges::set_collapsed(std::size_t index, bool collapsed)
{
  if (index >= ranges_.size() || ranges_[index].collapsed == collapsed)
  {
    return;
  }
  ranges_[index].collapsed = collapsed;
  revision_++;
}

inline void FoldRanges::set_all_collapsed(bool collapsed)
{
  bool changed = false;
  for (FoldRange &range : ranges_)
  {
    if (range.collapsed != collapsed)
    {
      range.collapsed = collapsed;
      changed = true;
    }
  }
  if (changed)
  {
    revision_++;
  }
}

inline std::uint64_t FoldRanges::checksum() const
{
  // FNV-1a over the fields an index is built from, one multiply per field. The
  // position of every range is folded in as well: a folded header's recorded
  // range_index is a position, so inserting or removing a range in front of it
  // invalidates the index even when the spans themselves are untouched.
  constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ull;
  constexpr std::uint64_t kPrime = 0x100000001b3ull;
  std::uint64_t hash = kOffsetBasis;
  const auto mix = [&hash](std::uint64_t value)
  {
    hash ^= value;
    hash *= kPrime;
  };
  mix(ranges_.size());
  for (std::size_t i = 0; i < ranges_.size(); i++)
  {
    const FoldRange &range = ranges_[i];
    mix(i);
    if (range.collapsed)
    {
      mix((std::uint64_t)(std::uint32_t)range.start_line);
      mix((std::uint64_t)(std::uint32_t)range.end_line);
    }
  }
  return hash;
}

inline void FoldRanges::set_prepared_index(std::shared_ptr<const Folding::FoldView> index) const
{
  prepared_index_ = std::move(index);
  prepared_revision_ = revision_;
  prepared_checksum_ = checksum();
  accesses_since_verify_ = 0;
}

inline bool FoldRanges::index_needs_rebuild() const
{
  if (!prepared_index_ || prepared_revision_ != revision_)
  {
    return true;
  }
  return prepared_checksum_ != checksum();
}

inline bool FoldRanges::verification_due() const
{
  if (++accesses_since_verify_ < kVerifyEveryAccesses)
  {
    return false;
  }
  accesses_since_verify_ = 0;
  return true;
}

#endif
