#ifndef JOT_FEATURES_CPP_DEFINITIONS_H
#define JOT_FEATURES_CPP_DEFINITIONS_H

#include <map>
#include <string>
#include <vector>

// The C++ definition checks behind `:cppcheck` and the Problems list: parse the
// workspace's headers and sources for function declarations and definitions, then
// pair them up so that
//
//   * a function declared with no definition anywhere becomes a
//     "missing implementation" diagnostic -- underlined on the leading half of
//     the name, so it lands on the unresolved symbol and not on the whole
//     signature -- and
//   * a signature implemented more than once becomes a "multiple definitions"
//     one (the linker's classic `multiple definition of f`), except a file-scope
//     `main`, which a folder of standalone programs repeats on purpose -- and
//     except a workspace in which every source file holds its own `main`, which
//     is that folder and is compiled one file at a time.
//
// The parser is deliberately a *declaration-scope* parser: it walks namespaces,
// classes and the preprocessor and skips every function body. At statement level
// `foo(1);` and `void f();` are not tellable apart, but a body never declares
// anything, so nothing inside one is ever parsed -- which removes that whole
// class of false positives. What remains are the ambiguities C++ keeps at
// namespace and class scope ("type or name" for `Foo x(1);`, `= 0`, `= delete`,
// inline and template repeats); each is answered where it is applied in
// cpp_definitions.cpp and every answer is stated in a comment there.
//
// What is deliberately *not* checked: whether a definition's return type matches
// the declaration's (return types are not part of the match key, so a wrong one
// still counts as implementing the declaration), and whether a definition is
// reachable at all from where it is used. Both need real type information.
namespace CppDefinitions
{
  // One function signature found at declaration scope: where its name sits, what
  // it is called, and everything that decides whether a missing or repeated
  // implementation gets reported.
  struct FunctionRecord
  {
    std::string file;
    int line = 0; // 0-based, at the first token of the name
    int col = 0;  // 0-based
    int end_line = 0;
    int end_col = 0; // 0-based, one past the closing paren
    // The declared name on its own (no `ns::` in front of it), which is what a
    // missing-body finding is marked on.
    int name_line = 0;
    int name_col = 0;
    int name_end_line = 0;
    int name_end_col = 0; // 0-based, one past the name's last character

    std::string scope;          // "ns::Class", empty at file scope
    std::string name;           // last component: "f", "~Widget", "Widget", "operator bool"
    std::string params;         // canonical parameter list, parameter names dropped
    std::string params_display; // canonical parameter list with the names kept
    std::string suffix;         // canonical trailing " const&" (empty when none)
    // What two records match on: scope, name, canonical params and the cv/ref
    // suffix. Storage class, return type, `inline`, `noexcept` and `virtual` are
    // not part of a signature.
    std::string key;
    std::string display; // "ns::Class::f(int) const" as messages print it

    bool definition = false;
    // A definition inside the class body: implicitly inline, so it never counts
    // as one of several implementations.
    bool in_class_definition = false;
    // `= default` / `= delete`: the compiler writes the body, nothing is missing.
    bool deleted_or_default = false;
    bool pure_virtual = false;
    bool template_function = false;
    // `inline` / `constexpr` / `consteval`, or a class-body definition: an
    // implementation that may legally appear once per translation unit.
    bool inline_function = false;
    // `static` at namespace scope, or anything inside an anonymous namespace:
    // one copy per translation unit, so two of them in different files are fine.
    bool internal_linkage = false;
    // Declared or defined under a `#if` that is not the include guard: it may
    // simply not be compiled on this platform, so a missing body is only a hint.
    bool conditional = false;
    bool is_header = false;
  };

  struct Issue
  {
    std::string file;
    int line = 0;
    int col = 0;
    int end_line = 0;
    int end_col = 0;
    int severity = 1; // the Diagnostic scale: 1=Error, 2=Warning, 3=Info
    std::string message;
  };

  struct ScanStats
  {
    int files_scanned = 0;
    int declarations = 0;
    int definitions = 0;
    int missing = 0;
    int duplicates = 0;
    long long classification_records = 0;
    long long bytes_read = 0;
    long long elapsed_ms = 0;
  };

  struct ScanLimits
  {
    long long max_file_bytes = 4 * 1024 * 1024;
    // A ceiling on the whole walk so a checkout full of generated sources cannot
    // turn a save into a long read.
    long long max_total_bytes = 64 * 1024 * 1024;
    int max_files = 20000;
  };

  struct ScanResult
  {
    std::vector<Issue> issues;
    ScanStats stats;
  };

  // Path predicates: which files hold C++ signatures, and which of those are
  // headers (a header is what a second translation unit would include).
  bool is_header_file(const std::string &path);
  bool is_source_file(const std::string &path);
  bool is_parseable_file(const std::string &path);

  // Every function signature in one file's text. Pure: no filesystem access, so
  // a test can run whole workspaces through it from memory.
  std::vector<FunctionRecord> parse_file(const std::string &path, const std::string &text);

  // Include finding positions as well as match keys so moving a declaration
  // refreshes its squiggle, while editing only a body's text avoids another walk.
  std::string signature_of(const std::string &path, const std::string &text);

  // Pairs the records of a workspace up and reports what is missing or repeated.
  std::vector<Issue> analyze(const std::vector<FunctionRecord> &records,
                             ScanStats *stats = nullptr);

  // Unsaved workspace text replaces disk text so declarations need not be saved
  // before they can produce findings; the same scan limits apply to both.
  ScanResult scan_workspace(const std::string &root,
                            const ScanLimits &limits = ScanLimits{},
                            const std::map<std::string, std::string> &unsaved = {});
} // namespace CppDefinitions

#endif // JOT_FEATURES_CPP_DEFINITIONS_H
