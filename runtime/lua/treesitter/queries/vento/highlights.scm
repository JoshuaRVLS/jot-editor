;; Adapted from nvim-treesitter (Apache-2.0).
;; https://github.com/nvim-treesitter/nvim-treesitter
(comment) @comment @spell

[
  "if"
  "/if"
  "else"
  "for"
  "/for"
  "layout"
  "/layout"
  "set"
  "/set"
  "import"
  "export"
  "/export"
  "include"
  "function"
  "/function"
  "fragment"
  "/fragment"
  "of"
  "async"
] @keyword

(tag
  [
    "{{"
    "{{-"
    "}}"
    "-}}"
  ] @punctuation.special)

"|>" @operator
