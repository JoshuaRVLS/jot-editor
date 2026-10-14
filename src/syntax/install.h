#ifndef TREE_SITTER_INSTALL_H
#define TREE_SITTER_INSTALL_H

#include <string>
#include <vector>

struct TreeSitterInstallCommand
{
  bool supported = false;
  std::string language;
  std::string command;
  std::string message;
};

struct TreeSitterInstallMetadata
{
  std::string name;
  std::string url;
  std::string source_subdir;
  std::vector<std::string> library_names;
  // Grammar commit to build, from the vendored lockfile. Empty means the
  // repository's default branch (a URL install, or a grammar with no pin).
  std::string revision;
};

namespace TreeSitterInstall
{
  void clear_languages();
  void register_language(const TreeSitterInstallMetadata &metadata);
  const std::vector<std::string> &supported_languages();
  bool is_supported_language(const std::string &language);
  // Whether this language builds a pinned revision, which is what decides if
  // :tsupdate has anything to do for it.
  bool has_revision(const std::string &language);
  TreeSitterInstallCommand command_for_language(const std::string &language);
  TreeSitterInstallCommand command_for_language(const std::string &language,
                                                const std::string &prefix);
} // namespace TreeSitterInstall

#endif
