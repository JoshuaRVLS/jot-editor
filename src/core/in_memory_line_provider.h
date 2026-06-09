#ifndef IN_MEMORY_LINE_PROVIDER_H
#define IN_MEMORY_LINE_PROVIDER_H

#include "line_provider.h"
#include <vector>

class InMemoryLineProvider : public LineProvider {
public:
  InMemoryLineProvider();
  explicit InMemoryLineProvider(std::vector<std::string> lines);

  const std::string &get_line(int n) const override;
  std::string &get_line_mutable(int n) override;
  char get_char(int line, int col) const override;

  size_t line_count() const override;

  void insert_line(int after, const std::string &line) override;
  void insert_lines(int after,
                    const std::vector<std::string> &lines) override;
  void delete_line(int n) override;
  void delete_lines(int start, int end) override;
  void append_line(const std::string &line) override;
  void clear() override;

  void
  set_all_lines(const std::vector<std::string> &lines) override;
  void
  swap_all_lines(std::vector<std::string> &out) override;
  std::vector<std::string> copy_all_lines() const override;

  void move_lines(int start, int end, int dest) override;

  void replace_lines(int start, int count,
                     const std::vector<std::string> &new_lines) override;

  void for_each_line(LineVisitor fn) override;

  bool is_lazy() const override { return false; }
  void scroll_hint(int) override {}
  bool save_to(const std::string &filepath) override;
  size_t memory_usage() const override;

private:
  std::vector<std::string> lines_;
};

#endif
