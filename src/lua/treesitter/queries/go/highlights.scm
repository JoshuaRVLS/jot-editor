"break" @keyword
"case" @keyword
"chan" @keyword
"const" @keyword
"continue" @keyword
"default" @keyword
"defer" @keyword
"else" @keyword
"fallthrough" @keyword
"for" @keyword
"func" @keyword
"go" @keyword
"goto" @keyword
"if" @keyword
"import" @keyword
"interface" @keyword
"map" @keyword
"package" @keyword
"range" @keyword
"return" @keyword
"select" @keyword
"struct" @keyword
"switch" @keyword
"type" @keyword
"var" @keyword
(interpreted_string_literal) @string
(raw_string_literal) @string
(comment) @comment
(int_literal) @number
(float_literal) @number
(call_expression function: (identifier) @function)
(function_declaration name: (identifier) @function)
(type_declaration (type_spec name: (type_identifier) @type))
(identifier) @variable