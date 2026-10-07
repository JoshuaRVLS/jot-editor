#include "tab_order.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace
{
  const char *const kHomeRow = "asdfjkl;ghnmxcvbziowerutyqpASDFJKLGHNMXCVBZIOWERUTYQP";

  int sum_of(const std::vector<int> &values)
  {
    int total = 0;
    for (int v : values)
    {
      total += v;
    }
    return total;
  }

  std::string base_name(const FileBuffer &buffer)
  {
    if (buffer.filepath.empty())
    {
      return {};
    }
    return std::filesystem::path(buffer.filepath).filename().string();
  }

  std::string extension_of(const FileBuffer &buffer)
  {
    if (buffer.filepath.empty())
    {
      return {};
    }
    std::string ext = std::filesystem::path(buffer.filepath).extension().string();
    if (!ext.empty() && ext.front() == '.')
    {
      ext.erase(ext.begin());
    }
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
  }

  bool contains(const std::vector<long long> &haystack, long long needle)
  {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
  }

  bool letter_taken(const std::unordered_map<long long, char> &letters, char c)
  {
    for (const auto &entry : letters)
    {
      if (entry.second == c)
      {
        return true;
      }
    }
    return false;
  }
} // namespace

bool TabOrder::is_real_tab(const FileBuffer &buffer)
{
  // The rule the strip has always drawn by: a fresh unnamed placeholder is not
  // a tab, but one that was edited or pointed at a file is.
  return !(buffer.is_placeholder && !buffer.modified && buffer.filepath.empty());
}

long long TabOrder::ensure_uid(FileBuffer &buffer)
{
  if (buffer.tab_uid == 0)
  {
    buffer.tab_uid = next_uid_++;
  }
  return buffer.tab_uid;
}

long long TabOrder::uid_at(const std::vector<FileBuffer> &buffers, int buffer_index) const
{
  if (buffer_index < 0 || buffer_index >= (int)buffers.size())
  {
    return 0;
  }
  return buffers[(size_t)buffer_index].tab_uid;
}

int TabOrder::live_index(const std::vector<FileBuffer> &buffers, long long uid) const
{
  if (uid == 0)
  {
    return -1;
  }
  for (size_t i = 0; i < buffers.size(); i++)
  {
    if (buffers[i].tab_uid == uid)
    {
      return (int)i;
    }
  }
  return -1;
}

void TabOrder::place_new(std::vector<long long> &order, size_t at, long long uid) const
{
  at = std::min(at, order.size());
  order.insert(order.begin() + (long)at, uid);
}

void TabOrder::sync(std::vector<FileBuffer> &buffers, long long current_uid, TabInsert insert)
{
  last_insert_ = insert;

  // 1. Every buffer gets an identity, and the live set is the answer to "is
  //    this uid still around".
  std::vector<long long> live;
  live.reserve(buffers.size());
  for (FileBuffer &buffer : buffers)
  {
    live.push_back(ensure_uid(buffer));
  }
  const auto alive = [&live](long long uid)
  { return std::find(live.begin(), live.end(), uid) != live.end(); };

  order_.erase(
      std::remove_if(order_.begin(), order_.end(), [&](long long uid) { return !alive(uid); }),
      order_.end());
  pinned_.erase(
      std::remove_if(pinned_.begin(), pinned_.end(), [&](long long uid) { return !alive(uid); }),
      pinned_.end());
  for (auto it = letters_.begin(); it != letters_.end();)
  {
    it = alive(it->first) ? std::next(it) : letters_.erase(it);
  }

  // 2. Newcomers, placed as one block so several at once keep their buffer
  //    order (a session restore or `:e` on a directory used to reverse).
  size_t anchor = order_.size();
  if (insert == TabInsert::TAB_INSERT_START)
  {
    anchor = 0;
  }
  else if (insert == TabInsert::TAB_INSERT_AFTER_CURRENT)
  {
    const auto at = std::find(order_.begin(), order_.end(), current_uid);
    anchor = at == order_.end() ? order_.size() : (size_t)std::distance(order_.begin(), at) + 1;
  }
  // Unpinned newcomers never land inside the pinned block.
  const size_t pinned_count = pinned_.size();
  for (long long uid : live)
  {
    if (contains(order_, uid))
    {
      continue;
    }
    size_t at = anchor;
    if (at < pinned_count)
    {
      at = pinned_count;
    }
    place_new(order_, at, uid);
    anchor = at + 1;
  }

  normalize();
}

void TabOrder::normalize()
{
  // Pinned buffers lead the strip (barbar shows them pinned at the front) and
  // each block keeps the relative order it had; membership is all pinned_
  // holds, so a swap can never desynchronize it.
  std::vector<long long> normalized;
  normalized.reserve(order_.size());
  for (long long uid : order_)
  {
    if (is_pinned_uid(uid))
    {
      normalized.push_back(uid);
    }
  }
  for (long long uid : order_)
  {
    if (!is_pinned_uid(uid))
    {
      normalized.push_back(uid);
    }
  }
  order_.swap(normalized);
}

std::vector<int> TabOrder::indices(const std::vector<FileBuffer> &buffers) const
{
  std::vector<int> out;
  out.reserve(order_.size());
  for (long long uid : order_)
  {
    const int index = live_index(buffers, uid);
    if (index < 0 || !is_real_tab(buffers[(size_t)index]))
    {
      continue;
    }
    out.push_back(index);
  }
  return out;
}

int TabOrder::position_of(const std::vector<FileBuffer> &buffers, int buffer_index) const
{
  const long long uid = uid_at(buffers, buffer_index);
  if (uid == 0)
  {
    return -1;
  }
  const std::vector<int> live = indices(buffers);
  for (size_t i = 0; i < live.size(); i++)
  {
    if (live[i] == buffer_index)
    {
      return (int)i;
    }
  }
  return -1;
}

int TabOrder::index_at(const std::vector<FileBuffer> &buffers, int position) const
{
  const std::vector<int> live = indices(buffers);
  if (live.empty())
  {
    return -1;
  }
  position = std::clamp(position, 0, (int)live.size() - 1);
  return live[(size_t)position];
}

bool TabOrder::pinned(int buffer_index, const std::vector<FileBuffer> &buffers) const
{
  return is_pinned_uid(uid_at(buffers, buffer_index));
}

void TabOrder::set_pinned(int buffer_index, const std::vector<FileBuffer> &buffers, bool pinned)
{
  const long long uid = uid_at(buffers, buffer_index);
  if (uid == 0)
  {
    return;
  }
  const auto at = std::find(pinned_.begin(), pinned_.end(), uid);
  if (pinned == (at != pinned_.end()))
  {
    return;
  }
  if (pinned)
  {
    pinned_.push_back(uid);
  }
  else
  {
    pinned_.erase(at);
  }
  normalize();
}

void TabOrder::toggle_pin(int buffer_index, const std::vector<FileBuffer> &buffers)
{
  set_pinned(buffer_index, buffers, !pinned(buffer_index, buffers));
}

bool TabOrder::move(const std::vector<FileBuffer> &buffers, int buffer_index, int delta)
{
  const long long uid = uid_at(buffers, buffer_index);
  const auto at = std::find(order_.begin(), order_.end(), uid);
  if (at == order_.end() || delta == 0)
  {
    return false;
  }
  const int from = (int)std::distance(order_.begin(), at);
  const int to = from + delta;
  if (to < 0 || to >= (int)order_.size())
  {
    return false;
  }
  const bool pinned_self = is_pinned_uid(uid);
  if (pinned_self != is_pinned_uid(order_[(size_t)to]))
  {
    // Crossing the pin boundary is not a move: refuse rather than unpin behind
    // the user's back.
    return false;
  }
  std::swap(order_[(size_t)from], order_[(size_t)to]);
  return true;
}

std::string TabOrder::sort_key(const FileBuffer &buffer, TabSort by)
{
  const std::string base = base_name(buffer);
  switch (by)
  {
  case TabSort::TAB_SORT_NAME:
    return base.empty() ? std::string("\uffff") : base;
  case TabSort::TAB_SORT_PATH:
    return buffer.filepath.empty() ? std::string("\uffff") : buffer.filepath;
  case TabSort::TAB_SORT_LANGUAGE:
    return extension_of(buffer) + "\x01" + base;
  case TabSort::TAB_SORT_BUFFER:
    return {}; // uid order, handled by the caller
  case TabSort::TAB_SORT_WINDOW:
    return {}; // buffer index order, handled by the caller
  case TabSort::TAB_SORT_NONE:
    break;
  }
  return base;
}

void TabOrder::sort(const std::vector<FileBuffer> &buffers, TabSort by)
{
  if (by == TabSort::TAB_SORT_NONE)
  {
    return;
  }
  std::vector<long long> pinned;
  std::vector<long long> rest;
  for (long long uid : order_)
  {
    (is_pinned_uid(uid) ? pinned : rest).push_back(uid);
  }
  const bool by_uid = by == TabSort::TAB_SORT_BUFFER;
  const bool by_index = by == TabSort::TAB_SORT_WINDOW;
  std::stable_sort(rest.begin(),
                   rest.end(),
                   [&](long long a, long long b)
                   {
                     if (by_uid)
                     {
                       return a < b;
                     }
                     if (by_index)
                     {
                       return live_index(buffers, a) < live_index(buffers, b);
                     }
                     const int ia = live_index(buffers, a);
                     const int ib = live_index(buffers, b);
                     const std::string ka =
                         ia >= 0 ? sort_key(buffers[(size_t)ia], by) : std::string();
                     const std::string kb =
                         ib >= 0 ? sort_key(buffers[(size_t)ib], by) : std::string();
                     if (ka == kb)
                     {
                       return a < b;
                     }
                     return ka < kb;
                   });
  std::vector<long long> merged = pinned;
  merged.insert(merged.end(), rest.begin(), rest.end());
  order_.swap(merged);
}

namespace TabStrip
{
  bool allocate_widths(const std::vector<int> &natural,
                       int available,
                       int min_cells,
                       std::vector<int> &out)
  {
    out.clear();
    if (natural.empty())
    {
      return true;
    }
    min_cells = std::max(1, min_cells);
    out = natural;
    if (sum_of(natural) <= available)
    {
      return true;
    }

    // Equal shares first: tabs wider than their share hand back the excess,
    // which is then spread over the tabs that are still too wide, so a short
    // "a.cpp" next to two long names keeps all of its label.
    for (int pass = 0; pass < (int)out.size(); pass++)
    {
      const int total = sum_of(out);
      if (total <= available)
      {
        break;
      }
      int tight = 0;
      for (int w : out)
      {
        if (w > min_cells)
        {
          tight++;
        }
      }
      if (tight == 0)
      {
        break;
      }
      const int share = std::max(min_cells, available / (int)out.size());
      for (int &w : out)
      {
        if (w > share)
        {
          w = std::max(min_cells, share);
        }
      }
      // Still over: trim the widest, one cell at a time, down to the floor.
      while (sum_of(out) > available)
      {
        int widest = -1;
        for (size_t i = 0; i < out.size(); i++)
        {
          if (out[i] > min_cells && (widest < 0 || out[i] > out[(size_t)widest]))
          {
            widest = (int)i;
          }
        }
        if (widest < 0)
        {
          break;
        }
        out[(size_t)widest]--;
      }
    }
    return sum_of(out) <= available;
  }

  std::vector<std::string> unique_names(const std::vector<std::string> &paths)
  {
    std::vector<std::string> bases;
    bases.reserve(paths.size());
    for (const std::string &path : paths)
    {
      const std::string base =
          path.empty() ? std::string() : std::filesystem::path(path).filename().string();
      bases.push_back(base.empty() ? "[No Name]" : base);
    }

    // The parent folder tells two `frame.cpp`s apart in nearly every tree, so
    // that is the label; only a name that is still ambiguous (the same folder,
    // i.e. one file open twice) falls back to the whole path.
    const auto prefixed = [&](size_t i)
    {
      const std::string parent = std::filesystem::path(paths[i]).parent_path().filename().string();
      if (paths[i].empty() || parent.empty())
      {
        return paths[i].empty() ? bases[i] : paths[i];
      }
      return bases[i] + " <" + parent + ">";
    };

    std::vector<std::string> out = bases;
    for (size_t i = 0; i < out.size(); i++)
    {
      int twins = 0;
      for (size_t j = 0; j < bases.size(); j++)
      {
        if (i != j && bases[j] == bases[i])
        {
          twins++;
        }
      }
      if (twins == 0 || paths[i].empty())
      {
        continue;
      }
      std::string label = prefixed(i);
      for (size_t j = 0; j < bases.size(); j++)
      {
        if (j == i || paths[j].empty() || bases[j] != bases[i])
        {
          continue;
        }
        if (prefixed(j) == label)
        {
          label = paths[i];
          break;
        }
      }
      out[i] = label;
    }
    return out;
  }
} // namespace TabStrip

int TabOrder::buffer_for_letter(const std::vector<FileBuffer> &buffers, char letter) const
{
  for (const auto &entry : letters_)
  {
    if (entry.second == letter)
    {
      return live_index(buffers, entry.first);
    }
  }
  return -1;
}

char TabOrder::letter_for(int buffer_index, const std::vector<FileBuffer> &buffers)
{
  const long long uid = uid_at(buffers, buffer_index);
  if (uid == 0)
  {
    return '\0';
  }
  const auto known = letters_.find(uid);
  if (known != letters_.end())
  {
    return known->second;
  }

  // Semantic first: the file's own initials (README.md answers to `r`), then
  // the home row, then anything unclaimed.
  std::vector<char> hints;
  const std::string base = base_name(buffers[(size_t)buffer_index]);
  for (unsigned char c : base)
  {
    if (std::isalnum(c))
    {
      hints.push_back((char)std::tolower(c));
    }
  }
  for (const char *p = kHomeRow; *p; p++)
  {
    hints.push_back(std::tolower((unsigned char)*p));
  }
  for (char c : hints)
  {
    if (!letter_taken(letters_, c))
    {
      letters_[uid] = c;
      return c;
    }
  }
  letters_[uid] = '?';
  return '?';
}
