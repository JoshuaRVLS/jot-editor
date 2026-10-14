"if" @keyword
"then" @keyword
"else" @keyword
"elif" @keyword
"fi" @keyword
"case" @keyword
"esac" @keyword
"for" @keyword
"while" @keyword
"until" @keyword
"do" @keyword
"done" @keyword
"in" @keyword
"function" @keyword
"declare" @keyword
"local" @keyword
"export" @keyword
; No "return" rule: tree-sitter-bash has no such token (the word is a plain
; `word` inside a command), and naming a token the grammar does not define
; fails the whole query rather than the one line.
(string) @string
(raw_string) @string
(comment) @comment
(variable_name) @variable