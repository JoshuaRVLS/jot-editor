#ifndef LAZY_LINE_PROVIDER_H
#define LAZY_LINE_PROVIDER_H

#include "line_provider.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr int kChunkSize = 512;
constexpr int kIndexInterval = 1024;
constexpr int kMaxCachedChunks = 128;
} // namespace

class LazyLineProvider : public LineProvider {
public:
  static std::unique_ptr<LazyLineProvider> open(const std::string &filepath);

  ~LazyLineProvider() override;

  const std::string &get_line(int n) const override;
  std::string &get_line_mutable(int n) override;
  char get_char(int line, int col) const override;

  size_t line_count() const override { return total_lines_; }
  bool empty() const override { return total_lines_ == 0; }

  void insert_line(int after, const std::string &line) override;
  void insert_lines(int after,
                    const std::vector<std::string> &lines) override;
  void delete_line(int n) override;
  void delete_lines(int start, int end) override;
  void append_line(const std::string &line) override;
  void clear() override;

  void set_all_lines(const std::vector<std::string> &lines) override;
  void swap_all_lines(std::vector<std::string> &out) override;
  std::vector<std::string> copy_all_lines() const override;

  void move_lines(int start, int end, int dest) override;
  void replace_lines(int start, int count,
                     const std::vector<std::string> &new_lines) override;
  void for_each_line(LineVisitor fn) override;

  bool is_lazy() const override { return true; }
  void scroll_hint(int center_line) override;
  bool save_to(const std::string &filepath) override;
  size_t memory_usage() const override;

private:
  struct Chunk {
    int chunk_id;
    std::vector<std::string> lines;
    bool is_edited = false;
    int64_t disk_start = 0;
    int64_t disk_end = 0;
  };

  struct IndexEntry {
    int line_number;
    int64_t byte_offset;
  };

  LazyLineProvider(const std::string &filepath, int fd);

  bool build_index();
  Chunk &ensure_chunk(int chunk_id);
  Chunk &load_chunk(int chunk_id);
  void evict_lru();
  void invalidate_chunk(int chunk_id);

  std::string filepath_;
  int fd_;
  size_t total_lines_ = 0;
  std::vector<IndexEntry> sparse_index_;

  mutable std::map<int, Chunk> chunk_cache_;
  mutable std::unordered_map<int, Chunk> edited_chunks_;
};

#endif
