#include "in_memory_line_provider.h"
#include <algorithm>
#include <fstream>

InMemoryLineProvider::InMemoryLineProvider() { lines_.push_back(""); }

InMemoryLineProvider::InMemoryLineProvider(std::vector<std::string> lines)
    : lines_(std::move(lines)) {
  if (lines_.empty())
    lines_.push_back("");
}

const std::string &InMemoryLineProvider::get_line(int n) const {
  if (n < 0 || n >= (int)lines_.size()) {
    static const std::string empty;
    return empty;
  }
  return lines_[n];
}

std::string &InMemoryLineProvider::get_line_mutable(int n) {
  if (n < 0 || n >= (int)lines_.size()) {
    if (lines_.empty()) lines_.push_back("");
    return lines_[0];
  }
  return lines_[n];
}

char InMemoryLineProvider::get_char(int line, int col) const {
  if (line < 0 || line >= (int)lines_.size()) return '\0';
  if (col < 0 || col >= (int)lines_[line].size()) return '\0';
  return lines_[line][col];
}

size_t InMemoryLineProvider::line_count() const { return lines_.size(); }

void InMemoryLineProvider::insert_line(int after, const std::string &line) {
  lines_.insert(lines_.begin() + after + 1, line);
}

void InMemoryLineProvider::insert_lines(int after,
                                        const std::vector<std::string> &lines) {
  lines_.insert(lines_.begin() + after + 1, lines.begin(), lines.end());
}

void InMemoryLineProvider::delete_line(int n) {
  lines_.erase(lines_.begin() + n);
}

void InMemoryLineProvider::delete_lines(int start, int end) {
  lines_.erase(lines_.begin() + start, lines_.begin() + end + 1);
}

void InMemoryLineProvider::append_line(const std::string &line) {
  lines_.push_back(line);
}

void InMemoryLineProvider::clear() {
  lines_.clear();
  lines_.push_back("");
}

void InMemoryLineProvider::set_all_lines(const std::vector<std::string> &lines) {
  lines_ = lines;
  if (lines_.empty())
    lines_.push_back("");
}

void InMemoryLineProvider::swap_all_lines(std::vector<std::string> &out) {
  lines_.swap(out);
  if (lines_.empty())
    lines_.push_back("");
}

std::vector<std::string> InMemoryLineProvider::copy_all_lines() const {
  return lines_;
}

void InMemoryLineProvider::move_lines(int start, int end, int dest) {
  if (start > end)
    std::swap(start, end);
  if (dest > start) {
    std::rotate(lines_.begin() + start, lines_.begin() + end + 1,
                lines_.begin() + dest + 1);
  } else {
    std::rotate(lines_.begin() + dest, lines_.begin() + start,
                lines_.begin() + end + 1);
  }
}

void InMemoryLineProvider::for_each_line(LineVisitor fn) {
  for (int i = 0; i < (int)lines_.size(); i++)
    fn(i, lines_[i]);
}

bool InMemoryLineProvider::save_to(const std::string &filepath) {
  std::ofstream file(filepath);
  if (!file.is_open())
    return false;
  for (const auto &line : lines_)
    file << line << '\n';
  return file.good();
}

size_t InMemoryLineProvider::memory_usage() const {
  size_t total = 0;
  for (const auto &line : lines_)
    total += line.capacity();
  return total + sizeof(*this);
}
