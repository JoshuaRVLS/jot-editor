#ifndef SYMBOL_INDEX_H
#define SYMBOL_INDEX_H

#include <string>
#include <vector>

struct SymbolMatch
{
  std::string name;
  std::string kind;
  std::string detail;
  int line = 0;
  int column = 0;
  // The outline tree, when this match came from extract_outline: how many
  // levels deep the row draws (0 = top level) and the flat index of the parent
  // (-1 at the roots), plus the child range [child_begin, child_end) in flat
  // order. extract_document_symbols leaves these flat and the consumers that
  // predate the tree (the breadcrumb winbar) read only the fields above.
  int depth = 0;
  int parent = -1;
  int child_begin = -1;
  int child_end = -1;
  bool has_children() const
  {
    return child_begin >= 0 && child_end > child_begin;
  }
};

namespace SymbolIndex
{
  std::vector<SymbolMatch> extract_document_symbols(const std::vector<std::string> &lines,
                                                    const std::string &filepath);

  // The outline as a tree in flat order: each container is immediately
  // followed by its own subtree. C-family files are parsed by CppDefinitions'
  // declaration-scope parser (functions with return type and parameters,
  // fields with their type, namespaces/classes/structs/unions/enums,
  // using-declarations); other languages fall back to the line-regex
  // extractor with indentation-derived nesting. Pure.
  std::vector<SymbolMatch> extract_outline(const std::vector<std::string> &lines,
                                           const std::string &filepath);

  // The flat indices a tree view shows: depth-first over `tree`, descending
  // only into nodes whose index is marked expanded in `expanded`. Parents
  // always precede their children in the result.
  std::vector<int> outline_visible_rows(const std::vector<SymbolMatch> &tree,
                                        const std::vector<char> &expanded);
} // namespace SymbolIndex

#endif // SYMBOL_INDEX_H
