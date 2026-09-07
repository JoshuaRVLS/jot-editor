; Keywords
"and" @keyword.operator
"or" @keyword.operator
"not" @keyword.operator
"in" @keyword
"goto" @keyword
"local" @keyword
(break_statement) @keyword
(label_statement) @keyword

(do_statement
  [
    "do"
    "end"
  ] @keyword.control)

(while_statement
  [
    "while"
    "do"
    "end"
  ] @keyword.control)

(repeat_statement
  [
    "repeat"
    "until"
  ] @keyword.control)

(if_statement
  [
    "if"
    "elseif"
    "else"
    "then"
    "end"
  ] @keyword.control)

(for_statement
  [
    "for"
    "do"
    "end"
  ] @keyword.control)

(function_declaration
  [
    "function"
    "end"
  ] @keyword.function)

(function_definition
  [
    "function"
    "end"
  ] @keyword.function)

"return" @keyword.control

; Operators
(binary_expression
  operator: _ @operator)

(unary_expression
  operator: _ @operator)

"=" @operator

; Punctuation
[
  ";"
  ":"
  ","
  "."
] @punctuation.delimiter

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

; Variables and constants
(identifier) @variable

((identifier) @variable.builtin
  (#eq? @variable.builtin "self"))

((identifier) @constant
  (#match? @constant "^[A-Z][A-Z_0-9]*$"))

(vararg_expression) @constant

(nil) @constant.builtin

[
  (false)
  (true)
] @boolean

; Tables and fields
(field
  name: (identifier) @field)

(dot_index_expression
  field: (identifier) @field)

(table_constructor
  [
    "{"
    "}"
  ] @constructor)

; Functions
(parameters
  (identifier) @parameter)

(function_declaration
  name: [
    (identifier) @function
    (dot_index_expression
      field: (identifier) @function)
  ])

(function_declaration
  name: (method_index_expression
    method: (identifier) @function.method))

(assignment_statement
  (variable_list
    .
    name: [
      (identifier) @function
      (dot_index_expression
        field: (identifier) @function)
    ])
  (expression_list
    .
    value: (function_definition)))

(table_constructor
  (field
    name: (identifier) @function
    value: (function_definition)))

(function_call
  name: [
    (identifier) @function
    (dot_index_expression
      field: (identifier) @function)
    (method_index_expression
      method: (identifier) @function.method)
  ])

; Literals
(comment) @comment

(hash_bang_line) @preproc

(number) @number

(string) @string

(escape_sequence) @string.escape