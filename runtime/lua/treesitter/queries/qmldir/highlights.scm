;; Adapted from nvim-treesitter (Apache-2.0).
;; https://github.com/nvim-treesitter/nvim-treesitter
; Preproc
(command
  (identifier) @keyword.directive)

; Keywords
(keyword) @keyword

; Literals
(number) @number

(float) @number.float

; Variables
[
  (identifier)
  (unit)
] @variable

; Comments
(comment) @comment @spell
