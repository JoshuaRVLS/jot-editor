#ifndef TREE_SITTER_INSTALL_H
#define TREE_SITTER_INSTALL_H

#include <string>
#include <vector>

struct TreeSitterInstallCommand {
  bool supported = false;
  std::string language;
  std::string command;
  std::string message;
};

struct TreeSitterInstallMetadata {
  std::string name;
  std::string url;
  std::string source_subdir;
  std::vector<std::string> library_names;
};

namespace TreeSitterInstall {
void clear_languages();
void register_language(const TreeSitterInstallMetadata &metadata);
const std::vector<std::string> &supported_languages();
bool is_supported_language(const std::string &language);
TreeSitterInstallCommand command_for_language(const std::string &language);
TreeSitterInstallCommand command_for_language(const std::string &language,
                                             const std::string &prefix);
} // namespace TreeSitterInstall

#endif
