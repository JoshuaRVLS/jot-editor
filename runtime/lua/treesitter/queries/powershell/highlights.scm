;; PowerShell highlighting.
;; Node names verified against airbus-cert/tree-sitter-powershell
;; src/node-types.json. Predicate-free (the native query runner does not
;; evaluate #lua-match? / #any-of? predicates).

;; Punctuation
[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  "."
  "::"
  ","
  ";"
  (empty_statement)
] @punctuation.delimiter

;; Keywords
[
  "if"
  "elseif"
  "else"
  "switch"
] @keyword.control

[
  "foreach"
  "for"
  "while"
  "do"
  "until"
  "in"
  "break"
  "continue"
] @keyword

[
  "function"
  "filter"
  "workflow"
  "exit"
  "trap"
  "param"
  "inlinescript"
] @keyword

"return" @keyword

[
  "class"
  "enum"
] @keyword

[
  "throw"
  "try"
  "catch"
  "finally"
] @keyword

[
  "parallel"
  "sequence"
] @keyword

[
  "begin"
  "process"
  "end"
] @keyword

;; Operators
[
  "-and"
  "-or"
  "-xor"
  "-band"
  "-bor"
  "-bxor"
  "+"
  "-"
  "/"
  "\\"
  "%"
  "*"
  ".."
  "-not"
  "-bnot"
  "!"
  "="
  "|"
  (pre_increment_expression)
  (pre_decrement_expression)
  (post_increment_expression)
  (post_decrement_expression)
  (comparison_operator)
  (assignement_operator)
  (command_invokation_operator)
] @operator

;; Literals
(string_literal) @string

[
  (expandable_string_literal)
  (expandable_here_string_literal)
  (verbatim_here_string_characters)
] @string

[
  (integer_literal)
  (decimal_integer_literal)
  (hexadecimal_integer_literal)
] @number

(real_literal) @number

(variable) @variable

(data_name
  (simple_name) @constant)

(comment) @comment

;; Types
(type_spec
  (type_name) @type)

(class_statement
  (simple_name) @type)

;; Functions, methods, members
(function_statement
  (function_name) @function)

(class_method_definition
  (simple_name) @function.method)

(class_property_definition
  (variable) @variable.member)

(member_access
  (member_name
    [
      (simple_name)
      (variable)
    ] @variable.member))

(key_expression) @property

(command
  .
  (command_name) @function)

(invokation_expression
  (member_name) @function)

;; Parameters
[
  (switch_parameter)
  (command_parameter)
] @variable.parameter

(script_parameter
  (variable) @variable.parameter)

(class_method_parameter
  (variable) @variable.parameter)