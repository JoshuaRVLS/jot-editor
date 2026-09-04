"and" @keyword
"do" @keyword
"else" @keyword
"elseif" @keyword
"end" @keyword
"for" @keyword
"function" @keyword
"goto" @keyword
"if" @keyword
"in" @keyword
"local" @keyword
"not" @keyword
"or" @keyword
"repeat" @keyword
"return" @keyword
"then" @keyword
"until" @keyword
"while" @keyword
(break_statement) @keyword
(nil) @constant.builtin
(true) @constant.builtin
(false) @constant.builtin
(string) @string
(comment) @comment
(number) @number
(function_call name: (identifier) @function)
(function_declaration name: (identifier) @function)
(identifier) @variable
