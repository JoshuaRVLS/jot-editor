#include "tree_sitter_manager.h"
#include "tree_sitter_catalog.h"
#include "config.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef JOT_TREESITTER
#include <dlfcn.h>
#endif

namespace {
namespace fs = std::filesystem;

std::string trim_copy(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

std::vector<std::string> split_list(const std::string &text, char delimiter = ':') {
  std::vector<std::string> out;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, delimiter)) {
    item = trim_copy(item);
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

std::string read_file(const fs::path &path) {
  std::ifstream file(path);
  if (!file.is_open()) return "";
  std::stringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

fs::path home_path(const std::string &suffix) {
  const char *home = getenv("HOME");
  if (!home) return fs::path();
  return fs::path(home) / suffix;
}

const char *builtin_query_c() { return R"TREEQUERY(
"break" @keyword.control
"case" @keyword.control
"const" @keyword.storage
"continue" @keyword.control
"default" @keyword.control
"do" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"for" @keyword.control
"goto" @keyword.control
"if" @keyword.control
"return" @keyword.control
"sizeof" @keyword
"static" @keyword
"struct" @keyword
"switch" @keyword.control
"typedef" @keyword
"union" @keyword
"volatile" @keyword
"while" @keyword.control
(escape_sequence) @string.escape
(string_literal) @string
(system_lib_string) @string
(comment) @comment
(number_literal) @number
(primitive_type) @type.builtin
(type_identifier) @type
(type_descriptor) @type
(field_identifier) @property
(identifier) @variable
(call_expression function: (identifier) @function)
(call_expression function: (field_expression field: (field_identifier) @function.method))
(function_declarator declarator: (identifier) @function)
(function_definition declarator: (function_declarator declarator: (identifier) @function))
"#include" @keyword.directive
"#define" @keyword.directive
(preproc_include) @keyword.directive
(preproc_def) @keyword.directive
(preproc_if) @keyword.directive
(preproc_elif) @keyword.directive
(preproc_else) @keyword.directive
(preproc_endif) @keyword.directive
(preproc_arg) @constant.macro
"(" @punctuation.bracket
")" @punctuation.bracket
"[" @punctuation.bracket
"]" @punctuation.bracket
"{" @punctuation.bracket
"}" @punctuation.bracket
"," @punctuation.delimiter
";" @punctuation.delimiter
"." @punctuation.delimiter
"->" @operator
"=" @operator
"+" @operator
"-" @operator
"*" @operator
"/" @operator
)TREEQUERY"; }

const char *builtin_query_cpp() { return R"TREEQUERY(
"break" @keyword.control
"case" @keyword.control
"class" @keyword
"const" @keyword.storage
"constexpr" @keyword.storage
"continue" @keyword.control
"default" @keyword.control
"do" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"for" @keyword.control
"goto" @keyword.control
"if" @keyword.control
"namespace" @keyword
"new" @keyword
"return" @keyword.control
"sizeof" @keyword
"static" @keyword.storage
"struct" @keyword
"switch" @keyword.control
"template" @keyword
"typedef" @keyword
"typename" @keyword
"union" @keyword
"using" @keyword
"virtual" @keyword.storage
"volatile" @keyword.storage
"while" @keyword.control
(escape_sequence) @string.escape
(raw_string_literal) @string
(string_literal) @string
(system_lib_string) @string
(comment) @comment
(number_literal) @number
(primitive_type) @type.builtin
(type_identifier) @type
(template_type) @type
(namespace_identifier) @namespace
(qualified_identifier scope: (namespace_identifier) @namespace)
(qualified_identifier name: (type_identifier) @type)
(field_identifier) @property
(identifier) @variable
(call_expression function: (identifier) @function)
(call_expression function: (field_expression field: (field_identifier) @function.method))
(call_expression function: (qualified_identifier name: (identifier) @function))
(call_expression function: (qualified_identifier name: (field_identifier) @function.method))
(call_expression function: (qualified_identifier name: (operator_name) @function))
(function_declarator declarator: (identifier) @function)
(function_declarator declarator: (qualified_identifier name: (identifier) @function))
(function_declarator declarator: (field_identifier) @function.method)
(function_definition declarator: (function_declarator declarator: (identifier) @function))
(function_definition declarator: (function_declarator declarator: (qualified_identifier name: (identifier) @function)))
"#include" @keyword.directive
"#define" @keyword.directive
(preproc_include) @keyword.directive
(preproc_def) @keyword.directive
(preproc_if) @keyword.directive
(preproc_elif) @keyword.directive
(preproc_else) @keyword.directive
(preproc_endif) @keyword.directive
(preproc_arg) @constant.macro
(operator_name) @operator
"(" @punctuation.bracket
")" @punctuation.bracket
"[" @punctuation.bracket
"]" @punctuation.bracket
"{" @punctuation.bracket
"}" @punctuation.bracket
"<" @punctuation.bracket
">" @punctuation.bracket
"," @punctuation.delimiter
";" @punctuation.delimiter
"." @punctuation.delimiter
"::" @punctuation.delimiter
"->" @operator
"=" @operator
"+" @operator
"-" @operator
"*" @operator
"/" @operator
)TREEQUERY"; }

#ifdef JOT_TREESITTER
const char *minimal_query_cpp() { return R"TREEQUERY(
"break" @keyword
"case" @keyword
"class" @keyword
"const" @keyword
"continue" @keyword
"do" @keyword
"else" @keyword
"enum" @keyword
"for" @keyword
"if" @keyword
"namespace" @keyword
"return" @keyword
"struct" @keyword
"switch" @keyword
"template" @keyword
"typename" @keyword
"using" @keyword
"while" @keyword
(raw_string_literal) @string
(string_literal) @string
(system_lib_string) @string
(comment) @comment
(number_literal) @number
(primitive_type) @type
(type_identifier) @type
(field_identifier) @property
(call_expression function: (identifier) @function)
(function_declarator declarator: (identifier) @function)
(preproc_include) @keyword
)TREEQUERY"; }
#endif

const char *builtin_query_python() { return R"TREEQUERY(
"and" @keyword
"as" @keyword
"assert" @keyword
"async" @keyword
"await" @keyword
"break" @keyword
"class" @keyword
"continue" @keyword
"def" @keyword
"del" @keyword
"elif" @keyword
"else" @keyword
"except" @keyword
"finally" @keyword
"for" @keyword
"from" @keyword
"global" @keyword
"if" @keyword
"import" @keyword
"in" @keyword
"is" @keyword
"lambda" @keyword
"nonlocal" @keyword
"not" @keyword
"or" @keyword
"pass" @keyword
"raise" @keyword
"return" @keyword
"try" @keyword
"while" @keyword
"with" @keyword
"yield" @keyword
"True" @keyword
"False" @keyword
"None" @keyword
(string) @string
(comment) @comment
(integer) @number
(float) @number
(call function: (identifier) @function)
(call function: (attribute attribute: (identifier) @function))
(function_definition name: (identifier) @function)
(class_definition name: (identifier) @type)
(attribute attribute: (identifier) @type)
(identifier) @variable
(parameters (identifier) @variable.parameter)
)TREEQUERY"; }

const char *builtin_query_javascript() { return R"TREEQUERY(
"break" @keyword
"case" @keyword
"catch" @keyword
"class" @keyword
"const" @keyword
"continue" @keyword
"debugger" @keyword
"default" @keyword
"delete" @keyword
"do" @keyword
"else" @keyword
"export" @keyword
"extends" @keyword
"finally" @keyword
"for" @keyword
"function" @keyword
"if" @keyword
"import" @keyword
"in" @keyword
"instanceof" @keyword
"let" @keyword
"new" @keyword
"of" @keyword
"return" @keyword
"switch" @keyword
"this" @keyword
"throw" @keyword
"try" @keyword
"typeof" @keyword
"var" @keyword
"void" @keyword
"while" @keyword
"with" @keyword
"yield" @keyword
(string) @string
(template_string) @string
(comment) @comment
(number) @number
(call_expression function: (identifier) @function)
(function_declaration name: (identifier) @function)
(class_declaration name: (identifier) @type)
(identifier) @variable
(formal_parameters (identifier) @variable.parameter)
)TREEQUERY"; }

const char *builtin_query_rust() { return R"TREEQUERY(
"as" @keyword
"async" @keyword
"await" @keyword
"break" @keyword
"const" @keyword
"continue" @keyword
"crate" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"fn" @keyword
"for" @keyword
"if" @keyword
"impl" @keyword
"in" @keyword
"let" @keyword
"loop" @keyword
"match" @keyword
"mod" @keyword
"move" @keyword
"mut" @keyword
"pub" @keyword
"ref" @keyword
"return" @keyword
"self" @keyword
"static" @keyword
"struct" @keyword
"super" @keyword
"trait" @keyword
"type" @keyword
"union" @keyword
"unsafe" @keyword
"use" @keyword
"where" @keyword
"while" @keyword
(string_literal) @string
(line_comment) @comment
(block_comment) @comment
(float_literal) @number
(integer_literal) @number
(macro_invocation macro: (identifier) @function)
(call_expression function: (identifier) @function)
(function_item name: (identifier) @function)
(struct_item name: (type_identifier) @type)
(enum_item name: (type_identifier) @type)
(identifier) @variable
)TREEQUERY"; }

const char *builtin_query_go() { return R"TREEQUERY(
"break" @keyword
"case" @keyword
"chan" @keyword
"const" @keyword
"continue" @keyword
"default" @keyword
"defer" @keyword
"else" @keyword
"fallthrough" @keyword
"for" @keyword
"func" @keyword
"go" @keyword
"goto" @keyword
"if" @keyword
"import" @keyword
"interface" @keyword
"map" @keyword
"package" @keyword
"range" @keyword
"return" @keyword
"select" @keyword
"struct" @keyword
"switch" @keyword
"type" @keyword
"var" @keyword
(interpreted_string_literal) @string
(raw_string_literal) @string
(comment) @comment
(int_literal) @number
(float_literal) @number
(call_expression function: (identifier) @function)
(function_declaration name: (identifier) @function)
(type_declaration (type_spec name: (type_identifier) @type))
(identifier) @variable
)TREEQUERY"; }

const char *builtin_query_json() { return R"TREEQUERY(
(string) @string
(number) @number
"true" @keyword
"false" @keyword
"null" @keyword
(pair key: (string) @property)
)TREEQUERY"; }

const char *builtin_query_html() { return R"TREEQUERY(
(tag_name) @tag
(attribute_name) @attribute
(attribute_value) @string
(comment) @comment
(text) @text
"<" @punctuation
">" @punctuation
"</" @punctuation
)TREEQUERY"; }

const char *builtin_query_css() { return R"TREEQUERY(
(tag_name) @tag
(class_name) @type
(id_name) @type
(property_name) @property
(string_value) @string
(comment) @comment
(integer_value) @number
(float_value) @number
"@" @keyword
"!important" @keyword
)TREEQUERY"; }

const char *builtin_query_bash() { return R"TREEQUERY(
"if" @keyword
"then" @keyword
"else" @keyword
"elif" @keyword
"fi" @keyword
"case" @keyword
"esac" @keyword
"for" @keyword
"while" @keyword
"until" @keyword
"do" @keyword
"done" @keyword
"in" @keyword
"function" @keyword
"declare" @keyword
"local" @keyword
"export" @keyword
"return" @keyword
(string) @string
(raw_string) @string
(comment) @comment
(variable_name) @variable
)TREEQUERY"; }

const char *builtin_query_lua() { return R"TREEQUERY(
"and" @keyword
"break" @keyword
"do" @keyword
"else" @keyword
"elseif" @keyword
"end" @keyword
"false" @keyword
"for" @keyword
"function" @keyword
"goto" @keyword
"if" @keyword
"in" @keyword
"local" @keyword
"nil" @keyword
"not" @keyword
"or" @keyword
"repeat" @keyword
"return" @keyword
"then" @keyword
"true" @keyword
"until" @keyword
"while" @keyword
(string) @string
(comment) @comment
(number) @number
(function_call name: (identifier) @function)
(function_declaration name: (identifier) @function)
(identifier) @variable
)TREEQUERY"; }

const char *builtin_query_markdown() { return R"TREEQUERY(
(atx_heading (heading_content) @keyword)
(setext_heading (heading_content) @keyword)
(code_fence_content) @string
(indented_code_block) @string
(fenced_code_block) @string
(emphasis) @property
(strong_emphasis) @property
(link_text) @property
(link_destination) @string
(fenced_code_block) @string
)TREEQUERY"; }

const char *builtin_query_toml() { return R"TREEQUERY(
(bare_key) @property
(quoted_key) @property
(string) @string
(integer) @number
(float) @number
(boolean) @keyword
(comment) @comment
"[" @punctuation
"]" @punctuation
)TREEQUERY"; }

const char *builtin_query_yaml() { return R"TREEQUERY(
(block_mapping_pair key: (flow_node) @property)
(block_mapping_pair key: (flow_node (plain_scalar (string_scalar) @property)))
(flow_node (plain_scalar (string_scalar) @string))
(flow_node (single_quote_scalar) @string)
(flow_node (double_quote_scalar) @string)
(integer_scalar) @number
(float_scalar) @number
(boolean_scalar) @keyword
(null_scalar) @keyword
(comment) @comment
)TREEQUERY"; }

const char *builtin_query_fallback() { return ""; }

using QueryProvider = const char *(*)();

QueryProvider query_for_lang(const std::string &lang) {
  if (lang == "c") return builtin_query_c;
  if (lang == "cpp") return builtin_query_cpp;
  if (lang == "python") return builtin_query_python;
  if (lang == "javascript" || lang == "typescript" || lang == "tsx")
    return builtin_query_javascript;
  if (lang == "rust") return builtin_query_rust;
  if (lang == "go") return builtin_query_go;
  if (lang == "json") return builtin_query_json;
  if (lang == "html") return builtin_query_html;
  if (lang == "css") return builtin_query_css;
  if (lang == "bash") return builtin_query_bash;
  if (lang == "lua") return builtin_query_lua;
  if (lang == "markdown") return builtin_query_markdown;
  if (lang == "toml") return builtin_query_toml;
  if (lang == "yaml") return builtin_query_yaml;
  return builtin_query_fallback;
}

} // namespace

TreeSitterManager::TreeSitterManager() { register_languages(); }
TreeSitterManager::~TreeSitterManager() {
#ifdef JOT_TREESITTER
  for (auto &entry : query_cache_) {
    if (entry.second) {
      ts_query_delete(entry.second);
    }
  }
  for (auto &entry : library_handles_) {
    if (entry.second) {
      dlclose(entry.second);
    }
  }
#endif
}

void TreeSitterManager::register_languages() {
  ext_to_lang_.clear();
  languages_.clear();
  for (const auto &catalog_entry : TreeSitterCatalog::entries()) {
    TSLanguageEntry entry;
    entry.language_id = catalog_entry.name;
    entry.highlight_query_source = query_for_lang(catalog_entry.name)();
    languages_[catalog_entry.name] = std::move(entry);
    for (const auto &ext : catalog_entry.extensions) {
      ext_to_lang_[ext] = catalog_entry.name;
    }
  }

  std::vector<std::string> default_library_paths;
  const char *env_paths = getenv("JOT_TREESITTER_PATH");
  if (env_paths && *env_paths) {
    default_library_paths = split_list(env_paths, ':');
  }
  for (const auto &path : {
           home_path(".local/lib/jot/tree-sitter"),
           home_path(".local/lib"),
           fs::path("/usr/local/lib"),
           fs::path("/usr/lib"),
           fs::path("/opt/homebrew/lib"),
       }) {
    if (!path.empty()) {
      default_library_paths.push_back(path.string());
    }
  }
  runtime_library_paths_ = default_library_paths;

  std::vector<std::string> default_query_paths;
  const char *env_query_paths = getenv("JOT_TREESITTER_QUERY_PATH");
  if (env_query_paths && *env_query_paths) {
    default_query_paths = split_list(env_query_paths, ':');
  }
  for (const auto &path : {
           home_path(".config/jot/treesitter/queries"),
           home_path(".local/share/jot/treesitter/queries"),
       }) {
    if (!path.empty()) {
      default_query_paths.push_back(path.string());
    }
  }
  runtime_query_paths_ = default_query_paths;
}

TreeSitterManager::QuerySource
TreeSitterManager::load_query_source(const std::string &language_name) const {
  for (const auto &root : runtime_query_paths_) {
    fs::path base(root);
    for (const auto &candidate : {
             base / language_name / "highlights.scm",
             base / (language_name + ".scm"),
         }) {
      std::string source = read_file(candidate);
      if (!source.empty()) {
        QuerySource query;
        query.source = source;
        query.path = candidate.string();
        query.runtime = true;
        return query;
      }
    }
  }
  auto it = languages_.find(language_name);
  QuerySource query;
  query.source = it != languages_.end() ? it->second.highlight_query_source
                                        : builtin_query_fallback();
  query.runtime = false;
  return query;
}

const TSLanguageEntry *TreeSitterManager::get_language(const std::string &extension) const {
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end()) return nullptr;
  auto lang_it = languages_.find(ext_it->second);
  if (lang_it == languages_.end()) return nullptr;
  return &lang_it->second;
}

bool TreeSitterManager::has_language(const std::string &extension) const {
  return get_language(extension) != nullptr;
}

bool TreeSitterManager::has_language_override(
    const std::string &extension) const {
  return language_override_extensions_.find(extension) !=
         language_override_extensions_.end();
}

std::string
TreeSitterManager::language_id_for_extension(const std::string &extension) const {
  const TSLanguageEntry *entry = get_language(extension);
  return entry ? entry->language_id : "";
}

TreeSitterRuntimeStatus
TreeSitterManager::runtime_status_for_extension(
    const std::string &extension) const {
  TreeSitterRuntimeStatus status;
  status.language_id = language_id_for_extension(extension);
  status.has_language = !status.language_id.empty();
  if (!status.has_language) {
    status.parser_message = "unsupported extension";
    return status;
  }
#ifdef JOT_TREESITTER
  auto parser_it = parser_languages_.find(status.language_id);
  status.parser_loaded =
      parser_it != parser_languages_.end() && parser_it->second != nullptr;
  auto parser_diag_it = parser_diagnostics_.find(status.language_id);
  status.parser_message = parser_diag_it == parser_diagnostics_.end()
                              ? (status.parser_loaded ? "parser loaded"
                                                      : "parser not attempted")
                              : parser_diag_it->second;

  auto query_it = query_cache_.find(status.language_id);
  status.query_loaded =
      query_it != query_cache_.end() && query_it->second != nullptr;
  auto runtime_it = runtime_query_used_.find(status.language_id);
  status.used_runtime_query =
      runtime_it != runtime_query_used_.end() && runtime_it->second;
  auto builtin_it = builtin_query_used_.find(status.language_id);
  status.used_builtin_query =
      builtin_it != builtin_query_used_.end() && builtin_it->second;
  auto query_diag_it = query_diagnostics_.find(status.language_id);
  status.query_message = query_diag_it == query_diagnostics_.end()
                             ? (status.query_loaded ? "query loaded"
                                                    : "query not attempted")
                             : query_diag_it->second;
#else
  status.parser_message = "Tree-sitter runtime not available";
  status.query_message = "Tree-sitter runtime not available";
#endif
  return status;
}

TreeSitterRuntimeStatus
TreeSitterManager::runtime_status_for_language(
    const std::string &language_id) const {
  TreeSitterRuntimeStatus status;
  status.language_id = TreeSitterCatalog::normalize_language_name(language_id);
  status.has_language = !status.language_id.empty() &&
                        languages_.find(status.language_id) != languages_.end();
  if (!status.has_language) {
    status.parser_message = "unsupported language";
    return status;
  }
#ifdef JOT_TREESITTER
  status.parser_loaded = load_language(status.language_id) != nullptr;
  auto parser_diag_it = parser_diagnostics_.find(status.language_id);
  status.parser_message = parser_diag_it == parser_diagnostics_.end()
                              ? (status.parser_loaded ? "parser loaded"
                                                      : "parser not attempted")
                              : parser_diag_it->second;

  auto query_it = query_cache_.find(status.language_id);
  status.query_loaded =
      query_it != query_cache_.end() && query_it->second != nullptr;
  auto runtime_it = runtime_query_used_.find(status.language_id);
  status.used_runtime_query =
      runtime_it != runtime_query_used_.end() && runtime_it->second;
  auto builtin_it = builtin_query_used_.find(status.language_id);
  status.used_builtin_query =
      builtin_it != builtin_query_used_.end() && builtin_it->second;
  auto query_diag_it = query_diagnostics_.find(status.language_id);
  status.query_message = query_diag_it == query_diagnostics_.end()
                             ? (status.query_loaded ? "query loaded"
                                                    : "query not attempted")
                             : query_diag_it->second;
#else
  status.parser_message = "Tree-sitter runtime not available";
  status.query_message = "Tree-sitter runtime not available";
#endif
  return status;
}

void TreeSitterManager::set_runtime_options(
    const std::vector<std::string> &library_paths,
    const std::vector<std::string> &query_paths,
    const std::vector<std::string> &language_overrides) {
  if (!library_paths.empty()) {
    runtime_library_paths_.insert(runtime_library_paths_.begin(),
                                  library_paths.begin(), library_paths.end());
  }
  if (!query_paths.empty()) {
    runtime_query_paths_.insert(runtime_query_paths_.begin(), query_paths.begin(),
                                query_paths.end());
  }
  for (const auto &raw : language_overrides) {
    size_t sep = raw.find(':');
    if (sep == std::string::npos) {
      sep = raw.find('=');
    }
    if (sep == std::string::npos) {
      continue;
    }
    std::string ext = trim_copy(raw.substr(0, sep));
    std::string lang =
        TreeSitterCatalog::normalize_language_name(trim_copy(raw.substr(sep + 1)));
    if (ext.empty() || lang.empty()) {
      continue;
    }
    if (ext.front() != '.') {
      ext = "." + ext;
    }
    if (languages_.find(lang) == languages_.end()) {
      TSLanguageEntry entry;
      entry.language_id = lang;
      entry.highlight_query_source = query_for_lang(lang)();
      languages_[lang] = std::move(entry);
    }
    ext_to_lang_[ext] = lang;
    language_override_extensions_.insert(ext);
  }
}

void TreeSitterManager::reload() {
#ifdef JOT_TREESITTER
  for (auto &entry : query_cache_) {
    if (entry.second) {
      ts_query_delete(entry.second);
    }
  }
  query_cache_.clear();
  parser_languages_.clear();
  for (auto &entry : library_handles_) {
    if (entry.second) {
      dlclose(entry.second);
    }
  }
  library_handles_.clear();
  parser_diagnostics_.clear();
  query_diagnostics_.clear();
  runtime_query_used_.clear();
  builtin_query_used_.clear();
#endif
}

#ifdef JOT_TREESITTER
namespace {
using TreeSitterLanguageFn = const TSLanguage *(*)();

std::vector<fs::path> library_candidates(
    const std::string &language_name,
    const std::vector<std::string> &runtime_paths) {
  std::vector<std::string> library_names;
  if (const auto *entry = TreeSitterCatalog::find_language(language_name)) {
    library_names = entry->library_names;
  } else {
    library_names =
        TreeSitterCatalog::entry_for_github_url("https://github.com/x/tree-sitter-" +
                                                language_name)
            .library_names;
  }

  std::vector<fs::path> candidates;
  for (const auto &name : library_names) {
    candidates.emplace_back(name);
  }
  for (const auto &root : runtime_paths) {
    for (const auto &name : library_names) {
      candidates.emplace_back(fs::path(root) / name);
    }
  }
  return candidates;
}

std::string symbol_for_language(const std::string &language_name) {
  if (const auto *entry = TreeSitterCatalog::find_language(language_name)) {
    return entry->symbol;
  }
  std::string symbol = "tree_sitter_" + language_name;
  std::replace(symbol.begin(), symbol.end(), '-', '_');
  return symbol;
}

struct LanguageLookupResult {
  const TSLanguage *language = nullptr;
  std::string message;
};

LanguageLookupResult language_from_handle(void *handle, const std::string &symbol) {
  LanguageLookupResult result;
  if (!handle) {
    result.message = "invalid library handle";
    return result;
  }
  auto fn = reinterpret_cast<TreeSitterLanguageFn>(dlsym(handle, symbol.c_str()));
  if (!fn) {
    result.message = "missing symbol " + symbol;
    return result;
  }
  const TSLanguage *lang = fn();
  if (!lang) {
    result.message = "symbol " + symbol + " returned null";
    return result;
  }
  const uint32_t abi = ts_language_abi_version(lang);
  if (abi < TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION ||
      abi > TREE_SITTER_LANGUAGE_VERSION) {
    result.message = "ABI " + std::to_string(abi) + " incompatible with " +
                     std::to_string(TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION) +
                     "-" + std::to_string(TREE_SITTER_LANGUAGE_VERSION);
    return result;
  }
  result.language = lang;
  result.message = "parser loaded";
  return result;
}
} // namespace

const TSLanguage *TreeSitterManager::load_language(
    const std::string &language_id) const {
  const std::string lid = TreeSitterCatalog::normalize_language_name(language_id);
  const TSLanguage *lang = nullptr;
  auto cached = parser_languages_.find(lid);
  if (cached != parser_languages_.end()) {
    lang = cached->second;
  } else {
    const std::string symbol = symbol_for_language(lid);
    auto handle_it = library_handles_.find(lid);
    if (handle_it != library_handles_.end()) {
      LanguageLookupResult lookup =
          language_from_handle(handle_it->second, symbol);
      lang = lookup.language;
      parser_diagnostics_[lid] = lookup.message;
    }
    std::vector<fs::path> candidates =
        library_candidates(lid, runtime_library_paths_);
    std::string last_error;
    for (const auto &candidate : candidates) {
      if (lang) {
        break;
      }
      void *handle = dlopen(candidate.string().c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!handle) {
        const char *error = dlerror();
        last_error = candidate.string() + ": " + (error ? error : "dlopen failed");
        continue;
      }
      LanguageLookupResult lookup = language_from_handle(handle, symbol);
      if (!lookup.language) {
        last_error = candidate.string() + ": " + lookup.message;
        dlclose(handle);
        continue;
      }
      lang = lookup.language;
      library_handles_[lid] = handle;
      break;
    }
    if (lang) {
      parser_languages_[lid] = lang;
      parser_diagnostics_[lid] = "parser loaded";
    } else {
      parser_languages_[lid] = nullptr;
      parser_diagnostics_[lid] =
          last_error.empty()
              ? "no parser library candidates for " + lid
              : "parser not loaded; tried " +
                    std::to_string(candidates.size()) +
                    " candidate(s); last error: " + last_error;
    }
  }

  return lang;
}

TSParser *TreeSitterManager::create_parser(const std::string &extension) const {
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end()) return nullptr;

  const std::string &lid = ext_it->second;
  const TSLanguage *lang = load_language(lid);
  if (!lang) return nullptr;

  TSParser *parser = ts_parser_new();
  if (!parser || !ts_parser_set_language(parser, lang)) {
    if (parser) {
      ts_parser_delete(parser);
    }
    parser_diagnostics_[lid] = "ts_parser_set_language failed";
    return nullptr;
  }
  return parser;
}

TSQuery *TreeSitterManager::get_highlight_query(const std::string &extension) {
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end()) return nullptr;
  const std::string &language_id = ext_it->second;
  auto cached = query_cache_.find(language_id);
  if (cached != query_cache_.end()) return cached->second;

  TSParser *parser = create_parser(extension);
  if (!parser) {
    query_diagnostics_[language_id] = "parser unavailable";
    return nullptr;
  }
  const TSLanguage *lang = ts_parser_language(parser);
  ts_parser_delete(parser);
  if (!lang) {
    query_diagnostics_[language_id] = "parser has no language";
    return nullptr;
  }

  auto compile_query = [&](const std::string &source, uint32_t &error_offset,
                           TSQueryError &error_type) {
    return ts_query_new(lang, source.c_str(), (uint32_t)source.size(),
                        &error_offset, &error_type);
  };

  auto compile_empty_query = [&]() {
    uint32_t empty_error_offset = 0;
    TSQueryError empty_error_type = TSQueryErrorNone;
    return compile_query("", empty_error_offset, empty_error_type);
  };

  QuerySource source = load_query_source(language_id);
  uint32_t error_offset = 0;
  TSQueryError error_type = TSQueryErrorNone;
  TSQuery *query = compile_query(source.source, error_offset, error_type);
  runtime_query_used_[language_id] = source.runtime && query != nullptr;
  builtin_query_used_[language_id] = !source.runtime && query != nullptr;
  if (query) {
    query_cache_[language_id] = query;
    query_diagnostics_[language_id] =
        source.runtime ? ("runtime query loaded: " + source.path)
                       : "built-in query loaded";
    return query;
  }

  std::string runtime_error;
  if (source.runtime) {
    runtime_error = "runtime query failed: " + source.path +
                    " error " + std::to_string((int)error_type) +
                    " at offset " + std::to_string(error_offset);
    const auto entry_it = languages_.find(language_id);
    const std::string fallback_source =
        entry_it != languages_.end() ? entry_it->second.highlight_query_source
                                     : builtin_query_fallback();
    error_offset = 0;
    error_type = TSQueryErrorNone;
    query = compile_query(fallback_source, error_offset, error_type);
    if (query) {
      query_cache_[language_id] = query;
      runtime_query_used_[language_id] = false;
      builtin_query_used_[language_id] = true;
      query_diagnostics_[language_id] =
          "built-in query loaded; " + runtime_error;
      return query;
    }
  }

  std::string builtin_error;
  if (!query && source.runtime) {
    builtin_error = "built-in query failed: error " +
                    std::to_string((int)error_type) + " at offset " +
                    std::to_string(error_offset);
  }

  if (!query && language_id == "cpp") {
    error_offset = 0;
    error_type = TSQueryErrorNone;
    query = compile_query(minimal_query_cpp(), error_offset, error_type);
    if (query) {
      query_cache_[language_id] = query;
      runtime_query_used_[language_id] = false;
      builtin_query_used_[language_id] = true;
      std::string message = "minimal built-in query loaded";
      if (!runtime_error.empty()) {
        message += "; " + runtime_error;
      }
      if (!builtin_error.empty()) {
        message += "; " + builtin_error;
      }
      query_diagnostics_[language_id] = message;
      return query;
    }
  }

  query = compile_empty_query();
  if (query) {
    query_cache_[language_id] = query;
    runtime_query_used_[language_id] = false;
    builtin_query_used_[language_id] = true;
    std::string message = "empty built-in query loaded";
    if (!runtime_error.empty()) {
      message += "; " + runtime_error;
    }
    if (!builtin_error.empty()) {
      message += "; " + builtin_error;
    } else if (source.runtime) {
      message += "; built-in query failed";
    }
    query_diagnostics_[language_id] = message;
    return query;
  }

  const std::string final_query_error =
      "query failed: error " + std::to_string((int)error_type) +
      " at offset " + std::to_string(error_offset);
  runtime_query_used_[language_id] = false;
  builtin_query_used_[language_id] = false;
  std::string message;
  if (!runtime_error.empty()) {
    message = runtime_error;
    if (!builtin_error.empty()) {
      message += "; " + builtin_error;
    }
    if (language_id == "cpp") {
      message += "; minimal built-in " + final_query_error;
    }
  } else {
    message = final_query_error;
  }
  query_diagnostics_[language_id] = message;
  return nullptr;
}
#endif
