;; Clojure / ClojureScript highlighting.
;; Node names verified against sogaiu/tree-sitter-clojure src/node-types.json.

;; Literals
(num_lit) @number
[
  (char_lit)
  (str_lit)
  (regex_lit)
] @string

[
  (bool_lit)
  (nil_lit)
] @constant.builtin

(kwd_lit) @constant

;; Comments
(comment) @comment

;; Symbols: function calls and definitions
(sym_lit) @variable
(list_lit
  .
  (sym_lit) @function)

;; Quasiquotation / metaprogramming operators
[
  "'"
  "`"
  "~"
  "@"
  "~@"
] @operator

;; Collections
[
  (vec_lit)
  (map_lit)
  (set_lit)
  (list_lit)
] @punctuation.bracket

(quoting_lit
  [
    "'"
    "`"
  ] @punctuation.delimiter)