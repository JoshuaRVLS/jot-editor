;; Adapted from nvim-treesitter (Apache-2.0).
;; https://github.com/nvim-treesitter/nvim-treesitter
(keysym) @constant

((keysym) @constant.builtin
  (#eq? @constant.builtin "Multi_key"))

(text) @string

"include" @keyword.import

[
  (octal)
  (hex)
] @number

[
  (modifier)
  "None"
] @keyword.modifier

[
  "%L"
  "%H"
  "%S"
] @string.special

[
  "!"
  "~"
] @operator

[
  ":"
  "<"
  ">"
  "\""
] @punctuation.delimiter

(comment) @comment @spell
