#ifndef LINE_PROVIDER_H
#define LINE_PROVIDER_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class LineProvider {
public:
  virtual ~LineProvider() = default;

  virtual const std::string &get_line(int n) const = 0;
  virtual std::string &get_line_mutable(int n) = 0;
  virtual char get_char(int line, int col) const = 0;

  virtual size_t line_count() const = 0;
  virtual bool empty() const { return line_count() == 0; }

  virtual void insert_line(int after, const std::string &line) = 0;
  virtual void insert_lines(int after,
                            const std::vector<std::string> &lines) = 0;
  virtual void delete_line(int n) = 0;
  virtual void delete_lines(int start, int end) = 0;
  virtual void append_line(const std::string &line) = 0;
  virtual void clear() = 0;

  virtual void
  set_all_lines(const std::vector<std::string> &lines) = 0;
  virtual void
  swap_all_lines(std::vector<std::string> &out) = 0;
  virtual std::vector<std::string> copy_all_lines() const = 0;

  virtual void move_lines(int start, int end, int dest) = 0;

  virtual void replace_lines(int start, int count,
                             const std::vector<std::string> &new_lines) = 0;

  using LineVisitor = std::function<void(int, std::string &)>;
  virtual void for_each_line(LineVisitor fn) = 0;

  virtual bool is_lazy() const = 0;
  virtual void scroll_hint(int center_line) = 0;
  virtual bool save_to(const std::string &filepath) = 0;
  virtual size_t memory_usage() const = 0;
};

#endif
