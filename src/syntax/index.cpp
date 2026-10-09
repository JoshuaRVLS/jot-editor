#include "syntax/index.h"
#include "base/string_util.h"
#include "syntax/cpp_definitions.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <regex>
#include <utility>

namespace
{
  std::string extension_for(const std::string &filepath)
  {
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
  }

  void add_match(std::vector<SymbolMatch> &out,
                 const std::smatch &m,
                 const std::string &line,
                 const std::string &kind,
                 int line_idx,
                 int group = 1)
  {
    if ((int)m.size() <= group)
    {
      return;
    }
    SymbolMatch symbol;
    symbol.name = m[(size_t)group].str();
    symbol.kind = kind;
    symbol.detail = string_util::trim_copy(line);
    symbol.line = line_idx;
    symbol.column = (int)m.position((size_t)group);
    out.push_back(std::move(symbol));
  }
} // namespace

namespace SymbolIndex
{
  std::vector<SymbolMatch> extract_document_symbols(const std::vector<std::string> &lines,
                                                    const std::string &filepath)
  {
    std::vector<SymbolMatch> out;
    const std::string ext = extension_for(filepath);

    std::vector<std::pair<std::regex, std::string>> patterns;
    if (ext == ".py")
    {
      patterns = {
          {std::regex("^\\s*class\\s+([A-Za-z_][A-Za-z0-9_]*)"), "class"},
          {std::regex("^\\s*def\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\("), "function"},
      };
    }
    else if (ext == ".rs")
    {
      patterns = {
          {std::regex("^\\s*(?:pub\\s+)?(?:async\\s+)?fn\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\("),
           "function"},
          {std::regex("^\\s*(?:pub\\s+)?(?:struct|enum|trait)\\s+([A-Za-z_][A-Za-z0-9_]*)"),
           "type"},
      };
    }
    else if (ext == ".go")
    {
      patterns = {
          {std::regex("^\\s*func\\s+(?:\\([^)]*\\)\\s*)?([A-Za-z_][A-Za-z0-9_]*)\\s*\\("),
           "function"},
          {std::regex("^\\s*type\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+"), "type"},
      };
    }
    else if (ext == ".js" || ext == ".ts" || ext == ".jsx" || ext == ".tsx")
    {
      patterns = {
          {std::regex(
               "^\\s*(?:export\\s+)?(?:async\\s+)?function\\s+([A-Za-z_$][A-Za-z0-9_$]*)\\s*\\("),
           "function"},
          {std::regex("^\\s*(?:export\\s+)?class\\s+([A-Za-z_$][A-Za-z0-9_$]*)"), "class"},
          {std::regex(
               "^\\s*(?:const|let|var)\\s+([A-Za-z_$][A-Za-z0-9_$]*)\\s*=\\s*(?:async\\s*)?\\("),
           "function"},
      };
    }
    else
    {
      patterns = {
          {std::regex("^\\s*(?:class|struct|enum)\\s+([A-Za-z_][A-Za-z0-9_]*)"), "type"},
          {std::regex(
               "^\\s*(?:[A-Za-z_][A-Za-z0-9_:<>*&\\s]+\\s+)+([A-Za-z_][A-Za-z0-9_:]*)\\s*\\([^;]*"
               "\\)\\s*(?:const\\s*)?(?:noexcept\\s*)?(?:override\\s*)?(?:final\\s*)?(?:\\{|$)"),
           "function"},
      };
    }

    for (int i = 0; i < (int)lines.size(); i++)
    {
      const std::string &line = lines[(size_t)i];
      for (const auto &pattern : patterns)
      {
        std::smatch match;
        if (std::regex_search(line, match, pattern.first))
        {
          add_match(out, match, line, pattern.second, i);
          break;
        }
      }
    }
    return out;
  }

  // The kinds the outline groups under: a container whose members nest under
  // it. Everything else is a leaf.
  bool is_container_kind(const std::string &kind)
  {
    return kind == "namespace" || kind == "class" || kind == "struct" || kind == "union"
           || kind == "enum";
  }

  // Indentation (spaces, tabs at 4) of a code line, for the fallback nesting.
  int indent_of(const std::string &line)
  {
    int n = 0;
    for (char c : line)
    {
      if (c == ' ')
      {
        n++;
      }
      else if (c == '\t')
      {
        n += 4;
      }
      else
      {
        break;
      }
    }
    return n;
  }

  // The regex extractor's flat list turned into a tree by indentation: a
  // symbol nests under the nearest earlier symbol indented further left. The
  // flat order already is depth-first (containers precede their members, which
  // is what the line scan gives for the languages this fallback serves), so
  // child ranges close naturally.
  std::vector<SymbolMatch> nest_by_indent(std::vector<SymbolMatch> flat)
  {
    for (auto &s : flat)
    {
      s.depth = 0;
      s.parent = -1;
      s.child_begin = -1;
      s.child_end = -1;
    }
    std::vector<SymbolMatch> tree;
    std::vector<int> stack; // indices into tree
    std::vector<int> indents;
    for (auto &s : flat)
    {
      const int indent = s.column > 0 ? s.column : indent_of(s.detail);
      while (!stack.empty() && indents.back() >= indent)
      {
        tree[(size_t)stack.back()].child_end = (int)tree.size();
        stack.pop_back();
        indents.pop_back();
      }
      s.parent = stack.empty() ? -1 : stack.back();
      s.depth = (int)stack.size();
      if (!stack.empty())
      {
        SymbolMatch &parent_node = tree[(size_t)stack.back()];
        if (parent_node.child_begin < 0)
        {
          parent_node.child_begin = (int)tree.size();
        }
      }
      stack.push_back((int)tree.size());
      indents.push_back(indent);
      tree.push_back(std::move(s));
    }
    while (!stack.empty())
    {
      tree[(size_t)stack.back()].child_end = (int)tree.size();
      stack.pop_back();
    }
    return tree;
  }

  // The C-family outline: CppDefinitions' real parser walked once for both the
  // function records and the outline extras, then everything merges into one
  // tree keyed on the outline scope path. Every distinct scope path a symbol
  // sits under gets a container node (namespace, class, struct, union, enum),
  // and members hang under their scope's node in line order. A member whose
  // scope names a class this file never declares (a fragment included into a
  // class) still gets its group: the scope path exists because the member does.
  std::vector<SymbolMatch> cpp_outline(const std::string &filepath,
                                       const std::vector<CppDefinitions::FunctionRecord> &records,
                                       const std::vector<CppDefinitions::SymbolRecord> &extras)
  {
    struct Member
    {
      std::string scope; // the outline scope it sits in ("" = file scope)
      std::string kind;
      std::string name;
      std::string detail;
      int line = 0;
      int col = 0;
      int end_col = 0;
      bool container = false; // a namespace/class/struct/union/enum: others nest under it
    };

    std::vector<Member> members;
    for (const auto &r : records)
    {
      Member m;
      m.scope = r.outline_scope;
      m.kind = r.outline_method ? "method" : "function";
      m.name = r.name;
      m.detail = r.return_type;
      m.line = r.line;
      m.col = r.name_col;
      m.end_col = r.name_end_col;
      members.push_back(std::move(m));
    }
    for (const auto &e : extras)
    {
      Member m;
      m.scope = e.scope;
      m.kind = e.kind.empty() ? "type" : e.kind;
      m.name = e.name;
      m.detail = e.detail;
      m.line = e.line;
      m.col = e.col;
      m.end_col = e.end_col;
      m.container = is_container_kind(m.kind);
      members.push_back(std::move(m));
    }
    // Deepest scope first, members of one scope in line order: a scope's
    // container node exists before any member hangs off it, and the children
    // of a parent are contiguous in declaration order.
    std::stable_sort(members.begin(),
                     members.end(),
                     [](const Member &a, const Member &b)
                     {
                       if (a.scope != b.scope)
                       {
                         return a.scope.size() > b.scope.size();
                       }
                       return a.line < b.line;
                     });

    std::vector<SymbolMatch> tree;
    std::map<std::string, int> node_of_scope;

    // The innermost ancestor path that already has a node, creating the nodes
    // between as namespaces (their kind is refined by the extras' own record
    // when that arrives -- no: a namespace node is only created here as an
    // ancestor placeholder for a member of a deeper scope the file never
    // declares, so "namespace" is the honest label).
    std::function<int(const std::string &, const std::string &, const std::string &, int, int)>
        node_for_scope = [&](const std::string &scope,
                             const std::string &kind,
                             const std::string &detail,
                             int line,
                             int col) -> int
    {
      if (scope.empty())
      {
        return -1;
      }
      auto it = node_of_scope.find(scope);
      if (it != node_of_scope.end())
      {
        // The declaring record wins over an ancestor placeholder's kind.
        if (!kind.empty())
        {
          tree[(size_t)it->second].kind = kind;
          tree[(size_t)it->second].detail = detail;
          if (line >= 0)
          {
            tree[(size_t)it->second].line = line;
            tree[(size_t)it->second].column = col;
          }
        }
        return it->second;
      }
      std::string parent_scope;
      const size_t cut = scope.rfind("::");
      if (cut != std::string::npos)
      {
        parent_scope = scope.substr(0, cut);
      }
      const int parent_index = node_for_scope(parent_scope, "", "", -1, 0);
      SymbolMatch node;
      node.name = scope.substr(cut == std::string::npos ? 0 : cut + 2);
      node.kind = kind.empty() ? "namespace" : kind;
      node.detail = kind.empty() ? "namespace" : detail;
      node.line = line;
      node.column = col;
      node.parent = parent_index;
      node.depth = parent_index < 0 ? 0 : tree[(size_t)parent_index].depth + 1;
      const int index = (int)tree.size();
      if (parent_index >= 0 && tree[(size_t)parent_index].child_begin < 0)
      {
        tree[(size_t)parent_index].child_begin = index;
      }
      node.child_begin = -1;
      node.child_end = index + 1;
      tree.push_back(node);
      node_of_scope[scope] = index;
      return index;
    };

    // Deepest scope first: every member's ancestor chain is complete before
    // the member itself hangs (a child range only ever grows, never reopens).
    for (const Member &m : members)
    {
      if (m.container)
      {
        node_for_scope(
            m.scope.empty() ? m.name : m.scope + "::" + m.name, m.kind, m.detail, m.line, m.col);
      }
    }
    for (const Member &m : members)
    {
      if (m.container)
      {
        continue;
      }
      SymbolMatch node;
      node.name = m.name;
      node.kind = m.kind;
      node.detail = m.detail;
      node.line = m.line;
      node.column = m.col;
      const int parent_index = node_for_scope(m.scope, "", "", -1, 0);
      node.parent = parent_index;
      node.depth = parent_index < 0 ? 0 : tree[(size_t)parent_index].depth + 1;
      if (parent_index >= 0 && tree[(size_t)parent_index].child_begin < 0)
      {
        tree[(size_t)parent_index].child_begin = (int)tree.size();
      }
      node.child_begin = -1;
      node.child_end = (int)tree.size() + 1;
      tree.push_back(node);
    }

    // The build above hangs members off their scope in two passes (containers,
    // then the leaves), so the flat list is grouped by scope depth rather than
    // depth-first: a container's subtree is not the run after it. Every
    // consumer reads the flat list as depth-first -- the fold ranges, the
    // visible-row walk, the row hit-tests -- so re-emit the tree here: roots in
    // source order, each followed by its own subtree, with the child ranges
    // closing around exactly the descendants.
    std::vector<std::vector<int>> children(tree.size());
    std::vector<int> roots;
    for (int i = 0; i < (int)tree.size(); i++)
    {
      if (tree[(size_t)i].parent >= 0 && tree[(size_t)i].parent < (int)tree.size())
      {
        children[(size_t)tree[(size_t)i].parent].push_back(i);
      }
      else
      {
        tree[(size_t)i].parent = -1;
        roots.push_back(i);
      }
    }
    const auto by_source_order = [&](int a, int b)
    {
      if (tree[(size_t)a].line != tree[(size_t)b].line)
      {
        return tree[(size_t)a].line < tree[(size_t)b].line;
      }
      return tree[(size_t)a].column < tree[(size_t)b].column;
    };
    std::stable_sort(roots.begin(), roots.end(), by_source_order);
    for (std::vector<int> &list : children)
    {
      std::stable_sort(list.begin(), list.end(), by_source_order);
    }

    std::vector<SymbolMatch> ordered;
    ordered.reserve(tree.size());
    const std::function<void(int, int, int)> emit = [&](int old_index, int depth, int parent) {
      SymbolMatch node = tree[(size_t)old_index];
      node.parent = parent;
      node.depth = depth;
      node.child_begin = -1;
      node.child_end = -1;
      const int self = (int)ordered.size();
      ordered.push_back(std::move(node));
      const int first_child = (int)ordered.size();
      for (int child : children[(size_t)old_index])
      {
        emit(child, depth + 1, self);
      }
      if ((int)ordered.size() > first_child)
      {
        ordered[(size_t)self].child_begin = first_child;
        ordered[(size_t)self].child_end = (int)ordered.size();
      }
    };
    for (int root : roots)
    {
      emit(root, 0, -1);
    }
    return ordered;
  }

  // The Python outline: the regex extractor's flat list (classes and defs in
  // file order, already depth-first because a class precedes its methods)
  // nested by indentation, with the flat `column` carrying the name's indent.
  std::vector<SymbolMatch> python_outline(const std::vector<SymbolMatch> &flat)
  {
    std::vector<SymbolMatch> tree;
    std::vector<int> stack;
    std::vector<int> indents;
    for (const SymbolMatch &s : flat)
    {
      while (!stack.empty() && indents.back() >= s.column)
      {
        tree[(size_t)stack.back()].child_end = (int)tree.size();
        stack.pop_back();
        indents.pop_back();
      }
      SymbolMatch node = s;
      node.parent = stack.empty() ? -1 : stack.back();
      node.depth = (int)stack.size();
      if (!stack.empty() && tree[(size_t)stack.back()].child_begin < 0)
      {
        tree[(size_t)stack.back()].child_begin = (int)tree.size();
      }
      node.child_begin = -1;
      node.child_end = (int)tree.size() + 1;
      stack.push_back((int)tree.size());
      indents.push_back(s.column);
      tree.push_back(std::move(node));
    }
    while (!stack.empty())
    {
      tree[(size_t)stack.back()].child_end = (int)tree.size();
      stack.pop_back();
    }
    return tree;
  }
} // namespace SymbolIndex

namespace SymbolIndex
{
  std::vector<SymbolMatch> extract_outline(const std::vector<std::string> &lines,
                                           const std::string &filepath)
  {
    const std::string ext = extension_for(filepath);
    if (ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".cc"
        || ext == ".cpp" || ext == ".cxx" || ext == ".ino" || ext == ".ii" || ext == ".ipp"
        || ext == ".tpp" || ext == ".hpp.in" || ext == ".cu" || ext == ".m" || ext == ".mm")
    {
      std::string text;
      for (const auto &line : lines)
      {
        text += line;
        text += '\n';
      }
      std::vector<CppDefinitions::FunctionRecord> records;
      std::vector<CppDefinitions::SymbolRecord> extras;
      records = CppDefinitions::parse_file(filepath, text, &extras);
      return cpp_outline(filepath, records, extras);
    }

    // Everything else: the line-regex extractor, nested by the name's column
    // (the extractor records it, so Python methods nest under their class
    // without re-deriving indentation from the line text).
    const std::vector<SymbolMatch> flat = extract_document_symbols(lines, filepath);
    if (ext == ".py")
    {
      return python_outline(flat);
    }
    // Other languages keep the old flat behaviour: their nesting rules are
    // theirs (Rust's `impl`, Go's receivers), and a wrong guess is worse than
    // a flat list.
    return flat;
  }

  std::vector<int> outline_visible_rows(const std::vector<SymbolMatch> &tree,
                                        const std::vector<char> &expanded)
  {
    std::vector<int> out;
    out.reserve(tree.size());
    // One pass over the flat order, skipping the closed subtrees: a node whose
    // parent chain passes through a collapsed node does not show. The flat
    // order is depth-first, so a node's subtree is the contiguous run after it
    // that ends at its child_end; walking with an "inside skipped range" mark
    // needs only the parent links.
    std::vector<int> collapsed_depth((int)tree.size(), -1);
    int hidden_below = -1; // flat index of the collapsed node we are inside, -1 = visible
    for (int i = 0; i < (int)tree.size(); i++)
    {
      if (hidden_below >= 0)
      {
        // Still inside the collapsed node's range?
        if (i < tree[(size_t)hidden_below].child_end)
        {
          continue;
        }
        hidden_below = -1;
      }
      out.push_back(i);
      const SymbolMatch &node = tree[(size_t)i];
      const bool open = node.has_children() && i < (int)expanded.size() && expanded[(size_t)i];
      if (node.has_children() && !open)
      {
        hidden_below = i;
      }
    }
    return out;
  }
} // namespace SymbolIndex
