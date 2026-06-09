#include "lazy_line_provider.h"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

std::unique_ptr<LazyLineProvider>
LazyLineProvider::open(const std::string &filepath) {
  int fd = ::open(filepath.c_str(), O_RDONLY);
  if (fd < 0)
    return nullptr;

  auto provider =
      std::unique_ptr<LazyLineProvider>(new LazyLineProvider(filepath, fd));
  if (!provider->build_index()) {
    return nullptr;
  }
  return provider;
}

LazyLineProvider::LazyLineProvider(const std::string &filepath, int fd)
    : filepath_(filepath), fd_(fd) {}

LazyLineProvider::~LazyLineProvider() {
  if (fd_ >= 0)
    ::close(fd_);
}

bool LazyLineProvider::build_index() {
  if (fd_ < 0)
    return false;

  off_t file_size = lseek(fd_, 0, SEEK_END);
  if (file_size < 0) {
    lseek(fd_, 0, SEEK_SET);
    file_size = 0;
  }
  lseek(fd_, 0, SEEK_SET);

  std::vector<char> buf(256 * 1024);
  int line_number = 0;
  int64_t byte_offset = 0;
  bool line_start = true;

  sparse_index_.push_back({0, 0});

  while (true) {
    ssize_t n = read(fd_, buf.data(), buf.size());
    if (n <= 0)
      break;

    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == '\n') {
        line_number++;
        if ((line_number % kIndexInterval) == 0) {
          sparse_index_.push_back({line_number, byte_offset + i + 1});
        }
        line_start = true;
      } else if (line_start && buf[i] == '\r') {
        byte_offset++;
        continue;
      } else {
        line_start = false;
      }
    }
    byte_offset += n;
  }

  total_lines_ = line_number;
  if (total_lines_ == 0 && file_size > 0)
    total_lines_ = 1;
  if (total_lines_ == 0)
    total_lines_ = 1;

  lseek(fd_, 0, SEEK_SET);
  return true;
}

const std::string &LazyLineProvider::get_line(int n) const {
  if (n < 0 || n >= (int)total_lines_) {
    static const std::string empty;
    return empty;
  }

  int chunk_id = n / kChunkSize;
  int offset = n - chunk_id * kChunkSize;

  auto edit_it = edited_chunks_.find(chunk_id);
  if (edit_it != edited_chunks_.end()) {
    if (offset >= 0 && offset < (int)edit_it->second.lines.size())
      return edit_it->second.lines[offset];
  }

  auto cache_it = chunk_cache_.find(chunk_id);
  if (cache_it != chunk_cache_.end()) {
    if (offset >= 0 && offset < (int)cache_it->second.lines.size())
      return cache_it->second.lines[offset];
  }

  Chunk &chunk = const_cast<LazyLineProvider *>(this)->load_chunk(chunk_id);
  if (offset >= 0 && offset < (int)chunk.lines.size())
    return chunk.lines[offset];

  static const std::string empty;
  return empty;
}

std::string &LazyLineProvider::get_line_mutable(int n) {
  if (n < 0 || n >= (int)total_lines_) {
    static std::string empty;
    return empty;
  }

  int chunk_id = n / kChunkSize;
  int offset = n - chunk_id * kChunkSize;

  Chunk &chunk = ensure_chunk(chunk_id);
  if (offset < 0 || offset >= (int)chunk.lines.size()) {
    static std::string empty;
    return empty;
  }
  return chunk.lines[offset];
}

char LazyLineProvider::get_char(int line, int col) const {
  const std::string &l = get_line(line);
  if (col < 0 || col >= (int)l.size())
    return ' ';
  return l[col];
}

auto LazyLineProvider::ensure_chunk(int chunk_id) -> Chunk & {
  auto edit_it = edited_chunks_.find(chunk_id);
  if (edit_it != edited_chunks_.end())
    return edit_it->second;

  auto cache_it = chunk_cache_.find(chunk_id);
  if (cache_it != chunk_cache_.end()) {
    Chunk chunk = std::move(cache_it->second);
    chunk.is_edited = true;
    chunk_cache_.erase(cache_it);
    auto [it, _] = edited_chunks_.emplace(chunk_id, std::move(chunk));
    return it->second;
  }

  Chunk loaded = load_chunk(chunk_id);
  loaded.is_edited = true;
  chunk_cache_.erase(chunk_id);
  auto [it, _] = edited_chunks_.emplace(chunk_id, std::move(loaded));
  return it->second;
}

auto LazyLineProvider::load_chunk(int chunk_id) -> Chunk & {
  int start_line = chunk_id * kChunkSize;
  int end_line = std::min(start_line + kChunkSize, (int)total_lines_);

  Chunk chunk;
  chunk.chunk_id = chunk_id;

  size_t index_position = start_line / kIndexInterval;
  if (index_position >= sparse_index_.size())
    index_position = sparse_index_.size() - 1;

  int64_t seek_offset = sparse_index_[index_position].byte_offset;
  int seek_line = sparse_index_[index_position].line_number;

  lseek(fd_, seek_offset, SEEK_SET);

  std::vector<char> buf(128 * 1024);
  std::string current_line;
  int current_line_num = seek_line;

  while (current_line_num < end_line) {
    ssize_t n = read(fd_, buf.data(), buf.size());
    if (n <= 0)
      break;

    for (ssize_t i = 0; i < n && current_line_num < end_line; i++) {
      if (buf[i] == '\n') {
        if (!current_line.empty() && current_line.back() == '\r')
          current_line.pop_back();
        if (current_line_num >= start_line)
          chunk.lines.push_back(std::move(current_line));
        current_line.clear();
        current_line_num++;
      } else {
        current_line += buf[i];
      }
    }
  }

  if (!current_line.empty()) {
    if (!current_line.empty() && current_line.back() == '\r')
      current_line.pop_back();
    if (current_line_num >= start_line)
      chunk.lines.push_back(std::move(current_line));
  }

  while (chunk.lines.empty())
    chunk.lines.push_back("");

  evict_lru();
  auto [it, _] = chunk_cache_.emplace(chunk_id, std::move(chunk));
  return it->second;
}

void LazyLineProvider::evict_lru() {
  while ((int)chunk_cache_.size() >= kMaxCachedChunks) {
    auto it = chunk_cache_.begin();
    if (it != chunk_cache_.end() && it->second.is_edited) {
      ++it;
      if (it == chunk_cache_.end())
        it = chunk_cache_.begin();
    }
    if (it == chunk_cache_.end())
      break;
    chunk_cache_.erase(it);
  }
}

void LazyLineProvider::invalidate_chunk(int chunk_id) {
  chunk_cache_.erase(chunk_id);
  edited_chunks_.erase(chunk_id);
}

void LazyLineProvider::insert_line(int after, const std::string &line) {
  insert_lines(after, {line});
}

void LazyLineProvider::insert_lines(int after,
                                    const std::vector<std::string> &lines) {
  int affected_chunk = after / kChunkSize;
  int line_in_chunk = after - (affected_chunk * kChunkSize) + 1;

  Chunk &chunk = ensure_chunk(affected_chunk);
  chunk.lines.insert(chunk.lines.begin() + line_in_chunk, lines.begin(),
                     lines.end());
  chunk.is_edited = true;

  for (int i = affected_chunk + 1;; i++) {
    auto cache = chunk_cache_.find(i);
    auto edit = edited_chunks_.find(i);
    if (cache == chunk_cache_.end() && edit == edited_chunks_.end())
      break;
    if (cache != chunk_cache_.end()) {
      cache->second.chunk_id++;
      auto node = chunk_cache_.extract(cache);
      node.key()++;
      chunk_cache_.insert(std::move(node));
    }
    if (edit != edited_chunks_.end()) {
      edit->second.chunk_id++;
      auto node = edited_chunks_.extract(edit);
      node.key()++;
      edited_chunks_.insert(std::move(node));
    }
  }

  total_lines_ += lines.size();
}

void LazyLineProvider::delete_line(int n) { delete_lines(n, n); }

void LazyLineProvider::delete_lines(int start, int end) {
  if (start > end)
    std::swap(start, end);

  int start_chunk = start / kChunkSize;
  int end_chunk = end / kChunkSize;
  int start_in_chunk = start - start_chunk * kChunkSize;

  if (start_chunk == end_chunk) {
    Chunk &chunk = ensure_chunk(start_chunk);
    chunk.lines.erase(chunk.lines.begin() + start_in_chunk,
                      chunk.lines.begin() + start_in_chunk + (end - start + 1));
    chunk.is_edited = true;
  } else {
    Chunk &first = ensure_chunk(start_chunk);
    int remove_count = first.lines.size() - start_in_chunk;
    first.lines.erase(first.lines.begin() + start_in_chunk, first.lines.end());

    Chunk &last = ensure_chunk(end_chunk);
    int end_in_chunk = end - end_chunk * kChunkSize + 1;
    first.lines.insert(first.lines.end(),
                       last.lines.begin() + end_in_chunk, last.lines.end());

    invalidate_chunk(end_chunk);

    for (int i = start_chunk + 1; i < end_chunk; i++)
      invalidate_chunk(i);
  }

  int removed = end - start + 1;
  total_lines_ = total_lines_ > (size_t)removed ? total_lines_ - removed : 1;

  for (int i = end_chunk + 1;; i++) {
    auto cache = chunk_cache_.find(i);
    auto edit = edited_chunks_.find(i);
    if (cache == chunk_cache_.end() && edit == edited_chunks_.end())
      break;
    if (cache != chunk_cache_.end()) {
      cache->second.chunk_id--;
      auto node = chunk_cache_.extract(cache);
      node.key()--;
      chunk_cache_.insert(std::move(node));
    }
    if (edit != edited_chunks_.end()) {
      edit->second.chunk_id--;
      auto node = edited_chunks_.extract(edit);
      node.key()--;
      edited_chunks_.insert(std::move(node));
    }
  }
}

void LazyLineProvider::append_line(const std::string &line) {
  int last_chunk = (total_lines_ > 0) ? ((int)total_lines_ - 1) / kChunkSize
                                      : 0;
  Chunk &chunk = ensure_chunk(last_chunk);
  chunk.lines.push_back(line);
  chunk.is_edited = true;
  total_lines_++;
}

void LazyLineProvider::clear() {
  total_lines_ = 1;
  chunk_cache_.clear();
  edited_chunks_.clear();
  edited_chunks_[0] = Chunk{0, {""}, true, 0, 0};
}

void LazyLineProvider::set_all_lines(const std::vector<std::string> &lines) {
  clear();
  total_lines_ = lines.empty() ? 1 : lines.size();
  edited_chunks_.clear();
  chunk_cache_.clear();

  for (size_t i = 0; i < lines.size(); i += kChunkSize) {
    int chunk_id = i / kChunkSize;
    Chunk chunk;
    chunk.chunk_id = chunk_id;
    chunk.is_edited = true;
    size_t end = std::min(i + kChunkSize, lines.size());
    chunk.lines.assign(lines.begin() + i, lines.begin() + end);
    edited_chunks_[chunk_id] = std::move(chunk);
  }
  if (edited_chunks_.empty())
    edited_chunks_[0] = Chunk{0, {""}, true, 0, 0};
}

void LazyLineProvider::swap_all_lines(std::vector<std::string> &out) {
  out = copy_all_lines();
  clear();
}

std::vector<std::string> LazyLineProvider::copy_all_lines() const {
  std::vector<std::string> result;
  result.reserve(total_lines_);
  for (size_t i = 0; i < total_lines_; i++)
    result.push_back(get_line(i));
  return result;
}

void LazyLineProvider::move_lines(int start, int end, int dest) {
  if (start > end)
    std::swap(start, end);

  std::vector<std::string> moved;
  for (int i = start; i <= end; i++)
    moved.push_back(get_line(i));

  delete_lines(start, end);

  int adjusted_dest = dest;
  if (dest > end)
    adjusted_dest -= (end - start + 1);

  insert_lines(adjusted_dest - 1, moved);
}

void LazyLineProvider::for_each_line(LineVisitor fn) {
  for (size_t i = 0; i < total_lines_; i++) {
    int chunk_id = (int)i / kChunkSize;
    int line_in_chunk = (int)i - chunk_id * kChunkSize;
    fn((int)i, ensure_chunk(chunk_id).lines[line_in_chunk]);
    ensure_chunk(chunk_id).is_edited = true;
  }
}

void LazyLineProvider::scroll_hint(int center_line) {
  int center_chunk = center_line / kChunkSize;
  for (int i = center_chunk - 1; i <= center_chunk + 2; i++) {
    if (i < 0 || i >= (int)((total_lines_ + kChunkSize - 1) / kChunkSize))
      continue;
    try {
      load_chunk(i);
    } catch (...) {
    }
  }
}

bool LazyLineProvider::save_to(const std::string &filepath) {
  std::ofstream out(filepath);
  if (!out.is_open())
    return false;

  for (size_t i = 0; i < total_lines_; i++)
    out << get_line(i) << '\n';

  return out.good();
}

size_t LazyLineProvider::memory_usage() const {
  size_t total = sizeof(*this);
  total += sparse_index_.size() * sizeof(IndexEntry);
  for (const auto &[_, chunk] : edited_chunks_)
    for (const auto &line : chunk.lines)
      total += line.capacity();
  for (const auto &[_, chunk] : chunk_cache_)
    for (const auto &line : chunk.lines)
      total += line.capacity();
  return total;
}
