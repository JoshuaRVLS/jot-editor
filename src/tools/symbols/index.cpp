#include "tools/symbols/index.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

namespace {
std::string extension_for(const std::string &filepath) {
  std::string ext = std::filesystem::path(filepath).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return ext;
}

std::string trim_copy(const std::string &s) {
  size_t start = s.find_first_not_of(" \t");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(" \t");
  return s.substr(start, end - start + 1);
}

void add_match(std::vector<SymbolMatch> &out, const std::smatch &m,
               const std::string &line, const std::string &kind,
               int line_idx, int group = 1) {
  if ((int)m.size() <= group) {
    return;
  }
  SymbolMatch symbol;
  symbol.name = m[(size_t)group].str();
  symbol.kind = kind;
  symbol.detail = trim_copy(line);
  symbol.line = line_idx;
  symbol.column = (int)m.position((size_t)group);
  out.push_back(std::move(symbol));
}
} // namespace

namespace SymbolIndex {
std::vector<SymbolMatch> extract_document_symbols(
    const std::vector<std::string> &lines, const std::string &filepath) {
  std::vector<SymbolMatch> out;
  const std::string ext = extension_for(filepath);

  std::vector<std::pair<std::regex, std::string>> patterns;
  if (ext == ".py") {
    patterns = {
        {std::regex("^\\s*class\\s+([A-Za-z_][A-Za-z0-9_]*)"), "class"},
        {std::regex("^\\s*def\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\("),
         "function"},
    };
  } else if (ext == ".rs") {
    patterns = {
        {std::regex("^\\s*(?:pub\\s+)?(?:async\\s+)?fn\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\("),
         "function"},
        {std::regex("^\\s*(?:pub\\s+)?(?:struct|enum|trait)\\s+([A-Za-z_][A-Za-z0-9_]*)"),
         "type"},
    };
  } else if (ext == ".go") {
    patterns = {
        {std::regex("^\\s*func\\s+(?:\\([^)]*\\)\\s*)?([A-Za-z_][A-Za-z0-9_]*)\\s*\\("),
         "function"},
        {std::regex("^\\s*type\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+"),
         "type"},
    };
  } else if (ext == ".js" || ext == ".ts" || ext == ".jsx" ||
             ext == ".tsx") {
    patterns = {
        {std::regex("^\\s*(?:export\\s+)?(?:async\\s+)?function\\s+([A-Za-z_$][A-Za-z0-9_$]*)\\s*\\("),
         "function"},
        {std::regex("^\\s*(?:export\\s+)?class\\s+([A-Za-z_$][A-Za-z0-9_$]*)"),
         "class"},
        {std::regex("^\\s*(?:const|let|var)\\s+([A-Za-z_$][A-Za-z0-9_$]*)\\s*=\\s*(?:async\\s*)?\\("),
         "function"},
    };
  } else {
    patterns = {
        {std::regex("^\\s*(?:class|struct|enum)\\s+([A-Za-z_][A-Za-z0-9_]*)"),
         "type"},
        {std::regex("^\\s*(?:[A-Za-z_][A-Za-z0-9_:<>*&\\s]+\\s+)+([A-Za-z_][A-Za-z0-9_:]*)\\s*\\([^;]*\\)\\s*(?:const\\s*)?(?:noexcept\\s*)?(?:override\\s*)?(?:final\\s*)?(?:\\{|$)"),
         "function"},
    };
  }

  for (int i = 0; i < (int)lines.size(); i++) {
    const std::string &line = lines[(size_t)i];
    for (const auto &pattern : patterns) {
      std::smatch match;
      if (std::regex_search(line, match, pattern.first)) {
        add_match(out, match, line, pattern.second, i);
        break;
      }
    }
  }
  return out;
}
} // namespace SymbolIndex
