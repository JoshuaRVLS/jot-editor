[
  "break"
  "case"
  "catch"
  "continue"
  "debugger"
  "default"
  "do"
  "else"
  "finally"
  "for"
  "if"
  "return"
  "switch"
  "throw"
  "try"
  "while"
  "with"
] @keyword.control

[
  "abstract"
  "class"
  "const"
  "declare"
  "enum"
  "extends"
  "function"
  "implements"
  "interface"
  "let"
  "new"
  "private"
  "protected"
  "public"
  "readonly"
  "static"
  "type"
  "var"
] @keyword.storage

[
  "export"
  "import"
  "namespace"
] @keyword.directive

[
  "as"
  "delete"
  "in"
  "instanceof"
  "keyof"
  "of"
  "typeof"
  "void"
  "yield"
] @keyword

(this) @variable.builtin
(super) @variable.builtin

(string) @string
(template_string) @string
(comment) @comment
(number) @number
(regex) @string
(escape_sequence) @string.escape

(call_expression function: (identifier) @function)
(call_expression function: (member_expression property: (property_identifier) @function.method))
(function_declaration name: (identifier) @function)
(method_definition name: (property_identifier) @function.method)
(class_declaration name: (type_identifier) @type)
(interface_declaration name: (type_identifier) @type)
(type_alias_declaration name: (type_identifier) @type)
(enum_declaration name: (identifier) @type)
(type_identifier) @type
(predefined_type) @type.builtin
(required_parameter pattern: (identifier) @variable.parameter)
(optional_parameter pattern: (identifier) @variable.parameter)
(property_identifier) @property
(pair key: (property_identifier) @property)

(identifier) @variable