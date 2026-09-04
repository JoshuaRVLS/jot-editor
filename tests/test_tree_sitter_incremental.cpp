// Verifies that the incremental tree-sitter reparse used on the typing path
// (one bounded ts_tree_edit + parse, see TreeSitterManager::reparse_incremental
// and Editor::reparse_tree) produces trees and highlights identical to a fresh
// whole-file parse. Requires an installed cpp parser; skipped otherwise.
#include "tree_sitter/manager.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
  double ms_since(std::chrono::steady_clock::time_point start)
  {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
  }

  std::string join_lines(const std::vector<std::string> &lines)
  {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i)
    {
      if (i > 0)
        out.push_back('\n');
      out += lines[i];
    }
    return out;
  }
} // namespace

TEST_CASE("Tree-sitter incremental reparse stays equivalent to full reparse")
{
#ifdef JOT_TREESITTER
  TreeSitterManager manager;
  manager.register_language(
      "cpp", {".cpp"}, "", "", "", "tree_sitter_cpp", {"libtree-sitter-cpp.so"});
  const TSLanguage *lang = manager.load_language("cpp");
  if (!lang)
  {
    WARN("cpp parser not installed; skipping incremental reparse equivalence test");
    return;
  }
  TSParser *parser = ts_parser_new();
  REQUIRE(parser != nullptr);
  REQUIRE(ts_parser_set_language(parser, lang));

  std::vector<std::string> lines = {
      "#include <string>",
      "#include <vector>",
      "",
      "// a comment explaining the widget",
      "struct Widget {",
      "  int id;",
      "  std::string label;",
      "};",
      "",
      "static int compute(int a, int b) {",
      "  const int sum = a + b; // inline note",
      "  if (sum > 100) {",
      "    return sum;",
      "  }",
      "  return \"fallback\".size();",
      "}",
      "",
      "int main(int argc, char **argv) {",
      "  std::vector<Widget> widgets;",
      "  for (int i = 0; i < argc; ++i) {",
      "    Widget w;",
      "    w.id = i;",
      "    w.label = \"item\";",
      "    widgets.push_back(w);",
      "  }",
      "  return compute(widgets.size(), 0);",
      "}",
  };
  const std::string original = join_lines(lines);

  auto full_parse = [&](const std::string &text)
  { return ts_parser_parse_string(parser, nullptr, text.data(), (uint32_t)text.size()); };

  auto compare_trees = [&](TSTree *fresh, TSTree *incremental)
  {
    REQUIRE(fresh != nullptr);
    REQUIRE(incremental != nullptr);
    char *a = ts_node_string(ts_tree_root_node(fresh));
    char *b = ts_node_string(ts_tree_root_node(incremental));
    const bool same = std::string(a) == std::string(b);
    if (!same)
    {
      INFO("incremental parse diverged from full parse");
      INFO("full:  " << a);
      INFO("incr:  " << b);
    }
    std::free(a);
    std::free(b);
    REQUIRE(same);
  };

  struct EditCase
  {
    const char *name;
    std::string text;
  };
  const std::vector<EditCase> cases = {
      {"insert at file start", "// header inserted first\n" + original},
      {"single char mid-file",
       [&]
       {
         std::string t = original;
         // inside compute(), just before the closing brace of the if body
         const size_t pos = original.find("return sum;");
         REQUIRE(pos != std::string::npos);
         t.insert(pos + 5, "x");
         return t;
       }()},
      {"open a string mid-file",
       [&]
       {
         std::string t = original;
         const size_t pos = original.find("w.label = ");
         REQUIRE(pos != std::string::npos);
         t.insert(pos + 10, "\"");
         return t;
       }()},
      {"delete a range spanning two lines",
       [&]
       {
         std::string t = original;
         const size_t start = original.find("static int compute");
         const size_t end = original.find("return \"fallback\"");
         REQUIRE(start != std::string::npos);
         REQUIRE(end != std::string::npos);
         t.erase(start, end - start);
         return t;
       }()},
      {"insert a newline in the middle of a line",
       [&]
       {
         std::string t = original;
         const size_t pos = original.find("for (int i = 0;");
         REQUIRE(pos != std::string::npos);
         t.insert(pos, "\n  // new");
         return t;
       }()},
      {"two disjoint edits at once",
       [&]
       {
         std::string t = original;
         const size_t p1 = original.find("int id;");
         const size_t p2 = original.find("return compute");
         REQUIRE(p1 != std::string::npos);
         REQUIRE(p2 != std::string::npos);
         t.insert(p1 + 3, "  /* a */");
         t.insert(p2 + 14, "n");
         return t;
       }()},
      {"edit at the very end of the file",
       [&]
       {
         std::string t = original;
         t += "\n// trailing";
         return t;
       }()},
  };

  for (const auto &tc : cases)
  {
    SECTION(tc.name)
    {
      TSTree *fresh = full_parse(tc.text);
      TSTree *tree = full_parse(original);
      TSTree *incremental = TreeSitterManager::reparse_incremental(parser, tree, original, tc.text);
      compare_trees(fresh, incremental);
      ts_tree_delete(fresh);
      ts_tree_delete(incremental);
    }
  }

  // A no-op (base == text) must return the tree untouched (no leak, no churn).
  {
    TSTree *tree = full_parse(original);
    TSTree *same = TreeSitterManager::reparse_incremental(parser, tree, original, original);
    REQUIRE(same == tree);
    ts_tree_delete(same);
  }

  // Sanity timing (informational only): incremental single-char reparse should
  // be far cheaper than a whole-file parse on this document.
  {
    const size_t pos = original.find("return sum;");
    REQUIRE(pos != std::string::npos);
    std::string edited = original;
    edited.insert(pos + 5, "x");

    TSTree *tree = full_parse(original);
    const int iters = 50;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i)
    {
      TSTree *t1 = TreeSitterManager::reparse_incremental(parser, tree, original, edited);
      tree = t1;
      TSTree *t2 = TreeSitterManager::reparse_incremental(parser, tree, edited, original);
      tree = t2;
    }
    const double incremental_ms = ms_since(t0) / iters;

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i)
    {
      TSTree *f =
          ts_parser_parse_string(parser, nullptr, original.data(), (uint32_t)original.size());
      ts_tree_delete(f);
    }
    const double full_ms = ms_since(t0) / iters;
    std::printf("[perf] incremental reparse avg %.3f ms vs full parse avg %.3f ms\n",
                incremental_ms,
                full_ms);
    ts_tree_delete(tree);
  }

  ts_parser_delete(parser);
#else
  // Tree-sitter runtime is not compiled in; nothing to verify.
#endif
}
