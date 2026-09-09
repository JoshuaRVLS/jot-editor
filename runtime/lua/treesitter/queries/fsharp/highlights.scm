;; F# highlighting.
;; Adapted from nvim-treesitter's query for ionide/tree-sitter-fsharp;
;; Lua predicates removed (native renderer cannot evaluate them).

;; Comments
[
  (line_comment)
  (block_comment)
] @comment

;; Values and parameters
(value_declaration_left
  .
  (_) @variable)

(function_declaration_left
  .
  (_) @function)

(argument_patterns
  [
    (const)
    (long_identifier)
    (_pattern)
  ] @variable.parameter)

(argument_patterns
  (typed_pattern
    (_pattern) @variable.parameter
    (_type) @type))

(member_signature
  .
  (identifier) @function.method)

(member_defn
  (method_or_prop_defn
    [
      (property_or_ident) @function
      (property_or_ident
        instance: (identifier) @variable.parameter
        method: (identifier) @function.method)
    ]))

(dot_expression
  .
  (_) @variable.member
  .
  (_))

(application_expression
  .
  (_) @function
  .
  (_) @variable)

;; Types
(type_name
  type_name: (_) @type)

[
  (_type)
  (atomic_type)
] @type

(union_type_case
  (identifier) @constant)

(field_initializer
  field: (_) @property)

(record_fields
  (record_field
    .
    (identifier) @property))

;; Modules
(named_module
  name: (_) @module)

(namespace
  name: (_) @module)

(module_defn
  (identifier) @module)

(import_decl
  .
  (_) @module)

;; Literals
[
  (xint)
  (int)
  (int16)
  (uint16)
  (int32)
  (uint32)
  (int64)
  (uint64)
  (nativeint)
  (unativeint)
] @number

[
  (ieee32)
  (ieee64)
  (float)
  (decimal)
] @number

(bool) @boolean

[
  (string)
  (triple_quoted_string)
  (verbatim_string)
  (char)
] @string

[
  "null"
  (unit)
] @constant.builtin

;; Attributes and directives
(compiler_directive_decl) @keyword.directive

(preproc_line
  "#line" @keyword.directive)

(preproc_if
  [
    "#if" @keyword.directive
    "#endif" @keyword.directive
  ])

(preproc_else
  "#else" @keyword.directive)

(attribute
  target: (identifier)? @keyword
  (_type) @attribute)

;; Operators
[
  "|"
  "="
  ">"
  "<"
  "-"
  "~"
  "->"
  "<-"
  "&"
  "&&"
  "||"
  ":>"
  ":?>"
  ".."
  "*"
  (infix_op)
  (prefix_op)
  (op_identifier)
] @operator

;; Punctuation
[
  "("
  ")"
  "{"
  "}"
  ".["
  "["
  "]"
  "[|"
  "|]"
  "{|"
  "|}"
] @punctuation.bracket

[
  "[<"
  ">]"
] @punctuation.bracket

[
  ","
  ";"
  ":"
  "."
] @punctuation.delimiter

;; Keywords
[
  "if"
  "then"
  "else"
  "elif"
  "when"
  "match"
  "match!"
] @keyword.control

[
  "and"
  "or"
  "not"
  "upcast"
  "downcast"
] @keyword

[
  "return"
  "return!"
  "yield"
  "yield!"
] @keyword

[
  "for"
  "while"
  "downto"
  "to"
] @keyword

[
  "open"
  "#r"
  "#load"
] @keyword.import

[
  "abstract"
  "delegate"
  "static"
  "inline"
  "mutable"
  "override"
  "rec"
  "global"
  (access_modifier)
] @keyword.storage

[
  "let"
  "let!"
  "use"
  "use!"
  "member"
] @keyword

[
  "enum"
  "type"
  "inherit"
  "interface"
  "and"
  "class"
  "struct"
] @keyword

[
  "as"
  "assert"
  "begin"
  "end"
  "done"
  "default"
  "in"
  "do"
  "do!"
  "fun"
  "function"
  "get"
  "set"
  "lazy"
  "new"
  "of"
  "struct"
  "val"
  "module"
  "namespace"
  "with"
] @keyword

(match_expression
  "with" @keyword.control)

(try_expression
  [
    "try"
    "with"
    "finally"
  ] @keyword)