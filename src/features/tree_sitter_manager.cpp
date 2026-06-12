#include "tree_sitter_manager.h"
#include <filesystem>

#ifdef JOT_TREESITTER
#ifdef JOT_TS_HAS_C
#include <tree_sitter/tree-sitter-c.h>
#endif
#ifdef JOT_TS_HAS_CPP
#include <tree_sitter/tree-sitter-cpp.h>
#endif
#ifdef JOT_TS_HAS_PYTHON
#include <tree_sitter/tree-sitter-python.h>
#endif
#ifdef JOT_TS_HAS_JAVASCRIPT
#include <tree_sitter/tree-sitter-javascript.h>
#endif
#ifdef JOT_TS_HAS_TYPESCRIPT
#include <tree_sitter/tree-sitter-typescript.h>
#endif
#ifdef JOT_TS_HAS_RUST
#include <tree_sitter/tree-sitter-rust.h>
#endif
#ifdef JOT_TS_HAS_GO
#include <tree_sitter/tree-sitter-go.h>
#endif
#ifdef JOT_TS_HAS_JSON
#include <tree_sitter/tree-sitter-json.h>
#endif
#ifdef JOT_TS_HAS_HTML
#include <tree_sitter/tree-sitter-html.h>
#endif
#ifdef JOT_TS_HAS_CSS
#include <tree_sitter/tree-sitter-css.h>
#endif
#ifdef JOT_TS_HAS_BASH
#include <tree_sitter/tree-sitter-bash.h>
#endif
#ifdef JOT_TS_HAS_LUA
#include <tree_sitter/tree-sitter-lua.h>
#endif
#ifdef JOT_TS_HAS_MARKDOWN
#include <tree_sitter/tree-sitter-markdown.h>
#endif
#ifdef JOT_TS_HAS_TOML
#include <tree_sitter/tree-sitter-toml.h>
#endif
#ifdef JOT_TS_HAS_YAML
#include <tree_sitter/tree-sitter-yaml.h>
#endif

extern "C" {
#ifndef JOT_TS_HAS_C
__attribute__((weak)) const TSLanguage *tree_sitter_c() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_CPP
__attribute__((weak)) const TSLanguage *tree_sitter_cpp() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_PYTHON
__attribute__((weak)) const TSLanguage *tree_sitter_python() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_JAVASCRIPT
__attribute__((weak)) const TSLanguage *tree_sitter_javascript() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_TYPESCRIPT
__attribute__((weak)) const TSLanguage *tree_sitter_typescript() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_RUST
__attribute__((weak)) const TSLanguage *tree_sitter_rust() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_GO
__attribute__((weak)) const TSLanguage *tree_sitter_go() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_JSON
__attribute__((weak)) const TSLanguage *tree_sitter_json() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_HTML
__attribute__((weak)) const TSLanguage *tree_sitter_html() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_CSS
__attribute__((weak)) const TSLanguage *tree_sitter_css() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_BASH
__attribute__((weak)) const TSLanguage *tree_sitter_bash() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_LUA
__attribute__((weak)) const TSLanguage *tree_sitter_lua() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_MARKDOWN
__attribute__((weak)) const TSLanguage *tree_sitter_markdown() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_TOML
__attribute__((weak)) const TSLanguage *tree_sitter_toml() { return nullptr; }
#endif
#ifndef JOT_TS_HAS_YAML
__attribute__((weak)) const TSLanguage *tree_sitter_yaml() { return nullptr; }
#endif
}
#endif

namespace {

const char *builtin_query_c() { return R"TREEQUERY(
"break" @keyword
"case" @keyword
"const" @keyword
"continue" @keyword
"default" @keyword
"do" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"for" @keyword
"goto" @keyword
"if" @keyword
"return" @keyword
"sizeof" @keyword
"static" @keyword
"struct" @keyword
"switch" @keyword
"typedef" @keyword
"union" @keyword
"volatile" @keyword
"while" @keyword
(string_literal) @string
(system_lib_string) @string
(comment) @comment
(number_literal) @number
(primitive_type) @type
(type_identifier) @type
(type_descriptor) @type
(field_identifier) @property
(call_expression function: (identifier) @function)
(call_expression function: (field_expression field: (field_identifier) @function))
(function_declarator declarator: (identifier) @function)
(function_definition declarator: (function_declarator declarator: (identifier) @function))
(preproc_include) @keyword
(preproc_def) @keyword
(preproc_if) @keyword
(preproc_elif) @keyword
(preproc_else) @keyword
(preproc_endif) @keyword
)TREEQUERY"; }

const char *builtin_query_cpp() { return R"TREEQUERY(
"break" @keyword
"case" @keyword
"class" @keyword
"const" @keyword
"constexpr" @keyword
"continue" @keyword
"default" @keyword
"do" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"for" @keyword
"goto" @keyword
"if" @keyword
"namespace" @keyword
"new" @keyword
"return" @keyword
"sizeof" @keyword
"static" @keyword
"struct" @keyword
"switch" @keyword
"template" @keyword
"typedef" @keyword
"typename" @keyword
"union" @keyword
"using" @keyword
"virtual" @keyword
"volatile" @keyword
"while" @keyword
(raw_string_literal) @string
(string_literal) @string
(comment) @comment
(number_literal) @number
(primitive_type) @type
(type_identifier) @type
(template_type) @type
(namespace_identifier) @type
(qualified_identifier scope: (namespace_identifier) @type)
(qualified_identifier name: (identifier) @type)
(qualified_identifier name: (type_identifier) @type)
(field_identifier) @property
(call_expression function: (identifier) @function)
(call_expression function: (field_expression field: (field_identifier) @function))
(call_expression function: (qualified_identifier name: (identifier) @function))
(call_expression function: (qualified_identifier name: (field_identifier) @function))
(call_expression function: (qualified_identifier name: (operator_name) @function))
(function_declarator declarator: (identifier) @function)
(function_declarator declarator: (qualified_identifier name: (identifier) @function))
(function_declarator declarator: (field_identifier) @function)
(function_definition declarator: (function_declarator declarator: (identifier) @function))
(function_definition declarator: (function_declarator declarator: (qualified_identifier name: (identifier) @function)))
(preproc_include) @keyword
(preproc_def) @keyword
(preproc_if) @keyword
(preproc_elif) @keyword
(preproc_else) @keyword
(preproc_endif) @keyword
)TREEQUERY"; }

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
)TREEQUERY"; }

const char *builtin_query_json() { return R"TREEQUERY(
(string) @string
(number) @number
"true" @keyword
"false" @keyword
"null" @keyword
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

const char *builtin_query_fallback() { return R"TREEQUERY(
(keyword) @keyword
(string) @string
(comment) @comment
(number) @number
(type) @type
(function) @function
)TREEQUERY"; }

using QueryProvider = const char *(*)();

QueryProvider query_for_lang(const std::string &lang) {
  if (lang == "c") return builtin_query_c;
  if (lang == "cpp") return builtin_query_cpp;
  if (lang == "python") return builtin_query_python;
  if (lang == "javascript" || lang == "typescript") return builtin_query_javascript;
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
#endif
}

void TreeSitterManager::register_languages() {
  ext_to_lang_ = {
    {".c",    "c"},       {".h",    "c"},
    {".cpp",  "cpp"},     {".hpp",  "cpp"},   {".cc",   "cpp"},
    {".cxx",  "cpp"},     {".hh",   "cpp"},   {".hxx",  "cpp"},
    {".py",   "python"},
    {".js",   "javascript"}, {".jsx", "javascript"}, {".mjs", "javascript"},
    {".ts",   "typescript"}, {".tsx", "typescript"},
    {".rs",   "rust"},
    {".go",   "go"},
    {".json", "json"},
    {".html", "html"},
    {".css",  "css"},
    {".sh",   "bash"},    {".bash", "bash"},
    {".lua",  "lua"},
    {".md",   "markdown"},
    {".toml", "toml"},
    {".yml",  "yaml"},    {".yaml", "yaml"},
  };

  for (const auto &[ext, lang] : ext_to_lang_) {
    if (languages_.find(lang) != languages_.end()) continue;
    TSLanguageEntry entry;
    entry.language_id = lang;
    entry.highlight_query_source = query_for_lang(lang)();
    languages_[lang] = std::move(entry);
  }
}

std::string TreeSitterManager::load_query_source(const std::string &language_name) const {
  auto it = languages_.find(language_name);
  if (it != languages_.end()) return it->second.highlight_query_source;
  return builtin_query_fallback();
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

std::string
TreeSitterManager::language_id_for_extension(const std::string &extension) const {
  const TSLanguageEntry *entry = get_language(extension);
  return entry ? entry->language_id : "";
}

#ifdef JOT_TREESITTER
TSParser *TreeSitterManager::create_parser(const std::string &extension) const {
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end()) return nullptr;

  const TSLanguage *lang = nullptr;
  const std::string &lid = ext_it->second;
  auto cached = parser_languages_.find(lid);
  if (cached != parser_languages_.end()) {
    lang = cached->second;
  } else {
    if (lid == "c") lang = tree_sitter_c();
    else if (lid == "cpp") lang = tree_sitter_cpp();
    else if (lid == "python") lang = tree_sitter_python();
    else if (lid == "javascript") lang = tree_sitter_javascript();
    else if (lid == "typescript") {
      lang = tree_sitter_typescript();
      if (!lang) lang = tree_sitter_javascript();
    }
    else if (lid == "rust") lang = tree_sitter_rust();
    else if (lid == "go") lang = tree_sitter_go();
    else if (lid == "json") lang = tree_sitter_json();
    else if (lid == "html") lang = tree_sitter_html();
    else if (lid == "css") lang = tree_sitter_css();
    else if (lid == "bash") lang = tree_sitter_bash();
    else if (lid == "lua") lang = tree_sitter_lua();
    else if (lid == "markdown") lang = tree_sitter_markdown();
    else if (lid == "toml") lang = tree_sitter_toml();
    else if (lid == "yaml") lang = tree_sitter_yaml();
    else return nullptr;
    parser_languages_[lid] = lang;
  }

  if (!lang) return nullptr;

  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, lang);
  return parser;
}

TSQuery *TreeSitterManager::get_highlight_query(const std::string &extension) {
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end()) return nullptr;
  auto cached = query_cache_.find(ext_it->second);
  if (cached != query_cache_.end()) return cached->second;

  TSParser *parser = create_parser(extension);
  if (!parser) return nullptr;
  const TSLanguage *lang = ts_parser_language(parser);
  ts_parser_delete(parser);
  if (!lang) return nullptr;

  std::string source = load_query_source(ext_it->second);
  uint32_t error_offset;
  TSQueryError error_type;
  TSQuery *query = ts_query_new(lang, source.c_str(), (uint32_t)source.size(),
                                &error_offset, &error_type);
  query_cache_[ext_it->second] = query;
  return query;
}
#endif
