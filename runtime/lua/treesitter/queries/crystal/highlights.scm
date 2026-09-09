;; Crystal highlighting.
;; Node names verified against crystal-lang-tools/tree-sitter-crystal
;; node-types.json (the grammar mirrors Ruby's node structure).

;; Comments
(comment) @comment

;; Strings and literals
[
  (string)
  (chained_string)
  (heredoc_body)
  (symbol)
  (char)
  (regex)
] @string

(escape_sequence) @string.escape

[
  (integer)
  (float)
] @number

[
  (true)
  (false)
  (nil)
] @constant.builtin

(self) @keyword

;; Keywords (anonymous tokens are matched by text, named ones by node)
[
  "def"
  "end"
  (if)
  (elsif)
  (else)
  (unless)
  (while)
  (until)
  (case)
  (when)
  (then)
  "for"
  (in)
  "do"
  (begin)
  (rescue)
  (ensure)
  (return)
  (break)
  (next)
  (yield)
  "class"
  "module"
  "struct"
  "enum"
  "lib"
  "union"
  (require)
  (include)
  (extend)
  "abstract"
  (private)
  (protected)
  "macro"
  (asm)
  (out)
  "with"
  (select)
  "of"
  (and)
  (or)
  (not)
  "fun"
  "type"
] @keyword

;; Calls and definitions
(call
  method: (identifier) @function)

(implicit_object_call
  method: (identifier) @function)

(method_def
  name: (identifier) @function)

(fun_def
  name: (identifier) @function)

;; Types
(constant) @type

(class_def
  name: (constant) @type)

(module_def
  name: (constant) @type)

;; Variables
(instance_var) @variable.member

(class_var) @variable

(global_var) @variable

;; Operators
(operator) @operator

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
  ","
  ";"
  "."
  "::"
  "=>"
  "->"
] @punctuation.delimiter