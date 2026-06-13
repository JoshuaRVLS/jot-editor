#ifndef SYMBOL_INDEX_H
#define SYMBOL_INDEX_H

#include <string>
#include <vector>

struct SymbolMatch {
  std::string name;
  std::string kind;
  std::string detail;
  int line = 0;
  int column = 0;
};

namespace SymbolIndex {
std::vector<SymbolMatch> extract_document_symbols(
    const std::vector<std::string> &lines, const std::string &filepath);
} // namespace SymbolIndex

#endif
