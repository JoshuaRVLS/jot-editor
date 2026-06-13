#ifndef WORKSPACE_SEARCH_H
#define WORKSPACE_SEARCH_H

#include <cstddef>
#include <string>
#include <vector>

struct WorkspaceSearchResult {
  std::string path;
  std::string relative_path;
  std::string line_text;
  int line = 0;
  int column = 0;
  int score = 0;
};

namespace WorkspaceSearch {
bool should_skip_path_component(const std::string &name);
bool text_looks_binary(const std::string &sample);
std::vector<WorkspaceSearchResult>
search(const std::string &root, const std::string &query,
       std::size_t max_file_bytes = 1024 * 1024, int max_results = 1000);
} // namespace WorkspaceSearch

#endif
