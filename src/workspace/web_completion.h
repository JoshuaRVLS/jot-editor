#ifndef WEB_COMPLETION_H
#define WEB_COMPLETION_H

#include <cstddef>
#include <string>
#include <vector>

// The workspace's own CSS vocabulary: the class names the markup uses and the
// custom properties the style sheets declare, offered as completions inside
// `class="..."` and `var(--)`.
//
// An LSP server answers this for a project it can index (ts_ls does, in
// JavaScript and TypeScript). It does not answer it for plain HTML and CSS,
// where the vocabulary is spread across every file in the workspace and a
// single-file server sees one of them -- so the scan below reads the workspace
// itself and the editor offers the result alongside whatever a server says.
//
// The engine is pure: text in, names out, plus one context test that says
// whether the caret is somewhere these names belong. The editor side
// (src/extras/web_index.cpp) runs the walk off the main thread and splices the
// names into the completion list, so every rule here is testable without an
// editor and without a server.
namespace WebCompletion
{
  // The vocabulary a workspace declares, both lists sorted and deduplicated.
  // The names are stored the way they are typed: a class without its leading
  // `.`, a custom property without its leading `--` (the `--` is already in the
  // buffer by the time a var is being completed, and re-inserting it would
  // double it).
  struct Index
  {
    std::vector<std::string> classes;
    std::vector<std::string> css_vars;

    bool empty() const
    {
      return classes.empty() && css_vars.empty();
    }
    void sort_unique();
  };

  struct ScanLimits
  {
    std::size_t max_files = 600;
    std::size_t max_file_bytes = 512 * 1024;
  };

  // Where the caret is, for deciding which half of the index applies. `css` is
  // the buffer's file type: the two contexts are spelled differently enough
  // (an attribute value vs. a function argument) that one test cannot cover
  // both, and a class name is never a valid completion in a style sheet.
  enum class Context
  {
    None,
    ClassName, // inside class="..." / className="..." in markup
    CssVar,    // inside var(--) or a `--prop:` declaration in a style sheet
  };

  // The names a CSS text declares: every `--name`. Reads declarations only --
  // a `--name` in a comment is not a declaration, so a comment's text is
  // skipped.
  void extract_from_css(const std::string &text, Index &out);

  // The names a markup text uses: every token in a `class="..."`, `class='...'`
  // or `className="..."` attribute. Template markers (a `{`/`$`/backtick in a
  // token) are dropped -- they name a value the scan cannot resolve.
  void extract_from_markup(const std::string &text, Index &out);

  // The names a style sheet *selects*: every `.name` in a selector position.
  // A `.` that follows a digit, an identifier or another `.` is not a class
  // selector (`1.5`, `a.b`), so those are skipped.
  void extract_selectors(const std::string &text, Index &out);

  // One file, dispatched on its extension. A file type the vocabulary does not
  // live in contributes nothing.
  void extract_from_file_text(const std::string &path, const std::string &text, Index &out);

  // Whether `path` is a file the scan reads at all, and which language its text
  // holds. The pair is what the completion path needs to pick the context test:
  // a style sheet spells its contexts differently from markup, and a class name
  // is never a valid completion in one.
  bool is_markup_path(const std::string &path);
  bool is_style_path(const std::string &path);
  bool is_scanned_path(const std::string &path);

  // Where the caret sits on `line` (0-based `col`). Used to decide whether to
  // offer the index, and which half of it.
  Context context_at(const std::string &line, int col, bool css);

  // Every scanned file under `root`, read and folded into one index. Missing or
  // unreadable paths yield an empty index rather than an error: the completion
  // simply has nothing extra to offer.
  Index scan_workspace(const std::string &root, const ScanLimits &limits = ScanLimits{});
} // namespace WebCompletion

#endif
