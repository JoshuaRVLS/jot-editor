;; Elm highlighting.
;; Node/token names verified against elm-tooling/tree-sitter-elm
;; (node-types.json + nvim-treesitter query for this grammar).

;; Comments
[
  (line_comment)
  (block_comment)
] @comment

;; Keywords
[
  "if"
  "then"
  "else"
  (case)
  (of)
] @keyword.control

[
  "let"
  "in"
  (as)
  (port)
  (alias)
  (infix)
  (module)
  (type)
] @keyword

[
  (import)
  (exposing)
] @keyword.import

;; Literals
(number_constant_expr) @number

[
  (string_constant_expr)
  (char_constant_expr)
] @string

(string_escape) @string.escape

;; Variables
(value_qid
  (lower_case_identifier) @variable)

(function_declaration_left
  (lower_pattern
    (lower_case_identifier) @variable.parameter))

;; Functions
(value_declaration
  functionDeclarationLeft: (function_declaration_left
    (lower_case_identifier) @function))

(function_call_expr
  target: (value_expr
    (value_qid
      (lower_case_identifier) @function)))

;; Operators
[
  (operator_identifier)
  (eq)
  (colon)
  (arrow)
  (backslash)
  "::"
] @operator

;; Modules
(module_declaration
  (upper_case_qid
    (upper_case_identifier) @module))

(import_clause
  (upper_case_qid
    (upper_case_identifier) @module))

(as_clause
  (upper_case_identifier) @module)

(value_expr
  (value_qid
    (upper_case_identifier) @module))

;; Types
(type_declaration
  (upper_case_identifier) @type)

(type_ref
  (upper_case_qid
    (upper_case_identifier) @type))

(type_variable
  (lower_case_identifier) @type)

(lower_type_name
  (lower_case_identifier) @type)

(exposed_type
  (upper_case_identifier) @type)

(type_alias_declaration
  (upper_case_identifier) @type)

;; Constructors
(type_declaration
  (union_variant
    (upper_case_identifier) @constructor))

(nullary_constructor_argument_pattern
  (upper_case_qid
    (upper_case_identifier) @constructor))

(value_expr
  (upper_case_qid
    (upper_case_identifier) @constructor))

;; Record fields
(field_type
  name: (lower_case_identifier) @property)

(field
  name: (lower_case_identifier) @property)

(field_access_expr
  (dot)
  (lower_case_identifier) @variable.member)

;; Punctuation
(double_dot) @punctuation.delimiter

[
  ","
  "|"
  (dot)
] @punctuation.delimiter

[
  "("
  ")"
  "{"
  "}"
  "["
  "]"
] @punctuation.bracket