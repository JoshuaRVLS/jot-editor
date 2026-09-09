;; Adapted from nvim-treesitter (Apache-2.0).
;; https://github.com/nvim-treesitter/nvim-treesitter
(keyword) @keyword

(dir_sep) @punctuation.delimiter

(glob) @punctuation.special

(linebreak) @character.special

(char_sequence) @string.special

(char_sequence
  [
    "["
    "]"
  ] @punctuation.bracket)

(char_sequence
  "!" @operator)

(char_range
  "-" @operator)

(escaped_char) @string.escape

(comment) @comment @spell
